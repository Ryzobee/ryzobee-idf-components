idf_component_register(
    SRCS "src/yoke_rgbw.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio rootmaker_utils
    PRIV_REQUIRES esp_driver_rmt freertos
)
