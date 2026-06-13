idf_component_register(
    SRCS "src/can_twai.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_gpio esp_timer nvs_flash dashboard
)

# Suppress legacy TWAI deprecation warning
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-error=cpp")
