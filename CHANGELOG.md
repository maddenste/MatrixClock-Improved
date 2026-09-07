# Changelog

All notable changes to MatrixClock Improved Firmware are documented here.

## v3.1.0 - 2026-09-07

- Learn oscillator drift from raw 64-bit elapsed time, independently of
  wall-clock phase correction, so slewing cannot bias the learned rate.
- Apply a qualified correction during the same boot without a time jump.
  Require two agreeing rate estimates; smooth later adjustments and never
  change the rate during an open chronograph session.
- Validate NTP response origins, server address, version, stratum and server
  processing time. Prefer consistent low-delay samples; retain a responsive
  server across checks and resolve again after failure.
- Keep the five-minute settling period and two-hour check interval. Reject
  implausible or uncertain rate readings and retain the last good correction.
- Move reset-recovery data outside the OTA bootloader's RTC-memory area.
  Add a checksum without changing the EEPROM record size, check calibration
  writes, and preserve compatible older saved rates as provisional seeds.
- Save the first qualified correction promptly, then changed corrections
  at most daily during normal running or through an intentional save/reboot.
- Use 64-bit timing for uninterrupted chronograph runs across the previous
  approximately 71-minute microsecond-counter wrap.
- Replace ambiguous calibration fields with correction in use, learning
  status, measured fast/slow error, progress, next check, time alignment,
  measurement timestamp and saved timestamp. Refresh with the existing
  one-second Device info request; retain five-second signal polling.
- Add numerical/source-contract and isolated browser checks. These do not
  replace compilation, on-device accuracy measurements or OTA testing.
- Place Display settings before the Scheduled reboot box. Daily reboot
  defaults off, with recovery reboot enabled by default for a last accepted
  NTP offset greater than 0.5
  seconds. Both use the selected hour/minute and preserve chronograph
  postponement. Startup during the scheduled minute does not reboot-loop.
- Store reboot choices in a separate checked record without moving existing
  Wi-Fi, authentication or calibration data. Factory reset restores defaults.
- Add a live days/hours/minutes/seconds uptime counter to Device info using
  the existing one-second polling request and the 64-bit elapsed timer.
- Show the next NTP check as a fixed local time in Device info while retaining
  the live countdown in Clock accuracy calibration and the waiting message
  when clock activity postpones an overdue check.
- Collapse the scheduled-reboot and API guidance behind clearly labelled help
  sections, matching the existing calibration explanation.
- Use the same date-and-time format, including year and seconds, for the last
  calibration measurement and the saved correction timestamp.
- Move time, display and scheduled-reboot controls plus maintenance actions to
  a separate authenticated Settings page. Keep live status, calibration, API
  messaging and Device info on the smaller main page.
- Replace the two scheduled-reboot checkboxes with one Reboot mode dropdown,
  making daily and large-error recovery modes mutually exclusive.
- Move the saved API message scroll-count control from the main page to the
  bottom of Display settings. It continues to save immediately without reboot.
- Place Device info above Clock accuracy calibration on the main page.
- Reorganise Display settings into consistent rows and make every display
  choice save and apply without reboot. A dedicated button writes all selected
  display choices together, avoiding one flash write per dropdown change.
  Validate changes atomically, restore the previous state after a failed write,
  and protect an active chronograph from a mid-session layout change.
- Label settings categories with either Reboot required or No reboot, so the
  effect of each control is clear before changing it.
- Add a live 32 by 8 web preview that follows display orientation, with red,
  green, blue, and white preview colour choices.

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
