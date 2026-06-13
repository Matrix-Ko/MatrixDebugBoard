#include "http_server.h"
#include "esp_log.h"

extern const char index_html_start[]  asm("_binary_index_html_start");
extern const char index_html_end[]    asm("_binary_index_html_end");
extern const char style_css_start[]   asm("_binary_style_css_start");
extern const char style_css_end[]     asm("_binary_style_css_end");
extern const char app_js_start[]      asm("_binary_app_js_start");
extern const char app_js_end[]        asm("_binary_app_js_end");
extern const char rs485_html_start[]  asm("_binary_rs485_html_start");
extern const char rs485_html_end[]    asm("_binary_rs485_html_end");
extern const char can_html_start[]    asm("_binary_can_html_start");
extern const char can_html_end[]      asm("_binary_can_html_end");
extern const char flash_html_start[]  asm("_binary_flash_html_start");
extern const char flash_html_end[]    asm("_binary_flash_html_end");
extern const char system_html_start[] asm("_binary_system_html_start");
extern const char system_html_end[]   asm("_binary_system_html_end");
extern const char uart_html_start[]   asm("_binary_uart_html_start");
extern const char uart_html_end[]     asm("_binary_uart_html_end");
extern const char ota_html_start[]    asm("_binary_ota_html_start");
extern const char ota_html_end[]      asm("_binary_ota_html_end");
extern const char ai_html_start[]     asm("_binary_ai_html_start");
extern const char ai_html_end[]       asm("_binary_ai_html_end");

#define HANDLER(name, ctype, start, end) \
static esp_err_t name(httpd_req_t *req) { \
    httpd_resp_set_type(req, ctype); \
    httpd_resp_send(req, start, end - start); \
    return ESP_OK; \
}

HANDLER(h_index,  "text/html",              index_html_start,  index_html_end)
HANDLER(h_style,  "text/css",               style_css_start,   style_css_end)
HANDLER(h_app,    "application/javascript", app_js_start,      app_js_end)
HANDLER(h_rs485,  "text/html",              rs485_html_start,  rs485_html_end)
HANDLER(h_can,    "text/html",              can_html_start,    can_html_end)
HANDLER(h_flash,  "text/html",              flash_html_start,  flash_html_end)
HANDLER(h_system, "text/html",              system_html_start, system_html_end)
HANDLER(h_uart,   "text/html",              uart_html_start,   uart_html_end)
HANDLER(h_ota,    "text/html",              ota_html_start,    ota_html_end)
HANDLER(h_ai,     "text/html",              ai_html_start,     ai_html_end)

esp_err_t web_assets_register(httpd_handle_t server) {
#define URI(u, h) do { \
    httpd_uri_t _u = {.uri=(u), .method=HTTP_GET, .handler=(h), .is_websocket=false}; \
    httpd_register_uri_handler(server, &_u); \
} while(0)
    URI("/",                    h_index);
    URI("/style.css",           h_style);
    URI("/app.js",              h_app);
    URI("/modules/rs485.html",  h_rs485);
    URI("/modules/can.html",    h_can);
    URI("/modules/flash.html",  h_flash);
    URI("/modules/system.html", h_system);
    URI("/modules/uart.html",   h_uart);
    URI("/modules/ota.html",    h_ota);
    URI("/modules/ai.html",     h_ai);
#undef URI
    return ESP_OK;
}
