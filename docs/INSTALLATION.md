# Install MatrixClock Improved

[Back to the project](../README.md)

For the matching **4 MB ESP8266 HACK LABS MatrixClock** only. No compiling is needed.

**Update through your browser using the steps below.** If the clock cannot be reached, use the separate [USB installation guide](USB_INSTALLATION.md).

## 1. Download

From the [latest release](https://github.com/maddenste/MatrixClock-Improved/releases/latest), download **`MatrixClock_Improved_v3_1_2_OTA.bin`** and **`SHA256SUMS.txt`** under **Assets**, not the Source code ZIP.

<details>
<summary>Check the download (SHA-256)</summary>

On Windows 11, open a terminal in the download folder and run:

```cmd
certutil -hashfile MatrixClock_Improved_v3_1_2_OTA.bin SHA256
```

Compare the result with `SHA256SUMS.txt`. Do not install the file if they differ.

</details>

## 2. Upload

**Keep power connected. Do not reset the clock during the upload.**

1. On the same Wi-Fi/LAN as the clock, open `http://CLOCK-IP/update`, replacing `CLOCK-IP` with its address—for example, `http://192.168.0.10/update`. Improved displays its IP address during startup; for stock firmware, check your router's connected-device list.
2. Sign in if requested. Stock HACK LABS firmware uses username **`nick`** and password **`nick`**; an existing Improved installation uses your saved credentials.
3. Select the downloaded `.bin` and start the upload (**Upload and reboot** on Improved).
4. Wait for the clock to restart.

**Upgrading from stock firmware?** Once Improved has started, open **Settings > Factory reset** to clear obsolete settings. This also clears Wi-Fi credentials and calibration; then follow step 3 below.

**Already running Improved?** No factory reset is normally needed. Your saved configuration remains in use.

## 3. Set up after a reset or clean installation

1. Connect your phone or computer to the clock's **`MatrixClock`** Wi-Fi network.
2. Open **[192.168.4.1](http://192.168.4.1/)**, select your home Wi-Fi and enter its password.
3. Rejoin your home network and open the IP address shown on the clock during startup.
4. Choose web login credentials, or leave both fields blank for no login. In **Settings**, select your timezone and NTP server.

Keep the clock on a trusted local network; do not expose its HTTP web interface directly to the internet.

## Need help?

- **Cannot find the clock:** check your router's connected-device list. On Improved firmware, press the hardware reset button to show the startup IP address again.
- **Cannot open the page:** check that your device is on the same LAN. If necessary, use [USB installation](USB_INSTALLATION.md), which erases saved settings.
- **No NTP time:** check Wi-Fi and NTP settings. RTC backup time does not mean an NTP sync succeeded.

[User manual](../MatrixClock_Improved_v3_1_2_User_Manual.pdf) · [Settings and button controls](SETTINGS.md) · [Report a problem](https://github.com/maddenste/MatrixClock-Improved/issues)
