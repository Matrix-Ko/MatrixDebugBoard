idf_component_register(
    SRCS "src/uart_485.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_uart esp_driver_gpio esp_timer nvs_flash dashboard
)
