# Changelog

## Unreleased

* Declare rootmaker_utils as a public managed dependency.

* Drain queued SPI transfers under the LVGL lock before deleting the display.
* Wait for the LVGL port worker to exit before releasing resources or allowing reinitialization; preserve cleanup retry after timeout.

* Specify display buffer capacity in pixels instead of bytes, avoiding twice the required allocation.

* Document the RootMaker development board scope without adding a targets list.

* Remove yoke_screen_draw_bitmap(); use LVGL widgets, images and canvases for all application drawing.

* Use the explicit managed LVGL port name and inherit public dependencies in the example.

* Claim LCD/backlight/touch reset/interrupt GPIOs exclusively before hardware setup.
* Keep SDA/SCL shared, reserve the touch address and release resources on rollback/cleanup.
* Include MIT/Apache-2.0 license texts, source change notices and attribution.
* Clarify SPI versus PWM, LVGL ownership and image/canvas buffer lifetime.

## 1.0.0 - 2026-08-04

### Added

* Split the Yoke-Screen driver out of the combined `yoke` component.
* ST7789 LCD + CST816T touch initialization with an LVGL display port.
