idf_component_register(
    SRCS "src/yoke_zxacc_maker.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2c esp_driver_gpio rootmaker_utils
)
