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
