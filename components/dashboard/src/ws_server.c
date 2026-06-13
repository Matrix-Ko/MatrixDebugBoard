#include "ws_server.h"
#include "uart_485.h"
#include "uart_debug.h"
#include "can_twai.h"
#include "spi_flash_ext.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "WS";

#define MAX_CLIENTS 4

static httpd_handle_t s_hd   = NULL;
static int s_fds[MAX_CLIENTS];
static int s_fd_cnt = 0;
static SemaphoreHandle_t s_mutex;

/* ---- client list ---- */
static void fd_add(int fd) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 0; i < s_fd_cnt; i++)
        if (s_fds[i] == fd) { xSemaphoreGive(s_mutex); return; }
    if (s_fd_cnt < MAX_CLIENTS) s_fds[s_fd_cnt++] = fd;
    xSemaphoreGive(s_mutex);
}

static void fd_remove(int fd) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 0; i < s_fd_cnt; i++) {
        if (s_fds[i] == fd) {
            s_fds[i] = s_fds[--s_fd_cnt];
            break;
        }
    }
    xSemaphoreGive(s_mutex);
}

/* ---- JSON helpers ---- */
static void json_get_str(const char *json, const char *key, char *out, size_t maxlen) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":\"", key);
    const char *p = strstr(json, search);
    if (!p) { out[0] = '\0'; return; }
    p += strlen(search);
    const char *e = strchr(p, '"');
    if (!e) { out[0] = '\0'; return; }
    size_t n = e - p;
    if (n >= maxlen) n = maxlen - 1;
    memcpy(out, p, n);
    out[n] = '\0';
}

static long json_get_int(const char *json, const char *key) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *p = strstr(json, search);
    if (!p) return -1;
    p += strlen(search);
    while (*p == ' ') p++;
    return strtol(p, NULL, 0);
}

static bool json_get_bool(const char *json, const char *key) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *p = strstr(json, search);
    if (!p) return false;
    p += strlen(search);
    while (*p == ' ') p++;
    return (*p == 't');
}

/* ---- dispatch incoming message ---- */
static void dispatch(const char *json, size_t len)
{
    (void)len;
    char type[32] = {0};
    json_get_str(json, "type", type, sizeof(type));

    if (strcmp(type, "rs485_send") == 0) {
        char data[512] = {0};
        json_get_str(json, "data", data, sizeof(data));
        /* Non-blocking: pushes to queue, tx_task sends and replies rs485_tx_done */
        uart_485_enqueue(data);

    } else if (strcmp(type, "rs485_cfg") == 0) {
        int baud   = (int)json_get_int(json, "baud");
        int bits   = (int)json_get_int(json, "bits");
        int parity = (int)json_get_int(json, "parity");
        int stop   = (int)json_get_int(json, "stop");
        if (baud > 0) uart_485_set_baud(baud);
        if (bits > 0 || parity >= 0 || stop > 0)
            uart_485_set_format(
                bits   > 0 ? bits   : 8,
                parity >= 0 ? parity : 0,
                stop   > 0 ? stop   : 1);

    } else if (strcmp(type, "can_send") == 0) {
        uint32_t id   = (uint32_t)json_get_int(json, "id");
        bool     ext  = json_get_bool(json, "ext");
        bool     rtr  = json_get_bool(json, "rtr");
        int      dlc  = (int)json_get_int(json, "dlc");
        char     data[32] = {0};
        json_get_str(json, "data", data, sizeof(data));
        can_twai_send(id, ext, rtr, dlc, data);

    } else if (strcmp(type, "can_cfg") == 0) {
        int baud = (int)json_get_int(json, "baud");
        char mode[32] = {0};
        json_get_str(json, "mode", mode, sizeof(mode));
        if (baud > 0 || mode[0]) can_twai_reconfigure(baud, mode);

    } else if (strcmp(type, "flash_info") == 0) {
        char buf[128];
        spi_flash_ext_get_info_json(buf, sizeof(buf));
        ws_broadcast(buf);

    } else if (strcmp(type, "flash_read") == 0) {
        uint32_t addr = (uint32_t)json_get_int(json, "addr");
        uint32_t rlen = (uint32_t)json_get_int(json, "len");
        if (rlen == 0 || rlen > 1024) rlen = 256;
        spi_flash_ext_read_ws(addr, rlen);

    } else if (strcmp(type, "flash_write") == 0) {
        uint32_t addr = (uint32_t)json_get_int(json, "addr");
        char data[2200] = {0};
        json_get_str(json, "data", data, sizeof(data));
        spi_flash_ext_write_ws(addr, data);

    } else if (strcmp(type, "flash_erase") == 0) {
        uint32_t addr = (uint32_t)json_get_int(json, "addr");
        uint32_t sz   = (uint32_t)json_get_int(json, "size");
        spi_flash_ext_erase_ws(addr, sz);

    } else if (strcmp(type, "uart_send") == 0) {
        int port = (int)json_get_int(json, "port");
        char data[512] = {0};
        json_get_str(json, "data", data, sizeof(data));
        uart_debug_send_hex(port, data);

    } else if (strcmp(type, "uart_cfg") == 0) {
        int port   = (int)json_get_int(json, "port");
        int baud   = (int)json_get_int(json, "baud");
        int bits   = (int)json_get_int(json, "bits");
        int parity = (int)json_get_int(json, "parity");
        int stop   = (int)json_get_int(json, "stop");
        if (baud > 0) uart_debug_set_baud(port, baud);
        if (bits > 0 || parity >= 0 || stop > 0)
            uart_debug_set_format(port,
                bits   > 0 ? bits   : 8,
                parity >= 0 ? parity : 0,
                stop   > 0 ? stop   : 1);

    } else if (strcmp(type, "relay_set") == 0) {
        int val = (int)json_get_int(json, "value");
        extern void relay_set(int v);
        relay_set(val ? 1 : 0);
    }
}

/* ---- WS handler ---- */
static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        fd_add(httpd_req_to_sockfd(req));
        ESP_LOGI(TAG, "Client connected fd=%d", httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    httpd_ws_frame_t frm = {0};
    frm.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &frm, 0);
    if (ret != ESP_OK) return ret;

    if (frm.len == 0) return ESP_OK;
    if (frm.len > 4096) return ESP_ERR_INVALID_SIZE;

    uint8_t *buf = calloc(1, frm.len + 1);
    if (!buf) return ESP_ERR_NO_MEM;
    frm.payload = buf;

    ret = httpd_ws_recv_frame(req, &frm, frm.len);
    if (ret == ESP_OK && frm.type == HTTPD_WS_TYPE_TEXT)
        dispatch((char *)buf, frm.len);

    free(buf);
    return ESP_OK;
}

/* ---- broadcast ---- */
void ws_broadcast(const char *json)
{
    if (!s_hd || !json) return;
    size_t jlen = strlen(json);

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int fds[MAX_CLIENTS];
    int cnt = s_fd_cnt;
    memcpy(fds, s_fds, cnt * sizeof(int));
    xSemaphoreGive(s_mutex);

    for (int i = 0; i < cnt; i++) {
        httpd_ws_frame_t frm = {
            .type    = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t *)json,
            .len     = jlen,
        };
        esp_err_t err = httpd_ws_send_frame_async(s_hd, fds[i], &frm);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Send failed fd=%d, removing", fds[i]);
            fd_remove(fds[i]);
        }
    }
}

esp_err_t ws_server_register(httpd_handle_t server)
{
    s_hd    = server;
    s_mutex = xSemaphoreCreateMutex();
    memset(s_fds, -1, sizeof(s_fds));

    httpd_uri_t u = {
        .uri         = "/ws",
        .method      = HTTP_GET,
        .handler     = ws_handler,
        .is_websocket = true,
    };
    esp_err_t ret = httpd_register_uri_handler(server, &u);
    ESP_LOGI(TAG, "WS /ws registered");
    return ret;
}
