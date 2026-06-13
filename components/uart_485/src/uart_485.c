#include "uart_485.h"
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
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

static const char *TAG = "RS485";
#define NVS_NS        "uart485"

#define UART_PORT     UART_NUM_2
#define PIN_TX        GPIO_NUM_8
#define PIN_RX        GPIO_NUM_9
#define PIN_DE        GPIO_NUM_46   /* Driver Enable: active-HIGH GPIO output */
#define PIN_RE        GPIO_NUM_16   /* Receiver Enable: active-LOW GPIO output */

#define BUF_SIZE      4096
#define TX_TIMEOUT_MS 500

/* TX queue: decouple WS dispatch from UART task so httpd is never blocked */
#define TX_QUEUE_LEN  8
#define TX_HEX_MAX    513
typedef struct { char hex[TX_HEX_MAX]; } tx_item_t;
static QueueHandle_t     s_tx_queue = NULL;

static int  s_baud    = 9600;
static int  s_bits    = 8;
static int  s_parity  = 0;
static int  s_stop    = 1;
static bool s_running = false;
static uint32_t s_tx_bytes  = 0;
static uint32_t s_rx_bytes  = 0;
static uint32_t s_tx_frames = 0;
static uint32_t s_rx_frames = 0;

static volatile bool     s_tx_busy  = false;
static SemaphoreHandle_t s_tx_mutex = NULL;

/* --- NVS --- */
static void nvs_load_cfg(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    int32_t v;
    if (nvs_get_i32(h, "baud",   &v) == ESP_OK && v > 0)           s_baud   = (int)v;
    if (nvs_get_i32(h, "bits",   &v) == ESP_OK && (v==7||v==8))    s_bits   = (int)v;
    if (nvs_get_i32(h, "parity", &v) == ESP_OK && v>=0 && v<=2)    s_parity = (int)v;
    if (nvs_get_i32(h, "stop",   &v) == ESP_OK && (v==1||v==2))    s_stop   = (int)v;
    nvs_close(h);
}

static void nvs_save_cfg(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, "baud",   (int32_t)s_baud);
    nvs_set_i32(h, "bits",   (int32_t)s_bits);
    nvs_set_i32(h, "parity", (int32_t)s_parity);
    nvs_set_i32(h, "stop",   (int32_t)s_stop);
    nvs_commit(h);
    nvs_close(h);
}

/* --- Map config values to IDF enums --- */
static uart_word_length_t bits_enum(int b) { return b==7 ? UART_DATA_7_BITS : UART_DATA_8_BITS; }
static uart_parity_t      par_enum(int p)  { return p==1 ? UART_PARITY_EVEN : p==2 ? UART_PARITY_ODD : UART_PARITY_DISABLE; }
static uart_stop_bits_t   stop_enum(int s) { return s==2 ? UART_STOP_BITS_2 : UART_STOP_BITS_1; }

static esp_err_t apply_uart_cfg(void)
{
    uart_config_t cfg = {
        .baud_rate  = s_baud,
        .data_bits  = bits_enum(s_bits),
        .parity     = par_enum(s_parity),
        .stop_bits  = stop_enum(s_stop),
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    return uart_param_config(UART_PORT, &cfg);
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
    (void)arg;
    uint8_t buf[256];
    char    json[700];

    while (1) {
        int n = uart_read_bytes(UART_PORT, buf, sizeof(buf) - 1, pdMS_TO_TICKS(20));
        if (n <= 0) continue;

        if (s_tx_busy) continue;   /* discard echo / glitch during TX window */

        s_rx_bytes  += (uint32_t)n;
        s_rx_frames++;

        char hex[513] = {0};
        for (int i = 0; i < n && i < 256; i++)
            snprintf(hex + i * 2, 3, "%02X", buf[i]);

        uint32_t ts = (uint32_t)(esp_timer_get_time() / 1000);
        snprintf(json, sizeof(json),
            "{\"type\":\"rs485_rx\",\"ts\":%" PRIu32 ",\"data\":\"%s\",\"len\":%d}",
            ts, hex, n);
        ws_broadcast(json);
    }
}

/* --- TX queue task --- */
static void tx_task(void *arg)
{
    (void)arg;
    tx_item_t item;
    char      conf[128];

    while (1) {
        if (xQueueReceive(s_tx_queue, &item, portMAX_DELAY) != pdTRUE) continue;

        esp_err_t err = uart_485_send_hex(item.hex);

        /* Notify the browser: TX is confirmed (or failed) */
        uint32_t ts = (uint32_t)(esp_timer_get_time() / 1000);
        snprintf(conf, sizeof(conf),
            "{\"type\":\"rs485_tx_done\",\"ts\":%" PRIu32 ",\"ok\":%s}",
            ts, err == ESP_OK ? "true" : "false");
        ws_broadcast(conf);
    }
}

/* --- Init --- */
esp_err_t uart_485_init(void)
{
    nvs_load_cfg();

    s_tx_mutex = xSemaphoreCreateMutex();
    s_tx_queue = xQueueCreate(TX_QUEUE_LEN, sizeof(tx_item_t));

    /* DE and RE are both plain GPIO outputs, controlled manually */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_DE) | (1ULL << PIN_RE),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    /* RX idle state: DE=0 (driver off), RE=0 (receiver on, active-low) */
    gpio_set_level(PIN_DE, 0);
    gpio_set_level(PIN_RE, 0);

    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &(uart_config_t){
        .baud_rate  = s_baud,
        .data_bits  = bits_enum(s_bits),
        .parity     = par_enum(s_parity),
        .stop_bits  = stop_enum(s_stop),
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    }));

    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, PIN_TX, PIN_RX,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, BUF_SIZE, BUF_SIZE, 0, NULL, 0));

    s_running = true;
    xTaskCreatePinnedToCore(rx_task, "rs485_rx", 4096, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(tx_task, "rs485_tx", 4096, NULL, 5, NULL, 0);
    ESP_LOGI(TAG, "RS485  baud=%d %d%s%d  DE=IO%d RE=IO%d",
             s_baud, s_bits,
             s_parity==1?"E":s_parity==2?"O":"N",
             s_stop, PIN_DE, PIN_RE);
    return ESP_OK;
}

/* --- Runtime reconfiguration --- */
esp_err_t uart_485_set_baud(int baud)
{
    esp_err_t ret = uart_set_baudrate(UART_PORT, (uint32_t)baud);
    if (ret == ESP_OK) { s_baud = baud; nvs_save_cfg(); }
    return ret;
}

esp_err_t uart_485_set_format(int bits, int parity, int stop)
{
    if ((bits != 7 && bits != 8) || parity < 0 || parity > 2 || (stop != 1 && stop != 2))
        return ESP_ERR_INVALID_ARG;
    s_bits = bits; s_parity = parity; s_stop = stop;
    esp_err_t ret = apply_uart_cfg();
    if (ret == ESP_OK) nvs_save_cfg();
    return ret;
}

/* --- Send (synchronous, used by HTTP handler and tx_task) --- */
esp_err_t uart_485_send_bytes(const uint8_t *data, size_t len)
{
    if (!data || len == 0) return ESP_ERR_INVALID_ARG;

    if (xSemaphoreTake(s_tx_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
        return ESP_ERR_TIMEOUT;

    s_tx_busy = true;

    /*
     * Enter TX: DE first avoids DE=0,RE=1 (SP3485 hi-Z/shutdown).
     * Sequence: DE=0,RE=0 → DE=1,RE=0 (loopback, brief) → DE=1,RE=1 (TX)
     */
    gpio_set_level(PIN_DE, 1);
    gpio_set_level(PIN_RE, 1);

    int n = uart_write_bytes(UART_PORT, (const char *)data, len);
    esp_err_t ret = uart_wait_tx_done(UART_PORT, pdMS_TO_TICKS(TX_TIMEOUT_MS));

    /*
     * Hold DE for one extra byte period after uart_wait_tx_done.
     * uart_wait_tx_done fires on UART_TX_DONE_INT (FIFO empty); the shift
     * register may still be clocking out the last stop bit on some IDF builds.
     * 1 byte = (1 000 000 / baud) × 10 µs.
     */
    esp_rom_delay_us((uint32_t)(10000000u / (uint32_t)s_baud));

    /*
     * Exit TX: RE first avoids DE=0,RE=1 (hi-Z) state.
     * Sequence: DE=1,RE=1 → DE=1,RE=0 (loopback, brief) → DE=0,RE=0 (RX)
     * No uart_flush_input: RE=1 during TX means SP3485 RO is disabled,
     * so no echo bytes can have entered the RX FIFO.
     */
    gpio_set_level(PIN_RE, 0);
    gpio_set_level(PIN_DE, 0);
    s_tx_busy = false;

    xSemaphoreGive(s_tx_mutex);

    if (n > 0) { s_tx_bytes += (uint32_t)n; s_tx_frames++; }
    return (ret == ESP_OK && n == (int)len) ? ESP_OK : ESP_FAIL;
}

esp_err_t uart_485_send_hex(const char *hex_str)
{
    if (!hex_str) return ESP_ERR_INVALID_ARG;
    uint8_t buf[512];
    int n = hex_decode(hex_str, buf, sizeof(buf));
    if (n <= 0) return uart_485_send_bytes((const uint8_t *)hex_str, strlen(hex_str));
    return uart_485_send_bytes(buf, (size_t)n);
}

/* Non-blocking enqueue for WS dispatch — tx_task sends and replies with rs485_tx_done */
esp_err_t uart_485_enqueue(const char *hex_str)
{
    if (!hex_str || !s_tx_queue) return ESP_ERR_INVALID_ARG;
    tx_item_t item;
    strncpy(item.hex, hex_str, TX_HEX_MAX - 1);
    item.hex[TX_HEX_MAX - 1] = '\0';
    /* xQueueSend with zero timeout: drop if queue full (8 slots = plenty for UI sends) */
    return xQueueSend(s_tx_queue, &item, 0) == pdTRUE ? ESP_OK : ESP_ERR_NO_MEM;
}

/* --- HTTP handlers --- */
static esp_err_t h_status(httpd_req_t *req)
{
    const char *par_str = s_parity == 1 ? "E" : s_parity == 2 ? "O" : "N";
    char buf[200];
    snprintf(buf, sizeof(buf),
        "{\"baud\":%d,\"bits\":%d,\"parity\":%d,\"stop\":%d,"
        "\"format\":\"%d%s%d\","
        "\"tx_bytes\":%" PRIu32 ",\"rx_bytes\":%" PRIu32 ","
        "\"tx_frames\":%" PRIu32 ",\"rx_frames\":%" PRIu32 ",\"running\":%s}",
        s_baud, s_bits, s_parity, s_stop, s_bits, par_str, s_stop,
        s_tx_bytes, s_rx_bytes, s_tx_frames, s_rx_frames,
        s_running ? "true" : "false");
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
    int baud = 0, bits = 0, parity = -1, stop = 0;
    if ((p = strstr(body, "\"baud\":")))   baud   = atoi(p + 7);
    if ((p = strstr(body, "\"bits\":")))   bits   = atoi(p + 7);
    if ((p = strstr(body, "\"parity\":"))) parity = atoi(p + 9);
    if ((p = strstr(body, "\"stop\":")))   stop   = atoi(p + 7);

    if (baud > 0) uart_485_set_baud(baud);
    if (bits > 0 || parity >= 0 || stop > 0) {
        uart_485_set_format(
            bits   > 0  ? bits   : s_bits,
            parity >= 0 ? parity : s_parity,
            stop   > 0  ? stop   : s_stop
        );
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
    char data[512] = {0};
    char *p = strstr(body, "\"data\":\"");
    if (p) {
        p += 8;
        char *e = strchr(p, '"');
        if (e && (e - p) < (int)sizeof(data)) memcpy(data, p, e - p);
    }
    if (data[0]) uart_485_send_hex(data);
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

static const api_route_t s_routes[] = {
    {"/api/rs485/status", HTTP_GET,  h_status},
    {"/api/rs485/config", HTTP_POST, h_config},
    {"/api/rs485/send",   HTTP_POST, h_send},
    {NULL, 0, NULL},
};

esp_err_t uart_485_register_routes(void)
{
    for (int i = 0; s_routes[i].uri; i++)
        http_server_register_route(&s_routes[i]);
    return ESP_OK;
}
