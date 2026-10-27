idf_component_register(
    SRCS "src/yoke_rad60.c" "src/yoke_rad60_protocol.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES esp_driver_uart rootmaker_utils
    PRIV_REQUIRES esp_driver_gpio heap freertos
)
