#include "wifi_module.h"
#include "wifi_manager.h"
#include "wifi_storage.h"
#include "dashboard.h"
#include "http_server.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "WIFI_MOD";

extern const char wifi_html_start[] asm("_binary_wifi_html_start");
extern const char wifi_html_end[]   asm("_binary_wifi_html_end");

static esp_err_t h_status(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    char *j = wifi_manager_get_status_json();
    httpd_resp_send(req, j, strlen(j));
    return ESP_OK;
}

static esp_err_t h_scan(httpd_req_t *req) {
    wifi_manager_scan();
    char *j = wifi_manager_get_scan_results_json();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, j, strlen(j));
    return ESP_OK;
}

static esp_err_t h_connect(httpd_req_t *req) {
    char buf[256];
    int n = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (n <= 0) return ESP_FAIL;
    buf[n] = '\0';
    char ssid[64] = {0}, pwd[64] = {0};
    char *p;
    if ((p = strstr(buf, "\"ssid\":\""))) {
        p += 8;
        char *e = strchr(p, '"');
        if (e && (e - p) < 64) memcpy(ssid, p, e - p);
    }
    if ((p = strstr(buf, "\"password\":\""))) {
        p += 12;
        char *e = strchr(p, '"');
        if (e && (e - p) < 64) memcpy(pwd, p, e - p);
    }
    if (ssid[0]) wifi_manager_connect(ssid, pwd);
    httpd_resp_sendstr(req, "{\"status\":\"connecting\"}");
    return ESP_OK;
}

static esp_err_t h_disconnect(httpd_req_t *req) {
    wifi_manager_disconnect();
    httpd_resp_sendstr(req, "{\"status\":\"disconnected\"}");
    return ESP_OK;
}

static esp_err_t h_clear(httpd_req_t *req) {
    wifi_storage_clear();
    httpd_resp_sendstr(req, "{\"status\":\"cleared\"}");
    return ESP_OK;
}

static esp_err_t h_ap_config(httpd_req_t *req) {
    char buf[256];
    int n = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (n <= 0) return ESP_FAIL;
    buf[n] = '\0';
    char ssid[32] = {0}, pass[64] = {0};
    char *p;
    if ((p = strstr(buf, "\"ap_ssid\":\""))) {
        p += 11;
        char *e = strchr(p, '"');
        if (e && (e - p) < (int)sizeof(ssid)) memcpy(ssid, p, e - p);
    }
    if ((p = strstr(buf, "\"ap_password\":\""))) {
        p += 15;
        char *e = strchr(p, '"');
        if (e && (e - p) < (int)sizeof(pass)) memcpy(pass, p, e - p);
    }
    if (!ssid[0]) {
        httpd_resp_sendstr(req, "{\"error\":\"ap_ssid required\"}");
        return ESP_OK;
    }
    esp_err_t ret = wifi_manager_set_ap_config(ssid, pass[0] ? pass : NULL);
    httpd_resp_sendstr(req, ret == ESP_OK ? "{\"status\":\"ok\"}" : "{\"status\":\"err\"}");
    return ESP_OK;
}

static esp_err_t h_wifi_html(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, wifi_html_start, wifi_html_end - wifi_html_start);
    return ESP_OK;
}

static const api_route_t s_routes[] = {
    {"/api/wifi/status",     HTTP_GET,  h_status},
    {"/api/wifi/scan",       HTTP_GET,  h_scan},
    {"/api/wifi/connect",    HTTP_POST, h_connect},
    {"/api/wifi/disconnect", HTTP_POST, h_disconnect},
    {"/api/wifi/clear",      HTTP_POST, h_clear},
    {"/api/wifi/ap_config",  HTTP_POST, h_ap_config},
    {"/modules/wifi.html",   HTTP_GET,  h_wifi_html},
    {NULL, 0, NULL},
};

esp_err_t wifi_module_init(void) {
    wifi_storage_init();
    wifi_manager_init();
    char ssid[64] = {0}, pwd[64] = {0};
    if (wifi_storage_load(ssid, sizeof(ssid), pwd, sizeof(pwd)) == ESP_OK && ssid[0]) {
        ESP_LOGI(TAG, "Auto-connect → %s", ssid);
        wifi_manager_connect(ssid, pwd);
    } else {
        ESP_LOGI(TAG, "No saved config, AP-only mode");
    }
    return ESP_OK;
}

esp_err_t wifi_module_register_routes(void) {
    for (int i = 0; s_routes[i].uri; i++)
        http_server_register_route(&s_routes[i]);
    ESP_LOGI(TAG, "Routes registered");
    return ESP_OK;
}

void wifi_module_get_ip(char *buf, size_t len) {
    char *ip = wifi_manager_get_current_ip();
    strlcpy(buf, (ip && ip[0]) ? ip : "N/A", len);
}

const char *wifi_module_get_state(void) {
    switch (wifi_manager_get_state()) {
        case WIFI_STATE_CONNECTED:  return "connected";
        case WIFI_STATE_CONNECTING: return "connecting";
        case WIFI_STATE_AP_MODE:    return "ap_mode";
        default:                    return "disconnected";
    }
}

const dashboard_module_t wifi_module = {
    .name      = "wifi",
    .tab_label = "WiFi",
    .init      = wifi_module_init,
};
