idf_component_register(
    SRCS "src/yoke_motor.c" "src/yoke_motor_mcpwm.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio rootmaker_utils
    PRIV_REQUIRES espressif__bdc_motor esp_driver_mcpwm
)
