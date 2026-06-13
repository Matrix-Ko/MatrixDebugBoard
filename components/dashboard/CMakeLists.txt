idf_component_register(
    SRCS
        "src/dashboard.c"
        "src/http_server.c"
        "src/ws_server.c"
        "src/web_assets.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_http_server nvs_flash esp_wifi esp_netif lwip esp_timer
        esp_driver_gpio uart_485 uart_debug can_twai spi_flash_ext
        app_update ota_manager
    EMBED_FILES
        "web/index.html"
        "web/style.css"
        "web/app.js"
        "web/modules/rs485.html"
        "web/modules/can.html"
        "web/modules/flash.html"
        "web/modules/system.html"
        "web/modules/uart.html"
        "web/modules/ota.html"
        "web/modules/ai.html"
)
