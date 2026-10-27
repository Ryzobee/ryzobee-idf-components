# Third-party notices

The following files retain their original Espressif copyright and Apache-2.0
SPDX notices:

- `src/yoke_screen.c`
- `src/lcd_panel_st7789_spec.c`
- `src/lcd_panel_st7789_spec.h`

Sources: [Espressif ESP-IDF LCD driver](https://github.com/espressif/esp-idf/tree/master/components/esp_lcd) and Espressif display integration code.

RootMaker modifications: RootMaker panel and touch setup, vendor initialization, shared I2C and exclusive GPIO lifecycle. Modified files carry a change notice.

Other original RootMaker code is licensed under MIT. The combined package
contains code under both MIT and Apache-2.0; the manifest therefore uses
`MIT AND Apache-2.0`. Complete license texts are included in `LICENSE`.
