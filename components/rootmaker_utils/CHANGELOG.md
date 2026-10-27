# Changelog

## Unreleased

* Use UART0_TX/RX, BOARD_SDA/SCL and rootmaker_rs_port_t; keep one name per definition.

* Document the RootMaker development board scope without adding a targets list.

* Reject partial/swapped I2C pin overlap while preserving exact-pair sharing.
* Expose read-only port descriptors and validate GPIO inputs.
* Correct predefined-port and reference-count documentation.

## 1.0.0 - 2026-08-28

### Added

* `rootmaker_port_t` interface port model and predefined RootMaker board ports.
* I2C master bus pool keyed by ordered SDA/SCL pins with reference-counted sharing
  and automatically selected controllers.
* `rootmaker_get_port_pins()` helper for non-I2C drivers.
