#include "can_twai.h"
#include "ws_server.h"
#include "http_server.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#include "driver/twai.h"
#pragma GCC diagnostic pop
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

static const char *TAG = "CAN";
#define NVS_NS   "can_cfg"

#define PIN_TX  GPIO_NUM_6
#define PIN_RX  GPIO_NUM_7

/* Current config */
static int  s_baud      = 500000;
static char s_mode[16]  = "normal";   /* normal | listen | selftest */
static bool s_running   = false;
static uint32_t s_tx_cnt = 0;
static uint32_t s_rx_cnt = 0;

static void nvs_load_cfg(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    int32_t v;
    if (nvs_get_i32(h, "baud", &v) == ESP_OK && v > 0) s_baud = (int)v;
    size_t sz = sizeof(s_mode);
    nvs_get_str(h, "mode", s_mode, &sz);
    nvs_close(h);
}

static void nvs_save_cfg(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, "baud", (int32_t)s_baud);
    nvs_set_str(h, "mode", s_mode);
    nvs_commit(h);
    nvs_close(h);
}

/* Map baud rate integer → TWAI timing config */
static twai_timing_config_t baud_to_timing(int baud)
{
    switch (baud) {
        case 25000:   { twai_timing_config_t t = TWAI_TIMING_CONFIG_25KBITS();   return t; }
        case 50000:   { twai_timing_config_t t = TWAI_TIMING_CONFIG_50KBITS();   return t; }
        case 100000:  { twai_timing_config_t t = TWAI_TIMING_CONFIG_100KBITS();  return t; }
        case 125000:  { twai_timing_config_t t = TWAI_TIMING_CONFIG_125KBITS();  return t; }
        case 250000:  { twai_timing_config_t t = TWAI_TIMING_CONFIG_250KBITS();  return t; }
        case 800000:  { twai_timing_config_t t = TWAI_TIMING_CONFIG_800KBITS();  return t; }
        case 1000000: { twai_timing_config_t t = TWAI_TIMING_CONFIG_1MBITS();    return t; }
        default:      { twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();  return t; }
    }
}

static esp_err_t twai_start_with_cfg(int baud, const char *mode)
{
    twai_general_config_t gen = TWAI_GENERAL_CONFIG_DEFAULT(PIN_TX, PIN_RX,
        (strcmp(mode, "listen")   == 0) ? TWAI_MODE_LISTEN_ONLY :
        (strcmp(mode, "selftest") == 0) ? TWAI_MODE_NO_ACK      :
                                          TWAI_MODE_NORMAL);
    gen.rx_queue_len = 32;
    gen.tx_queue_len = 16;

    twai_timing_config_t tim  = baud_to_timing(baud);
    twai_filter_config_t filt = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t ret = twai_driver_install(&gen, &tim, &filt);
    if (ret != ESP_OK) return ret;
    ret = twai_start();
    if (ret == ESP_OK) {
        s_baud = baud;
        strncpy(s_mode, mode, sizeof(s_mode) - 1);
        s_running = true;
        nvs_save_cfg();
        ESP_LOGI(TAG, "Started baud=%d mode=%s (saved)", baud, mode);
    }
    return ret;
}

/* --- Hex decode --- */
static int hex_decode(const char *hex, uint8_t *out, int maxlen)
{
    int n = 0;
    const char *p = hex;
    while (*p && n < maxlen) {
        while (*p == ' ') p++;
        if (!*p || !*(p+1)) break;
        char hi = *p++, lo = *p++;
        int hn = (hi>='0'&&hi<='9')?(hi-'0'):(hi>='a'&&hi<='f')?(hi-'a'+10):(hi>='A'&&hi<='F')?(hi-'A'+10):-1;
        int ln = (lo>='0'&&lo<='9')?(lo-'0'):(lo>='a'&&lo<='f')?(lo-'a'+10):(lo>='A'&&lo<='F')?(lo-'A'+10):-1;
        if (hn < 0 || ln < 0) break;
        out[n++] = (uint8_t)((hn << 4) | ln);
    }
    return n;
}

/* --- RX task --- */
static void rx_task(void *arg)
{
    (void)arg;
    twai_message_t msg;
    char json[256];

    while (1) {
        if (!s_running) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }

        if (twai_receive(&msg, pdMS_TO_TICKS(100)) != ESP_OK) continue;
        s_rx_cnt++;

        char hex[17] = {0};
        for (int i = 0; i < msg.data_length_code && i < 8; i++)
            snprintf(hex + i * 2, 3, "%02X", msg.data[i]);

        uint32_t ts = (uint32_t)(esp_timer_get_time() / 1000);
        snprintf(json, sizeof(json),
            "{\"type\":\"can_rx\",\"ts\":%" PRIu32 ",\"id\":%" PRIu32 ",\"ext\":%s,\"rtr\":%s,\"dlc\":%d,\"data\":\"%s\"}",
            ts, msg.identifier,
            msg.extd ? "true" : "false",
            msg.rtr  ? "true" : "false",
            msg.data_length_code, hex);
        ws_broadcast(json);

        /* Check bus status, broadcast if warning/error */
        twai_status_info_t st;
        if (twai_get_status_info(&st) == ESP_OK) {
            if (st.state == TWAI_STATE_BUS_OFF) {
                ws_broadcast("{\"type\":\"can_status\",\"state\":\"bus_off\"}");
                twai_initiate_recovery();
            } else if (st.tx_error_counter > 96 || st.rx_error_counter > 96) {
                char sbuf[128];
                snprintf(sbuf, sizeof(sbuf),
                    "{\"type\":\"can_status\",\"state\":\"warning\","
                    "\"tx_err\":%" PRIu32 ",\"rx_err\":%" PRIu32 "}",
                    st.tx_error_counter, st.rx_error_counter);
                ws_broadcast(sbuf);
            }
        }
    }
}

/* --- Status broadcast task (1 Hz) --- */
static void status_task(void *arg)
{
    (void)arg;
    char buf[192];
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (!s_running) continue;
        twai_status_info_t st;
        const char *state_str = "unknown";
        int tx_err = 0, rx_err = 0;
        if (twai_get_status_info(&st) == ESP_OK) {
            tx_err = st.tx_error_counter;
            rx_err = st.rx_error_counter;
            switch (st.state) {
                case TWAI_STATE_RUNNING:  state_str = "running";  break;
                case TWAI_STATE_BUS_OFF:  state_str = "bus_off";  break;
                case TWAI_STATE_RECOVERING: state_str = "recovering"; break;
                default: state_str = "stopped";
            }
        }
        snprintf(buf, sizeof(buf),
            "{\"type\":\"can_status\",\"state\":\"%s\","
            "\"tx_err\":%d,\"rx_err\":%d,\"tx_cnt\":%" PRIu32 ",\"rx_cnt\":%" PRIu32 ","
            "\"baud\":%d,\"mode\":\"%s\"}",
            state_str, tx_err, rx_err, s_tx_cnt, s_rx_cnt, s_baud, s_mode);
        ws_broadcast(buf);
    }
}

esp_err_t can_twai_init(void)
{
    nvs_load_cfg();
    esp_err_t ret = twai_start_with_cfg(s_baud, s_mode);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    xTaskCreatePinnedToCore(rx_task,     "can_rx",  4096, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(status_task, "can_st",  4096, NULL, 3, NULL, 0);
    return ESP_OK;
}

esp_err_t can_twai_send(uint32_t id, bool ext, bool rtr, int dlc, const char *hex_data)
{
    if (!s_running) return ESP_ERR_INVALID_STATE;
    twai_message_t msg = {0};
    msg.identifier = id;
    msg.extd       = ext ? 1 : 0;
    msg.rtr        = rtr ? 1 : 0;
    msg.self       = (strcmp(s_mode, "selftest") == 0) ? 1 : 0;
    if (rtr) {
        /* RTR: DLC indicates expected data length, no data bytes */
        msg.data_length_code = (dlc >= 0 && dlc <= 8) ? (uint8_t)dlc : 0;
    } else {
        int decoded = (hex_data && hex_data[0]) ? hex_decode(hex_data, msg.data, 8) : 0;
        /* If explicit DLC is larger, pad with zeros (already zeroed) */
        msg.data_length_code = (dlc >= decoded && dlc <= 8) ? (uint8_t)dlc : (uint8_t)decoded;
    }
    esp_err_t ret = twai_transmit(&msg, pdMS_TO_TICKS(50));
    if (ret == ESP_OK) s_tx_cnt++;
    return ret;
}

esp_err_t can_twai_reconfigure(int baud, const char *mode)
{
    /* Stop → reinstall → start */
    if (s_running) {
        twai_stop();
        twai_driver_uninstall();
        s_running = false;
    }
    int new_baud  = (baud > 0)     ? baud : s_baud;
    const char *new_mode = (mode && mode[0]) ? mode : s_mode;
    esp_err_t ret = twai_start_with_cfg(new_baud, new_mode);
    char rbuf[96];
    snprintf(rbuf, sizeof(rbuf),
        "{\"type\":\"can_cfg_ack\",\"baud\":%d,\"mode\":\"%s\",\"ok\":%s}",
        new_baud, new_mode, ret == ESP_OK ? "true" : "false");
    ws_broadcast(rbuf);
    return ret;
}

/* --- HTTP API --- */
static esp_err_t h_status(httpd_req_t *req)
{
    twai_status_info_t st = {0};
    if (s_running) twai_get_status_info(&st);
    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"running\":%s,\"baud\":%d,\"mode\":\"%s\","
        "\"tx_err\":%d,\"rx_err\":%d,\"tx_cnt\":%" PRIu32 ",\"rx_cnt\":%" PRIu32 "}",
        s_running ? "true" : "false",
        s_baud, s_mode,
        (int)st.tx_error_counter, (int)st.rx_error_counter,
        s_tx_cnt, s_rx_cnt);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, strlen(buf));
    return ESP_OK;
}

static esp_err_t h_config(httpd_req_t *req)
{
    char body[256];
    int n = httpd_req_recv(req, body, sizeof(body) - 1);
    if (n <= 0) return ESP_FAIL;
    body[n] = '\0';

    int baud = 0;
    char mode[16] = {0};
    char *p;
    if ((p = strstr(body, "\"baud\":"))) baud = atoi(p + 7);
    if ((p = strstr(body, "\"mode\":\""))) {
        p += 8;
        char *e = strchr(p, '"');
        if (e && (e - p) < (int)sizeof(mode)) memcpy(mode, p, e - p);
    }
    can_twai_reconfigure(baud, mode[0] ? mode : NULL);
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

static esp_err_t h_send(httpd_req_t *req)
{
    char body[256];
    int n = httpd_req_recv(req, body, sizeof(body) - 1);
    if (n <= 0) return ESP_FAIL;
    body[n] = '\0';

    uint32_t id = 0;
    bool ext = false, rtr = false;
    int dlc = -1;
    char data[20] = {0};
    char *p;
    if ((p = strstr(body, "\"id\":")))  id  = (uint32_t)strtoul(p + 5, NULL, 0);
    if (strstr(body, "\"ext\":true"))   ext = true;
    if (strstr(body, "\"rtr\":true"))   rtr = true;
    if ((p = strstr(body, "\"dlc\":"))) dlc = atoi(p + 6);
    if ((p = strstr(body, "\"data\":\""))) {
        p += 8; char *e = strchr(p, '"');
        if (e && (e - p) < (int)sizeof(data)) memcpy(data, p, e - p);
    }
    esp_err_t ret = can_twai_send(id, ext, rtr, dlc, data);
    httpd_resp_sendstr(req, ret == ESP_OK ? "{\"status\":\"ok\"}" : "{\"status\":\"err\"}");
    return ESP_OK;
}

static esp_err_t h_recover(httpd_req_t *req)
{
    twai_initiate_recovery();
    httpd_resp_sendstr(req, "{\"status\":\"recovering\"}");
    return ESP_OK;
}

static const api_route_t s_routes[] = {
    {"/api/can/status",  HTTP_GET,  h_status},
    {"/api/can/config",  HTTP_POST, h_config},
    {"/api/can/send",    HTTP_POST, h_send},
    {"/api/can/recover", HTTP_POST, h_recover},
    {NULL, 0, NULL},
};

esp_err_t can_twai_register_routes(void)
{
    for (int i = 0; s_routes[i].uri; i++)
        http_server_register_route(&s_routes[i]);
    return ESP_OK;
}
