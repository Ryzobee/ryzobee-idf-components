# Changelog

## Unreleased

* Retain failed initialization rollback for retry without adding application-side synchronization.

* Document the RootMaker development board scope without adding a targets list.

* Serialize button creation, removal and subscription changes on the ESP timer task.
* Reject invalid event indices and explicitly reject unsupported multiple-click subscriptions.
* Document callback restrictions and correct the example active-high button description.

## 1.0.0 - 2026-08-04

### Added

* Split the Yoke-KEYW driver out of the combined `yoke` component.
* Instance-based button and GPIO indicator LED driver built on
  `espressif/button`.
