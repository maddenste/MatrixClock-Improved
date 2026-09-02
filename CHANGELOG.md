# Changelog

All notable changes to MatrixClock Improved Firmware are documented here.

## v3.0.1 - 2026-09-02

### Home Assistant and API

- Added an optional per-message `scrolls=1` to `scrolls=5` API parameter, so
  Home Assistant can choose the number of passes for an individual message.
- Fixed the on-page message form to submit to the same address used to open
  the web interface, so it continues to work when local web authentication is
  enabled and the clock is accessed through a DNS name.

## v3.0.0 - 2026-09-01

First public release of the modified MatrixClock firmware, derived from the
original HACK LABS MatrixClock v2.2 package.

### Display and controls

- Improved display rendering, orientation handling, transitions, brightness,
  startup status, separators, date scrolling, and animations.
- Added physical button controls for date display, chronograph control,
  message cancellation, and timed factory reset.
- Added precision `MM:SS:cc` and normal `HH:MM:ss` chronograph modes.
- Improved chronograph timing, history, elapsed-hour indicators, limits, and
  idle/restart behaviour.

### Timekeeping

- Added accurate NTP synchronisation with RTC fallback.
- Added configurable timezone, daylight-saving, and NTP-provider settings.
- Added filtered oscillator-drift calibration with stored compensation.
- Added configurable daily reboot behaviour.

### Setup and reliability

- Added access-point Wi-Fi provisioning and clean-install handling.
- Added persistent settings with product and schema migration safeguards.
- Added factory reset, clean 4 MB recovery image support, and local OTA
  firmware updates.
- Improved runtime efficiency, memory use, diagnostics, and recovery paths.

### Web interface and integration

- Rebuilt the local web interface with live device information, NTP status,
  calibration reporting, and responsive settings controls.
- Added optional local web authentication and protected firmware-update pages.
- Added Home Assistant / HTTP API scrolling-message integration, playback
  controls, cancellation, and safe chronograph interaction.

### Attribution

- Retained the original HACK LABS MatrixClock attribution and GPL v3 notices.
- Added modified-build information for MatrixClock Improved Firmware v3.0.0.
