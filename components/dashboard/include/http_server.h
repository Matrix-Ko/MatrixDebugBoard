#pragma once
#include "esp_http_server.h"
#include "esp_err.h"

typedef struct {
    const char       *uri;
    httpd_method_t    method;
    esp_err_t       (*handler)(httpd_req_t *req);
} api_route_t;

esp_err_t       http_server_init(void);
esp_err_t       http_server_register_route(const api_route_t *route);
httpd_handle_t  http_server_get_handle(void);
