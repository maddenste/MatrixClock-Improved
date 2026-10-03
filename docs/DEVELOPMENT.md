# Build and developer checks

[Back to the project](../README.md)

You do not need to build anything to install the [released firmware](INSTALLATION.md). This page is for modifying the source.

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
2. Create a folder named `MatrixClock_Improved_v3_1_2` and copy the repository's
   `MatrixClock_Improved_v3_1_2.ino` into it. Arduino requires the folder and
   sketch to share the same name. Keep the repository copy unchanged.
3. Open that copied `.ino` in Arduino IDE.
4. Select **Tools > Board > ESP8266 Boards > NodeMCU 1.0 (ESP-12E Module)**.
5. Select the clock's serial port.
6. Ensure the board is configured for 4 MB flash, then use **Verify** or
   **Upload**.

The libraries used by this sketch (`SPI`, `Ticker`, `ESP8266WiFi`,
`ESP8266WebServer`, `EEPROM`, `WiFiUdp`, `Wire`, and `time`) are provided by
the ESP8266 board package; no separate library downloads are required.

## Timezone verification

Developers can run the read-only desktop checks with Python 3.9+ and the
`python-dateutil` and `tzdata` packages:

```powershell
python -B tools/verify_timezone_presets.py
```

These checks exercise the source preset data and a desktop rule model; they
do not replace testing on the ESP8266 or predict future legislative changes.

## Release files

The complete corresponding source is [MatrixClock_Improved_v3_1_2.ino](../MatrixClock_Improved_v3_1_2.ino). Compiled firmware and checksums are distributed through [GitHub Releases](https://github.com/maddenste/MatrixClock-Improved/releases/latest).

The [changelog](../CHANGELOG.md) retains the detailed development history. Preserve the original HACK LABS notices when modifying or distributing this GPL v3 work.
