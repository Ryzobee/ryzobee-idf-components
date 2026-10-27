# Changelog

## Unreleased

* Return INVALID_ARG for NULL handles and retain partial MCPWM construction on cleanup failure.

* Retain failed initialization rollback for cleanup retry on the next init call.

* Document the RootMaker development board scope without adding a targets list.

* Include MIT/Apache-2.0 license texts, source change notices and attribution.
* Clarify PWM duty, direction, stop/resume and cleanup semantics.

## 1.0.0 - 2026-08-04

### Added

* Split the Yoke-motor driver out of the combined `yoke` component.
* Brushed DC motor driver (MCPWM) with percent-based speed control.
