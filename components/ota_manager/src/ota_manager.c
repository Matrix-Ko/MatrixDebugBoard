#include "ota_manager.h"
#include "http_server.h"
#include "ws_server.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "OTA";

#define OTA_NVS_NS       "ota_cfg"
#define OTA_CHK_BUF_SZ   640
#define OTA_DL_BUF_SZ    4096
#define OTA_CHK_TIMEOUT  12000
#define OTA_DL_TIMEOUT   90000
#define OTA_MAX_URL      256
#define OTA_MAX_NOTES    128
#define OTA_TASK_STACK   8192
#define OTA_CHK_STACK    6144

typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_CHECKING,
    OTA_STATE_UP_TO_DATE,
    OTA_STATE_AVAILABLE,
    OTA_STATE_UPDATING,
    OTA_STATE_SUCCESS,
    OTA_STATE_FAILED,
} ota_state_t;

static const char *const STATE_STR[] = {
    "idle", "checking", "up_to_date", "available", "updating", "success", "failed"
};

static struct {
    ota_state_t        state;
    char               remote_ver[16];
    char               dl_url[OTA_MAX_URL];
    char               notes[OTA_MAX_NOTES];
    char               check_url[OTA_MAX_URL];
    int                progress;
    char               error[96];
    bool               auto_check;
    uint32_t           interval_min;
    SemaphoreHandle_t  mtx;
    esp_timer_handle_t timer;
} s = {
    .state        = OTA_STATE_IDLE,
    .interval_min = 60,
};

/* ---- tiny JSON helpers (matches project style) ---- */
static void jget(const char *json, const char *key, char *out, size_t maxlen)
{
    char pat[72];
    snprintf(pat, sizeof(pat), "\"%s\":\"", key);
    const char *p = strstr(json, pat);
    if (!p) { out[0] = '\0'; return; }
    p += strlen(pat);
    const char *e = strchr(p, '"');
    if (!e) { out[0] = '\0'; return; }
    size_t n = (size_t)(e - p);
    if (n >= maxlen) n = maxlen - 1;
    memcpy(out, p, n);
    out[n] = '\0';
}

static bool jget_bool(const char *json, const char *key)
{
    char pat[72];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(json, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p == ' ') p++;
    return (*p == 't');
}

static long jget_long(const char *json, const char *key)
{
    char pat[72];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(json, pat);
    if (!p) return -1;
    p += strlen(pat);
    while (*p == ' ') p++;
    return strtol(p, NULL, 10);
}

/* ---- semver: "1.2.3" → 0x00010203 ---- */
static uint32_t parse_ver(const char *s)
{
    unsigned a = 0, b = 0, c = 0;
    sscanf(s, "%u.%u.%u", &a, &b, &c);
    return (a << 16) | (b << 8) | c;
}

/* ---- NVS ---- */
static void nvs_load(void)
{
    nvs_handle_t h;
    if (nvs_open(OTA_NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    size_t len = sizeof(s.check_url);
    nvs_get_str(h, "check_url", s.check_url, &len);
    uint8_t ac = 0;
    if (nvs_get_u8(h, "auto_chk", &ac) == ESP_OK) s.auto_check = (bool)ac;
    uint32_t im = 60;
    if (nvs_get_u32(h, "interval_m", &im) == ESP_OK) s.interval_min = im;
    nvs_close(h);
}

static void nvs_save(void)
{
    nvs_handle_t h;
    if (nvs_open(OTA_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "check_url", s.check_url);
    nvs_set_u8(h, "auto_chk", (uint8_t)s.auto_check);
    nvs_set_u32(h, "interval_m", s.interval_min);
    nvs_commit(h);
    nvs_close(h);
}

/* ---- WS broadcast helpers ---- */
static void ws_push_state(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    char buf[384];
    xSemaphoreTake(s.mtx, portMAX_DELAY);
    snprintf(buf, sizeof(buf),
        "{\"type\":\"ota_state\","
        "\"state\":\"%s\","
        "\"local_version\":\"%s\","
        "\"remote_version\":\"%s\","
        "\"notes\":\"%s\","
        "\"progress\":%d,"
        "\"error\":\"%s\"}",
        STATE_STR[s.state],
        app->version,
        s.remote_ver,
        s.notes,
        s.progress,
        s.error);
    xSemaphoreGive(s.mtx);
    ws_broadcast(buf);
}

static void ws_push_progress(int pct)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"type\":\"ota_progress\",\"percent\":%d}", pct);
    ws_broadcast(buf);
}

/* ---- Version check task ---- */
static void check_task(void *arg)
{
    (void)arg;

    xSemaphoreTake(s.mtx, portMAX_DELAY);
    s.state    = OTA_STATE_CHECKING;
    s.error[0] = '\0';
    char url[OTA_MAX_URL];
    memcpy(url, s.check_url, sizeof(url));
    xSemaphoreGive(s.mtx);
    ws_push_state();

    if (!url[0]) {
        xSemaphoreTake(s.mtx, portMAX_DELAY);
        s.state = OTA_STATE_FAILED;
        snprintf(s.error, sizeof(s.error), "未配置检测地址");
        xSemaphoreGive(s.mtx);
        ws_push_state();
        vTaskDelete(NULL);
        return;
    }

    esp_http_client_config_t cfg = {
        .url             = url,
        .timeout_ms      = OTA_CHK_TIMEOUT,
        .buffer_size     = OTA_CHK_BUF_SZ,
        .buffer_size_tx  = 256,
        .keep_alive_enable = false,
        .disable_auto_redirect = false,
        .max_redirection_count = 3,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        xSemaphoreTake(s.mtx, portMAX_DELAY);
        s.state = OTA_STATE_FAILED;
        snprintf(s.error, sizeof(s.error), "HTTP client 初始化失败");
        xSemaphoreGive(s.mtx);
        ws_push_state();
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        xSemaphoreTake(s.mtx, portMAX_DELAY);
        s.state = OTA_STATE_FAILED;
        snprintf(s.error, sizeof(s.error), "连接失败 (0x%x)", err);
        xSemaphoreGive(s.mtx);
        ws_push_state();
        vTaskDelete(NULL);
        return;
    }

    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);

    char body[OTA_CHK_BUF_SZ];
    int  n = 0;
    if (status == 200) {
        n = esp_http_client_read(client, body, (int)sizeof(body) - 1);
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (status != 200 || n <= 0) {
        xSemaphoreTake(s.mtx, portMAX_DELAY);
        s.state = OTA_STATE_FAILED;
        snprintf(s.error, sizeof(s.error), "HTTP %d", status);
        xSemaphoreGive(s.mtx);
        ws_push_state();
        vTaskDelete(NULL);
        return;
    }
    body[n] = '\0';

    char remote_ver[16] = {0};
    char dl_url[OTA_MAX_URL] = {0};
    char notes[OTA_MAX_NOTES] = {0};
    jget(body, "version", remote_ver, sizeof(remote_ver));
    jget(body, "url",     dl_url,     sizeof(dl_url));
    jget(body, "notes",   notes,      sizeof(notes));

    if (!remote_ver[0] || !dl_url[0]) {
        xSemaphoreTake(s.mtx, portMAX_DELAY);
        s.state = OTA_STATE_FAILED;
        snprintf(s.error, sizeof(s.error), "manifest 格式错误");
        xSemaphoreGive(s.mtx);
        ws_push_state();
        vTaskDelete(NULL);
        return;
    }

    const esp_app_desc_t *app = esp_app_get_description();
    uint32_t local_v  = parse_ver(app->version);
    uint32_t remote_v = parse_ver(remote_ver);

    xSemaphoreTake(s.mtx, portMAX_DELAY);
    snprintf(s.remote_ver, sizeof(s.remote_ver), "%s", remote_ver);
    snprintf(s.dl_url,     sizeof(s.dl_url),     "%s", dl_url);
    snprintf(s.notes,      sizeof(s.notes),       "%s", notes);
    s.state = (remote_v > local_v) ? OTA_STATE_AVAILABLE : OTA_STATE_UP_TO_DATE;
    xSemaphoreGive(s.mtx);

    ESP_LOGI(TAG, "Local=%s Remote=%s → %s",
             app->version, remote_ver, STATE_STR[s.state]);
    ws_push_state();
    vTaskDelete(NULL);
}

/* ---- OTA download + flash task ---- */
static void update_task(void *arg)
{
    (void)arg;

    xSemaphoreTake(s.mtx, portMAX_DELAY);
    s.state    = OTA_STATE_UPDATING;
    s.progress = 0;
    s.error[0] = '\0';
    char url[OTA_MAX_URL];
    memcpy(url, s.dl_url, sizeof(url));
    xSemaphoreGive(s.mtx);
    ws_push_state();

    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) {
        xSemaphoreTake(s.mtx, portMAX_DELAY);
        s.state = OTA_STATE_FAILED;
        snprintf(s.error, sizeof(s.error), "无可用 OTA 分区");
        xSemaphoreGive(s.mtx);
        ws_push_state();
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Target partition: %s @ 0x%08lx (%lu bytes)",
             part->label, (unsigned long)part->address, (unsigned long)part->size);

    esp_http_client_config_t cfg = {
        .url             = url,
        .timeout_ms      = OTA_DL_TIMEOUT,
        .buffer_size     = OTA_DL_BUF_SZ,
        .buffer_size_tx  = 512,
        .keep_alive_enable = false,
        .disable_auto_redirect = false,
        .max_redirection_count = 3,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        xSemaphoreTake(s.mtx, portMAX_DELAY);
        s.state = OTA_STATE_FAILED;
        snprintf(s.error, sizeof(s.error), "HTTP client 初始化失败");
        xSemaphoreGive(s.mtx);
        ws_push_state();
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        xSemaphoreTake(s.mtx, portMAX_DELAY);
        s.state = OTA_STATE_FAILED;
        snprintf(s.error, sizeof(s.error), "下载连接失败 (0x%x)", err);
        xSemaphoreGive(s.mtx);
        ws_push_state();
        vTaskDelete(NULL);
        return;
    }

    int content_len = esp_http_client_fetch_headers(client);
    int http_status = esp_http_client_get_status_code(client);

    if (http_status != 200) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        xSemaphoreTake(s.mtx, portMAX_DELAY);
        s.state = OTA_STATE_FAILED;
        snprintf(s.error, sizeof(s.error), "下载 HTTP %d", http_status);
        xSemaphoreGive(s.mtx);
        ws_push_state();
        vTaskDelete(NULL);
        return;
    }

    /* Guard against oversized images before allocating OTA handle */
    if (content_len > 0 && (size_t)content_len > part->size) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        xSemaphoreTake(s.mtx, portMAX_DELAY);
        s.state = OTA_STATE_FAILED;
        snprintf(s.error, sizeof(s.error), "固件过大 (%d bytes)", content_len);
        xSemaphoreGive(s.mtx);
        ws_push_state();
        vTaskDelete(NULL);
        return;
    }

    esp_ota_handle_t ota_hdl;
    err = esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &ota_hdl);
    if (err != ESP_OK) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        xSemaphoreTake(s.mtx, portMAX_DELAY);
        s.state = OTA_STATE_FAILED;
        snprintf(s.error, sizeof(s.error), "OTA begin (0x%x)", err);
        xSemaphoreGive(s.mtx);
        ws_push_state();
        vTaskDelete(NULL);
        return;
    }

    char *buf = malloc(OTA_DL_BUF_SZ);
    if (!buf) {
        esp_ota_abort(ota_hdl);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        xSemaphoreTake(s.mtx, portMAX_DELAY);
        s.state = OTA_STATE_FAILED;
        snprintf(s.error, sizeof(s.error), "内存不足");
        xSemaphoreGive(s.mtx);
        ws_push_state();
        vTaskDelete(NULL);
        return;
    }

    int written  = 0;
    int last_pct = -1;
    bool io_err  = false;

    while (1) {
        int n = esp_http_client_read(client, buf, OTA_DL_BUF_SZ);
        if (n < 0) { io_err = true; break; }
        if (n == 0) break;

        err = esp_ota_write(ota_hdl, buf, (size_t)n);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write: 0x%x", err);
            io_err = true;
            break;
        }
        written += n;

        if (content_len > 0) {
            int pct = (written * 100) / content_len;
            if (pct != last_pct) {
                last_pct = pct;
                ws_push_progress(pct);
                xSemaphoreTake(s.mtx, portMAX_DELAY);
                s.progress = pct;
                xSemaphoreGive(s.mtx);
            }
        }
        /* Yield so WDT + other tasks keep running */
        vTaskDelay(1);
    }
    free(buf);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (io_err || written == 0) {
        esp_ota_abort(ota_hdl);
        xSemaphoreTake(s.mtx, portMAX_DELAY);
        s.state = OTA_STATE_FAILED;
        snprintf(s.error, sizeof(s.error), io_err ? "下载中断" : "固件为空");
        xSemaphoreGive(s.mtx);
        ws_push_state();
        vTaskDelete(NULL);
        return;
    }

    /* Validate SHA256 + image header */
    err = esp_ota_end(ota_hdl);
    if (err != ESP_OK) {
        xSemaphoreTake(s.mtx, portMAX_DELAY);
        s.state = OTA_STATE_FAILED;
        snprintf(s.error, sizeof(s.error), "固件校验失败 (0x%x)", err);
        xSemaphoreGive(s.mtx);
        ws_push_state();
        vTaskDelete(NULL);
        return;
    }

    err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        xSemaphoreTake(s.mtx, portMAX_DELAY);
        s.state = OTA_STATE_FAILED;
        snprintf(s.error, sizeof(s.error), "设置启动分区失败 (0x%x)", err);
        xSemaphoreGive(s.mtx);
        ws_push_state();
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "OTA OK — written %d bytes, rebooting in 2 s", written);
    xSemaphoreTake(s.mtx, portMAX_DELAY);
    s.state    = OTA_STATE_SUCCESS;
    s.progress = 100;
    xSemaphoreGive(s.mtx);
    ws_push_progress(100);
    ws_push_state();

    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
}

/* ---- Auto-check timer ---- */
static void timer_cb(void *arg)
{
    (void)arg;
    xSemaphoreTake(s.mtx, portMAX_DELAY);
    bool busy    = (s.state == OTA_STATE_CHECKING || s.state == OTA_STATE_UPDATING);
    bool has_url = (s.check_url[0] != '\0');
    xSemaphoreGive(s.mtx);
    if (!busy && has_url) {
        xTaskCreate(check_task, "ota_chk", OTA_CHK_STACK, NULL, 5, NULL);
    }
}

/* ---- HTTP handlers ---- */
static esp_err_t h_status(httpd_req_t *req)
{
    const esp_app_desc_t *app = esp_app_get_description();
    char buf[1024];
    xSemaphoreTake(s.mtx, portMAX_DELAY);
    snprintf(buf, sizeof(buf),
        "{\"local_version\":\"%s\","
        "\"remote_version\":\"%s\","
        "\"state\":\"%s\","
        "\"progress\":%d,"
        "\"notes\":\"%s\","
        "\"error\":\"%s\","
        "\"check_url\":\"%s\","
        "\"auto_check\":%s,"
        "\"interval_min\":%lu}",
        app->version,
        s.remote_ver,
        STATE_STR[s.state],
        s.progress,
        s.notes,
        s.error,
        s.check_url,
        s.auto_check ? "true" : "false",
        (unsigned long)s.interval_min);
    xSemaphoreGive(s.mtx);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, (ssize_t)strlen(buf));
    return ESP_OK;
}

static esp_err_t h_config(httpd_req_t *req)
{
    char body[384];
    int n = httpd_req_recv(req, body, (int)sizeof(body) - 1);
    if (n <= 0) return ESP_FAIL;
    body[n] = '\0';

    xSemaphoreTake(s.mtx, portMAX_DELAY);

    char url[OTA_MAX_URL] = {0};
    jget(body, "url", url, sizeof(url));
    if (url[0]) snprintf(s.check_url, sizeof(s.check_url), "%s", url);

    if (strstr(body, "\"auto_check\":")) s.auto_check = jget_bool(body, "auto_check");

    long iv = jget_long(body, "interval");
    if (iv >= 1 && iv <= 10080) s.interval_min = (uint32_t)iv;

    /* Restart periodic timer if configured */
    if (s.timer) {
        esp_timer_stop(s.timer);
        if (s.auto_check && s.check_url[0] && s.interval_min > 0) {
            uint64_t period = (uint64_t)s.interval_min * 60ULL * 1000000ULL;
            esp_timer_start_periodic(s.timer, period);
            ESP_LOGI(TAG, "Auto-check every %lu min", (unsigned long)s.interval_min);
        }
    }
    nvs_save();
    xSemaphoreGive(s.mtx);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

static esp_err_t h_check(httpd_req_t *req)
{
    xSemaphoreTake(s.mtx, portMAX_DELAY);
    ota_state_t cur = s.state;
    xSemaphoreGive(s.mtx);

    if (cur == OTA_STATE_CHECKING || cur == OTA_STATE_UPDATING) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"busy\"}");
        return ESP_OK;
    }
    xTaskCreate(check_task, "ota_chk", OTA_CHK_STACK, NULL, 5, NULL);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"checking\"}");
    return ESP_OK;
}

static esp_err_t h_update(httpd_req_t *req)
{
    xSemaphoreTake(s.mtx, portMAX_DELAY);
    ota_state_t cur     = s.state;
    bool        has_url = (s.dl_url[0] != '\0');
    xSemaphoreGive(s.mtx);

    if (cur == OTA_STATE_CHECKING || cur == OTA_STATE_UPDATING) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"busy\"}");
        return ESP_OK;
    }
    if (!has_url) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"msg\":\"no firmware URL\"}");
        return ESP_OK;
    }
    xTaskCreate(update_task, "ota_dl", OTA_TASK_STACK, NULL, 5, NULL);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"updating\"}");
    return ESP_OK;
}

/* ---- Public API ---- */
esp_err_t ota_manager_init(void)
{
    s.mtx = xSemaphoreCreateMutex();

    /* Mark app valid to cancel any pending rollback */
    esp_err_t rv = esp_ota_mark_app_valid_cancel_rollback();
    if (rv == ESP_OK) {
        ESP_LOGI(TAG, "App marked valid (rollback cancelled)");
    }

    nvs_load();

    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGI(TAG, "Version: %s | Check URL: %s",
             app->version,
             s.check_url[0] ? s.check_url : "(not configured)");

    esp_timer_create_args_t ta = {
        .callback = timer_cb,
        .name     = "ota_timer",
    };
    esp_timer_create(&ta, &s.timer);

    if (s.auto_check && s.check_url[0] && s.interval_min > 0) {
        uint64_t period = (uint64_t)s.interval_min * 60ULL * 1000000ULL;
        esp_timer_start_periodic(s.timer, period);
        ESP_LOGI(TAG, "Auto-check enabled: every %lu min", (unsigned long)s.interval_min);
    }
    return ESP_OK;
}

esp_err_t ota_manager_register_routes(void)
{
    const api_route_t routes[] = {
        { "/api/ota/status", HTTP_GET,  h_status },
        { "/api/ota/config", HTTP_POST, h_config },
        { "/api/ota/check",  HTTP_POST, h_check  },
        { "/api/ota/update", HTTP_POST, h_update },
    };
    for (int i = 0; i < (int)(sizeof(routes)/sizeof(routes[0])); i++) {
        ESP_ERROR_CHECK(http_server_register_route(&routes[i]));
    }
    return ESP_OK;
}
