#pragma once
#include "esp_err.h"

typedef struct {
    const char  *name;
    const char  *tab_label;
    esp_err_t  (*init)(void);
    esp_err_t  (*get_status)(char *buf, size_t len);
    const char *(*get_html)(void);
} dashboard_module_t;

esp_err_t dashboard_init(void);
esp_err_t dashboard_start(void);
esp_err_t dashboard_register_module(const dashboard_module_t *mod);
