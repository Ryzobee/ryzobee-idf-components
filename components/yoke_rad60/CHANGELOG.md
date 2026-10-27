# Changelog

## Unreleased

* Retain failed initialization rollback, including UART construction.
* Use yoke_rad60_target_t and yoke_rad60_detection_t; keep implementation dependencies private.

* Update communication health after every valid frame and bound receive batches so continuous reports cannot postpone health checks.

* Document the RootMaker development board scope without adding a targets list.

* Resynchronize after invalid report lengths while preserving incomplete report/response frames.

* Document snapshot lock wait, communication status and valid output conditions.

## 1.0.0 - 2026-08-04

### Added

* Split the Yoke-RAD60 driver out of the combined `yoke` component.
* UART radar driver with background processing task and cached snapshots.
