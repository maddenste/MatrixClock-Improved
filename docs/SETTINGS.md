# Timekeeping and settings

[Back to the project](../README.md)

## Clock drift calibration

After NTP establishes the time, the display is driven by the ESP8266's software clock rather than polling the DS3231 for each second. Calibration measures the elapsed timer against NTP, requires two agreeing rate estimates and applies a correction between checks.

The clock has a five-minute settling period and checks NTP every two hours. Uncertain or implausible measurements are rejected; the last good correction is retained. The main page reports learning progress, active compensation and time alignment. Calibration rate changes are postponed while a stopwatch session is open.

**This is software-clock drift calibration, not DS3231 hardware calibration.** The RTC remains the battery-backed backup clock. No guaranteed accuracy figure is claimed for every device or network.

## NTP server

For help choosing a time server, see the [NTP Pool's usage guidance](https://www.ntppool.org/en/use.html). Enter the server hostname in the clock's NTP settings, not the website address.

## Timezones

In **Settings > Time settings**, select a location, then choose:

- **Automatic:** apply that location's seasonal daylight-saving rules.
- **Disabled:** keep its standard-time offset throughout the year.
- **Custom rule:** use an explicitly entered POSIX rule (maximum 63 characters).

Use **Save and reboot** to apply time settings. Fixed-offset locations do not
change seasonally. The menu displays standard offsets; the active summer offset
may differ. These 40 presets reflect IANA 2026.3 rules and require a firmware
update if governments change their timezone laws.

Need a custom rule? Use the [Techlogics POSIX timezone generator](https://techlogics.net/electronics/timezone-db.php), also linked in our CH899 guide. Select your city or timezone and choose **Copy POSIX string**. In the clock's time settings, select **Custom rule**, paste the string into the custom-rule field, then choose **Save and reboot**.

Copy the POSIX string, not an IANA name such as `Europe/London` or a generated code snippet. Check the generated DST dates against your region's current rules; the generator describes its output as indicative. Rules must fit the clock's 63-character limit.

Upgrades retain recognised regional choices and valid custom rules. Older fixed
UTC selections remain fixed and appear as **saved fixed offset**; select a
regional preset if you want automatic DST. The firmware cannot infer a city
from a shared offset. Invalid stored values are recovered with a warning in
Settings. An invalid custom rule is cleared and falls back to the selected
location's Automatic mode; an unknown location retains the old legacy-offset
fallback. Please check the selection if the recovery warning appears.

When startup NTP is
unavailable and a verified saved correction plus plausible RTC calendar are
available, the software clock enters compensated RTC holdover and retries NTP.
The first accepted reply re-anchors time and updates the RTC. An RTC seed is
not treated as a drift measurement. Without a verified saved correction, the
clock continues using direct RTC fallback. The RTC stores local time and cannot
independently correct a missed daylight-saving transition while NTP is absent.

## Display and maintenance

Display controls are on the Settings page and can be saved together without restarting. Time settings use **Save and reboot**. The page labels which changes require a reboot.

Restart modes let you choose no scheduled restart, a daily restart or recovery for a large accepted NTP error. An active stopwatch postpones the scheduled restart.

Factory reset clears saved settings, Wi-Fi and calibration. Resetting drift calibration clears its measurements and restarts learning; it is separate from a factory reset.

## Multi-function button

The firmware repurposes the existing GPIO0 button for everyday controls; no extra button or wiring is needed. The hardware reset button remains separate.

- **Clock display:** short press to scroll the date; hold for one second, then release, to enter the stopwatch.
- **Stopwatch:** short press to start, stop or resume timing.
- **Stopped stopwatch:** hold for one second to clear a non-zero result. With the result at zero, hold for one second again to return to the clock.
- **Scrolling API message:** press to cancel the remaining passes after the current pass. A second press within one second returns immediately to the clock.
- **Factory reset:** hold continuously for at least 12.5 seconds, then release. A flashing **RESET** warning begins after eight seconds; releasing before 12.5 seconds cancels the reset. A completed reset erases settings, Wi-Fi credentials and calibration.

## Local access

Optional web credentials protect the local pages and API. Leaving both fields blank disables web login. The connection remains HTTP: use a trusted LAN or VPN, not internet port forwarding.

For button controls and stopwatch operation, see the [user manual](../MatrixClock_Improved_v3_1_2_User_Manual.pdf).
