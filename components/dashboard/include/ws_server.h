#pragma once
#include "esp_http_server.h"
#include "esp_err.h"

/* Register WebSocket handler on /ws */
esp_err_t ws_server_register(httpd_handle_t server);

/* Broadcast a JSON string to all connected WS clients */
void ws_broadcast(const char *json);
