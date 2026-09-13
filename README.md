# MatrixClock Improved Firmware v3.1.2

An enhanced public-release firmware for the original HACK LABS MatrixClock
hardware, maintained and extended by Steve Madden. This release is derived
from the HACK LABS MatrixClock v2.2 package and is for the ESP8266-based 4 MB
MatrixClock board only.

The original HACK LABS attribution is retained in the source. This modified
build is licensed under GNU GPL v3; see [LICENSE](LICENSE).

Original project source: [HACK Labs MatrixClock](https://github.com/hack-apollo/hack_clock).

## Release contents

| File | Purpose |
| --- | --- |
| `MatrixClock_Improved_v3_1_2.ino` | Complete corresponding source code. |
| `MatrixClock_Improved_v3_1_2_OTA.bin` | Single firmware file for web OTA updates and clean USB installation. |
| `MatrixClock_Improved_v3_1_2_User_Manual.pdf` | Illustrated installation, setup and operating guide. |
| `screenshots/` | Public-safe examples of the local web interface. |
| `SHA256SUMS.txt` | SHA-256 integrity check for the distributed v3.1.2 OTA file. |

v3.1.2 uses one firmware binary for both supported installation methods. Use
`MatrixClock_Improved_v3_1_2_OTA.bin` on the web update page, or write the same
file at address `0x000000` with esptool after erasing the ESP8266 for a clean
USB installation. A separate full-flash or Factory image is not required.

## Web interface screenshots

These screenshots were captured from a running v3.1.0 clock. Their interface
layout remains representative of v3.1.2. Network-specific values have been
blurred for privacy; calibration figures and selected settings are examples
from that installation and will differ on another clock. No passwords or web
authentication credentials are shown.

### Main page: live status, calibration and API messaging

![MatrixClock v3.1.0 main page](screenshots/main-page-v3-1-0.png)

### Settings page

![MatrixClock v3.1.0 settings page](screenshots/settings-page-v3-1-0.png)

### Firmware update page

![MatrixClock v3.1.0 firmware update page](screenshots/firmware-update-v3-1-0.png)

## Important compatibility notes

- Use this firmware only on the matching ESP8266 MatrixClock hardware with
  4 MB flash.
- The OTA file is the tested upgrade path from the original HACK LABS firmware
  on compatible hardware.
- For a clean USB installation, erase the ESP8266 and write the same v3.1.2
  OTA binary at address `0x000000` using esptool.
- The firmware binary must not be written to a different ESP8266 product.
- Web authentication and the API are designed for a trusted local network.
  They use HTTP, not HTTPS; do not expose the clock directly to the internet
  or forward its web port. Use a VPN for remote access instead.

## First-time setup

After a clean install, the clock starts its open setup access point:

```text
Wi-Fi name: MatrixClock
Setup address: http://192.168.4.1/
```

Connect a phone or computer to that Wi-Fi network, open the setup address,
choose the home Wi-Fi network, enter its password, and save.
Once the clock reconnects to the home network, open the IP address shown on its
boot-up display. To show the address again, press the hardware reset button to
restart the device.

The normal web interface includes all clock, timezone, NTP, chronograph,
display, API, authentication, calibration, and firmware-update settings.

## Timezones

In **Settings > Time settings**, select a location, then choose:

- **Automatic:** apply that location's seasonal daylight-saving rules.
- **Disabled:** keep its standard-time offset throughout the year.
- **Custom rule:** use an explicitly entered POSIX rule (maximum 63 characters).

Use **Save and reboot** to apply time settings. Fixed-offset locations do not
change seasonally. The menu displays standard offsets; the active summer offset
may differ. These 40 presets reflect IANA 2026.3 rules and require a firmware
update if governments change their timezone laws.

Upgrades retain recognised regional choices and valid custom rules. Older fixed
UTC selections remain fixed and appear as **saved fixed offset**; select a
regional preset if you want automatic DST. The firmware cannot infer a city
from a shared offset. Invalid stored values are recovered with a warning in
Settings. An invalid custom rule is cleared and falls back to the selected
location's Automatic mode; an unknown location retains the old legacy-offset
fallback. Please check the selection if the recovery warning appears.

No EEPROM layout or chronograph changes are required. When startup NTP is
unavailable and a verified saved correction plus plausible RTC calendar are
available, the software clock enters compensated RTC holdover and retries NTP.
The first accepted reply re-anchors time and updates the RTC. An RTC seed is
not treated as a drift measurement. Without a verified saved correction, the
clock continues using direct RTC fallback. The RTC stores local time and cannot
independently correct a missed daylight-saving transition while NTP is absent.

Developers can run the read-only desktop checks with Python 3.9+ and the
`python-dateutil` and `tzdata` packages:

```powershell
python -B tools/verify_timezone_presets.py
```

These checks exercise the source preset data and a desktop rule model; they
do not replace testing on the ESP8266 or predict future legislative changes.

## Installing the OTA update

`MatrixClock_Improved_v3_1_2_OTA.bin` is the single distributed firmware file.
It can be installed through the web updater on a compatible clock, or written
at address `0x000000` after a complete ESP8266 erase for a clean USB install.
The original v3.0.0 OTA update was tested from the original HACK LABS firmware
on compatible 4 MB ESP8266 MatrixClock hardware, providing a direct web-based
upgrade path from the original firmware.

### Upgrading directly from the original HACK LABS firmware

1. Find the clock's current IP address on the local network.
2. Open `http://<device-ip>/update` in a browser. For example:
   `http://192.168.0.10/update`.
3. Sign in to the original firmware's update page with:
   - Username: `nick`
   - Password: `nick`
4. Select `MatrixClock_Improved_v3_1_2_OTA.bin`.
5. Start the update and wait for the clock to restart. Do not remove power or
   reset the clock while the firmware is being written.
6. Confirm that MatrixClock Improved has started. Then open **Settings** and
   select **Factory reset**. This is recommended after upgrading from the
   original firmware so old settings cannot remain in use.
7. After the factory reset, connect to the open `MatrixClock` setup network,
   open `http://192.168.4.1`, and select the home Wi-Fi network again.
8. Open the IP address shown during boot. On the first normal-page visit,
   choose a username and password or leave both fields blank for no web login.

### Updating an existing MatrixClock Improved installation

1. Find the clock's current IP address.
2. Open `http://<device-ip>/update` in a browser.
3. Sign in with the clock's current username and password if web security is
   enabled.
4. Select `MatrixClock_Improved_v3_1_2_OTA.bin`.
5. Select **Upload and reboot** and wait for the clock to restart. Do not remove
   power during the update.

A newly reset MatrixClock Improved installation asks you to choose credentials
on its first normal-page visit. Leave both fields blank to keep local web access
open.

## Clean installation over USB

Use this method when the clock cannot be reached through its web interface, or
when a completely clean installation is wanted. It erases all flash contents,
including the existing firmware, Wi-Fi data, settings and calibration records,
then installs v3.1.2 directly from the normal OTA binary.

1. Install or download [Espressif esptool](https://github.com/espressif/esptool/releases).
   Its official [ESP8266 command documentation](https://docs.espressif.com/projects/esptool/en/latest/esp8266/esptool/basic-options.html)
   explains drivers and serial-port selection.
2. Connect the MatrixClock by USB and close Arduino Serial Monitor or any
   other program using the COM port.
3. Replace `COM3` below with the clock's Windows COM port:

```cmd
esptool --chip esp8266 --port COM3 --baud 115200 erase_flash
```

4. When the erase completes successfully, write the v3.1.2 firmware at address
   `0x000000`:

```cmd
esptool --chip esp8266 --port COM3 --baud 115200 write_flash 0x000000 MatrixClock_Improved_v3_1_2_OTA.bin
```

5. Connect to the `MatrixClock` setup Wi-Fi network and open
   `http://192.168.4.1/` to connect the clock to the home network.
6. Open the IP address shown during the next boot. On the first normal-page
   visit, choose new web credentials or leave both fields blank for no web
   login.

## Building from source with Arduino IDE

This release was built using Arduino IDE with the ESP8266 board package 3.1.2.

Use these build settings:

```text
Board:          NodeMCU 1.0 (ESP-12E Module)
CPU frequency:  80 MHz
Flash size:     4 MB
Flash mode:     DIO
Upload speed:   115200
```

1. Install the ESP8266 boards package if it is not already present.
2. Open `MatrixClock_Improved_v3_1_2.ino` from its matching folder.
3. Select **Tools > Board > ESP8266 Boards > NodeMCU 1.0 (ESP-12E Module)**.
4. Select the clock's serial port.
5. Ensure the board is configured for 4 MB flash, then use **Verify** or
   **Upload**.

The libraries used by this sketch (`SPI`, `Ticker`, `ESP8266WiFi`,
`ESP8266WebServer`, `EEPROM`, `WiFiUdp`, `Wire`, and `time`) are provided by
the ESP8266 board package; no separate library downloads are required.

## API quick start

The web page shows the current API address. In the examples below,
`CLOCK-IP` means the clock's current LAN IP address, shown on the clock during
startup and on its web page. A DHCP lease can change this address after a
reboot. For reliable Home Assistant use, reserve a fixed address for the clock
in the router (DHCP reservation/static lease), then use that address in the
API configuration. To send a message from a system on the same network, make a
form-encoded HTTP POST request:

```text
POST http://CLOCK-IP/api/message
Content-Type: application/x-www-form-urlencoded

message=MatrixClock v3.1.2&scrolls=2
```

If web security is enabled, include the MatrixClock username and password.
PowerShell can prompt for them without putting the password in your command
history:

```powershell
$credential = Get-Credential
Invoke-WebRequest `
  -Uri "http://CLOCK-IP/api/message" `
  -Method POST `
  -Credential $credential `
  -ContentType "application/x-www-form-urlencoded" `
  -Body "message=MatrixClock v3.1.2&scrolls=2"
```

For Home Assistant, store the credentials in `secrets.yaml` and use a REST
command:

```yaml
rest_command:
  matrixclock_message:
    url: "http://CLOCK-IP/api/message"
    method: POST
    username: !secret matrixclock_username
    password: !secret matrixclock_password
    content_type: "application/x-www-form-urlencoded"
    payload: "message={{ message }}&scrolls={{ scrolls | default(1) }}"
```

Call it with a service action such as:

```yaml
action: rest_command.matrixclock_message
data:
  message: "Bin day tomorrow"
  scrolls: 2
```

Leave the MatrixClock username and password blank to disable web security; in
that case the API does not require credentials. Messages are local-network
only, use HTTP rather than HTTPS, and are unavailable while the chronograph
is open. They can be cancelled from the clock's web page or physical button.

The DS3231 RTC temperature shown in Device info is also available as a
lightweight numeric API response:

```text
GET http://CLOCK-IP/api/temperature
```

```json
{"temperature_c":22.25}
```

Home Assistant can poll it once per minute without opening the clock's main
page or its live-display connection:

```yaml
rest:
  - resource: "http://CLOCK-IP/api/temperature"
    authentication: basic
    username: !secret matrixclock_username
    password: !secret matrixclock_password
    scan_interval: 60
    sensor:
      - name: MatrixClock RTC temperature
        unique_id: matrixclock_rtc_temperature
        value_template: "{{ value_json.temperature_c }}"
        device_class: temperature
        state_class: measurement
        unit_of_measurement: "°C"
```

Omit `authentication`, `username`, and `password` when MatrixClock web
security is disabled. This is the RTC's internal temperature and should not be
treated as a calibrated room-temperature measurement.

## Verify downloaded files

On Windows, run this from the release folder:

```cmd
certutil -hashfile MatrixClock_Improved_v3_1_2_OTA.bin SHA256
```

Compare the result with the v3.1.2 `SHA256SUMS.txt`. The same verified OTA file
is used for web updates and clean USB installations. The corresponding source
is maintained in this repository.

## Licence and attribution

The original HACK LABS MatrixClock notices remain in the source header. The
complete modified source and any distributed firmware binaries are released
under GNU GPL version 3. See `LICENSE` and `CHANGELOG.md`.

This project is provided without warranty. Flashing firmware is undertaken at
your own risk.
