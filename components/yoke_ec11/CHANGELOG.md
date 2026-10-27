# Changelog

## Unreleased

* Return INVALID_ARG for NULL handles and retain failed initialization rollback for retry.

* Document the RootMaker development board scope without adding a targets list.

* Declare ESP-IDF >=5.5.3, matching the verified SDK and address-change API.
* Accept read-only initialization configuration and document read-to-clear/address semantics.

## 1.0.0 - 2026-08-04

### Added

* Split the Yoke-EC11 driver out of the combined `yoke` component.
* Driver for the Yoke-EC11-V10 I2C rotary encoder, button, and two onboard
  WS2812 LEDs.
