idf_component_register(
    SRCS "src/wifi_module.c" "src/wifi_manager.c" "src/wifi_storage.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_netif nvs_flash lwip dashboard
    EMBED_FILES "web/wifi.html"
)
