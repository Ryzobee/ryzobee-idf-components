# Changelog

## Unreleased

* Return INVALID_ARG for NULL handles, retain failed initialization rollback, and keep RMT dependencies private.

* Document the RootMaker development board scope without adding a targets list.

* End every frame with a 300 us all-low reset interval, including consecutive writes.

* Document immediate whole-strip transmission, serialization and explicit off frames.

## 1.0.0 - 2026-08-04

### Added

* Split the Yoke-RGBW driver out of the combined `yoke` component.
* RMT-based RGBW WS2812 strip driver.
