#pragma once
#include "esp_err.h"
#include "dashboard.h"

esp_err_t    wifi_module_init(void);
esp_err_t    wifi_module_register_routes(void);
void         wifi_module_get_ip(char *buf, size_t len);
const char  *wifi_module_get_state(void);

extern const dashboard_module_t wifi_module;
