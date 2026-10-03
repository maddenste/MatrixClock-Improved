# Home Assistant and HTTP API

[Back to the project](../README.md)

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

message=MatrixClock%20v3.1.2&scrolls=2
```

If web security is enabled, include the MatrixClock username and password.
This example works with Windows PowerShell 5.1 and PowerShell 7. It prompts
for credentials and encodes the message automatically. Replace `CLOCK-IP`
with your clock's address:

```powershell
$credential = Get-Credential
$request = @{
  Uri = "http://CLOCK-IP/api/message"
  Method = "POST"
  Credential = $credential
  UseBasicParsing = $true
  ContentType = "application/x-www-form-urlencoded"
  Body = @{ message = "MatrixClock v3.1.2"; scrolls = 2 }
}
if ($PSVersionTable.PSVersion.Major -ge 6) {
  $request.Authentication = "Basic"
  $request.AllowUnencryptedAuthentication = $true
}
Invoke-WebRequest @request
```

PowerShell 7 requires explicit permission to send credentials over HTTP.
This is not encryption: use this example only on a trusted LAN. If clock
security is disabled, omit the credential prompt and the `Credential` entry,
and omit the entire `if` block.

For Home Assistant, store the credentials in `secrets.yaml` and add this REST
command to `configuration.yaml`. If you already have a `rest_command:` section,
add `matrixclock_message` beneath it rather than creating a second section:

```yaml
rest_command:
  matrixclock_message:
    url: "http://CLOCK-IP/api/message"
    method: POST
    authentication: basic
    username: !secret matrixclock_username
    password: !secret matrixclock_password
    content_type: "application/x-www-form-urlencoded"
    payload: "message={{ message | urlencode }}&scrolls={{ scrolls | default(1) | int }}"
```

Encoding preserves characters such as `&` and `+` rather than interpreting
them as form separators. Use a scroll count from 1 to 5. Add these entries to
`secrets.yaml`, replacing the example values:

```yaml
matrixclock_username: "your-clock-username"
matrixclock_password: "your-clock-password"
```

Check the Home Assistant configuration and restart Home Assistant after adding
the command. Then call it from **Developer tools > Actions**, a script or an
automation:

```yaml
action: rest_command.matrixclock_message
data:
  message: "Bin day tomorrow"
  scrolls: 2
```

Leave the MatrixClock username and password blank to disable web security; in
that case the API does not require credentials; omit `authentication`,
`username` and `password` from the REST command. Messages are local-network
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
