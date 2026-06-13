idf_component_register(
    SRCS "src/spi_flash_ext.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_spi esp_timer dashboard
)
