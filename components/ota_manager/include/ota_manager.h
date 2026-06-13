#pragma once
#include "esp_err.h"

/* Initialize OTA manager: load NVS config, mark app valid, start auto-check timer */
esp_err_t ota_manager_init(void);

/* Register HTTP API routes with the dashboard HTTP server */
esp_err_t ota_manager_register_routes(void);
