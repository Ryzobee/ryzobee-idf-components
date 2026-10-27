idf_component_register(
    SRCS "src/yoke_keyw.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio espressif__button rootmaker_utils
    PRIV_REQUIRES esp_timer freertos
)
