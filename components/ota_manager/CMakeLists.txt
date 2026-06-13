idf_component_register(
    SRCS "src/ota_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES
        app_update esp_http_client nvs_flash esp_timer freertos dashboard
)
