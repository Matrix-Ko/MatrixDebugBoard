#include "dashboard.h"
#include "http_server.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "DASHBOARD";

/* ── Send-history NVS API ─────────────────────────────────────────────────── */
#define HIST_NVS_NS  "send_hist"
#define HIST_BUF_LEN 2048

static bool hist_key_ok(const char *k) {
    return k && (strcmp(k,"rs485")==0 || strcmp(k,"uart0")==0 ||
                 strcmp(k,"uart1")==0 || strcmp(k,"can")==0);
}

static esp_err_t h_hist_get(httpd_req_t *req) {
    char qs[32]={0}, key[16]={0};
    if (httpd_req_get_url_query_str(req, qs, sizeof(qs)) == ESP_OK)
        httpd_query_key_value(qs, "key", key, sizeof(key));
    static char buf[HIST_BUF_LEN];
    strcpy(buf, "[]");
    if (hist_key_ok(key)) {
        nvs_handle_t h;
        if (nvs_open(HIST_NVS_NS, NVS_READONLY, &h) == ESP_OK) {
            size_t len = sizeof(buf);
            if (nvs_get_str(h, key, buf, &len) != ESP_OK) strcpy(buf, "[]");
            nvs_close(h);
        }
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, strlen(buf));
    return ESP_OK;
}

static esp_err_t h_hist_save(httpd_req_t *req) {
    char qs[32]={0}, key[16]={0};
    if (httpd_req_get_url_query_str(req, qs, sizeof(qs)) == ESP_OK)
        httpd_query_key_value(qs, "key", key, sizeof(key));
    if (!hist_key_ok(key)) {
        httpd_resp_sendstr(req, "{\"error\":\"invalid key\"}");
        return ESP_OK;
    }
    static char body[HIST_BUF_LEN];
    int n = httpd_req_recv(req, body, sizeof(body) - 1);
    if (n <= 0) return ESP_FAIL;
    body[n] = '\0';
    nvs_handle_t h;
    if (nvs_open(HIST_NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, key, body);
        nvs_commit(h);
        nvs_close(h);
    }
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

static const api_route_t s_hist_routes[] = {
    {"/api/hist", HTTP_GET,  h_hist_get},
    {"/api/hist", HTTP_POST, h_hist_save},
    {NULL, 0, NULL},
};

/* ── Public API ───────────────────────────────────────────────────────────── */

esp_err_t dashboard_init(void) {
    ESP_LOGI(TAG, "Init");
    return ESP_OK;
}

esp_err_t dashboard_start(void) {
    esp_err_t ret = http_server_init();
    if (ret != ESP_OK) return ret;
    for (int i = 0; s_hist_routes[i].uri; i++)
        http_server_register_route(&s_hist_routes[i]);
    return ESP_OK;
}

esp_err_t dashboard_register_module(const dashboard_module_t *mod) {
    (void)mod;
    return ESP_OK;
}
