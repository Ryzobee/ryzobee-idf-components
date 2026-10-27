idf_component_register(
    SRCS "src/yoke_screen.c" "src/lcd_panel_st7789_spec.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES lvgl__lvgl espressif__esp_lvgl_port rootmaker_utils
    PRIV_REQUIRES esp_lcd esp_driver_spi esp_driver_i2c esp_driver_gpio heap freertos
)
