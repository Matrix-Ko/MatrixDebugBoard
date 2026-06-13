#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

esp_err_t uart_debug_init(void);
esp_err_t uart_debug_register_routes(void);
esp_err_t uart_debug_send(int port, const uint8_t *data, size_t len);
esp_err_t uart_debug_send_hex(int port, const char *hex_str);
esp_err_t uart_debug_set_baud(int port, int baud);
esp_err_t uart_debug_set_format(int port, int bits, int parity, int stop);
