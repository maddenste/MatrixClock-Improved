# Install MatrixClock Improved

[Back to the project](../README.md)

For the matching **4 MB ESP8266 HACK LABS MatrixClock** only. No compiling is required.

## 1. Download the firmware

Open the [latest release](https://github.com/maddenste/MatrixClock-Improved/releases/latest) and download:

- **`MatrixClock_Improved_v3_1_2_OTA.bin`** — the firmware.
- **`SHA256SUMS.txt`** — its integrity checksum.

Use the release **Assets**, not the Source code ZIP. The ZIP contains the source, not the ready-to-upload binary. One binary covers both installation methods below; no separate Factory image is needed.

### Verify downloaded files

On Windows 11, run this from the release folder:

```cmd
certutil -hashfile MatrixClock_Improved_v3_1_2_OTA.bin SHA256
```

Compare the result with the v3.1.2 `SHA256SUMS.txt`. The same verified OTA file
is used for web updates and clean USB installations. The corresponding source
is maintained in this repository.

## 2. Choose how to install

**Clock reachable in your browser?** Use the web update below. This is the simplest route.

**Clock unreachable, or starting from a clean device?** Use [USB installation](#clean-installation-over-usb). It erases all settings and calibration.

### Web update

Keep power connected throughout the update.

#### Upgrading directly from the original HACK LABS firmware

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

#### Updating an existing MatrixClock Improved installation

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

### Clean installation over USB

Use this method when the clock cannot be reached through its web interface, or
when a completely clean installation is wanted. It erases all flash contents,
including the existing firmware, Wi-Fi data, settings and calibration records,
then installs v3.1.2 directly from the normal OTA binary.

1. Install or download [Espressif esptool](https://github.com/espressif/esptool/releases).
   Follow the official [installation instructions](https://docs.espressif.com/projects/esptool/en/latest/esp8266/installation.html) and [serial-port options](https://docs.espressif.com/projects/esptool/en/latest/esp8266/esptool/basic-options.html).
2. Connect the MatrixClock by USB and close Arduino Serial Monitor or any
   other program using the COM port.
3. Open a terminal in the folder containing the downloaded `.bin`. Replace `COM3` below with the clock's Windows 11 COM port:

```cmd
esptool --chip esp8266 --port COM3 --baud 115200 erase-flash
```

4. When the erase completes successfully, write the v3.1.2 firmware at address
   `0x000000`:

```cmd
esptool --chip esp8266 --port COM3 --baud 115200 write-flash 0x000000 MatrixClock_Improved_v3_1_2_OTA.bin
```

5. Connect to the `MatrixClock` setup Wi-Fi network and open
   `http://192.168.4.1/` to connect the clock to the home network.
6. Open the IP address shown during the next boot. On the first normal-page
   visit, choose new web credentials or leave both fields blank for no web
   login.

These commands use current esptool command names. Older versions use `erase_flash` and `write_flash`. If installed with Python but the command is not found, use `python -m esptool` instead. For a downloaded Windows executable, put `esptool.exe` beside the `.bin` and use `.\esptool.exe` instead of `esptool`.

## 3. Set up your clock

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

## If something goes wrong

- **No web page:** use the IP address shown during boot and make sure your phone or computer is on the same LAN.
- **No home Wi-Fi saved:** join `MatrixClock` and open `http://192.168.4.1/`.
- **No network time:** check the Wi-Fi connection and NTP settings. RTC fallback is not a successful NTP sync.
- **USB port busy:** close Serial Monitor and any other program using that port.
- **Still stuck:** [report the problem](https://github.com/maddenste/MatrixClock-Improved/issues) with your firmware version, installation method and exact error. Do not include Wi-Fi passwords.

See the [user manual](../MatrixClock_Improved_v3_1_2_User_Manual.pdf) for everyday operation.
