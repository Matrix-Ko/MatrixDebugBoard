#include "uart_debug.h"
#include "ws_server.h"
#include "http_server.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

static const char *TAG   = "UART_DBG";
#define NVS_NS            "uart_dbg"
#define BUF_SIZE          2048
#define TX_TIMEOUT_MS     50

typedef struct {
    uart_port_t port_num;
    int         tx_pin;
    int         rx_pin;
    int         baud;
    int         bits;    /* 7 or 8 */
    int         parity;  /* 0=None 1=Even 2=Odd */
    int         stop;    /* 1 or 2 */
    uint32_t    tx_bytes;
    uint32_t    rx_bytes;
    uint32_t    tx_frames;
    uint32_t    rx_frames;
    bool        running;
} uart_ch_t;

static uart_ch_t s_ch[2] = {
    { UART_NUM_0, GPIO_NUM_43, GPIO_NUM_44, 115200, 8, 0, 1, 0, 0, 0, 0, false },
    { UART_NUM_1, GPIO_NUM_17, GPIO_NUM_18, 115200, 8, 0, 1, 0, 0, 0, 0, false },
};

/* --- Map to IDF enums --- */
static uart_word_length_t bits_enum(int b) { return b == 7 ? UART_DATA_7_BITS : UART_DATA_8_BITS; }
static uart_parity_t      par_enum(int p)  { return p == 1 ? UART_PARITY_EVEN : p == 2 ? UART_PARITY_ODD : UART_PARITY_DISABLE; }
static uart_stop_bits_t   stop_enum(int s) { return s == 2 ? UART_STOP_BITS_2 : UART_STOP_BITS_1; }

/* --- NVS --- */
static void nvs_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    int32_t v;
    for (int i = 0; i < 2; i++) {
        char k[12];
        snprintf(k, sizeof(k), "baud%d", i);
        if (nvs_get_i32(h, k, &v) == ESP_OK && v > 0) s_ch[i].baud = (int)v;
        snprintf(k, sizeof(k), "bits%d", i);
        if (nvs_get_i32(h, k, &v) == ESP_OK && (v==7||v==8)) s_ch[i].bits = (int)v;
        snprintf(k, sizeof(k), "par%d", i);
        if (nvs_get_i32(h, k, &v) == ESP_OK && v>=0 && v<=2) s_ch[i].parity = (int)v;
        snprintf(k, sizeof(k), "stop%d", i);
        if (nvs_get_i32(h, k, &v) == ESP_OK && (v==1||v==2)) s_ch[i].stop = (int)v;
    }
    nvs_close(h);
}

static void nvs_save(int idx)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    char k[12];
    snprintf(k, sizeof(k), "baud%d", idx); nvs_set_i32(h, k, (int32_t)s_ch[idx].baud);
    snprintf(k, sizeof(k), "bits%d", idx); nvs_set_i32(h, k, (int32_t)s_ch[idx].bits);
    snprintf(k, sizeof(k), "par%d",  idx); nvs_set_i32(h, k, (int32_t)s_ch[idx].parity);
    snprintf(k, sizeof(k), "stop%d", idx); nvs_set_i32(h, k, (int32_t)s_ch[idx].stop);
    nvs_commit(h);
    nvs_close(h);
}

/* --- Hex decode --- */
static int hex_decode(const char *hex, uint8_t *out, size_t maxlen)
{
    int n = 0;
    const char *p = hex;
    while (*p && n < (int)maxlen) {
        while (*p == ' ' || *p == '\r' || *p == '\n') p++;
        if (!*p || !*(p+1)) break;
        char hi = *p++, lo = *p++;
        int hn = (hi>='0'&&hi<='9')?(hi-'0'):(hi>='a'&&hi<='f')?(hi-'a'+10):(hi>='A'&&hi<='F')?(hi-'A'+10):-1;
        int ln = (lo>='0'&&lo<='9')?(lo-'0'):(lo>='a'&&lo<='f')?(lo-'a'+10):(lo>='A'&&lo<='F')?(lo-'A'+10):-1;
        if (hn < 0 || ln < 0) continue;
        out[n++] = (uint8_t)((hn << 4) | ln);
    }
    return n;
}

/* --- RX task --- */
static void rx_task(void *arg)
{
    int idx = (int)(intptr_t)arg;
    uart_ch_t *ch = &s_ch[idx];
    uint8_t buf[256];
    char    json[700];

    while (1) {
        int n = uart_read_bytes(ch->port_num, buf, sizeof(buf) - 1, pdMS_TO_TICKS(20));
        if (n <= 0) continue;

        ch->rx_bytes  += (uint32_t)n;
        ch->rx_frames++;

        char hex[513] = {0};
        for (int i = 0; i < n && i < 256; i++)
            snprintf(hex + i * 2, 3, "%02X", buf[i]);

        uint32_t ts = (uint32_t)(esp_timer_get_time() / 1000);
        snprintf(json, sizeof(json),
            "{\"type\":\"uart_rx\",\"port\":%d,\"ts\":%" PRIu32 ",\"data\":\"%s\",\"len\":%d}",
            idx, ts, hex, n);
        ws_broadcast(json);
    }
}

/* --- Apply UART params to hardware --- */
static esp_err_t apply_port_cfg(int idx)
{
    uart_ch_t *ch = &s_ch[idx];
    uart_config_t cfg = {
        .baud_rate  = ch->baud,
        .data_bits  = bits_enum(ch->bits),
        .parity     = par_enum(ch->parity),
        .stop_bits  = stop_enum(ch->stop),
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    return uart_param_config(ch->port_num, &cfg);
}

/* --- Init one port --- */
static esp_err_t init_port(int idx)
{
    uart_ch_t *ch = &s_ch[idx];

    esp_err_t ret = apply_port_cfg(idx);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "UART%d param_config: %s", idx, esp_err_to_name(ret)); return ret; }

    ret = uart_set_pin(ch->port_num, ch->tx_pin, ch->rx_pin,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "UART%d set_pin: %s", idx, esp_err_to_name(ret)); return ret; }

    ret = uart_driver_install(ch->port_num, BUF_SIZE, BUF_SIZE, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART%d driver_install: %s", idx, esp_err_to_name(ret));
        return ret;
    }

    ch->running = true;
    char tname[16];
    snprintf(tname, sizeof(tname), "uart%d_rx", idx);
    xTaskCreatePinnedToCore(rx_task, tname, 4096, (void *)(intptr_t)idx, 5, NULL, 0);
    ESP_LOGI(TAG, "UART%d ready baud=%d %dN%d TX=IO%d RX=IO%d",
             idx, ch->baud, ch->bits, ch->stop, ch->tx_pin, ch->rx_pin);
    return ESP_OK;
}

esp_err_t uart_debug_init(void)
{
    nvs_load();
    esp_err_t r0 = init_port(0);
    esp_err_t r1 = init_port(1);
    return (r0 == ESP_OK || r1 == ESP_OK) ? ESP_OK : ESP_FAIL;
}

esp_err_t uart_debug_send(int port, const uint8_t *data, size_t len)
{
    if (port < 0 || port > 1 || !data || len == 0) return ESP_ERR_INVALID_ARG;
    uart_ch_t *ch = &s_ch[port];
    if (!ch->running) return ESP_ERR_INVALID_STATE;
    int n = uart_write_bytes(ch->port_num, (const char *)data, len);
    uart_wait_tx_done(ch->port_num, pdMS_TO_TICKS(TX_TIMEOUT_MS));
    if (n > 0) { ch->tx_bytes += (uint32_t)n; ch->tx_frames++; }
    return (n == (int)len) ? ESP_OK : ESP_FAIL;
}

esp_err_t uart_debug_send_hex(int port, const char *hex_str)
{
    if (!hex_str) return ESP_ERR_INVALID_ARG;
    uint8_t buf[512];
    int n = hex_decode(hex_str, buf, sizeof(buf));
    if (n <= 0) return uart_debug_send(port, (const uint8_t *)hex_str, strlen(hex_str));
    return uart_debug_send(port, buf, (size_t)n);
}

esp_err_t uart_debug_set_baud(int port, int baud)
{
    if (port < 0 || port > 1 || baud <= 0) return ESP_ERR_INVALID_ARG;
    uart_ch_t *ch = &s_ch[port];
    if (!ch->running) return ESP_ERR_INVALID_STATE;
    esp_err_t ret = uart_set_baudrate(ch->port_num, (uint32_t)baud);
    if (ret == ESP_OK) { ch->baud = baud; nvs_save(port); }
    return ret;
}

esp_err_t uart_debug_set_format(int port, int bits, int parity, int stop)
{
    if (port < 0 || port > 1) return ESP_ERR_INVALID_ARG;
    if ((bits != 7 && bits != 8) || parity < 0 || parity > 2 || (stop != 1 && stop != 2))
        return ESP_ERR_INVALID_ARG;
    uart_ch_t *ch = &s_ch[port];
    if (!ch->running) return ESP_ERR_INVALID_STATE;
    ch->bits = bits; ch->parity = parity; ch->stop = stop;
    esp_err_t ret = apply_port_cfg(port);
    if (ret == ESP_OK) nvs_save(port);
    return ret;
}

/* --- HTTP handlers --- */
static esp_err_t h_status(httpd_req_t *req)
{
    const char *p0 = s_ch[0].parity==1?"E":s_ch[0].parity==2?"O":"N";
    const char *p1 = s_ch[1].parity==1?"E":s_ch[1].parity==2?"O":"N";
    char buf[400];
    snprintf(buf, sizeof(buf),
        "{\"ports\":["
        "{\"port\":0,\"baud\":%d,\"bits\":%d,\"parity\":%d,\"stop\":%d,"
         "\"format\":\"%d%s%d\","
         "\"tx_bytes\":%" PRIu32 ",\"rx_bytes\":%" PRIu32 ","
         "\"tx_frames\":%" PRIu32 ",\"rx_frames\":%" PRIu32 ",\"running\":%s},"
        "{\"port\":1,\"baud\":%d,\"bits\":%d,\"parity\":%d,\"stop\":%d,"
         "\"format\":\"%d%s%d\","
         "\"tx_bytes\":%" PRIu32 ",\"rx_bytes\":%" PRIu32 ","
         "\"tx_frames\":%" PRIu32 ",\"rx_frames\":%" PRIu32 ",\"running\":%s}"
        "]}",
        s_ch[0].baud, s_ch[0].bits, s_ch[0].parity, s_ch[0].stop,
        s_ch[0].bits, p0, s_ch[0].stop,
        s_ch[0].tx_bytes, s_ch[0].rx_bytes, s_ch[0].tx_frames, s_ch[0].rx_frames,
        s_ch[0].running ? "true" : "false",
        s_ch[1].baud, s_ch[1].bits, s_ch[1].parity, s_ch[1].stop,
        s_ch[1].bits, p1, s_ch[1].stop,
        s_ch[1].tx_bytes, s_ch[1].rx_bytes, s_ch[1].tx_frames, s_ch[1].rx_frames,
        s_ch[1].running ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, strlen(buf));
    return ESP_OK;
}

static esp_err_t h_config(httpd_req_t *req)
{
    char body[200];
    int n = httpd_req_recv(req, body, sizeof(body) - 1);
    if (n <= 0) return ESP_FAIL;
    body[n] = '\0';

    char *p;
    int port = 0, baud = 0, bits = 0, parity = -1, stop = 0;
    if ((p = strstr(body, "\"port\":")))   port   = atoi(p + 7);
    if ((p = strstr(body, "\"baud\":")))   baud   = atoi(p + 7);
    if ((p = strstr(body, "\"bits\":")))   bits   = atoi(p + 7);
    if ((p = strstr(body, "\"parity\":"))) parity = atoi(p + 9);
    if ((p = strstr(body, "\"stop\":")))   stop   = atoi(p + 7);

    if (port < 0 || port > 1) { httpd_resp_sendstr(req, "{\"status\":\"err\"}"); return ESP_OK; }
    if (baud > 0) uart_debug_set_baud(port, baud);
    if (bits > 0 || parity >= 0 || stop > 0) {
        uart_debug_set_format(port,
            bits   > 0 ? bits   : s_ch[port].bits,
            parity >= 0 ? parity : s_ch[port].parity,
            stop   > 0 ? stop   : s_ch[port].stop);
    }
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

static esp_err_t h_send(httpd_req_t *req)
{
    char body[1100];
    int n = httpd_req_recv(req, body, sizeof(body) - 1);
    if (n <= 0) return ESP_FAIL;
    body[n] = '\0';

    char *p;
    int port = 0;
    if ((p = strstr(body, "\"port\":"))) port = atoi(p + 7);

    char data[512] = {0};
    if ((p = strstr(body, "\"data\":\""))) {
        p += 8;
        char *e = strchr(p, '"');
        if (e && (e - p) < (int)sizeof(data)) memcpy(data, p, e - p);
    }
    if (data[0]) uart_debug_send_hex(port, data);
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

static const api_route_t s_routes[] = {
    { "/api/uart/status", HTTP_GET,  h_status },
    { "/api/uart/config", HTTP_POST, h_config },
    { "/api/uart/send",   HTTP_POST, h_send   },
    { NULL, 0, NULL },
};

esp_err_t uart_debug_register_routes(void)
{
    for (int i = 0; s_routes[i].uri; i++)
        http_server_register_route(&s_routes[i]);
    return ESP_OK;
}
