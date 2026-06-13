#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

/* W25Q128JVSIQ: 128Mbit = 16MB, page=256B, sector=4KB, block64=64KB */
#define FLASH_TOTAL_SIZE   (16UL * 1024 * 1024)
#define FLASH_PAGE_SIZE    256
#define FLASH_SECTOR_SIZE  4096
#define FLASH_BLOCK32_SIZE (32 * 1024)
#define FLASH_BLOCK64_SIZE (64 * 1024)
#define FLASH_JEDEC_MFR    0xEF   /* Winbond */
#define FLASH_JEDEC_TYPE   0x40
#define FLASH_JEDEC_CAP    0x18   /* 128Mbit */

esp_err_t spi_flash_ext_init(void);
esp_err_t spi_flash_ext_register_routes(void);

/* Low-level API */
esp_err_t flash_read_jedec(uint8_t *mfr, uint8_t *type, uint8_t *cap);
esp_err_t flash_read(uint32_t addr, uint8_t *buf, size_t len);
esp_err_t flash_write(uint32_t addr, const uint8_t *buf, size_t len);
esp_err_t flash_erase_sector(uint32_t addr);
esp_err_t flash_erase_block32(uint32_t addr);
esp_err_t flash_erase_block64(uint32_t addr);
esp_err_t flash_erase_chip(void);
uint8_t   flash_read_status1(void);

/* WS helpers called from ws_server */
void spi_flash_ext_get_info_json(char *buf, size_t len);
void spi_flash_ext_read_ws(uint32_t addr, uint32_t rlen);
void spi_flash_ext_write_ws(uint32_t addr, const char *hex_data);
void spi_flash_ext_erase_ws(uint32_t addr, uint32_t size);

/* ── File Manager ─────────────────────────────────────────────────────────── */
#define FM_NAME_LEN      32           /* filename bytes incl. null */
#define FM_TS_LEN        14           /* "YYYYMMDDHHMMSS", no null */
#define FM_MAX_FILES     63           /* directory slot count */
#define FM_MAX_FILE_SIZE (128UL*1024) /* per-file cap: 128 KB */

typedef enum { FM_FMT_CSV = 0, FM_FMT_JSON = 1 } fm_format_t;

esp_err_t fm_init(void);
esp_err_t fm_save(const char *name, fm_format_t fmt, const char *ts,
                  const uint8_t *data, uint32_t size, int *out_idx);
esp_err_t fm_delete(int idx);
esp_err_t fm_format(void);

/* Storage stats: file count and total used bytes */
void fm_get_stats(int *out_file_count, uint32_t *out_used_bytes);
