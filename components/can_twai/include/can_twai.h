#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

esp_err_t can_twai_init(void);
esp_err_t can_twai_register_routes(void);
esp_err_t can_twai_send(uint32_t id, bool ext, bool rtr, int dlc, const char *hex_data);
esp_err_t can_twai_reconfigure(int baud, const char *mode);
