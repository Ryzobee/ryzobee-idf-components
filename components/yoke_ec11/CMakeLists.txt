idf_component_register(
    SRCS "src/yoke_ec11.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2c rootmaker_utils
)
