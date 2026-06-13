idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES dashboard wifi_module uart_485 uart_debug can_twai spi_flash_ext ota_manager esp_driver_gpio esp_timer nvs_flash
)
