#pragma once
#include "esp_wifi.h"
#include "esp_netif.h"

typedef enum {
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_AP_MODE,
} wifi_state_t;

esp_err_t    wifi_manager_init(void);
esp_err_t    wifi_manager_start_ap(void);
esp_err_t    wifi_manager_connect(const char *ssid, const char *password);
esp_err_t    wifi_manager_disconnect(void);
esp_err_t    wifi_manager_scan(void);
wifi_state_t wifi_manager_get_state(void);
char        *wifi_manager_get_current_ip(void);
char        *wifi_manager_get_status_json(void);
char        *wifi_manager_get_scan_results_json(void);
esp_err_t    wifi_manager_set_ap_config(const char *ssid, const char *pass);
