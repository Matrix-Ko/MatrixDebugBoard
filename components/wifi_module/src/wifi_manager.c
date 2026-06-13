#include "wifi_manager.h"
#include "wifi_storage.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/timers.h"
#include <string.h>

static const char *TAG = "WIFI_MGR";

#define AP_SSID_DEFAULT "MatrixDebug"
#define AP_PASS_DEFAULT "12345678"
#define MAX_RETRY      5
#define RETRY_DELAY_MS 30000
#define SCAN_TIMEOUT_MS 10000

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_event_group;
static SemaphoreHandle_t  s_scan_sem;
static wifi_state_t       s_state = WIFI_STATE_DISCONNECTED;
static int                s_retry = 0;
static char               s_ssid[64]    = {0};
static char               s_ip[16]      = {0};
static char               s_ap_ssid[32] = AP_SSID_DEFAULT;
static char               s_ap_pass[64] = AP_PASS_DEFAULT;
static bool               s_scan_busy = false;
static TimerHandle_t      s_retry_timer = NULL;

static wifi_ap_record_t   s_scan_res[20];
static uint16_t           s_scan_cnt  = 0;

/* Forward */
static void retry_connect_cb(TimerHandle_t xTimer);

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START) {
            /* STA started but may not have credentials yet — don't auto-connect */
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            s_state = WIFI_STATE_DISCONNECTED;
            memset(s_ip, 0, sizeof(s_ip));
            if (s_retry < MAX_RETRY) {
                s_retry++;
                ESP_LOGI(TAG, "Reconnect %d/%d", s_retry, MAX_RETRY);
                esp_wifi_connect();
            } else {
                /* Back-off: wait RETRY_DELAY_MS then try once more */
                ESP_LOGW(TAG, "Max retries, back-off %d s", RETRY_DELAY_MS / 1000);
                xEventGroupSetBits(s_event_group, WIFI_FAIL_BIT);
                if (s_retry_timer) xTimerStart(s_retry_timer, 0);
            }
        } else if (id == WIFI_EVENT_AP_STACONNECTED) {
            ESP_LOGI(TAG, "AP: client connected");
        } else if (id == WIFI_EVENT_SCAN_DONE) {
            wifi_event_sta_scan_done_t *e = (wifi_event_sta_scan_done_t *)data;
            if (e->status != 0) ESP_LOGW(TAG, "Scan status %d", e->status);
            xSemaphoreGive(s_scan_sem);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&e->ip_info.ip));
        ESP_LOGI(TAG, "IP: %s", s_ip);
        s_retry = 0;
        s_state = WIFI_STATE_CONNECTED;
        xEventGroupSetBits(s_event_group, WIFI_CONNECTED_BIT);
        if (s_retry_timer) xTimerStop(s_retry_timer, 0);
        wifi_config_t cfg;
        if (esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK)
            strncpy(s_ssid, (char *)cfg.sta.ssid, sizeof(s_ssid) - 1);
    }
}

static void retry_connect_cb(TimerHandle_t xTimer)
{
    (void)xTimer;
    ESP_LOGI(TAG, "Back-off retry");
    s_retry = 0;
    esp_wifi_connect();
    s_state = WIFI_STATE_CONNECTING;
}

esp_err_t wifi_manager_init(void)
{
    s_event_group = xEventGroupCreate();
    s_scan_sem    = xSemaphoreCreateBinary();
    s_retry_timer = xTimerCreate("wifi_retry", pdMS_TO_TICKS(RETRY_DELAY_MS),
                                 pdFALSE, NULL, retry_connect_cb);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               wifi_event_handler, NULL));

    /* Load AP config from NVS (falls back to defaults if not saved yet) */
    wifi_storage_load_ap(s_ap_ssid, sizeof(s_ap_ssid), s_ap_pass, sizeof(s_ap_pass));

    wifi_config_t ap_cfg = {
        .ap = {
            .channel        = 6,
            .max_connection = 4,
            .authmode       = WIFI_AUTH_WPA2_PSK,
        },
    };
    strlcpy((char *)ap_cfg.ap.ssid,     s_ap_ssid, sizeof(ap_cfg.ap.ssid));
    strlcpy((char *)ap_cfg.ap.password, s_ap_pass,  sizeof(ap_cfg.ap.password));
    ap_cfg.ap.ssid_len = strlen(s_ap_ssid);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_state = WIFI_STATE_AP_MODE;
    ESP_LOGI(TAG, "AP: %s  IP: 192.168.4.1", s_ap_ssid);
    return ESP_OK;
}

esp_err_t wifi_manager_start_ap(void)
{
    s_state = WIFI_STATE_AP_MODE;
    return ESP_OK;
}

esp_err_t wifi_manager_connect(const char *ssid, const char *password)
{
    esp_wifi_disconnect();

    wifi_config_t sta = {0};
    strlcpy((char *)sta.sta.ssid,     ssid,     sizeof(sta.sta.ssid));
    strlcpy((char *)sta.sta.password, password, sizeof(sta.sta.password));
    sta.sta.threshold.authmode = WIFI_AUTH_OPEN;
    sta.sta.pmf_cfg.capable    = true;
    sta.sta.pmf_cfg.required   = false;

    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &sta);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "set_config: %s", esp_err_to_name(ret)); return ret; }

    ret = esp_wifi_connect();
    if (ret != ESP_OK) { ESP_LOGE(TAG, "connect: %s", esp_err_to_name(ret)); return ret; }

    strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);
    s_state = WIFI_STATE_CONNECTING;
    s_retry = 0;

    wifi_storage_save(ssid, password);
    ESP_LOGI(TAG, "Connecting → %s", ssid);
    return ESP_OK;
}

esp_err_t wifi_manager_disconnect(void)
{
    esp_wifi_disconnect();
    s_state = WIFI_STATE_DISCONNECTED;
    memset(s_ssid, 0, sizeof(s_ssid));
    memset(s_ip,   0, sizeof(s_ip));
    return ESP_OK;
}

esp_err_t wifi_manager_scan(void)
{
    if (s_scan_busy) return ESP_ERR_INVALID_STATE;
    s_scan_busy = true;
    xSemaphoreTake(s_scan_sem, 0);

    wifi_scan_config_t cfg = {0};
    esp_err_t ret = esp_wifi_scan_start(&cfg, false);
    if (ret != ESP_OK) { s_scan_busy = false; return ret; }

    if (xSemaphoreTake(s_scan_sem, pdMS_TO_TICKS(SCAN_TIMEOUT_MS)) != pdTRUE) {
        esp_wifi_scan_stop();
        s_scan_cnt  = 0;
        s_scan_busy = false;
        return ESP_ERR_TIMEOUT;
    }

    s_scan_cnt = 20;
    ret = esp_wifi_scan_get_ap_records(&s_scan_cnt, s_scan_res);
    s_scan_busy = false;
    ESP_LOGI(TAG, "Scan done: %d APs", s_scan_cnt);
    return ret;
}

wifi_state_t wifi_manager_get_state(void)      { return s_state; }
char        *wifi_manager_get_current_ip(void) { return s_ip;    }

esp_err_t wifi_manager_set_ap_config(const char *ssid, const char *pass)
{
    if (!ssid || !ssid[0]) return ESP_ERR_INVALID_ARG;
    strncpy(s_ap_ssid, ssid, sizeof(s_ap_ssid) - 1);
    s_ap_ssid[sizeof(s_ap_ssid) - 1] = '\0';
    if (pass && pass[0]) {
        strncpy(s_ap_pass, pass, sizeof(s_ap_pass) - 1);
        s_ap_pass[sizeof(s_ap_pass) - 1] = '\0';
    }
    wifi_storage_save_ap(s_ap_ssid, s_ap_pass);

    wifi_config_t ap_cfg = {
        .ap = {
            .channel        = 6,
            .max_connection = 4,
            .authmode       = WIFI_AUTH_WPA2_PSK,
        },
    };
    strlcpy((char *)ap_cfg.ap.ssid,     s_ap_ssid, sizeof(ap_cfg.ap.ssid));
    strlcpy((char *)ap_cfg.ap.password, s_ap_pass,  sizeof(ap_cfg.ap.password));
    ap_cfg.ap.ssid_len = strlen(s_ap_ssid);

    esp_err_t ret = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    ESP_LOGI(TAG, "AP reconfigured: %s (%s)", s_ap_ssid,
             ret == ESP_OK ? "ok" : esp_err_to_name(ret));
    return ret;
}

char *wifi_manager_get_status_json(void)
{
    static char buf[256];
    const char *st;
    switch (s_state) {
        case WIFI_STATE_CONNECTED:    st = "connected";    break;
        case WIFI_STATE_CONNECTING:   st = "connecting";   break;
        case WIFI_STATE_AP_MODE:      st = "ap_mode";      break;
        default:                      st = "disconnected";
    }
    snprintf(buf, sizeof(buf),
        "{\"state\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\",\"ap_ssid\":\"%s\"}",
        st, s_ssid, s_ip, s_ap_ssid);
    return buf;
}

char *wifi_manager_get_scan_results_json(void)
{
    static char buf[2048];
    int off = 0, written = 0;
    off += snprintf(buf + off, sizeof(buf) - off, "{\"networks\":[");
    for (int i = 0; i < s_scan_cnt; i++) {
        if (s_scan_res[i].ssid[0] == '\0') continue;
        if (written++) off += snprintf(buf + off, sizeof(buf) - off, ",");
        /* Escape SSID */
        char esc[sizeof(s_scan_res[i].ssid) * 2 + 1] = {0};
        int e = 0;
        for (int s = 0; s_scan_res[i].ssid[s] && s < (int)sizeof(s_scan_res[i].ssid); s++) {
            char c = s_scan_res[i].ssid[s];
            if (c == '"' || c == '\\') esc[e++] = '\\';
            esc[e++] = c;
        }
        off += snprintf(buf + off, sizeof(buf) - off,
            "{\"ssid\":\"%s\",\"rssi\":%d,\"auth\":%s}",
            esc, s_scan_res[i].rssi,
            s_scan_res[i].authmode != WIFI_AUTH_OPEN ? "true" : "false");
    }
    off += snprintf(buf + off, sizeof(buf) - off, "]}");
    return buf;
}
