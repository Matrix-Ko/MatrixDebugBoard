#pragma once
#include "esp_err.h"
#include <stdbool.h>

esp_err_t wifi_storage_init(void);
esp_err_t wifi_storage_save(const char *ssid, const char *password);
esp_err_t wifi_storage_load(char *ssid, size_t ssid_len, char *password, size_t pwd_len);
bool      wifi_storage_has_config(void);
esp_err_t wifi_storage_clear(void);
esp_err_t wifi_storage_save_ap(const char *ssid, const char *password);
esp_err_t wifi_storage_load_ap(char *ssid, size_t ssid_len, char *password, size_t pwd_len);
