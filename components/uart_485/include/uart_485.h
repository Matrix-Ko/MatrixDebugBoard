#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

esp_err_t uart_485_init(void);
esp_err_t uart_485_register_routes(void);
esp_err_t uart_485_set_baud(int baud);
esp_err_t uart_485_set_format(int bits, int parity, int stop);
esp_err_t uart_485_send_hex(const char *hex_str);
esp_err_t uart_485_send_bytes(const uint8_t *data, size_t len);
/* Non-blocking enqueue for WS dispatch — sends confirmation via WebSocket after TX */
esp_err_t uart_485_enqueue(const char *hex_str);
