#include "spi_flash_ext.h"
#include "ws_server.h"
#include "http_server.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <ctype.h>

static const char *TAG = "FLASH";

/* ---- Pin & SPI config ---- */
#define PIN_MISO  GPIO_NUM_35
#define PIN_CLK   GPIO_NUM_36
#define PIN_MOSI  GPIO_NUM_37
#define PIN_CS    GPIO_NUM_38
#define SPI_HOST  SPI3_HOST
#define SPI_FREQ  40000000   /* 40 MHz */

/* ---- W25Q128 command set ---- */
#define CMD_WRITE_ENABLE   0x06
#define CMD_WRITE_DISABLE  0x04
#define CMD_READ_SR1       0x05
#define CMD_READ_SR2       0x35
#define CMD_WRITE_SR       0x01
#define CMD_READ_DATA      0x03
#define CMD_FAST_READ      0x0B
#define CMD_PAGE_PROGRAM   0x02
#define CMD_SECTOR_ERASE   0x20   /* 4 KB */
#define CMD_BLOCK32_ERASE  0x52   /* 32 KB */
#define CMD_BLOCK64_ERASE  0xD8   /* 64 KB */
#define CMD_CHIP_ERASE     0xC7
#define CMD_JEDEC_ID       0x9F
#define CMD_POWER_DOWN     0xB9
#define CMD_RELEASE_PD     0xAB

#define SR1_BUSY  BIT(0)
#define SR1_WEL   BIT(1)

static spi_device_handle_t s_dev = NULL;
static SemaphoreHandle_t   s_mutex = NULL;
static bool s_ready = false;

/* ---- Low-level SPI transaction ---- */
static esp_err_t spi_xfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
    spi_transaction_t t = {0};
    t.length    = len * 8;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    return spi_device_transmit(s_dev, &t);
}

/* ---- Wait BUSY bit ---- */
static void flash_wait_busy(void)
{
    uint8_t cmd = CMD_READ_SR1, sr;
    int timeout = 100000;
    do {
        uint8_t rx[2] = {0};
        uint8_t tx[2] = {CMD_READ_SR1, 0};
        spi_xfer(tx, rx, 2);
        sr = rx[1];
        if (sr & SR1_BUSY) vTaskDelay(1);
        else break;
    } while (--timeout > 0);
    (void)cmd;
    (void)sr;
}

/* ---- Write enable ---- */
static esp_err_t flash_write_enable(void)
{
    uint8_t cmd = CMD_WRITE_ENABLE;
    return spi_xfer(&cmd, NULL, 1);
}

/* ---- Public: read status register 1 ---- */
uint8_t flash_read_status1(void)
{
    uint8_t tx[2] = {CMD_READ_SR1, 0};
    uint8_t rx[2] = {0};
    spi_xfer(tx, rx, 2);
    return rx[1];
}

/* ---- Public: read JEDEC ID ---- */
esp_err_t flash_read_jedec(uint8_t *mfr, uint8_t *type, uint8_t *cap)
{
    uint8_t tx[4] = {CMD_JEDEC_ID, 0, 0, 0};
    uint8_t rx[4] = {0};
    esp_err_t ret = spi_xfer(tx, rx, 4);
    if (ret == ESP_OK) {
        if (mfr)  *mfr  = rx[1];
        if (type) *type = rx[2];
        if (cap)  *cap  = rx[3];
    }
    return ret;
}

/* ---- Public: read data ---- */
esp_err_t flash_read(uint32_t addr, uint8_t *buf, size_t len)
{
    if (!buf || len == 0) return ESP_ERR_INVALID_ARG;
    uint8_t *tx = calloc(1, len + 5);
    uint8_t *rx = calloc(1, len + 5);
    if (!tx || !rx) { free(tx); free(rx); return ESP_ERR_NO_MEM; }
    tx[0] = CMD_FAST_READ;
    tx[1] = (addr >> 16) & 0xFF;
    tx[2] = (addr >>  8) & 0xFF;
    tx[3] = (addr      ) & 0xFF;
    tx[4] = 0x00;
    esp_err_t ret = spi_xfer(tx, rx, len + 5);
    if (ret == ESP_OK) memcpy(buf, rx + 5, len);
    free(tx); free(rx);
    return ret;
}

/* ---- Public: page program ---- */
static esp_err_t flash_page_program(uint32_t addr, const uint8_t *data, size_t len)
{
    flash_write_enable();
    uint8_t *tx = calloc(1, len + 4);
    if (!tx) return ESP_ERR_NO_MEM;
    tx[0] = CMD_PAGE_PROGRAM;
    tx[1] = (addr >> 16) & 0xFF;
    tx[2] = (addr >>  8) & 0xFF;
    tx[3] = (addr      ) & 0xFF;
    memcpy(tx + 4, data, len);
    esp_err_t ret = spi_xfer(tx, NULL, len + 4);
    free(tx);
    if (ret == ESP_OK) flash_wait_busy();
    return ret;
}

/* ---- Public: write (handles page boundaries) ---- */
esp_err_t flash_write(uint32_t addr, const uint8_t *buf, size_t len)
{
    while (len > 0) {
        uint32_t page_off = addr % FLASH_PAGE_SIZE;
        size_t   chunk    = FLASH_PAGE_SIZE - page_off;
        if (chunk > len) chunk = len;
        esp_err_t ret = flash_page_program(addr, buf, chunk);
        if (ret != ESP_OK) return ret;
        addr += chunk;
        buf  += chunk;
        len  -= chunk;
    }
    return ESP_OK;
}

/* ---- Erase functions ---- */
static esp_err_t flash_erase_cmd(uint8_t cmd, uint32_t addr)
{
    flash_write_enable();
    uint8_t tx[4] = {cmd,
        (uint8_t)(addr >> 16),
        (uint8_t)(addr >>  8),
        (uint8_t)(addr      )};
    esp_err_t ret = spi_xfer(tx, NULL, 4);
    if (ret == ESP_OK) flash_wait_busy();
    return ret;
}

esp_err_t flash_erase_sector(uint32_t addr)  { return flash_erase_cmd(CMD_SECTOR_ERASE,  addr & ~(FLASH_SECTOR_SIZE  - 1)); }
esp_err_t flash_erase_block32(uint32_t addr) { return flash_erase_cmd(CMD_BLOCK32_ERASE, addr & ~(FLASH_BLOCK32_SIZE - 1)); }
esp_err_t flash_erase_block64(uint32_t addr) { return flash_erase_cmd(CMD_BLOCK64_ERASE, addr & ~(FLASH_BLOCK64_SIZE - 1)); }

esp_err_t flash_erase_chip(void)
{
    flash_write_enable();
    uint8_t cmd = CMD_CHIP_ERASE;
    esp_err_t ret = spi_xfer(&cmd, NULL, 1);
    if (ret == ESP_OK) flash_wait_busy();
    return ret;
}

/* ════════════════════════════════════════════════════════════════════════════
 * FILE MANAGER — simple sequential file system on W25Q128
 *
 * Flash layout:
 *   Sector 0  (0x000000): Directory (fm_dir_t, 4048 bytes ≤ 4096-byte sector)
 *   Sector 1+ (0x001000): File data, allocated sequentially
 *
 * Directory capacity: 63 entries × 64 bytes = 4032 + 16-byte header = 4048
 * ════════════════════════════════════════════════════════════════════════════*/
#define FM_DIR_ADDR      0            /* byte address of directory sector */
#define FM_DATA_START    1            /* first data sector (1-based) */
#define FM_DIR_MAGIC     0x474D464D  /* "MFMG" little-endian */
#define FM_ENTRY_VALID   0x01
#define FM_ENTRY_EMPTY   0xFF
#define FM_ENTRY_DEL     0x00

/* 64 bytes per entry — fields are naturally aligned, no pragma pack needed */
typedef struct {
    uint8_t  status;               /* FM_ENTRY_{VALID,EMPTY,DEL} */
    uint8_t  format;               /* fm_format_t */
    char     timestamp[FM_TS_LEN]; /* "YYYYMMDDHHMMSS", not null-terminated */
    char     name[FM_NAME_LEN];    /* custom name, null-terminated */
    uint32_t start_sector;         /* 1-based sector number for file data */
    uint32_t size;                 /* file byte count */
    uint8_t  _res[8];
} fm_entry_t;  /* 1+1+14+32+4+4+8 = 64 bytes */

/* 4048 bytes total (fits comfortably in one 4096-byte sector) */
typedef struct {
    uint32_t   magic;               /* FM_DIR_MAGIC */
    uint16_t   version;             /* 0x0001 */
    uint16_t   count;               /* active-file count */
    uint8_t    _res[8];
    fm_entry_t entries[FM_MAX_FILES]; /* 63 × 64 = 4032 bytes */
} fm_dir_t;  /* 16 + 4032 = 4048 bytes */

static esp_err_t fm_read_dir(fm_dir_t *d)
{
    return flash_read(FM_DIR_ADDR, (uint8_t *)d, sizeof(*d));
}

static esp_err_t fm_write_dir(const fm_dir_t *d)
{
    esp_err_t r = flash_erase_sector(FM_DIR_ADDR);
    if (r != ESP_OK) return r;
    return flash_write(FM_DIR_ADDR, (const uint8_t *)d, sizeof(*d));
}

esp_err_t fm_init(void)
{
    fm_dir_t *d = malloc(sizeof(*d));
    if (!d) return ESP_ERR_NO_MEM;
    esp_err_t r = fm_read_dir(d);
    if (r != ESP_OK) { free(d); return r; }
    if (d->magic != FM_DIR_MAGIC) {
        ESP_LOGI(TAG, "FM: blank flash, initializing directory");
        memset(d, 0xFF, sizeof(*d));
        d->magic   = FM_DIR_MAGIC;
        d->version = 0x0001;
        d->count   = 0;
        memset(d->_res, 0, sizeof(d->_res));
        for (int i = 0; i < FM_MAX_FILES; i++) d->entries[i].status = FM_ENTRY_EMPTY;
        r = fm_write_dir(d);
    } else {
        ESP_LOGI(TAG, "FM: directory OK, %u files", d->count);
    }
    free(d);
    return r;
}

esp_err_t fm_save(const char *name, fm_format_t fmt, const char *ts,
                  const uint8_t *data, uint32_t size, int *out_idx)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    if (!data || size == 0 || size > FM_MAX_FILE_SIZE) return ESP_ERR_INVALID_ARG;

    fm_dir_t *d = malloc(sizeof(*d));
    if (!d) return ESP_ERR_NO_MEM;

    esp_err_t r = fm_read_dir(d);
    if (r != ESP_OK || d->magic != FM_DIR_MAGIC) { free(d); return ESP_ERR_INVALID_STATE; }

    /* Find first empty or deleted slot */
    int slot = -1;
    for (int i = 0; i < FM_MAX_FILES; i++) {
        uint8_t s = d->entries[i].status;
        if (s == FM_ENTRY_EMPTY || s == FM_ENTRY_DEL) { slot = i; break; }
    }
    if (slot < 0) { free(d); return ESP_ERR_NO_MEM; }

    /* Find next free sector = end of last valid file */
    uint32_t next = FM_DATA_START;
    for (int i = 0; i < FM_MAX_FILES; i++) {
        if (d->entries[i].status == FM_ENTRY_VALID) {
            uint32_t end = d->entries[i].start_sector +
                (d->entries[i].size + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE;
            if (end > next) next = end;
        }
    }

    uint32_t need  = (size + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE;
    uint32_t total = FLASH_TOTAL_SIZE / FLASH_SECTOR_SIZE; /* 4096 */
    if (next + need > total) { free(d); return ESP_ERR_NO_MEM; }

    /* Erase required sectors then write file data */
    for (uint32_t s = 0; s < need; s++) {
        r = flash_erase_sector((next + s) * FLASH_SECTOR_SIZE);
        if (r != ESP_OK) { free(d); return r; }
    }
    r = flash_write(next * FLASH_SECTOR_SIZE, data, size);
    if (r != ESP_OK) { free(d); return r; }

    /* Fill directory entry */
    fm_entry_t *e = &d->entries[slot];
    memset(e, 0, sizeof(*e));
    e->status = FM_ENTRY_VALID;
    e->format = (uint8_t)fmt;
    if (ts)   memcpy(e->timestamp, ts,   strnlen(ts, FM_TS_LEN));
    if (name) strncpy(e->name,     name, FM_NAME_LEN - 1);
    e->start_sector = next;
    e->size = size;

    int cnt = 0;
    for (int i = 0; i < FM_MAX_FILES; i++)
        if (d->entries[i].status == FM_ENTRY_VALID) cnt++;
    d->count = (uint16_t)cnt;

    r = fm_write_dir(d);
    if (out_idx) *out_idx = slot;
    free(d);
    return r;
}

esp_err_t fm_delete(int idx)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    if (idx < 0 || idx >= FM_MAX_FILES) return ESP_ERR_INVALID_ARG;

    fm_dir_t *d = malloc(sizeof(*d));
    if (!d) return ESP_ERR_NO_MEM;

    esp_err_t r = fm_read_dir(d);
    if (r != ESP_OK || d->magic != FM_DIR_MAGIC) { free(d); return ESP_ERR_INVALID_STATE; }
    if (d->entries[idx].status != FM_ENTRY_VALID) { free(d); return ESP_ERR_NOT_FOUND; }

    d->entries[idx].status = FM_ENTRY_DEL;
    int cnt = 0;
    for (int i = 0; i < FM_MAX_FILES; i++)
        if (d->entries[i].status == FM_ENTRY_VALID) cnt++;
    d->count = (uint16_t)cnt;

    r = fm_write_dir(d);
    free(d);
    return r;
}

esp_err_t fm_format(void)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;

    fm_dir_t *d = malloc(sizeof(*d));
    if (!d) return ESP_ERR_NO_MEM;
    memset(d, 0xFF, sizeof(*d));
    d->magic   = FM_DIR_MAGIC;
    d->version = 0x0001;
    d->count   = 0;
    memset(d->_res, 0, sizeof(d->_res));
    for (int i = 0; i < FM_MAX_FILES; i++) d->entries[i].status = FM_ENTRY_EMPTY;

    esp_err_t r = fm_write_dir(d);
    free(d);
    ESP_LOGI(TAG, "FM: storage formatted");
    return r;
}

void fm_get_stats(int *out_file_count, uint32_t *out_used_bytes)
{
    if (out_file_count) *out_file_count = 0;
    if (out_used_bytes) *out_used_bytes = 0;
    if (!s_ready) return;

    fm_dir_t *d = malloc(sizeof(*d));
    if (!d) return;
    if (fm_read_dir(d) != ESP_OK) { free(d); return; }

    int cnt = 0;
    uint32_t used = 0;
    for (int i = 0; i < FM_MAX_FILES; i++) {
        if (d->entries[i].status == FM_ENTRY_VALID) {
            cnt++;
            used += d->entries[i].size;
        }
    }
    free(d);
    if (out_file_count) *out_file_count = cnt;
    if (out_used_bytes) *out_used_bytes = used;
}

/* ---- Init ---- */
esp_err_t spi_flash_ext_init(void)
{
    spi_bus_config_t bus = {
        .miso_io_num   = PIN_MISO,
        .mosi_io_num   = PIN_MOSI,
        .sclk_io_num   = PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = FLASH_PAGE_SIZE + 8,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI_HOST, &bus, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dev = {
        .clock_speed_hz = SPI_FREQ,
        .mode           = 0,
        .spics_io_num   = PIN_CS,
        .queue_size     = 4,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI_HOST, &dev, &s_dev));

    s_mutex = xSemaphoreCreateMutex();

    uint8_t rp = CMD_RELEASE_PD;
    spi_xfer(&rp, NULL, 1);
    vTaskDelay(pdMS_TO_TICKS(1));

    uint8_t mfr = 0, type = 0, cap = 0;
    flash_read_jedec(&mfr, &type, &cap);
    ESP_LOGI(TAG, "JEDEC: MFR=0x%02X TYPE=0x%02X CAP=0x%02X", mfr, type, cap);

    if (mfr == FLASH_JEDEC_MFR) {
        s_ready = true;
        ESP_LOGI(TAG, "W25Q128 ready — 16 MB");
    } else {
        ESP_LOGW(TAG, "Unknown JEDEC, continuing anyway");
        s_ready = true;
    }

    fm_init();
    return ESP_OK;
}

/* ---- WS helpers ---- */
void spi_flash_ext_get_info_json(char *buf, size_t len)
{
    uint8_t mfr = 0, type = 0, cap = 0;
    if (s_ready) flash_read_jedec(&mfr, &type, &cap);
    snprintf(buf, len,
        "{\"type\":\"flash_info\",\"size\":%lu,"
        "\"jedec\":\"%02X%02X%02X\","
        "\"page\":%d,\"sector\":%d,\"block64\":%d,\"ready\":%s}",
        (unsigned long)FLASH_TOTAL_SIZE,
        mfr, type, cap,
        FLASH_PAGE_SIZE, FLASH_SECTOR_SIZE, FLASH_BLOCK64_SIZE,
        s_ready ? "true" : "false");
}

static int _hex_dec(const char *h, uint8_t *out, int max)
{
    int n = 0;
    const char *p = h;
    while (*p && n < max) {
        while (*p == ' ' || *p == '\n') p++;
        if (!*p || !*(p+1)) break;
        char hi = *p++, lo = *p++;
        int hn = (hi>='0'&&hi<='9')?(hi-'0'):(hi>='a'&&hi<='f')?(hi-'a'+10):(hi>='A'&&hi<='F')?(hi-'A'+10):-1;
        int ln = (lo>='0'&&lo<='9')?(lo-'0'):(lo>='a'&&lo<='f')?(lo-'a'+10):(lo>='A'&&lo<='F')?(lo-'A'+10):-1;
        if (hn < 0 || ln < 0) { p -= 2; break; }
        out[n++] = (hn << 4) | ln;
    }
    return n;
}

void spi_flash_ext_read_ws(uint32_t addr, uint32_t rlen)
{
    if (!s_ready) { ws_broadcast("{\"type\":\"flash_err\",\"msg\":\"not ready\"}"); return; }
    if (rlen > 1024) rlen = 1024;
    uint8_t *buf = malloc(rlen);
    if (!buf) { ws_broadcast("{\"type\":\"flash_err\",\"msg\":\"oom\"}"); return; }

    esp_err_t ret = flash_read(addr, buf, rlen);
    if (ret != ESP_OK) {
        free(buf);
        ws_broadcast("{\"type\":\"flash_err\",\"msg\":\"read failed\"}");
        return;
    }

    size_t jsz = rlen * 2 + 80;
    char *json = malloc(jsz);
    if (!json) { free(buf); return; }
    int off = snprintf(json, jsz,
        "{\"type\":\"flash_data\",\"addr\":%" PRIu32 ",\"len\":%" PRIu32 ",\"data\":\"",
        addr, rlen);
    for (uint32_t i = 0; i < rlen && off < (int)jsz - 3; i++)
        off += snprintf(json + off, jsz - off, "%02X", buf[i]);
    snprintf(json + off, jsz - off, "\"}");
    ws_broadcast(json);
    free(json); free(buf);
}

void spi_flash_ext_write_ws(uint32_t addr, const char *hex_data)
{
    if (!s_ready) { ws_broadcast("{\"type\":\"flash_err\",\"msg\":\"not ready\"}"); return; }
    size_t hlen = strlen(hex_data);
    uint8_t *buf = malloc(hlen / 2 + 1);
    if (!buf) { ws_broadcast("{\"type\":\"flash_err\",\"msg\":\"oom\"}"); return; }
    int n = _hex_dec(hex_data, buf, hlen / 2 + 1);
    char rsp[96];
    if (n > 0) {
        esp_err_t ret = flash_write(addr, buf, n);
        snprintf(rsp, sizeof(rsp),
            "{\"type\":\"flash_ok\",\"op\":\"write\",\"addr\":%" PRIu32 ",\"bytes\":%d,\"ok\":%s}",
            addr, n, ret == ESP_OK ? "true" : "false");
    } else {
        snprintf(rsp, sizeof(rsp), "{\"type\":\"flash_err\",\"msg\":\"bad hex\"}");
    }
    free(buf);
    ws_broadcast(rsp);
}

void spi_flash_ext_erase_ws(uint32_t addr, uint32_t size)
{
    if (!s_ready) { ws_broadcast("{\"type\":\"flash_err\",\"msg\":\"not ready\"}"); return; }
    esp_err_t ret;
    const char *op;
    if (size == 0 || size <= FLASH_SECTOR_SIZE) {
        ret = flash_erase_sector(addr); op = "sector";
    } else if (size <= FLASH_BLOCK32_SIZE) {
        ret = flash_erase_block32(addr); op = "block32";
    } else if (size <= FLASH_BLOCK64_SIZE) {
        ret = flash_erase_block64(addr); op = "block64";
    } else {
        ret = flash_erase_chip(); op = "chip";
    }
    char rsp[96];
    snprintf(rsp, sizeof(rsp),
        "{\"type\":\"flash_ok\",\"op\":\"erase_%s\",\"addr\":%" PRIu32 ",\"ok\":%s}",
        op, addr, ret == ESP_OK ? "true" : "false");
    ws_broadcast(rsp);
}

/* ════════════════════════════════════════════════════════════════════════════
 * HTTP API — raw flash access (legacy, kept for low-level debug)
 * ════════════════════════════════════════════════════════════════════════════*/
static esp_err_t h_info(httpd_req_t *req)
{
    char buf[128];
    spi_flash_ext_get_info_json(buf, sizeof(buf));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, strlen(buf));
    return ESP_OK;
}

static esp_err_t h_read(httpd_req_t *req)
{
    char query[64] = {0};
    httpd_req_get_url_query_str(req, query, sizeof(query));
    uint32_t addr = 0, rlen = 256;
    char val[16];
    if (httpd_query_key_value(query, "addr", val, sizeof(val)) == ESP_OK)
        addr = (uint32_t)strtoul(val, NULL, 0);
    if (httpd_query_key_value(query, "len", val, sizeof(val)) == ESP_OK)
        rlen = (uint32_t)atoi(val);
    if (rlen > 1024) rlen = 1024;

    uint8_t *data = malloc(rlen);
    if (!data) return ESP_ERR_NO_MEM;
    esp_err_t ret = flash_read(addr, data, rlen);

    if (ret != ESP_OK) {
        free(data);
        httpd_resp_sendstr(req, "{\"status\":\"err\",\"message\":\"read failed\"}");
        return ESP_OK;
    }

    size_t jsz = rlen * 2 + 64;
    char *json = malloc(jsz);
    if (!json) { free(data); return ESP_ERR_NO_MEM; }
    int off = snprintf(json, jsz,
        "{\"status\":\"ok\",\"addr\":%" PRIu32 ",\"len\":%" PRIu32 ",\"data\":\"", addr, rlen);
    for (uint32_t i = 0; i < rlen; i++)
        off += snprintf(json + off, jsz - off, "%02X", data[i]);
    snprintf(json + off, jsz - off, "\"}");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    free(json); free(data);
    return ESP_OK;
}

static esp_err_t h_write(httpd_req_t *req)
{
    char *body = malloc(2200);
    if (!body) return ESP_ERR_NO_MEM;
    int n = httpd_req_recv(req, body, 2199);
    if (n <= 0) { free(body); return ESP_FAIL; }
    body[n] = '\0';

    uint32_t addr = 0;
    char data[1100] = {0};
    char *p;
    if ((p = strstr(body, "\"addr\":"))) addr = (uint32_t)strtoul(p + 7, NULL, 0);
    if ((p = strstr(body, "\"data\":\""))) {
        p += 8; char *e = strchr(p, '"');
        if (e && (e - p) < 1100) memcpy(data, p, e - p);
    }
    free(body);

    uint8_t *buf = malloc(512);
    if (!buf) return ESP_ERR_NO_MEM;
    int cnt = _hex_dec(data, buf, 512);
    esp_err_t ret = (cnt > 0) ? flash_write(addr, buf, cnt) : ESP_ERR_INVALID_ARG;
    free(buf);

    char rsp[64];
    snprintf(rsp, sizeof(rsp),
        "{\"status\":\"%s\",\"written\":%d}", ret == ESP_OK ? "ok" : "err", cnt);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, rsp);
    return ESP_OK;
}

static esp_err_t h_erase(httpd_req_t *req)
{
    char body[128];
    int n = httpd_req_recv(req, body, sizeof(body) - 1);
    if (n <= 0) return ESP_FAIL;
    body[n] = '\0';

    uint32_t addr = 0;
    char type[16] = "sector";
    char *p;
    if ((p = strstr(body, "\"addr\":"))) addr = (uint32_t)strtoul(p + 7, NULL, 0);
    if ((p = strstr(body, "\"type\":\""))) {
        p += 8; char *e = strchr(p, '"');
        if (e && (e - p) < (int)sizeof(type)) { memcpy(type, p, e - p); type[e-p]=0; }
    }

    esp_err_t ret;
    if      (strcmp(type, "chip")    == 0) ret = flash_erase_chip();
    else if (strcmp(type, "block64") == 0) ret = flash_erase_block64(addr);
    else if (strcmp(type, "block32") == 0) ret = flash_erase_block32(addr);
    else                                    ret = flash_erase_sector(addr);

    httpd_resp_sendstr(req, ret == ESP_OK ? "{\"status\":\"ok\"}" : "{\"status\":\"err\"}");
    return ESP_OK;
}

/* ════════════════════════════════════════════════════════════════════════════
 * HTTP API — File Manager
 * ════════════════════════════════════════════════════════════════════════════*/

/* GET /api/files — list all valid files */
static esp_err_t h_fm_list(httpd_req_t *req)
{
    fm_dir_t *d = malloc(sizeof(*d));
    if (!d) { httpd_resp_sendstr(req, "{\"err\":\"oom\"}"); return ESP_OK; }
    fm_read_dir(d);

    int bufsz = 8192;
    char *json = malloc(bufsz);
    if (!json) { free(d); httpd_resp_sendstr(req, "{\"err\":\"oom\"}"); return ESP_OK; }

    int off = 0, rem = bufsz;
    off += snprintf(json, rem, "{\"files\":[");
    rem = bufsz - off;
    bool first = true;
    int vcnt = 0;

    for (int i = 0; i < FM_MAX_FILES && rem > 160; i++) {
        fm_entry_t *e = &d->entries[i];
        if (e->status != FM_ENTRY_VALID) continue;
        vcnt++;
        char ts[FM_TS_LEN + 1] = {0};
        memcpy(ts, e->timestamp, FM_TS_LEN);
        char nm[FM_NAME_LEN + 1] = {0};
        memcpy(nm, e->name, FM_NAME_LEN);
        int n = snprintf(json + off, rem,
            "%s{\"idx\":%d,\"name\":\"%s\",\"format\":\"%s\",\"ts\":\"%s\",\"size\":%" PRIu32 "}",
            first ? "" : ",", i, nm,
            e->format == FM_FMT_JSON ? "json" : "csv",
            ts, e->size);
        off += n; rem -= n;
        first = false;
    }
    int n = snprintf(json + off, rem, "],\"count\":%d,\"total\":%d}", vcnt, FM_MAX_FILES);
    off += n;

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, off);
    free(json); free(d);
    return ESP_OK;
}

/* GET /api/file?idx=N — download a file (chunked) */
static esp_err_t h_fm_get(httpd_req_t *req)
{
    char qs[32] = {0}, val[16] = {0};
    int idx = -1;
    if (httpd_req_get_url_query_str(req, qs, sizeof(qs)) == ESP_OK)
        if (httpd_query_key_value(qs, "idx", val, sizeof(val)) == ESP_OK)
            idx = atoi(val);

    fm_dir_t *d = malloc(sizeof(*d));
    if (!d) { httpd_resp_sendstr(req, "OOM"); return ESP_OK; }
    fm_read_dir(d);

    if (idx < 0 || idx >= FM_MAX_FILES || d->entries[idx].status != FM_ENTRY_VALID) {
        free(d);
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_sendstr(req, "not found");
        return ESP_OK;
    }

    fm_entry_t *e = &d->entries[idx];
    uint32_t fsz   = e->size;
    uint32_t faddr = e->start_sector * FLASH_SECTOR_SIZE;
    const char *ct  = (e->format == FM_FMT_JSON) ? "application/json" : "text/csv; charset=utf-8";
    const char *ext = (e->format == FM_FMT_JSON) ? "json" : "csv";

    char ts[FM_TS_LEN + 1] = {0};
    memcpy(ts, e->timestamp, FM_TS_LEN);
    char nm[FM_NAME_LEN + 1] = {0};
    memcpy(nm, e->name, FM_NAME_LEN);
    free(d);

    char disp[128];
    snprintf(disp, sizeof(disp), "attachment; filename=\"%s_%s.%s\"", ts, nm, ext);
    httpd_resp_set_type(req, ct);
    httpd_resp_set_hdr(req, "Content-Disposition", disp);

    uint8_t *chunk = malloc(FLASH_SECTOR_SIZE);
    if (!chunk) { httpd_resp_sendstr(req, "OOM"); return ESP_OK; }

    uint32_t remaining = fsz, addr = faddr;
    while (remaining > 0) {
        uint32_t csz = remaining < FLASH_SECTOR_SIZE ? remaining : FLASH_SECTOR_SIZE;
        flash_read(addr, chunk, csz);
        httpd_resp_send_chunk(req, (char *)chunk, (ssize_t)csz);
        addr += csz; remaining -= csz;
    }
    httpd_resp_send_chunk(req, NULL, 0);
    free(chunk);
    return ESP_OK;
}

/* POST /api/file/save
 * Body format: "name|format|ts\n<raw file data>"
 * Example:     "RS485_log|csv|20260525103000\ntimestamp,dir,data\n..."
 */
static esp_err_t h_fm_save(httpd_req_t *req)
{
    int total = req->content_len;
    if (total <= 0 || total > (int)(FM_MAX_FILE_SIZE + 128)) {
        httpd_resp_set_status(req, "413 Content Too Large");
        httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"too large\"}");
        return ESP_OK;
    }

    /* Prefer PSRAM for large receive buffer */
    char *body = heap_caps_malloc(total + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!body) body = malloc(total + 1);
    if (!body) { httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"oom\"}"); return ESP_OK; }

    int rcvd = 0;
    while (rcvd < total) {
        int n = httpd_req_recv(req, body + rcvd, total - rcvd);
        if (n <= 0) { free(body); return ESP_FAIL; }
        rcvd += n;
    }
    body[rcvd] = '\0';

    /* Split at first '\n': header line | file data */
    char *sep = memchr(body, '\n', rcvd);
    if (!sep) { free(body); httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"fmt\"}"); return ESP_OK; }
    *sep = '\0';

    char name[FM_NAME_LEN + 1] = {0};
    char fmt_str[8] = {0};
    char ts[FM_TS_LEN + 1] = {0};
    char *sp = NULL;
    char *tok = strtok_r(body, "|", &sp);
    if (tok) strncpy(name, tok, FM_NAME_LEN);
    tok = strtok_r(NULL, "|", &sp);
    if (tok) strncpy(fmt_str, tok, sizeof(fmt_str) - 1);
    tok = strtok_r(NULL, "|", &sp);
    if (tok) strncpy(ts, tok, FM_TS_LEN);

    /* Sanitize filename */
    for (char *p = name; *p; p++)
        if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-') *p = '_';
    if (!name[0]) strncpy(name, "log", sizeof(name));

    const uint8_t *fdata = (uint8_t *)(sep + 1);
    uint32_t flen = (uint32_t)(rcvd - (int)(sep + 1 - body));

    if (flen == 0) { free(body); httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"empty\"}"); return ESP_OK; }

    fm_format_t fmt = (strncmp(fmt_str, "json", 4) == 0) ? FM_FMT_JSON : FM_FMT_CSV;
    int idx = -1;
    esp_err_t ret = fm_save(name, fmt, ts, fdata, flen, &idx);

    char rsp[96];
    snprintf(rsp, sizeof(rsp),
             "{\"ok\":%s,\"idx\":%d,\"size\":%" PRIu32 "}",
             ret == ESP_OK ? "true" : "false", idx, flen);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, rsp);
    free(body);
    return ESP_OK;
}

/* POST /api/file/delete?idx=N */
static esp_err_t h_fm_delete(httpd_req_t *req)
{
    char qs[32] = {0}, val[16] = {0};
    int idx = -1;
    if (httpd_req_get_url_query_str(req, qs, sizeof(qs)) == ESP_OK)
        if (httpd_query_key_value(qs, "idx", val, sizeof(val)) == ESP_OK)
            idx = atoi(val);

    esp_err_t r = fm_delete(idx);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, r == ESP_OK ? "{\"ok\":true}" : "{\"ok\":false}");
    return ESP_OK;
}

/* POST /api/files/format */
static esp_err_t h_fm_format(httpd_req_t *req)
{
    esp_err_t r = fm_format();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, r == ESP_OK ? "{\"ok\":true}" : "{\"ok\":false}");
    return ESP_OK;
}

/* ---- Route table ---- */
static const api_route_t s_routes[] = {
    /* Legacy raw-flash access */
    {"/api/flash/info",    HTTP_GET,  h_info},
    {"/api/flash/read",    HTTP_GET,  h_read},
    {"/api/flash/write",   HTTP_POST, h_write},
    {"/api/flash/erase",   HTTP_POST, h_erase},
    /* File Manager */
    {"/api/files",         HTTP_GET,  h_fm_list},
    {"/api/file",          HTTP_GET,  h_fm_get},
    {"/api/file/save",     HTTP_POST, h_fm_save},
    {"/api/file/delete",   HTTP_POST, h_fm_delete},
    {"/api/files/format",  HTTP_POST, h_fm_format},
    {NULL, 0, NULL},
};

esp_err_t spi_flash_ext_register_routes(void)
{
    for (int i = 0; s_routes[i].uri; i++)
        http_server_register_route(&s_routes[i]);
    return ESP_OK;
}
