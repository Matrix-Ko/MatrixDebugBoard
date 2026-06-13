#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "dashboard.h"
#include "wifi_module.h"
#include "uart_485.h"
#include "uart_debug.h"
#include "can_twai.h"
#include "spi_flash_ext.h"
#include "ota_manager.h"

static const char *TAG = "MAIN";

#define GPIO_LED    GPIO_NUM_2
#define GPIO_RELAY  GPIO_NUM_42
#define NVS_NS_REL  "relay_cfg"

/* --- Relay control with NVS persistence --- */
int s_relay_state = 0;

void relay_set(int v)
{
    s_relay_state = v ? 1 : 0;
    gpio_set_level(GPIO_RELAY, s_relay_state);

    nvs_handle_t h;
    if (nvs_open(NVS_NS_REL, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i32(h, "state", s_relay_state);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "Relay → %s (saved)", s_relay_state ? "ON" : "OFF");
}

static void relay_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << GPIO_RELAY),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    /* Restore last state from NVS */
    nvs_handle_t h;
    if (nvs_open(NVS_NS_REL, NVS_READONLY, &h) == ESP_OK) {
        int32_t v = 0;
        if (nvs_get_i32(h, "state", &v) == ESP_OK) s_relay_state = (int)v;
        nvs_close(h);
    }
    gpio_set_level(GPIO_RELAY, s_relay_state);
    ESP_LOGI(TAG, "Relay init → %s", s_relay_state ? "ON" : "OFF");
}

/* --- LED blink task --- */
static void led_task(void *arg)
{
    (void)arg;
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << GPIO_LED),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    while (1) {
        gpio_set_level(GPIO_LED, 1);
        vTaskDelay(pdMS_TO_TICKS(900));
        gpio_set_level(GPIO_LED, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void app_main(void)
{
    printf("\n========================================\n");
    printf("  MatrixDebugBoard ESP32-S3\n");
    printf("  RS485 + CAN + UART + SPI Flash Debugger\n");
    printf("========================================\n\n");

    /* NVS must be initialized before any component that uses it */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    relay_init();  /* early — before any WS connections possible */

    wifi_module_init();

    ESP_ERROR_CHECK(dashboard_init());
    ESP_ERROR_CHECK(dashboard_start());

    wifi_module_register_routes();
    uart_485_init();
    uart_485_register_routes();
    uart_debug_init();
    uart_debug_register_routes();
    can_twai_init();
    can_twai_register_routes();
    spi_flash_ext_init();
    spi_flash_ext_register_routes();

    ota_manager_init();
    ota_manager_register_routes();

    xTaskCreatePinnedToCore(led_task, "led", 2048, NULL, 1, NULL, 1);

    ESP_LOGI(TAG, "Ready. AP: http://192.168.4.1");
}
