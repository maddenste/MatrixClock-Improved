# USB installation and recovery

[Back to installation](INSTALLATION.md)

For the matching **4 MB ESP8266 HACK LABS MatrixClock**, when a browser update is unavailable or a clean installation is wanted.

**This erases the firmware, Wi-Fi credentials, settings and calibration.** Keep power connected throughout. Use the normal OTA `.bin`; no separate Factory image is needed.

## Before starting

- Download the `.bin` and checksum from [installation step 1](INSTALLATION.md#1-download) and check the download.
- Install [Espressif esptool](https://github.com/espressif/esptool/releases), following its [official installation instructions](https://docs.espressif.com/projects/esptool/en/latest/esp8266/installation.html).
- Connect the clock by USB. Close Serial Monitor and other programs using its port.

## Erase and upload

These examples use Windows 11. Other systems use a different serial-port name; see [esptool serial-port options](https://docs.espressif.com/projects/esptool/en/latest/esp8266/esptool/basic-options.html).

1. Open a terminal in the folder containing the downloaded `.bin`.
2. Replace `COM3` with your clock's port, shown under **Device Manager > Ports (COM & LPT)**.
3. Erase the flash:

```cmd
esptool --chip esp8266 --port COM3 --baud 115200 erase-flash
```

4. Only after the erase succeeds, write the firmware at address `0x000000`:

```cmd
esptool --chip esp8266 --port COM3 --baud 115200 write-flash 0x000000 MatrixClock_Improved_v3_1_2_OTA.bin
```

5. Wait for a successful write. If the clock does not restart automatically, restart it after esptool has finished. Then follow [Wi-Fi setup](INSTALLATION.md#3-set-up-after-a-reset-or-clean-installation).

## Command or connection problems

- **Downloaded Windows executable:** put `esptool.exe` beside the `.bin` and use `.\esptool.exe` instead of `esptool`.
- **Installed using Python:** if the command is not found, use `python -m esptool` instead.
- **Older esptool:** use `erase_flash` and `write_flash` instead of the hyphenated command names.
- **Port busy:** close any other program connected to the clock's COM port.
- **Cannot connect:** check the USB cable and selected port, then consult [esptool troubleshooting](https://docs.espressif.com/projects/esptool/en/latest/esp8266/troubleshooting.html).

If you [report a problem](https://github.com/maddenste/MatrixClock-Improved/issues), include the exact error, esptool version and your operating system. Do not include passwords.
