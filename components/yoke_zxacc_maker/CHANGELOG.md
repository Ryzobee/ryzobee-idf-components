# Changelog

## Unreleased

* Declare rootmaker_utils as public and retain failed initialization rollback for retry.

* Document the RootMaker development board scope without adding a targets list.

* Clarify device attachment versus probing, millisecond button timing and second-based wake-up.

## 1.0.0 - 2026-08-04

### Added

* Split the Yoke-ZXACC-maker driver out of the combined `yoke` component.
* PY32F002B power-management baseboard driver (battery voltage, charge state,
  button power timing, timed wake-up, shutdown).
