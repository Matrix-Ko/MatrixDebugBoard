#include "http_server.h"
#include "ws_server.h"
#include "dashboard.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "spi_flash_ext.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "HTTP";
static httpd_handle_t s_server = NULL;

extern void wifi_module_get_ip(char *buf, size_t len);
extern const char *wifi_module_get_state(void);

static esp_err_t h_sys_info(httpd_req_t *req) {
    char ip[16] = "N/A";
    wifi_module_get_ip(ip, sizeof(ip));
    const char *ws = wifi_module_get_state();
    const esp_app_desc_t *app = esp_app_get_description();
    char buf[200];
    snprintf(buf, sizeof(buf),
        "{\"chip\":\"ESP32-S3\",\"version\":\"%s\",\"ip\":\"%s\",\"wifi_status\":\"%s\","
        "\"ap_ip\":\"192.168.4.1\"}",
        app->version, ip, ws);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, strlen(buf));
    return ESP_OK;
}

static esp_err_t h_sys_stats(httpd_req_t *req) {
    /* ── Memory ── */
    size_t free_heap  = esp_get_free_heap_size();
    size_t total_heap = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    size_t used_heap  = total_heap - free_heap;

    /* ── CPU load via FreeRTOS runtime stats ── */
    uint32_t cpu_load = 0;
    UBaseType_t task_count = uxTaskGetNumberOfTasks();
    TaskStatus_t *tasks = malloc(task_count * sizeof(TaskStatus_t));
    uint32_t total_runtime = 0;
    if (tasks) {
        task_count = uxTaskGetSystemState(tasks, task_count, &total_runtime);
        uint32_t idle_time = 0;
        for (UBaseType_t i = 0; i < task_count; i++) {
            /* Idle task names: "IDLE", "IDLE0", "IDLE1" */
            if (strncmp(tasks[i].pcTaskName, "IDLE", 4) == 0) {
                idle_time += tasks[i].ulRunTimeCounter;
            }
        }
        /* ESP32-S3 dual-core: available time = total_runtime × 2 */
        if (total_runtime > 0) {
            uint64_t busy = (uint64_t)total_runtime * 2 - idle_time;
            cpu_load = (uint32_t)(busy * 100 / ((uint64_t)total_runtime * 2));
            if (cpu_load > 100) cpu_load = 100;
        }
        free(tasks);
    }

    /* ── External flash file storage ── */
    int   fm_count = 0;
    uint32_t fm_used = 0;
    fm_get_stats(&fm_count, &fm_used);
    /* Total usable file area: 16MB - 4KB(dir) - 4KB(reserved) */
    uint32_t fm_total = (uint32_t)(16UL * 1024 * 1024 - 8192);

    char buf[320];
    snprintf(buf, sizeof(buf),
        "{\"cpu_pct\":%lu,\"heap_total\":%zu,\"heap_used\":%zu,\"heap_free\":%zu,"
        "\"fm_total\":%lu,\"fm_used\":%lu,\"fm_files\":%d,\"task_count\":%u}",
        (unsigned long)cpu_load,
        total_heap, used_heap, free_heap,
        (unsigned long)fm_total, (unsigned long)fm_used, fm_count,
        (unsigned)task_count);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, strlen(buf));
    return ESP_OK;
}

static void deferred_reboot(void *arg) {
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(600));
    esp_restart();
}

static esp_err_t h_reboot(httpd_req_t *req) {
    httpd_resp_sendstr(req, "{\"status\":\"rebooting\"}");
    xTaskCreate(deferred_reboot, "reboot", 2048, NULL, 5, NULL);
    return ESP_OK;
}

static esp_err_t h_relay_set(httpd_req_t *req) {
    char body[64];
    int n = httpd_req_recv(req, body, sizeof(body) - 1);
    if (n <= 0) return ESP_FAIL;
    body[n] = '\0';
    int val = 0;
    char *p = strstr(body, "\"value\":");
    if (p) val = atoi(p + 8);
    extern void relay_set(int v);
    relay_set(val ? 1 : 0);
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"status\":\"ok\",\"relay\":%d}", val ? 1 : 0);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, strlen(buf));
    return ESP_OK;
}

static esp_err_t h_relay_status(httpd_req_t *req) {
    extern int s_relay_state;
    char buf[32];
    snprintf(buf, sizeof(buf), "{\"relay\":%d}", s_relay_state);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, strlen(buf));
    return ESP_OK;
}

extern esp_err_t web_assets_register(httpd_handle_t server);

esp_err_t http_server_init(void) {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.uri_match_fn      = httpd_uri_match_wildcard;
    cfg.max_uri_handlers  = 48;
    cfg.stack_size        = 8192;

    ESP_ERROR_CHECK(httpd_start(&s_server, &cfg));
    ESP_LOGI(TAG, "Started on port %d", cfg.server_port);

    ws_server_register(s_server);
    web_assets_register(s_server);

    httpd_uri_t u_info        = {.uri="/api/system/info",   .method=HTTP_GET,  .handler=h_sys_info,     .is_websocket=false};
    httpd_uri_t u_stats       = {.uri="/api/system/stats",  .method=HTTP_GET,  .handler=h_sys_stats,    .is_websocket=false};
    httpd_uri_t u_reboot      = {.uri="/api/system/reboot", .method=HTTP_POST, .handler=h_reboot,       .is_websocket=false};
    httpd_uri_t u_relay_set   = {.uri="/api/relay/set",     .method=HTTP_POST, .handler=h_relay_set,    .is_websocket=false};
    httpd_uri_t u_relay_status= {.uri="/api/relay/status",  .method=HTTP_GET,  .handler=h_relay_status, .is_websocket=false};
    httpd_register_uri_handler(s_server, &u_info);
    httpd_register_uri_handler(s_server, &u_stats);
    httpd_register_uri_handler(s_server, &u_reboot);
    httpd_register_uri_handler(s_server, &u_relay_set);
    httpd_register_uri_handler(s_server, &u_relay_status);
    return ESP_OK;
}

esp_err_t http_server_register_route(const api_route_t *r) {
    httpd_uri_t u = {.uri=r->uri, .method=r->method, .handler=r->handler, .is_websocket=false};
    return httpd_register_uri_handler(s_server, &u);
}

httpd_handle_t http_server_get_handle(void) { return s_server; }
