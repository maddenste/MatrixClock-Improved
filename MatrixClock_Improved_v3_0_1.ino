/*******************************************************************************
HACK LABS MatrixClock

-------------------------------------------------------------------------------
Original project attribution

Author: HACK实验室
YouTube ID: HACK实验室, welcome to subscribe:
https://www.youtube.com/channel/UCxFY1FcIYK9d7riTvIh6eiA
Version 2.0

HACK_CLOCK is a free download and may be used, modified, evaluated and
distributed without charge provided the user adheres to version three of the GNU
General Public License (GPL) and does not remove the copyright notice or this
text. The GPL v3 text is available at:
https://www.gnu.org/licenses/gpl-3.0.html

作者：HACK实验室
B站ID：HACK实验室  欢迎订阅：
https://space.bilibili.com/395145107
微信公众号：HACK实验室，开源资料唯一发布点，定期分享开源硬件以及有价值的技术文章，欢迎关注.
Version 2.0
HACK_CLOCK是一个完全开源的硬件项目，允许用户免费下载使用，但需遵守GPL V3开源协议，
协议文本可在gnu.org网站上获得.

-------------------------------------------------------------------------------
MODIFIED BUILD INFORMATION

MatrixClock Improved Firmware v3.0.1

This firmware is a modified and extended build of the original HACK LABS
MatrixClock project attributed above.

Modified, integrated, and maintained by:
Steve Madden
steve@maddenuk.net

Modified build date: 2026-09-02

Key changes include:

- Improved display rendering, orientation handling, transitions, brightness,
  startup status, separators, date scrolling and animations
- Physical button controls for date display, chronograph control, message
  cancellation, and factory reset
- Precision and normal chronograph display modes, timing improvements,
  stopwatch history and elapsed-hour indicators
- NTP synchronisation, RTC fallback, timezone and daylight-saving support,
  configurable NTP providers and oscillator-drift calibration
- Persistent settings, product/schema migration safeguards, factory reset,
  clean-install handling, and configurable daily restart
- Wi-Fi access-point provisioning and an improved local web interface
- Optional local web authentication, protected OTA firmware updates, and
  Home Assistant / HTTP API scrolling-message integration
- Improved device status, NTP diagnostics, calibration reporting,
  accessibility, reliability, memory use, and runtime efficiency

The original HACK LABS attribution and notices above are retained.

This modified work is distributed under the GNU General Public License,
version 3.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License version 3 for
more details.
*******************************************************************************/

// Public release version. Update this single value for every release and keep
// the GitHub tag and OTA filename aligned with it.
#define MATRIXCLOCK_FIRMWARE_VERSION "v3.0.1"

#include <SPI.h>
#include <Ticker.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Updater.h>
#include <EEPROM.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <time.h>

// Public-release build: keep the UART quiet.  Set to 1 for engineering
// diagnostics during development; production firmware emits no Serial data.
#define MATRIXCLOCK_SERIAL_OUTPUT 0
#if !MATRIXCLOCK_SERIAL_OUTPUT
class MatrixClockSilentSerial {
public:
    void begin(unsigned long) {}
    template <typename T> void print(const T&) {}
    template <typename T> void println(const T&) {}
    template <typename... Args> void printf(const char*, Args...) {}
};
MatrixClockSilentSerial MatrixClockSerial;
#define Serial MatrixClockSerial
#endif

#define SDA        5      // Pin sda (I2C)
#define SCL        4      // Pin scl (I2C)
#define CS         15     // Pin cs  (SPI)
#define anzMAX     4        //number of matrix modules
//#define REVERSE_HORIZONTAL
//#define REVERSE_VERTICAL
char ssid[64] = "";                  // provisioned Wi-Fi SSID
char pass[64] = "";                  // provisioned Wi-Fi password

unsigned short maxPosX = anzMAX * 8 - 1;            
const uint8_t CHRONOGRAPH_CLOCK_GAP_COLUMNS = 8;    // Visible blank columns.
// The date's first visible glyph is drawn at d_PosX - 1, so reproducing its
// eight-column visual gap requires one additional anchor step.
const uint8_t CHRONOGRAPH_CLOCK_LEAD_COLUMNS =
    CHRONOGRAPH_CLOCK_GAP_COLUMNS + 1;
unsigned short LEDarr[anzMAX][8];                   
unsigned short helpArrMAX[anzMAX * 8];              
unsigned short helpArrPos[anzMAX * 8];              
unsigned int z_PosX = 0;                            
unsigned int d_PosX = 0;                            
volatile bool f_tckr1s = false;
// The display polls the calibrated software clock every 50 ms, rather than
// trusting an uncorrected one-second ticker to decide when a new second starts.
// This keeps the visible clock aligned after a ppm correction is applied.
volatile bool rtcSecondTickDue = false;
volatile bool displayTickDue = false;
bool kk;
bool mainColonVisible = true;
bool provisioningMode = false;
unsigned short currentHour24 = 0;
const char setupMessage[] = "MatrixClock 192.168.4.1";
const char firmwareBuild[] = "MatrixClock Improved v3.0.1";
int setupMessageX = -((int)(sizeof(setupMessage) - 1) * 6);
unsigned long epoch = 0;
unsigned int localPort = 2390;                      // local port to listen for UDP packets
const char* ntpServerName = "time.cloudflare.com";
const int NTP_PACKET_SIZE = 48;                     // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[NTP_PACKET_SIZE];                 // buffer to hold incoming and outgoing packets
IPAddress timeServerIP;                            
unsigned long ntpEstimatedEpoch = 0;
// NTP phase is retained at microsecond resolution.  The old millisecond-only
// estimate could cross a second boundary while the packet was being parsed.
uint32_t ntpEstimatedFractionUs = 0;
uint32_t ntpReferenceMicros = 0;
uint32_t lastNtpRoundTripUs = 0;
uint8_t lastNtpStratum = 0;
uint8_t lastNtpLeapIndicator = 3;
IPAddress lastNtpServerAddress;
uint32_t lastNtpSuccessEpoch = 0;
bool lastNtpMetadataValid = false;
// Drift calibration is stored separately from ClockSettings so future firmware
// can retain the existing settings layout.
// Once NTP has established an exact boundary, keep the display on the
// ESP8266's monotonic microsecond clock.  The DS3231 remains a battery-backed
// backup clock, but is no longer sampled to decide when a displayed second
// changes.
unsigned long softwareEpochBase = 0;
uint32_t softwareClockLastMicros = 0;
uint64_t softwareClockElapsedUs = 0;
bool softwareClockValid = false;
enum NtpCheckResult : uint8_t {
    NTP_CHECK_AWAITING,
    NTP_CHECK_SUCCESS,
    NTP_CHECK_NO_STABLE_REPLY,
    NTP_CHECK_INCONSISTENT_REPLIES,
    NTP_CHECK_NO_VALID_REPLY
};
NtpCheckResult lastNtpCheckResult = NTP_CHECK_AWAITING;
uint32_t lastNtpCheckRttUs = 0;
String chronographHistory[3] = {"00:00:00:00", "00:00:00:00", "00:00:00:00"};
const uint32_t CHRONOGRAPH_MAX_MS = 89999990UL; // 24:59:59.99
const uint32_t CHRONOGRAPH_EXTENDED_MAX_MS = 359999990UL; // 99:59:59.99
String apiMessage;
bool apiMessagePending = false;
bool apiMessageActive = false;
int apiMessageX = 0;
uint8_t apiMessagePass = 0;
// Per-message override supplied by the HTTP API. A value of zero preserves
// the existing continuous-playback behaviour; messages without an override
// inherit the saved web-interface setting.
uint8_t apiMessageScrollCount = 1;
// One non-interactive startup marquee confirms the address of a successfully
// connected clock before the normal time display appears.
bool startupIpMarqueeActive = false;
int startupIpMarqueeX = 0;
String startupIpMarqueeMessage;
bool ntpFailureMarqueeActive = false;
int ntpFailureMarqueeX = 0;
String ntpFailureMarqueeMessage;
bool apiClockExitMarquee = false;
bool ntpClockExitMarquee = false;
const uint8_t API_MESSAGE_QUEUE_SIZE = 1;
String apiMessageQueue[API_MESSAGE_QUEUE_SIZE];
uint8_t apiMessageQueueHead = 0;
uint8_t apiMessageQueueTail = 0;
uint8_t apiMessageQueueCount = 0;
bool apiBlockedByChronograph = false;
bool apiCancelAfterCurrent = false;
bool apiGracefulReturn = false;
uint32_t apiCancelDeadline = 0;
struct NtpMeasurement;
void rtc_resetToBaseline();
String escapeHtml(const String& value);
String escapeJson(const String& value);
void sendDeviceInfoMarkup();

void onDisplayTick() { displayTickDue = true; }

const unsigned char DS3231_ADDRESS = 0x68;
const unsigned char secondREG = 0x00;
const unsigned char minuteREG = 0x01;
const unsigned char hourREG = 0x02;
const unsigned char WTREG = 0x03;                   //weekday
const unsigned char dateREG = 0x04;
const unsigned char monthREG = 0x05;
const unsigned char yearREG = 0x06;
const unsigned char controlREG = 0x0E;

struct DateTime {
    unsigned short sek1, sek2, sek12, min1, min2, min12, std1, std2, std12;
    unsigned short tag1, tag2, tag12, mon1, mon2, mon12, jahr1, jahr2, jahr12, WT;//day, month, year (German variable names)
} MEZ;


// The object for the Ticker
Ticker tckr;
Ticker scrollTckr;
// A UDP instance to let us send and receive packets over UDP
WiFiUDP udp;
ESP8266WebServer web(80);

struct ClockSettings {
    uint32_t magic;
    char ssid[64];
    char password[64];
    char ntpServer[64];
    char timezone[32];
    char customTimezone[64];
    int utcOffset;
    uint8_t brightness;
    uint8_t timeFormat;
    uint8_t scrolling;
    uint8_t scrollSpeed;
    uint8_t cleanTransitions;
    uint8_t daylightSaving;
    uint8_t clockColonBlink;
    uint8_t restartHour;
    uint8_t restartMinute;
    uint8_t apiScrollCount;
    uint8_t chronographDisplayMode;
};

ClockSettings settings;

// Keep calibration outside ClockSettings so the long-established user-settings
// layout remains binary-compatible with earlier MatrixClock builds.
const uint32_t DRIFT_CALIBRATION_MAGIC = 0x4D434452; // "MCDR"
const uint16_t DRIFT_CALIBRATION_VERSION = 1;
const uint16_t DRIFT_CALIBRATION_OFFSET = sizeof(ClockSettings);
const uint32_t DRIFT_INITIAL_CHECK_DELAY_MS = 300000UL; // 5 minutes
const uint32_t DRIFT_CHECK_INTERVAL_MS = 7200000UL; // 2 hours
const uint32_t DRIFT_NTP_TIMEOUT_US = 300000UL;
const uint32_t DRIFT_MAX_RTT_US = 250000UL;
const int32_t DRIFT_MAX_OFFSET_US = 5000000L;
const int32_t DRIFT_MAX_SAMPLE_SPREAD_US = 150000L;
const int32_t DRIFT_MAX_RATE_PPM_MILLI = 100000L; // +/-100 ppm
const uint8_t DRIFT_SAMPLES_PER_CHECK = 5;
const uint8_t DRIFT_MIN_SAMPLES = 3;
const uint8_t DRIFT_MIN_RATE_ESTIMATES = 2;
// A stable NTP offset can be corrected while running without a visible time
// jump.  This is deliberately separate from the stored oscillator-rate
// calibration and applies only to the wall clock, never the chronograph.
const int32_t PHASE_SLEW_IGNORE_US = 10000L;       // Do not chase sub-10 ms noise.
const int32_t PHASE_SLEW_MAX_OFFSET_US = 500000L;  // Reject a suspect half-second step.
const int32_t PHASE_SLEW_RATE_PPM_MILLI = 200000L; // 200 ppm: smooth, bounded correction.

struct DriftCalibration {
    uint32_t magic;
    uint16_t version;
    int32_t ratePpmMilli;       // 1/1000 ppm; applied on the next boot
    uint32_t updatedEpoch;
    int32_t lastOffsetUs;
    uint8_t acceptedChecks;
    uint8_t reserved[3];
};

// In-progress calibration is checkpointed in ESP8266 RTC user memory.  This
// memory survives reset without causing flash wear.  The active EEPROM rate is
// deliberately kept separate and remains fixed for the whole running session.
const uint32_t DRIFT_SESSION_MAGIC = 0x4D435253; // "MCRS"
const uint16_t DRIFT_SESSION_VERSION = 2;
const uint32_t DRIFT_SESSION_RTC_OFFSET = 0;

struct DriftSessionCheckpoint {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t checksum;
    int32_t activeRateSnapshot;
    int32_t pendingRatePpmMilli;
    int32_t latestOffsetUs;
    uint32_t latestRoundTripUs;
    uint32_t latestEpoch;
    uint8_t acceptedChecks;
    uint8_t rateEstimateCount;
    uint8_t pendingValid;
    uint8_t reserved;
};

static_assert((sizeof(DriftSessionCheckpoint) % 4) == 0,
              "RTC calibration checkpoint must be 4-byte aligned");

// Explicit declarations prevent the Arduino sketch preprocessor from
// generating prototypes above the custom checkpoint type definition.
uint32_t driftSessionChecksum(const DriftSessionCheckpoint& source);
void clearDriftSessionCheckpoint();
void saveDriftSessionCheckpoint();
bool readDriftSessionCheckpoint(DriftSessionCheckpoint& record);

struct NtpMeasurement {
    uint32_t epoch;
    uint32_t fractionUs;
    uint32_t referenceMicros;
    uint32_t roundTripUs;
};

DriftCalibration driftCalibration;
int32_t activeDriftPpmMilli = 0;
int64_t softwareClockRateRemainder = 0;
int64_t softwareClockPhaseRemainingUs = 0;
int64_t softwareClockPhaseRemainder = 0;
uint32_t nextDriftCheckAt = 0;
bool driftReferenceValid = false;
uint64_t driftReferenceObservedUs = 0;
int64_t driftReferenceOffsetUs = 0;
uint8_t driftAcceptedChecks = 0;
bool driftLastCheckValid = false;
int32_t driftLastOffsetUs = 0;
uint32_t driftLastRoundTripUs = 0;
uint64_t driftLastObservedUs = 0;
bool driftCandidateValid = false;
int32_t driftCandidatePpmMilli = 0;
uint8_t driftRateEstimateCount = 0;

uint32_t driftSessionChecksum(const DriftSessionCheckpoint& source) {
    DriftSessionCheckpoint record = source;
    record.checksum = 0;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&record);
    uint32_t hash = 2166136261UL;
    for (size_t i = 0; i < sizeof(record); i++) {
        hash ^= bytes[i];
        hash *= 16777619UL;
    }
    return hash;
}

void clearDriftSessionCheckpoint() {
    DriftSessionCheckpoint blank = {};
    ESP.rtcUserMemoryWrite(DRIFT_SESSION_RTC_OFFSET,
                           reinterpret_cast<uint32_t*>(&blank), sizeof(blank));
}

void saveDriftSessionCheckpoint() {
    DriftSessionCheckpoint record = {};
    record.magic = DRIFT_SESSION_MAGIC;
    record.version = DRIFT_SESSION_VERSION;
    record.size = sizeof(record);
    record.activeRateSnapshot = activeDriftPpmMilli;
    record.pendingRatePpmMilli = driftCandidatePpmMilli;
    record.latestOffsetUs = driftLastOffsetUs;
    record.latestRoundTripUs = driftLastRoundTripUs;
    record.latestEpoch = (uint32_t)(driftLastObservedUs / 1000000ULL);
    record.acceptedChecks = driftAcceptedChecks;
    record.rateEstimateCount = driftRateEstimateCount;
    record.pendingValid = driftCandidateValid ? 1 : 0;
    record.checksum = driftSessionChecksum(record);
    ESP.rtcUserMemoryWrite(DRIFT_SESSION_RTC_OFFSET,
                           reinterpret_cast<uint32_t*>(&record), sizeof(record));
}

bool readDriftSessionCheckpoint(DriftSessionCheckpoint& record) {
    if (!ESP.rtcUserMemoryRead(DRIFT_SESSION_RTC_OFFSET,
                               reinterpret_cast<uint32_t*>(&record), sizeof(record)))
        return false;
    if (record.magic != DRIFT_SESSION_MAGIC ||
        record.version != DRIFT_SESSION_VERSION ||
        record.size != sizeof(record) ||
        record.checksum != driftSessionChecksum(record) ||
        record.activeRateSnapshot < -DRIFT_MAX_RATE_PPM_MILLI ||
        record.activeRateSnapshot > DRIFT_MAX_RATE_PPM_MILLI ||
        record.pendingRatePpmMilli < -DRIFT_MAX_RATE_PPM_MILLI ||
        record.pendingRatePpmMilli > DRIFT_MAX_RATE_PPM_MILLI ||
        record.rateEstimateCount > record.acceptedChecks)
        return false;
    return true;
}

uint32_t chronographLimitMs() {
    return settings.chronographDisplayMode == 1
        ? CHRONOGRAPH_EXTENDED_MAX_MS
        : CHRONOGRAPH_MAX_MS;
}

void setDisplayTicker(bool chronograph) {
    scrollTckr.detach();
    // Precision chronograph keeps the 100 Hz ticker for accurate
    // centisecond/button timing. Normal mode has no centiseconds to render,
    // so it uses the selected display rate while running.
    if (chronograph && settings.chronographDisplayMode == 0)
        scrollTckr.attach(0.010, onDisplayTick);
    else
        scrollTckr.attach(settings.scrollSpeed == 1 ? 0.025 : 0.040, onDisplayTick);
}

// Versioned settings signature prevents older firmware data being misread.
const uint32_t SETTINGS_MAGIC = 0x4D434C51;

// Persistent product identity is kept away from the established settings and
// drift-calibration records.  It lets a clean install recognise this
// firmware's EEPROM while still allowing older MatrixClock builds (which do
// not have this marker) to migrate their compatible settings.
const uint32_t PRODUCT_MARKER_MAGIC = 0x4D434B50; // "MCKP"
const uint16_t PRODUCT_SCHEMA_VERSION = 1;
const uint16_t PRODUCT_MARKER_OFFSET = 480;

struct ProductMarker {
    uint32_t magic;
    uint16_t schema;
    uint16_t reserved;
};

// Authentication is stored separately so the long-established ClockSettings
// layout remains compatible with existing installations.  Blank credentials
// deliberately disable protection; an uninitialised record opens the first-use
// setup form.  Passwords are stored only in EEPROM and are never logged.
const uint32_t AUTH_MAGIC = 0x4D434155; // "MCAU"
const uint16_t AUTH_VERSION = 1;
const uint16_t AUTH_OFFSET = 400;
struct AuthSettings {
    uint32_t magic;
    uint16_t version;
    uint8_t initialized;
    uint8_t reserved;
    char username[20];
    char password[40];
    uint32_t checksum;
};
static_assert(AUTH_OFFSET + sizeof(AuthSettings) <= PRODUCT_MARKER_OFFSET,
              "Authentication record must fit before product marker");
AuthSettings authSettings = {};
bool authSessionValid = false;
String authSessionToken;

// Explicit declaration prevents the Arduino sketch preprocessor from placing
// a generated prototype above AuthSettings.
uint32_t authChecksum(const AuthSettings& source);

uint32_t authChecksum(const AuthSettings& source) {
    AuthSettings record = source;
    record.checksum = 0;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&record);
    uint32_t hash = 2166136261UL;
    for (size_t i = 0; i < sizeof(record); i++) {
        hash ^= bytes[i];
        hash *= 16777619UL;
    }
    return hash;
}

void saveAuthSettings() {
    authSettings.magic = AUTH_MAGIC;
    authSettings.version = AUTH_VERSION;
    authSettings.username[sizeof(authSettings.username) - 1] = '\0';
    authSettings.password[sizeof(authSettings.password) - 1] = '\0';
    authSettings.checksum = authChecksum(authSettings);
    EEPROM.put(AUTH_OFFSET, authSettings);
    EEPROM.commit();
    authSessionValid = false;
}

void loadAuthSettings() {
    EEPROM.get(AUTH_OFFSET, authSettings);
    bool valid = authSettings.magic == AUTH_MAGIC &&
                 authSettings.version == AUTH_VERSION &&
                 authSettings.checksum == authChecksum(authSettings);
    if (!valid) {
        memset(&authSettings, 0, sizeof(authSettings));
        authSettings.magic = AUTH_MAGIC;
        authSettings.version = AUTH_VERSION;
        authSettings.initialized = 0;
        saveAuthSettings();
    }
    authSettings.username[sizeof(authSettings.username) - 1] = '\0';
    authSettings.password[sizeof(authSettings.password) - 1] = '\0';
    authSessionToken = String(ESP.getChipId(), HEX) + String(ESP.random(), HEX) + String(ESP.random(), HEX);
}

bool authEnabled() {
    return authSettings.initialized && authSettings.username[0] != '\0' &&
           authSettings.password[0] != '\0';
}

bool authCookieValid() {
    return authSessionValid && web.hasHeader("Cookie") &&
           web.header("Cookie").indexOf(String("mc_session=") + authSessionToken) >= 0;
}

void sendLoginPage(bool firstUse, const String& error = String()) {
    String html = "<!doctype html><html><head><meta name='viewport' content='width=device-width'><link rel='icon' href='/favicon.ico'><title>Matrix Clock login</title><style>*{box-sizing:border-box}html,body{min-height:100%;margin:0}body{min-height:100vh;display:grid;place-items:center;padding:1em;background:#090e18 radial-gradient(circle at 20% 0%,#243653 0%,#101827 48%,#090e18 100%) no-repeat fixed;background-size:cover;color:#eef4ff;font:16px -apple-system,BlinkMacSystemFont,Segoe UI,sans-serif}.card{width:min(460px,100%);padding:1.5em;border:1px solid rgba(180,210,255,.2);border-radius:22px;background:rgba(24,37,59,.64);box-shadow:0 16px 40px rgba(0,0,0,.32);backdrop-filter:blur(18px)}h1{margin-top:0;color:#f7faff}label{display:block;margin:.9em 0;color:#dbe7fb}input,button{width:100%;min-height:2.8em;padding:.65em;font:inherit;border-radius:12px}input{border:1px solid rgba(180,210,255,.25);background:rgba(7,14,26,.62);color:#f1f6ff}button{font-weight:600;color:white;border:0;background:#1677ff;box-shadow:0 5px 14px rgba(22,119,255,.25)}.error{padding:.7em;border-radius:10px;background:rgba(196,61,89,.2);color:#ffd8df}.hint{color:#b8c7de;line-height:1.45}</style></head><body><main class='card'><h1>Matrix Clock</h1>";
    html += firstUse ? "<p class='hint'>Set a username and password to protect the clock, or leave both blank to continue without protection.</p>" : "<p class='hint'>Enter your Matrix Clock credentials.</p>";
    if (error.length()) html += "<p class='error'>" + escapeHtml(error) + "</p>";
    html += "<form method='POST' action='/login'><label>Username<input name='username' autocomplete='username' maxlength='19'></label><label>Password<input name='password' type='password' autocomplete='current-password' maxlength='39'></label><button type='submit'>";
    html += firstUse ? "Save and continue" : "Sign in";
    html += "</button></form></main></body></html>";
    web.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0", true);
    web.send(200, "text/html", html);
}

bool requireWebAuth() {
    if (provisioningMode) return true;
    if (!authSettings.initialized) {
        if (web.uri() != "/login") {
            web.sendHeader("Location", "/login", true);
            web.send(302, "text/plain", "First-use setup");
            return false;
        }
        return true;
    }
    if (!authEnabled()) return true;
    if (authCookieValid()) return true;
    if (web.hasHeader("Authorization") &&
        web.authenticate(authSettings.username, authSettings.password)) {
        authSessionValid = true;
        web.sendHeader("Set-Cookie", String("mc_session=") + authSessionToken + "; HttpOnly; SameSite=Strict; Path=/", true);
        return true;
    }
    // Keep machine clients (including Home Assistant) on a proper HTTP Basic
    // Auth challenge; browsers use the friendlier login page below.
    if (web.uri().startsWith("/api/")) {
        web.requestAuthentication(BASIC_AUTH, "MatrixClock");
        return false;
    }
    web.sendHeader("Location", "/login", true);
    web.send(302, "text/plain", "Login required");
    return false;
}

void handleLogin() {
    bool firstUse = !authSettings.initialized;
    if (web.method() == HTTP_GET) {
        sendLoginPage(firstUse);
        return;
    }
    String username = web.arg("username");
    String password = web.arg("password");
    if (username.length() >= sizeof(authSettings.username) || password.length() >= sizeof(authSettings.password)) {
        sendLoginPage(firstUse, "Credentials are too long.");
        return;
    }
    if (!firstUse && (!authEnabled() || username != authSettings.username || password != authSettings.password)) {
        sendLoginPage(false, "Invalid username or password.");
        return;
    }
    if (firstUse) {
        memset(authSettings.username, 0, sizeof(authSettings.username));
        memset(authSettings.password, 0, sizeof(authSettings.password));
        strlcpy(authSettings.username, username.c_str(), sizeof(authSettings.username));
        strlcpy(authSettings.password, password.c_str(), sizeof(authSettings.password));
        authSettings.initialized = 1;
        saveAuthSettings();
    }
    authSessionValid = authEnabled();
    web.sendHeader("Set-Cookie", String("mc_session=") + authSessionToken + "; HttpOnly; SameSite=Strict; Path=/", true);
    web.sendHeader("Location", "/", true);
    web.send(302, "text/plain", "Signed in");
}

static_assert(PRODUCT_MARKER_OFFSET + sizeof(ProductMarker) <= 512,
              "Product marker must fit in EEPROM");

void loadSettings() {
    EEPROM.begin(512);
    EEPROM.get(0, settings);
    ProductMarker marker;
    EEPROM.get(PRODUCT_MARKER_OFFSET, marker);

    const bool markerPresent = marker.magic == PRODUCT_MARKER_MAGIC;
    const bool legacyMatrixClock = settings.magic == SETTINGS_MAGIC;
    // A current marker with corrupt settings, a marker from a newer schema,
    // or completely foreign EEPROM data all require a clean initialisation.
    const bool incompatibleData =
        (markerPresent && (marker.schema != PRODUCT_SCHEMA_VERSION || !legacyMatrixClock)) ||
        (!markerPresent && !legacyMatrixClock);
    bool markerValid = markerPresent && marker.schema == PRODUCT_SCHEMA_VERSION;

    if (incompatibleData) {
        // Erase application settings only.  Preserve the ESP8266 SDK Wi-Fi
        // profile so previously stored network credentials remain available.
        // This deliberately does not touch the running firmware, bootloader,
        // or RF calibration sectors.  The RTC is reset so no date/time
        // survives an install over unrelated firmware.
        for (int i = 0; i < 512; i++) EEPROM.write(i, 0xFF);
        EEPROM.commit();
        rtc_resetToBaseline();
        memset(&settings, 0, sizeof(settings));
        markerValid = false;
    }

    settings.ssid[sizeof(settings.ssid) - 1] = '\0';
    settings.password[sizeof(settings.password) - 1] = '\0';
    settings.ntpServer[sizeof(settings.ntpServer) - 1] = '\0';
    settings.timezone[sizeof(settings.timezone) - 1] = '\0';
    settings.customTimezone[sizeof(settings.customTimezone) - 1] = '\0';

    bool settingsChanged = false;
    if (settings.magic != SETTINGS_MAGIC) {
        memset(&settings, 0, sizeof(settings));
        settings.magic = SETTINGS_MAGIC;
        strlcpy(settings.ssid, ssid, sizeof(settings.ssid));
        strlcpy(settings.password, pass, sizeof(settings.password));
        strlcpy(settings.ntpServer, ntpServerName, sizeof(settings.ntpServer));
        strlcpy(settings.timezone, "UTC0", sizeof(settings.timezone));
        settings.customTimezone[0] = '\0';
        settings.brightness = 5;
        settings.timeFormat = 24;
        settings.scrolling = 1;
        settings.scrollSpeed = 0; // Slow (40 ms)
        settings.cleanTransitions = 0; // Vertical sweep
        settings.daylightSaving = 0;
        settings.clockColonBlink = 1; // Blink
        settings.restartHour = 2;
        settings.restartMinute = 0;
        settings.apiScrollCount = 1;
        settings.chronographDisplayMode = 1; // Normal mode (HH:MM SS)
        settingsChanged = true;
    }

    // Validate persisted values before they are used as array indexes, enum
    // selections, timing values, or C strings. The struct layout remains
    // unchanged so existing EEPROM settings stay compatible.
    if (settings.ntpServer[0] == '\0') {
        strlcpy(settings.ntpServer, "time.cloudflare.com", sizeof(settings.ntpServer));
        settingsChanged = true;
    }
    if (settings.timezone[0] == '\0') {
        strlcpy(settings.timezone, "UTC0", sizeof(settings.timezone));
        settingsChanged = true;
    }
    if (settings.utcOffset < -660 || settings.utcOffset > 660) {
        settings.utcOffset = 0;
        settingsChanged = true;
    }
    if (settings.brightness < 1 || settings.brightness > 5) {
        settings.brightness = 5;
        settingsChanged = true;
    }
    if (settings.timeFormat != 12 && settings.timeFormat != 24) {
        settings.timeFormat = 24;
        settingsChanged = true;
    }
    if (settings.scrolling > 3) {
        settings.scrolling = 1;
        settingsChanged = true;
    }
    if (settings.scrollSpeed > 1) {
        // Value 2 was the old Fast option. Keep legacy clocks on Fast rather
        // than leaving an invalid value.
        settings.scrollSpeed = 1;
        settingsChanged = true;
    }
    if (settings.cleanTransitions > 1) {
        settings.cleanTransitions = 0;
        settingsChanged = true;
    }
    if (settings.daylightSaving > 2) {
        settings.daylightSaving = 0;
        settingsChanged = true;
    }
    if (settings.clockColonBlink > 1) {
        settings.clockColonBlink = 1;
        settingsChanged = true;
    }
    if (settings.restartHour > 23 || settings.restartMinute > 55 ||
        (settings.restartMinute % 5) != 0) {
        settings.restartHour = 2;
        settings.restartMinute = 0;
        settingsChanged = true;
    }
    if (settings.apiScrollCount > 5) {
        settings.apiScrollCount = 1;
        settingsChanged = true;
    }
    if (settings.chronographDisplayMode > 1) {
        settings.chronographDisplayMode = 1;
        settingsChanged = true;
    }

    if (settingsChanged || !markerValid) {
        EEPROM.put(0, settings);
        marker.magic = PRODUCT_MARKER_MAGIC;
        marker.schema = PRODUCT_SCHEMA_VERSION;
        marker.reserved = 0;
        EEPROM.put(PRODUCT_MARKER_OFFSET, marker);
        EEPROM.commit();
    }

    strlcpy(ssid, settings.ssid, sizeof(ssid));
    strlcpy(pass, settings.password, sizeof(pass));
    ntpServerName = settings.ntpServer;
}

String formatDriftPpm(int32_t ratePpmMilli) {
    char text[24];
    int32_t magnitude = ratePpmMilli < 0 ? -ratePpmMilli : ratePpmMilli;
    snprintf(text, sizeof(text), "%c%ld.%03ld ppm",
             ratePpmMilli < 0 ? '-' : '+',
             (long)(magnitude / 1000L), (long)(magnitude % 1000L));
    return String(text);
}

String formatDriftOffset(int32_t offsetUs) {
    char text[24];
    int32_t magnitude = offsetUs < 0 ? -offsetUs : offsetUs;
    snprintf(text, sizeof(text), "%c%ld.%03ld ms",
             offsetUs < 0 ? '-' : '+',
             (long)(magnitude / 1000L), (long)(magnitude % 1000L));
    return String(text);
}

void resetDriftCalibration(bool persist) {
    memset(&driftCalibration, 0, sizeof(driftCalibration));
    driftCalibration.magic = DRIFT_CALIBRATION_MAGIC;
    driftCalibration.version = DRIFT_CALIBRATION_VERSION;
    activeDriftPpmMilli = 0;
    softwareClockRateRemainder = 0;
    softwareClockPhaseRemainingUs = 0;
    softwareClockPhaseRemainder = 0;
    driftReferenceValid = false;
    driftReferenceObservedUs = 0;
    driftReferenceOffsetUs = 0;
    driftAcceptedChecks = 0;
    driftLastCheckValid = false;
    driftLastOffsetUs = 0;
    driftLastRoundTripUs = 0;
    driftLastObservedUs = 0;
    driftCandidateValid = false;
    driftCandidatePpmMilli = 0;
    driftRateEstimateCount = 0;
    clearDriftSessionCheckpoint();
    if (persist) {
        EEPROM.put(DRIFT_CALIBRATION_OFFSET, driftCalibration);
        EEPROM.commit();
    }
}

void loadDriftCalibration() {
    static_assert(DRIFT_CALIBRATION_OFFSET + sizeof(DriftCalibration) <= 512,
                  "Drift calibration must fit in EEPROM");
    EEPROM.get(DRIFT_CALIBRATION_OFFSET, driftCalibration);
    if (driftCalibration.magic != DRIFT_CALIBRATION_MAGIC ||
        driftCalibration.version != DRIFT_CALIBRATION_VERSION ||
        driftCalibration.ratePpmMilli < -DRIFT_MAX_RATE_PPM_MILLI ||
        driftCalibration.ratePpmMilli > DRIFT_MAX_RATE_PPM_MILLI) {
        resetDriftCalibration(true);
        return;
    }
    activeDriftPpmMilli = driftCalibration.ratePpmMilli;

    DriftSessionCheckpoint checkpoint;
    if (!readDriftSessionCheckpoint(checkpoint)) {
        clearDriftSessionCheckpoint();
        return;
    }
    // A checkpoint belongs only to the active rate from which it was
    // calculated.  Reject stale data after a firmware/settings migration or a
    // previously completed calibration commit.
    if (checkpoint.activeRateSnapshot != activeDriftPpmMilli) {
        clearDriftSessionCheckpoint();
        return;
    }

    // A boot-time NTP sync steps the software clock, so a pre-reset offset
    // baseline cannot safely span the reboot. Completed rate estimates can.
    driftReferenceValid = false;
    driftReferenceObservedUs = 0;
    driftReferenceOffsetUs = 0;
    driftAcceptedChecks = checkpoint.acceptedChecks;
    driftLastCheckValid = checkpoint.acceptedChecks != 0;
    driftLastOffsetUs = checkpoint.latestOffsetUs;
    driftLastRoundTripUs = checkpoint.latestRoundTripUs;
    driftLastObservedUs = (uint64_t)checkpoint.latestEpoch * 1000000ULL;
    driftCandidateValid = checkpoint.pendingValid != 0;
    driftCandidatePpmMilli = checkpoint.pendingRatePpmMilli;
    driftRateEstimateCount = checkpoint.rateEstimateCount;

    // Promote a sufficiently supported estimate only at boot.  The running
    // session therefore never changes speed halfway through the day.
    if (driftCandidateValid &&
        driftRateEstimateCount >= DRIFT_MIN_RATE_ESTIMATES) {
        int64_t targetRate = driftCandidatePpmMilli;
        if (driftCalibration.updatedEpoch != 0)
            targetRate = ((int64_t)driftCalibration.ratePpmMilli + targetRate) / 2;
        if (targetRate >= -DRIFT_MAX_RATE_PPM_MILLI &&
            targetRate <= DRIFT_MAX_RATE_PPM_MILLI) {
            driftCalibration.ratePpmMilli = (int32_t)targetRate;
            driftCalibration.updatedEpoch = checkpoint.latestEpoch;
            driftCalibration.lastOffsetUs = checkpoint.latestOffsetUs;
            driftCalibration.acceptedChecks = checkpoint.acceptedChecks;
            EEPROM.put(DRIFT_CALIBRATION_OFFSET, driftCalibration);
            EEPROM.commit();
            activeDriftPpmMilli = driftCalibration.ratePpmMilli;
        }
        // The estimate has now either been consumed or rejected. Start a new
        // cycle against the rate selected for this boot.
        driftReferenceValid = false;
        driftReferenceObservedUs = 0;
        driftReferenceOffsetUs = 0;
        driftAcceptedChecks = 0;
        driftLastCheckValid = false;
        driftLastOffsetUs = 0;
        driftLastRoundTripUs = 0;
        driftLastObservedUs = 0;
        driftCandidateValid = false;
        driftCandidatePpmMilli = 0;
        driftRateEstimateCount = 0;
        clearDriftSessionCheckpoint();
    }
}

uint64_t adjustedClockDeltaUs(uint32_t rawDeltaUs, int64_t& remainder) {
    const int64_t scale = 1000000000LL; // milli-ppm scale
    int64_t numerator = (int64_t)rawDeltaUs * (scale + activeDriftPpmMilli) + remainder;
    uint64_t adjusted = (uint64_t)(numerator / scale);
    remainder = numerator % scale;
    return adjusted;
}

// Apply a temporary phase correction at a tightly bounded rate.  Keeping this
// separate from adjustedClockDeltaUs() prevents an NTP wall-clock correction
// from changing chronograph measurements.
int64_t phaseSlewDeltaUs(uint32_t rawDeltaUs, int64_t& remainingUs,
                          int64_t& remainder) {
    if (remainingUs == 0)
        return 0;

    const int64_t scale = 1000000000LL; // milli-ppm scale
    const int64_t rate = remainingUs > 0
        ? PHASE_SLEW_RATE_PPM_MILLI : -PHASE_SLEW_RATE_PPM_MILLI;
    int64_t numerator = (int64_t)rawDeltaUs * rate + remainder;
    int64_t correctionUs = numerator / scale;
    remainder = numerator % scale;

    // Do not leave a sub-microsecond remainder stranded indefinitely.
    if (correctionUs == 0)
        return 0;

    if ((remainingUs > 0 && correctionUs >= remainingUs) ||
        (remainingUs < 0 && correctionUs <= remainingUs)) {
        correctionUs = remainingUs;
        remainingUs = 0;
        remainder = 0;
    } else {
        remainingUs -= correctionUs;
    }
    return correctionUs;
}

void scheduleClockPhaseSlew(int64_t offsetUs) {
    if (offsetUs > -PHASE_SLEW_IGNORE_US && offsetUs < PHASE_SLEW_IGNORE_US) {
        // A fresh stable check says the remaining phase error is negligible.
        // Stop any older target rather than continuing to chase stale data.
        softwareClockPhaseRemainingUs = 0;
        softwareClockPhaseRemainder = 0;
        return;
    }
    if (offsetUs < -PHASE_SLEW_MAX_OFFSET_US || offsetUs > PHASE_SLEW_MAX_OFFSET_US)
        return;

    // The measured offset is relative to the already-slewed software clock,
    // so replacing the remaining target avoids accumulating stale corrections.
    softwareClockPhaseRemainingUs = offsetUs;
    softwareClockPhaseRemainder = 0;
}

// The chronograph is independent from the wall clock, but it uses the same
// ESP8266 oscillator.  Apply the learned oscillator-rate correction to each
// running interval while keeping a separate remainder for the stopwatch.
uint32_t adjustedChronographDeltaMs(uint32_t startMicros, uint32_t nowMicros,
                                    int64_t& remainder) {
    uint64_t adjustedUs = adjustedClockDeltaUs(
        (uint32_t)(nowMicros - startMicros), remainder);
    return (uint32_t)(adjustedUs / 1000ULL);
}

uint64_t softwareClockElapsedAt(uint32_t referenceMicros) {
    uint32_t rawDeltaUs = (uint32_t)(referenceMicros - softwareClockLastMicros);
    int64_t temporaryRateRemainder = softwareClockRateRemainder;
    int64_t temporaryPhaseRemainingUs = softwareClockPhaseRemainingUs;
    int64_t temporaryPhaseRemainder = softwareClockPhaseRemainder;
    uint64_t rateAdjustedUs = adjustedClockDeltaUs(rawDeltaUs, temporaryRateRemainder);
    int64_t phaseAdjustedUs = phaseSlewDeltaUs(rawDeltaUs, temporaryPhaseRemainingUs,
                                                temporaryPhaseRemainder);
    return (uint64_t)((int64_t)softwareClockElapsedUs + (int64_t)rateAdjustedUs +
                      phaseAdjustedUs);
}

uint32_t softwareClockEpochNow() {
    if (!softwareClockValid)
        return 0;
    return softwareEpochBase + (uint32_t)(softwareClockElapsedAt(micros()) / 1000000ULL);
}

void commitDriftCalibrationIfReady() {
    if (!driftCandidateValid ||
        driftRateEstimateCount < DRIFT_MIN_RATE_ESTIMATES)
        return;
    int32_t storedRate = driftCalibration.ratePpmMilli;
    int64_t targetRate = driftCandidatePpmMilli;
    // A running average prevents one unusually warm/cold day from replacing a
    // previously stable calibration outright.
    if (driftCalibration.updatedEpoch != 0)
        targetRate = ((int64_t)storedRate + targetRate) / 2;
    if (targetRate < -DRIFT_MAX_RATE_PPM_MILLI || targetRate > DRIFT_MAX_RATE_PPM_MILLI) {
        return;
    }
    driftCalibration.ratePpmMilli = (int32_t)targetRate;
    driftCalibration.updatedEpoch = softwareClockEpochNow();
    driftCalibration.lastOffsetUs = driftLastOffsetUs;
    driftCalibration.acceptedChecks = driftAcceptedChecks;
    EEPROM.put(DRIFT_CALIBRATION_OFFSET, driftCalibration);
    EEPROM.commit();
    activeDriftPpmMilli = driftCalibration.ratePpmMilli;
    clearDriftSessionCheckpoint();
}

String driftStoredText() {
    return driftCalibration.updatedEpoch == 0 ? "Not calibrated" :
           formatDriftPpm(driftCalibration.ratePpmMilli);
}

String driftEstimateText() {
    return driftCandidateValid ? formatDriftPpm(driftCandidatePpmMilli) :
           "Collecting samples";
}

String driftLatestCheckText() {
    if (!driftLastCheckValid)
        return "Awaiting initial check";
    return formatDriftOffset(driftLastOffsetUs) + " offset";
}

String driftSampleText() {
    return String(driftAcceptedChecks) + " checks, " +
           String(driftRateEstimateCount) + " rate estimates";
}

const char* htmlSelected(bool value) { return value ? " selected" : ""; }
const char* signalQuality(int rssi) {
    if (rssi >= -55) return "Excellent";
    if (rssi >= -67) return "Good";
    if (rssi >= -75) return "Fair";
    return "Bad";
}

void flushWebChunk(String& chunk) {
    if (!chunk.length())
        return;
    web.sendContent(chunk);
    chunk = "";
    yield();
}

void appendWebOption(String& chunk, const char* value, const char* label,
                     bool selected) {
    chunk += F("<option value='");
    chunk += value;
    chunk += '\'';
    chunk += htmlSelected(selected);
    chunk += '>';
    chunk += label;
    chunk += F("</option>");
    if (chunk.length() >= 512)
        flushWebChunk(chunk);
}

void appendNumericWebOption(String& chunk, int value, const char* label,
                            bool selected) {
    char valueText[12];
    snprintf(valueText, sizeof(valueText), "%d", value);
    appendWebOption(chunk, valueText, label, selected);
}

// Setup mode has deliberately separate, compact markup.  Building the full
// normal settings page while in AP mode left too little heap for the display
// refresh and could stall the marquee while a phone loaded the page.
void handleSetupWebRoot(uint32_t webStartedAt) {
    int networkCount = WiFi.scanComplete();
    bool scanRunning = networkCount == WIFI_SCAN_RUNNING;
    if (networkCount < 0)
        networkCount = 0;

    web.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0", true);
    web.sendHeader("Pragma", "no-cache");
    web.sendHeader("Expires", "0");
    web.setContentLength(CONTENT_LENGTH_UNKNOWN);
    web.send(200, "text/html", "");
    web.sendContent_P(PSTR("<!doctype html><html><head><meta name='viewport' content='width=device-width'>"
                      "<link rel='icon' href='/favicon.ico'><title>Matrix Clock setup</title><style>"
                      "*{box-sizing:border-box}html,body{min-height:100%;margin:0}body{min-height:100vh;display:grid;place-items:center;padding:1em;background:#090e18 radial-gradient(circle at 20% 0%,#243653 0%,#101827 48%,#090e18 100%) no-repeat fixed;background-size:cover;color:#eef4ff;font:16px -apple-system,BlinkMacSystemFont,Segoe UI,sans-serif}.card{width:min(620px,100%);padding:1.4em;border:1px solid rgba(180,210,255,.2);border-radius:22px;background:rgba(24,37,59,.64);box-shadow:0 16px 40px rgba(0,0,0,.32);backdrop-filter:blur(18px)}h1{margin-top:0;color:#f7faff}fieldset{margin:1em 0;padding:1em;border:1px solid rgba(180,210,255,.2);border-radius:15px;background:rgba(10,18,32,.3)}label{display:block;margin:.8em 0;color:#dbe7fb}input,select,button{width:100%;min-height:3.2em;padding:.7em;font:inherit;border-radius:12px}input,select{border:1px solid rgba(180,210,255,.25);background:rgba(7,14,26,.62);color:#f1f6ff}button{font-weight:600;color:white;border:0;background:#1677ff;box-shadow:0 5px 14px rgba(22,119,255,.25)}button:active{transform:translateY(2px);filter:brightness(.82)}.danger{margin-top:.8em;background:#c43d59;box-shadow:0 5px 14px rgba(196,61,89,.25)}.notice{padding:.75em .9em;border-radius:12px;background:rgba(64,139,255,.16);border:1px solid rgba(120,180,255,.28);color:#dceaff;line-height:1.5}.small{font-size:.9em;color:#aebfda}</style></head><body><main class='card'><h1>Matrix Clock</h1><form method='POST' action='/save'><fieldset><legend>Network settings</legend><label>Wi-Fi network<select name='ssid'><option value=''>Select a network</option>"));

    for (int i = 0; i < networkCount; i++) {
        String safeSsid = escapeHtml(WiFi.SSID(i));
        web.sendContent_P(PSTR("<option value='"));
        web.sendContent(safeSsid);
        web.sendContent_P(PSTR("'>"));
        web.sendContent(safeSsid);
        web.sendContent_P(PSTR("</option>"));
        yield();
    }
    web.sendContent_P(PSTR("<option value='__manual__'>Manual SSID</option></select></label>"
                      "<label>Manual SSID (hidden network)<input name='ssid_manual' placeholder='Only needed for manual selection'></label>"
                      "<label>Wi-Fi password<input name='password' type='password' autocomplete='current-password'></label>"));
    if (scanRunning)
        web.sendContent_P(PSTR("<p class='small'>Network scan is still running. Choose Manual SSID or refresh shortly.</p>"));
    web.sendContent_P(PSTR("</fieldset><p class='notice'>Once connected, open the MatrixClock setup page from a phone or computer on the same Wi-Fi network as the clock.</p><button type='submit'>Save and reboot</button></form>"
                      "<form method='POST' action='/reset' onsubmit=\"return confirm('Erase all saved settings and Wi-Fi credentials, then reboot?')\"><button class='danger' type='submit'>Factory reset</button></form></main></body></html>"));
    Serial.printf("WEB / setup complete ms=%lu heap=%u networks=%d\n",
                  (unsigned long)(millis() - webStartedAt), ESP.getFreeHeap(), networkCount);
}

void handleWebRoot() {
    const uint32_t webStartedAt = millis();
    Serial.printf("WEB / start heap=%u mode=%d wifi=%d provisioning=%d\n",
                  ESP.getFreeHeap(), (int)WiFi.getMode(), (int)WiFi.status(),
                  provisioningMode ? 1 : 0);
    if (provisioningMode) {
        handleSetupWebRoot(webStartedAt);
        return;
    }
    if (!requireWebAuth()) return;
    static const char* const tzValues[] = {"UTC+12","UTC+11","UTC+10","UTC+9","UTC+8","UTC+7","UTC+6","UTC+5","UTC+4","UTC+3:30","UTC+3","UTC+2","UTC+1","UTC0","Europe/London","Europe/Paris","UTC-2","UTC-3","UTC-3:30","UTC-4","UTC-5","UTC-5:30","UTC-6","UTC-6:30","UTC-7","UTC-8","UTC-9","UTC-9:30","Australia/Sydney","UTC-11","UTC-12","UTC-13","UTC-14"};
    static const char* const tzLabels[] = {"(UTC-12:00) Baker Island","(UTC-11:00) American Samoa","(UTC-10:00) Honolulu","(UTC-09:00) Anchorage","(UTC-08:00) Los Angeles, Vancouver","(UTC-07:00) Denver, Phoenix","(UTC-06:00) Chicago, Mexico City","(UTC-05:00) New York, Lima","(UTC-04:00) Halifax, Santiago","(UTC-03:30) Newfoundland","(UTC-03:00) Buenos Aires","(UTC-02:00) South Georgia","(UTC-01:00) Azores","(UTC+00:00) UTC / Reykjavik (no DST)","(UTC+00:00) London (GMT/BST)","(UTC+01:00) Paris, Berlin","(UTC+02:00) Athens, Johannesburg","(UTC+03:00) Moscow, Nairobi","(UTC+03:30) Tehran","(UTC+04:00) Dubai, Abu Dhabi","(UTC+05:00) Karachi, Islamabad","(UTC+05:30) India, Sri Lanka","(UTC+06:00) Dhaka, Almaty","(UTC+06:30) Yangon","(UTC+07:00) Bangkok, Jakarta","(UTC+08:00) Singapore, Beijing","(UTC+09:00) Tokyo, Seoul","(UTC+09:30) Darwin","(UTC+10:00) Sydney, Melbourne","(UTC+11:00) Solomon Islands","(UTC+12:00) Auckland, Fiji","(UTC+13:00) Samoa, Tonga","(UTC+14:00) Kiritimati"};
    const bool ntpIsBuiltIn =
        strcmp(settings.ntpServer, "time.cloudflare.com") == 0 ||
        strcmp(settings.ntpServer, "ntp1.npl.co.uk") == 0 ||
        strcmp(settings.ntpServer, "pool.ntp.org") == 0 ||
        strcmp(settings.ntpServer, "ntp.se") == 0 ||
        strcmp(settings.ntpServer, "ntp.nict.jp") == 0 ||
        strcmp(settings.ntpServer, "time.nist.gov") == 0 ||
        strcmp(settings.ntpServer, "uk.pool.ntp.org") == 0 ||
        strcmp(settings.ntpServer, "europe.pool.ntp.org") == 0;
    const String safeCustomNtpValue = ntpIsBuiltIn
        ? String() : escapeHtml(String(settings.ntpServer));
    const String safeCustomTimezone = escapeHtml(String(settings.customTimezone));
    const String safeSsid = escapeHtml(WiFi.SSID());
    const String ipAddress = WiFi.localIP().toString();
    const int currentRssi = WiFi.RSSI();

    // Stream static content from flash and generate each dynamic option only
    // when it is about to be sent. This keeps peak heap use bounded.
    web.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0", true);
    web.sendHeader("Pragma", "no-cache");
    web.sendHeader("Expires", "0");
    web.setContentLength(CONTENT_LENGTH_UNKNOWN);
    web.send(200, "text/html", "");

    // Send the page frame directly from flash so CSS cannot disappear when
    // memory is tight. The dynamic form fields below remain streamed.
    web.sendContent_P(PSTR("<!doctype html><html><head><meta name='viewport' content='width=device-width'><link rel='icon' href='/favicon.ico'><title>Matrix Clock</title><style>*{box-sizing:border-box}body{font:16px -apple-system,BlinkMacSystemFont,Segoe UI,sans-serif;max-width:720px;margin:0 auto;padding:2em 1em;background:#090e18 radial-gradient(circle at 20% 0%,#243653,#101827 48%,#090e18) no-repeat fixed;background-size:cover;color:#eef4ff}.card{padding:1.4em;margin:1em 0;border:1px solid rgba(180,210,255,.2);border-radius:22px;background:rgba(24,37,59,.64);box-shadow:0 16px 40px rgba(0,0,0,.32);backdrop-filter:blur(18px)}a,a:visited,a:hover,a:active{color:#1677ff}h1{font-weight:650;letter-spacing:-.03em;color:#f7faff}fieldset{margin:1em 0;padding:1em;border:1px solid rgba(180,210,255,.2);border-radius:15px;background:rgba(10,18,32,.3)}label{display:block;margin:.8em 0;color:#dbe7fb}.small{font-size:.9em;color:#aebfda;line-height:1.5}input,select,button{width:100%;min-height:2.6em;font:inherit;padding:.6em .7em;border-radius:10px}input,select{border:1px solid rgba(180,210,255,.25);background:rgba(7,14,26,.62);color:#f1f6ff}button{font-weight:600;color:#fff;border:0;border-radius:12px;background:#1677ff;box-shadow:0 5px 14px rgba(22,119,255,.25);transition:transform .08s ease,filter .08s ease}button:not(:disabled):active{transform:translateY(2px) scale(.99);filter:brightness(.82)}button.danger{margin-top:.8em;background:#c43d59}.api-cancel{margin-top:.7em;background:#c43d59}.calibration-reset{background:#c43d59}.status-grid,.inline-fields,.calibration-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:.65em}.status-grid{grid-template-columns:repeat(3,minmax(0,1fr));margin:1em 0}.status-item,.calibration-grid>div{padding:.75em .85em;border:1px solid rgba(180,210,255,.16);border-radius:13px;background:rgba(255,255,255,.06)}.status-label,.calibration-grid span{display:block;font-size:.76em;letter-spacing:.04em;text-transform:uppercase;color:#9eb1ce;margin-bottom:.25em}.status-value,.calibration-grid strong{display:block;color:#f1f6ff;font-weight:650}.event-list{display:grid;gap:.45em}.event-row,.event-heading{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:1em;padding:.55em .65em;border-radius:9px;background:rgba(255,255,255,.045);font:12px ui-monospace,monospace}.event-heading{margin-top:.65em;color:#9eb1ce;font-weight:650;border-bottom:1px solid rgba(180,210,255,.18);background:transparent}.event-label{color:#aebfda}.event-value{color:#f1f6ff;text-align:right}.api-row{display:flex;align-items:flex-end;gap:.65em}.api-row label{flex:1;margin:.8em 0 0}.api-row button{width:auto;white-space:nowrap}.api-status{display:block;min-height:1.4em;margin-top:.35em;color:#b9d5ff}.api-help,.calibration-help{color:#dbe7fb;line-height:1.5}.api-help small{display:block;margin-top:.45em;color:#aebfda}.api-playback{margin-top:1.1em;padding:1em;border:1px solid rgba(180,210,255,.16);border-radius:14px;background:rgba(255,255,255,.045)}.api-playback-title{font-weight:650;margin-bottom:.65em}.calibration-actions{margin-top:.8em}.calibration-actions form{margin:0}@media(max-width:520px){.status-grid,.inline-fields,.calibration-grid{grid-template-columns:1fr}.event-row,.event-heading{grid-template-columns:1fr;gap:.15em}.event-value{text-align:left}.api-row{align-items:stretch;flex-direction:column}.api-row button{width:100%}}</style></head><body><div class='card'><h1>Matrix Clock</h1>"));
    String pageChunk;
    pageChunk.reserve(640);

    pageChunk = F("<div class='status-grid'><div class='status-item'><span class='status-label'>Wi-Fi</span><span class='status-value'>");
    pageChunk += safeSsid;
    pageChunk += F("</span></div><div class='status-item'><span class='status-label'>IP address</span><span class='status-value'>");
    pageChunk += ipAddress;
    pageChunk += F("</span></div><div class='status-item'><span class='status-label'>Signal</span><span id='signalStatus' class='status-value'>");
    pageChunk += signalQuality(currentRssi);
    pageChunk += F(" <small>(");
    pageChunk += currentRssi;
    pageChunk += F(" dBm)</small></span></div></div><form method='POST' action='/save'><fieldset><legend>Time settings</legend><label>Time format <select name='format'>");
    appendWebOption(pageChunk, "24", "24-hour", settings.timeFormat == 24);
    appendWebOption(pageChunk, "12", "12-hour", settings.timeFormat == 12);
    pageChunk += F("</select></label><label>Timezone <select name='timezone'>");
    flushWebChunk(pageChunk);

    for (size_t i = 0; i < sizeof(tzValues) / sizeof(tzValues[0]); i++)
        appendWebOption(pageChunk, tzValues[i], tzLabels[i],
                        strcmp(settings.timezone, tzValues[i]) == 0);
    flushWebChunk(pageChunk);

    pageChunk = F("</select></label><label>Daylight saving <select name='dst'>");
    appendWebOption(pageChunk, "1", "Automatic", settings.daylightSaving == 1);
    appendWebOption(pageChunk, "0", "Disabled", settings.daylightSaving == 0);
    appendWebOption(pageChunk, "2", "Custom rule", settings.daylightSaving == 2);
    pageChunk += F("</select></label><label>Custom POSIX rule (used in Custom rule mode) <input name='customtz' value='");
    pageChunk += safeCustomTimezone;
    pageChunk += F("' placeholder='GMT0BST,M3.5.0/1,M10.5.0/2'><small><a href='https://techlogics.net/electronics/timezone-db.php' target='_blank' rel='noopener'>Open POSIX rule generator</a> &middot; <a href='https://www.iana.org/time-zones/theory' target='_blank' rel='noopener'>Format reference</a></small></label><label>NTP provider <select name='ntp'>");
    flushWebChunk(pageChunk);

    appendWebOption(pageChunk, "time.cloudflare.com", "Global - Cloudflare",
                    strcmp(settings.ntpServer, "time.cloudflare.com") == 0);
    appendWebOption(pageChunk, "ntp1.npl.co.uk", "United Kingdom - NPL",
                    strcmp(settings.ntpServer, "ntp1.npl.co.uk") == 0);
    appendWebOption(pageChunk, "pool.ntp.org", "Global - NTP Pool",
                    strcmp(settings.ntpServer, "pool.ntp.org") == 0);
    appendWebOption(pageChunk, "ntp.se", "Sweden / Europe - Netnod",
                    strcmp(settings.ntpServer, "ntp.se") == 0);
    appendWebOption(pageChunk, "ntp.nict.jp", "Japan / Asia - NICT",
                    strcmp(settings.ntpServer, "ntp.nict.jp") == 0);
    appendWebOption(pageChunk, "time.nist.gov", "United States - NIST",
                    strcmp(settings.ntpServer, "time.nist.gov") == 0);
    appendWebOption(pageChunk, "uk.pool.ntp.org", "United Kingdom - NTP Pool",
                    strcmp(settings.ntpServer, "uk.pool.ntp.org") == 0);
    appendWebOption(pageChunk, "europe.pool.ntp.org", "Europe - NTP Pool",
                    strcmp(settings.ntpServer, "europe.pool.ntp.org") == 0);
    appendWebOption(pageChunk, "custom", "Custom server", !ntpIsBuiltIn);
    pageChunk += F("</select></label><label>Custom NTP server (used when Custom is selected) <input name='customntp' value='");
    pageChunk += safeCustomNtpValue;
    pageChunk += F("' placeholder='URL / IP Address'></label><div class='inline-fields'><label>Daily restart hour <select name='restarthour'>");
    flushWebChunk(pageChunk);

    for (int hour = 0; hour < 24; hour++) {
        char hourText[3];
        snprintf(hourText, sizeof(hourText), "%02d", hour);
        appendNumericWebOption(pageChunk, hour, hourText,
                               settings.restartHour == hour);
    }
    flushWebChunk(pageChunk);
    web.sendContent_P(PSTR("</select></label><label>Daily restart minute <select name='restartminute'>"));
    for (int minute = 0; minute < 60; minute += 5) {
        char minuteText[3];
        snprintf(minuteText, sizeof(minuteText), "%02d", minute);
        appendNumericWebOption(pageChunk, minute, minuteText,
                               settings.restartMinute == minute);
    }
    pageChunk += F("</select></label></div></fieldset><fieldset><legend>Display settings</legend><div class='inline-fields'><label>Brightness <select name='brightness'>");
    appendWebOption(pageChunk, "1", "20%", settings.brightness == 1);
    appendWebOption(pageChunk, "2", "40%", settings.brightness == 2);
    appendWebOption(pageChunk, "3", "60%", settings.brightness == 3);
    appendWebOption(pageChunk, "4", "80%", settings.brightness == 4);
    appendWebOption(pageChunk, "5", "100%", settings.brightness == 5);
    pageChunk += F("</select></label><label>Chronograph mode <select name='chronomode'>");
    appendWebOption(pageChunk, "0", "Precision (MM:SS:cc)",
                    settings.chronographDisplayMode == 0);
    appendWebOption(pageChunk, "1", "Normal mode (HH:MM:ss)",
                    settings.chronographDisplayMode == 1);
    pageChunk += F("</select></label></div><div class='inline-fields'><label>HH(:)MM separator <select name='colonblink'>");
    flushWebChunk(pageChunk);

    appendWebOption(pageChunk, "0", "Solid", settings.clockColonBlink == 0);
    appendWebOption(pageChunk, "1", "Blink", settings.clockColonBlink == 1);
    pageChunk += F("</select></label><label>Digit transition <select name='transition'>");
    appendWebOption(pageChunk, "1", "Vertical sweep", !settings.cleanTransitions);
    appendWebOption(pageChunk, "0", "Instant change", settings.cleanTransitions);
    pageChunk += F("</select></label></div><div class='inline-fields'><label>Date Scrolling Interval <select name='scroll'>");
    appendWebOption(pageChunk, "1", "Every minute", settings.scrolling == 1);
    appendWebOption(pageChunk, "2", "Every 15 Minutes", settings.scrolling == 2);
    appendWebOption(pageChunk, "3", "Every hour", settings.scrolling == 3);
    appendWebOption(pageChunk, "0", "Off", settings.scrolling == 0);
    pageChunk += F("</select></label><label>Scroll speed <select name='scrollspeed'>");
    appendWebOption(pageChunk, "0", "Slow", settings.scrollSpeed == 0);
    appendWebOption(pageChunk, "1", "Fast", settings.scrollSpeed == 1);
    pageChunk += F("</select></label></div></fieldset><button type='submit'>Save and reboot</button></form>");
    flushWebChunk(pageChunk);

    web.sendContent_P(PSTR("<fieldset class='calibration-card'><legend>Clock accuracy calibration</legend><p class='calibration-help'>The first calibration baseline is collected five minutes after boot, followed by filtered checks every two hours. Completed rate estimates are retained across resets, and a qualifying pending correction is applied on the next boot.</p><div class='calibration-grid'><div><span>Active compensation</span><strong id='driftStored'>"));
    web.sendContent(driftStoredText());
    web.sendContent_P(PSTR("</strong></div><div><span>Current estimate</span><strong id='driftEstimate'>"));
    web.sendContent(driftEstimateText());
    web.sendContent_P(PSTR("</strong></div><div><span>Latest NTP check</span><strong id='driftLatest'>"));
    web.sendContent(driftLatestCheckText());
    web.sendContent_P(PSTR("</strong></div><div><span>Calibration samples</span><strong id='driftSamples'>"));
    web.sendContent(driftSampleText());
    web.sendContent_P(PSTR("</strong></div></div></fieldset><fieldset><legend>Home Assistant / API integration</legend><p class='api-help'>Send a scrolling message with:<br><code>POST http://"));
    web.sendContent(ipAddress);
    web.sendContent_P(PSTR("/api/message</code><br><code>Content-Type: application/x-www-form-urlencoded</code><br><code>message=MatrixClock " MATRIXCLOCK_FIRMWARE_VERSION "</code><br><code>&amp;scrolls=1-5</code><small>Use the form below for a quick test. Home Assistant can call the same address with a REST command. Add an optional scrolls value to override this clock's saved setting for one message. Messages are limited to 64 printable characters and are unavailable while the chronograph is open.</small></p><form action='/api/message' method='POST' onsubmit=\"event.preventDefault();let f=this,b=document.getElementById('sendMessageButton'),s=document.getElementById('sendMessageStatus');b.disabled=true;s.textContent='Sending...';fetch(f.action,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(new FormData(f))}).then(r=>r.json()).then(x=>{s.textContent=x.accepted?'Message sent':'Message rejected';f.reset()}).catch(()=>{s.textContent='Unable to contact clock'}).finally(()=>{b.disabled=false})\"><div class='api-row'><label>Send message to clock <input name='message' maxlength='64' required placeholder='MatrixClock v3.0.1'></label><button id='sendMessageButton' type='submit'>Send</button></div></form><button type='button' class='api-cancel' onclick=\"this.disabled=true;fetch('/api/cancel',{method:'POST'}).then(()=>{document.getElementById('sendMessageStatus').textContent='Cancellation requested'}).catch(()=>{document.getElementById('sendMessageStatus').textContent='Unable to contact clock'}).finally(()=>setTimeout(()=>this.disabled=false,1000))\" ondblclick=\"return false\">Cancel message</button><small id='sendMessageStatus' class='api-status' aria-live='polite'></small><div class='api-playback'><div class='api-playback-title'>API message playback</div><label>Number of scrolls<select name='apiscroll' onchange=\"let s=document.getElementById('playbackStatus');fetch('/api/playback',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({apiscroll:this.value})}).then(r=>{if(!r.ok)throw 0;return r.json()}).then(()=>{s.textContent='Setting saved'}).catch(()=>{s.textContent='Unable to save setting'})\">"));
    appendWebOption(pageChunk, "0", "Continuous", settings.apiScrollCount == 0);
    for (int count = 1; count <= 5; count++) {
        char countText[2];
        snprintf(countText, sizeof(countText), "%d", count);
        appendNumericWebOption(pageChunk, count, countText,
                               settings.apiScrollCount == count);
    }
    flushWebChunk(pageChunk);
    web.sendContent_P(PSTR("</select></label><small id='playbackStatus' class='api-status' aria-live='polite'></small></div></fieldset><script>document.addEventListener('DOMContentLoaded',()=>{let i=document.querySelector('input[name=message]');if(i)i.removeAttribute('required')});document.addEventListener('submit',e=>{let f=e.target,i=f.querySelector&&f.querySelector('input[name=message]');if(i&&!i.value.trim())i.value='MatrixClock " MATRIXCLOCK_FIRMWARE_VERSION "'},true)</script>"));
    web.sendContent_P(PSTR("<script>document.addEventListener('DOMContentLoaded',()=>{let i=document.querySelector('input[name=message]');if(i){let d='MatrixClock " MATRIXCLOCK_FIRMWARE_VERSION "';i.value=i.defaultValue='';i.placeholder=d}})</script>"));
    sendDeviceInfoMarkup();
    web.sendContent_P(PSTR("<script>(()=>{let timers={};new MutationObserver(m=>m.forEach(x=>{let e=x.target;if(!e.id||!e.classList.contains('api-status'))return;clearTimeout(timers[e.id]);if(e.textContent)timers[e.id]=setTimeout(()=>e.textContent='',2000)})).observe(document.body,{subtree:true,childList:true,characterData:true})})();</script>"));
    web.sendContent_P(PSTR("<button type='button' style='margin-bottom:.8em' onclick=\"openSecurityModal(this)\">Change username and password</button><div id='securityModal' role='dialog' aria-modal='true' aria-labelledby='securityTitle' style='display:none;position:fixed;inset:0;overflow:hidden;background:rgba(0,0,0,.62);z-index:9999'><div class='card' style='position:fixed;top:50%;left:50%;transform:translate(-50%,-50%);margin:0;width:min(460px,calc(100vw - 2em));max-height:calc(100vh - 2em);overflow:auto'><h2 id='securityTitle'>Change username and password</h2><p class='small'>Leave both username and password blank to disable security.</p><form method='POST' action='/security' onsubmit=\"if(this.password.value!==this.confirmation.value){alert('Passwords do not match');return false}return true\"><label>Username<input name='username' maxlength='19' autocomplete='username'></label><label>New password<input name='password' type='password' maxlength='39' autocomplete='new-password'></label><label>Confirm password<input name='confirmation' type='password' maxlength='39' autocomplete='new-password'></label><button type='submit'>Save settings and reboot</button></form><button type='button' style='margin-top:.7em;background:#52627a' onclick=\"closeSecurityModal()\">Cancel</button></div></div><div class='calibration-actions'><form method='POST' action='/calibration/reset'><button type='submit' class='calibration-reset'>Reset drift calibration and reboot</button></form></div><form method='POST' action='/reset' onsubmit=\"return confirm('Erase all saved settings and Wi-Fi credentials, then reboot into setup mode?')\"><button class='danger' type='submit'>Factory reset</button></form><p><a href='/update'>Firmware update</a></p><script>let securityOpener=null;function openSecurityModal(b){let m=document.getElementById('securityModal');securityOpener=b;document.body.dataset.securityScrollY=window.scrollY;document.body.style.overflow='hidden';m.style.display='block';let i=m.querySelector('input[name=username]');if(i)i.focus()}function closeSecurityModal(){let m=document.getElementById('securityModal');m.style.display='none';document.body.style.overflow='';if(securityOpener)securityOpener.focus()}document.addEventListener('keydown',e=>{let m=document.getElementById('securityModal');if(!m||m.style.display==='none')return;if(e.key==='Escape'){e.preventDefault();closeSecurityModal();return}if(e.key==='Tab'){let a=[...m.querySelectorAll('input,button')].filter(x=>!x.disabled);if(!a.length)return;let n=a.indexOf(document.activeElement);if(e.shiftKey&&n<=0){e.preventDefault();a[a.length-1].focus()}else if(!e.shiftKey&&n===a.length-1){e.preventDefault();a[0].focus()}}});async function updateSignal(){let e=document.getElementById('signalStatus');if(!e)return;try{let r=await fetch('/status');let x=await r.json();e.innerHTML=x.quality+' <small>('+x.rssi+' dBm)</small>'}catch(e){}}async function updateDriftCalibration(){try{let r=await fetch('/calibration/status');let x=await r.json();for(let k of ['Stored','Estimate','Latest','Samples']){let e=document.getElementById('drift'+k);if(e)e.textContent=x[k.toLowerCase()]}}catch(e){}}updateSignal();setInterval(updateSignal,5000);updateDriftCalibration();setInterval(updateDriftCalibration,60000);</script></div></body></html>"));
    web.sendContent_P(PSTR("<script>(()=>{const m=document.getElementById('securityModal'),c=m&&m.querySelector('.card');if(!m||!c)return;c.style.top='0';c.style.transform='translateX(-50%)';const show=window.openSecurityModal;window.openSecurityModal=function(button){c.style.top='0';c.style.transform='translateX(-50%)';show(button)};})();</script>"));
    Serial.printf("WEB / complete ms=%lu heap=%u\n",
                  (unsigned long)(millis() - webStartedAt), ESP.getFreeHeap());
}

String escapeHtml(const String& value) {
    String escaped = value;
    escaped.replace("&", "&amp;");
    escaped.replace("<", "&lt;");
    escaped.replace(">", "&gt;");
    escaped.replace("\"", "&quot;");
    escaped.replace("'", "&#39;");
    return escaped;
}

String escapeJson(const String& value) {
    String escaped = value;
    escaped.replace("\\", "\\\\");
    escaped.replace("\"", "\\\"");
    escaped.replace("\n", "\\n");
    escaped.replace("\r", "\\r");
    escaped.replace("\t", "\\t");
    return escaped;
}

// Device-info labels and layout are fixed in the page.  Only their text values
// are refreshed, avoiding a full HTML rebuild and DOM replacement every time.
void sendDeviceInfoMarkup() {
    web.sendContent_P(PSTR("<fieldset><legend>Device info</legend><div class='event-list'><div class='event-heading'><span>NTP status</span><span></span></div><div class='event-row'><span class='event-label'>Server</span><span id='infoServer' class='event-value'>Loading...</span></div><div class='event-row'><span class='event-label'>Resolved IP</span><span id='infoResolvedIp' class='event-value'>Loading...</span></div><div class='event-row'><span class='event-label'>Stratum</span><span id='infoStratum' class='event-value'>Loading...</span></div><div class='event-row'><span class='event-label'>Status</span><span id='infoStatus' class='event-value'>Loading...</span></div><div class='event-row'><span class='event-label'>Response time</span><span id='infoResponseTime' class='event-value'>Loading...</span></div><div class='event-row'><span class='event-label'>Last successful check</span><span id='infoLastSuccess' class='event-value'>Loading...</span></div><div class='event-heading'><span>Chronograph history</span><span>HH:MM:ss:cc</span></div><div class='event-row'><span class='event-label'>Last 1</span><span id='infoLast1' class='event-value'>Loading...</span></div><div class='event-row'><span class='event-label'>Last 2</span><span id='infoLast2' class='event-value'>Loading...</span></div><div class='event-row'><span class='event-label'>Last 3</span><span id='infoLast3' class='event-value'>Loading...</span></div></div></fieldset><script>function setDeviceInfoValue(id,value){let e=document.getElementById(id);if(e&&e.textContent!==value)e.textContent=value}async function updateLog(){try{let r=await fetch('/log',{cache:'no-store'});if(!r.ok)throw 0;let x=await r.json();for(let k in x)setDeviceInfoValue('info'+k[0].toUpperCase()+k.slice(1),x[k])}catch(e){}}updateLog();setInterval(updateLog,1000);</script>"));
    web.sendContent_P(PSTR("<script>(()=>{let last=document.getElementById('infoLastSuccess').closest('.event-row');last.insertAdjacentHTML('afterend',\"<div class='event-row'><span class='event-label'>Next check in</span><span id='infoNextCheck' class='event-value'>Loading...</span></div>\");let history=document.getElementById('infoLast3').closest('.event-row');history.insertAdjacentHTML('afterend',\"<div style='display:flex;align-items:center;justify-content:flex-end;gap:.7em;min-height:2.2em'><span id='historyResetStatus' style='color:#9eb1ce;font:12px ui-monospace,monospace'></span><button id='historyResetButton' type='button' style='width:auto;min-height:2.2em;padding:.35em .7em;font-size:.82em' onclick='resetStopwatchHistory()'>Reset history</button></div>\")})();async function resetStopwatchHistory(){let b=document.getElementById('historyResetButton'),s=document.getElementById('historyResetStatus');b.disabled=true;try{let r=await fetch('/history/reset',{method:'POST',cache:'no-store'});if(!r.ok)throw 0;let x=await r.json();setDeviceInfoValue('infoLast1',x.last1);setDeviceInfoValue('infoLast2',x.last2);setDeviceInfoValue('infoLast3',x.last3);s.textContent='History cleared';setTimeout(()=>s.textContent='',2000)}catch(e){s.textContent='Unable to clear'}setTimeout(()=>b.disabled=false,300)}</script>"));
}

String ntpServerDisplayText() {
    String server = settings.ntpServer;
    if (server == "time.cloudflare.com")
        return "Cloudflare Global (time.cloudflare.com)";
    if (server == "ntp1.npl.co.uk")
        return "NPL UK (ntp1.npl.co.uk)";
    if (server == "pool.ntp.org")
        return "NTP Pool Global (pool.ntp.org)";
    if (server == "ntp.se")
        return "Netnod Sweden/Europe (ntp.se)";
    if (server == "ntp.nict.jp")
        return "NICT Japan/Asia (ntp.nict.jp)";
    if (server == "time.nist.gov")
        return "NIST United States (time.nist.gov)";
    if (server == "uk.pool.ntp.org")
        return "NTP Pool UK (uk.pool.ntp.org)";
    if (server == "europe.pool.ntp.org")
        return "NTP Pool Europe (europe.pool.ntp.org)";
    return server.length() ? server : "Not configured";
}

String ntpStratumText() {
    if (!lastNtpMetadataValid)
        return "Awaiting valid reply";
    if (lastNtpStratum == 1)
        return "1 (primary source)";
    return String(lastNtpStratum);
}

String ntpReplyStatusText() {
    if (lastNtpCheckResult == NTP_CHECK_NO_STABLE_REPLY)
        return "Check failed - no stable reply";
    if (lastNtpCheckResult == NTP_CHECK_INCONSISTENT_REPLIES)
        return "Check failed - inconsistent replies";
    if (lastNtpCheckResult == NTP_CHECK_NO_VALID_REPLY)
        return "Check failed - no valid reply";
    if (!lastNtpMetadataValid)
        return "Awaiting valid reply";
    if (lastNtpLeapIndicator == 1)
        return "Synchronised (+1 leap second pending)";
    if (lastNtpLeapIndicator == 2)
        return "Synchronised (-1 leap second pending)";
    return "Synchronised";
}

String ntpResponseTimeText() {
    if (lastNtpCheckRttUs == 0)
        return "Awaiting valid reply";
    char text[16];
    snprintf(text, sizeof(text), "%lu ms",
             (unsigned long)(lastNtpCheckRttUs / 1000UL));
    return String(text);
}

String ntpLastSuccessText() {
    if (!lastNtpMetadataValid || lastNtpSuccessEpoch == 0)
        return "Awaiting valid reply";
    time_t checkTime = (time_t)lastNtpSuccessEpoch;
    tm* localCheck = localtime(&checkTime);
    if (!localCheck)
        return "Unavailable";
    char text[12];
    snprintf(text, sizeof(text), "%02d:%02d:%02d",
             localCheck->tm_hour, localCheck->tm_min, localCheck->tm_sec);
    return String(text);
}

String ntpNextCheckText() {
    if (!softwareClockValid || nextDriftCheckAt == 0)
        return "Awaiting time sync";

    int32_t remainingMs = (int32_t)(nextDriftCheckAt - millis());
    if (remainingMs <= 0)
        return "Waiting for clock mode...";

    // Round upward to the current second, so the countdown never promises a
    // check earlier than the scheduler will actually make it.
    uint32_t remainingSeconds = (uint32_t)((remainingMs + 999L) / 1000L);
    uint32_t hours = remainingSeconds / 3600UL;
    uint32_t minutes = (remainingSeconds / 60UL) % 60UL;
    uint32_t seconds = remainingSeconds % 60UL;
    char text[16];
    snprintf(text, sizeof(text), "%02lu:%02lu:%02lu",
             (unsigned long)hours, (unsigned long)minutes,
             (unsigned long)seconds);
    return String(text);
}

void handleWebLog() {
    if (!requireWebAuth()) return;
    String payload;
    payload.reserve(640);
    payload += '{';
    auto appendValue = [&payload](const char* key, const String& value, bool first) {
        if (!first) payload += ',';
        payload += '\"';
        payload += key;
        payload += F("\":\"");
        payload += escapeJson(value);
        payload += '\"';
    };
    appendValue("server", ntpServerDisplayText(), true);
    appendValue("resolvedIp", lastNtpMetadataValid
                ? lastNtpServerAddress.toString() : String("Awaiting valid reply"), false);
    appendValue("stratum", ntpStratumText(), false);
    appendValue("status", ntpReplyStatusText(), false);
    appendValue("responseTime", ntpResponseTimeText(), false);
    appendValue("lastSuccess", ntpLastSuccessText(), false);
    appendValue("nextCheck", ntpNextCheckText(), false);
    appendValue("last1", chronographHistory[0], false);
    appendValue("last2", chronographHistory[1], false);
    appendValue("last3", chronographHistory[2], false);
    payload += '}';
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, "application/json", payload);
}

void handleStopwatchHistoryReset() {
    if (!requireWebAuth()) return;
    for (uint8_t i = 0; i < 3; i++)
        chronographHistory[i] = "00:00:00:00";
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, "application/json",
             "{\"last1\":\"00:00:00:00\",\"last2\":\"00:00:00:00\",\"last3\":\"00:00:00:00\"}");
}

void handleApiMessage() {
    if (!requireWebAuth()) return;
    if (apiBlockedByChronograph) {
        web.send(409, "application/json", "{\"error\":\"messages disabled in stopwatch mode\"}");
        return;
    }
    if (apiMessageActive) {
        web.send(409, "application/json", "{\"error\":\"a message is already playing\"}");
        return;
    }
    if (!web.hasArg("message")) {
        web.send(400, "application/json", "{\"error\":\"missing message\"}");
        return;
    }
    String message = web.arg("message");
    message.trim();
    if (message.length() == 0)
        message = String("MatrixClock ") + MATRIXCLOCK_FIRMWARE_VERSION;
    if (message.length() > 64) {
        web.send(400, "application/json", "{\"error\":\"message must be no more than 64 characters\"}");
        return;
    }
    for (size_t i = 0; i < message.length(); i++) {
        if ((unsigned char)message[i] < 32 || (unsigned char)message[i] > 126) {
            web.send(400, "application/json", "{\"error\":\"message contains unsupported characters\"}");
            return;
        }
    }
    uint8_t requestedScrollCount = settings.apiScrollCount;
    if (web.hasArg("scrolls")) {
        String scrollsText = web.arg("scrolls");
        scrollsText.trim();
        bool validScrollCount = scrollsText.length() == 1 &&
                                scrollsText[0] >= '1' && scrollsText[0] <= '5';
        if (validScrollCount)
            requestedScrollCount = (uint8_t)(scrollsText[0] - '0');
    }
    // Keep only the newest pending notification.  This prevents stale
    // messages from being shown after a newer one has arrived.
    apiMessageQueueCount = 0;
    apiMessageQueueHead = apiMessageQueueTail = 0;
    apiMessageQueue[apiMessageQueueTail] = message;
    apiMessageQueueTail = (apiMessageQueueTail + 1) % API_MESSAGE_QUEUE_SIZE;
    apiMessageQueueCount++;
    apiMessageScrollCount = requestedScrollCount;
    apiMessagePending = true;
    web.sendHeader("Cache-Control", "no-store");
    web.send(202, "application/json", String("{\"accepted\":true,\"scrolls\":") +
             String(apiMessageScrollCount) + "}");
}

void handleApiCancel() {
    if (!requireWebAuth()) return;
    apiMessagePending = false;
    apiMessageQueueCount = 0;
    apiMessageQueueHead = apiMessageQueueTail = 0;
    if (apiMessageActive) {
        apiCancelAfterCurrent = true;
        apiCancelDeadline = millis() + 1000UL;
    }
    web.sendHeader("Cache-Control", "no-store");
    web.send(202, "application/json", "{\"accepted\":true}");
}

void handleApiPlaybackSave() {
    if (!requireWebAuth()) return;
    if (!web.hasArg("apiscroll")) {
        web.send(400, "application/json", "{\"error\":\"missing scroll count\"}");
        return;
    }
    settings.apiScrollCount = constrain(web.arg("apiscroll").toInt(), 0, 5);
    EEPROM.put(0, settings);
    EEPROM.commit();
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, "application/json", "{\"saved\":true}");
}

void handleStatus() {
    if (!requireWebAuth()) return;
    int rssi = WiFi.RSSI();
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, "application/json", String("{\"quality\":\"") + signalQuality(rssi) +
             "\",\"rssi\":" + String(rssi) + "}");
}

void handleDriftCalibrationStatus() {
    if (!requireWebAuth()) return;
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, "application/json", String("{\"stored\":\"") + driftStoredText() +
             "\",\"estimate\":\"" + driftEstimateText() +
             "\",\"latest\":\"" + driftLatestCheckText() +
             "\",\"samples\":\"" + driftSampleText() + "\"}");
}

bool otaUploadStarted = false;
bool otaUploadFailed = false;
String otaUploadError;

void handleOtaPage() {
    if (!requireWebAuth()) return;
    String html = "<!doctype html><html><head><meta name='viewport' content='width=device-width'>"
                  "<link rel='icon' href='/favicon.ico'><meta http-equiv='Cache-Control' content='no-store'>"
                  "<title>Firmware update - Matrix Clock</title><style>"
                  "*{box-sizing:border-box}html,body{min-height:100%;margin:0}body{font:16px -apple-system,BlinkMacSystemFont,Segoe UI,sans-serif;"
                  "min-height:100vh;width:100%;padding:2em 1em;display:grid;place-items:center;background-color:#090e18;background-image:radial-gradient(circle at 20% 0%,#243653 0%,#101827 48%,#090e18 100%);background-repeat:no-repeat;background-size:cover;background-attachment:fixed;color:#eef4ff}"
                  ".card{width:min(620px,100%);padding:1.5em;margin:0;border:1px solid rgba(180,210,255,.2);border-radius:22px;"
                  "background:rgba(24,37,59,.64);box-shadow:0 16px 40px rgba(0,0,0,.32);backdrop-filter:blur(18px)}"
                  "h1{margin-top:0;color:#f7faff}p{line-height:1.5;color:#dbe7fb}.notice{padding:.8em 1em;"
                  "border-radius:12px;background:rgba(64,139,255,.16);border:1px solid rgba(120,180,255,.28)}"
                  "input{width:100%;min-height:2.6em;padding:.6em .7em;border:1px solid rgba(180,210,255,.25);border-radius:10px;"
                  "background:rgba(7,14,26,.62);color:#f1f6ff}button{width:100%;min-height:2.6em;margin-top:1em;padding:.6em .7em;"
                  "font:inherit;font-weight:600;color:white;border:0;border-radius:12px;background:#1677ff;transition:transform .08s ease,filter .08s ease,box-shadow .08s ease;box-shadow:0 5px 14px rgba(22,119,255,.25)}button:not(:disabled):active{transform:translateY(2px) scale(.99);filter:brightness(.82);box-shadow:0 1px 4px rgba(0,0,0,.3)}"
                  "a,a:visited,a:hover,a:active{color:#1677ff}.small{color:#aebfda;line-height:1.5}</style></head><body><main class='card'><h1>Firmware update</h1>"
                  "<p class='notice'>Current firmware: <strong>" MATRIXCLOCK_FIRMWARE_VERSION "</strong></p>"
                  "<p class='small'>Check GitHub for a newer release, download its OTA firmware file, then return here to upload it. Do not use the Factory 4 MB image on this page.</p>"
                  "<p><a href='https://github.com/maddenste/MatrixClock-Improved/releases' target='_blank' rel='noopener'>Check for updates on GitHub</a></p>"
                  "<p class='notice'>Select OTA firmware file</p>"
                  "<form method='POST' action='/update' enctype='multipart/form-data' onsubmit=\"this.querySelector('button').disabled=true\">"
                  "<input type='file' name='firmware' accept='.bin,application/octet-stream' required>"
                  "<button type='submit'>Upload and reboot</button></form><p><a href='/'>Return to clock</a></p>"
                  "</main></body></html>";
    web.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0", true);
    web.send(200, "text/html", html);
}

void handleOtaUpload() {
    if (!requireWebAuth()) {
        otaUploadFailed = true;
        otaUploadError = "Authentication required.";
        return;
    }
    HTTPUpload& upload = web.upload();
    if (upload.status == UPLOAD_FILE_START) {
        otaUploadStarted = false;
        otaUploadFailed = false;
        otaUploadError = "";
        String filename = upload.filename;
        filename.toLowerCase();
        if (!filename.endsWith(".bin")) {
            otaUploadFailed = true;
            otaUploadError = "Only .bin firmware files are accepted.";
            return;
        }
        size_t maxSketch = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        if (!Update.begin(maxSketch)) {
            otaUploadFailed = true;
            otaUploadError = "Not enough OTA space for this firmware image.";
            return;
        }
        // Give immediate visual feedback once the image is accepted and
        // before the first firmware block is written.
        showStaticStatus("WRITE");
        otaUploadStarted = true;
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (otaUploadStarted && !otaUploadFailed) {
            size_t written = Update.write(upload.buf, upload.currentSize);
            if (written != upload.currentSize) {
                otaUploadFailed = true;
                otaUploadError = "Firmware write failed.";
            }
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (otaUploadStarted && !otaUploadFailed && !Update.end(true)) {
            otaUploadFailed = true;
            otaUploadError = "Firmware validation failed.";
        }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        otaUploadStarted = false;
        otaUploadFailed = true;
        otaUploadError = "Firmware upload was aborted.";
    }
}

void handleOtaComplete() {
    if (!requireWebAuth()) return;
    web.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0", true);
    web.sendHeader("Pragma", "no-cache");
    web.sendHeader("Expires", "0");
    if (otaUploadStarted && !otaUploadFailed) {
        web.send(200, "text/html", "<!doctype html><html><head><meta name='viewport' content='width=device-width'><link rel='icon' href='/favicon.ico'><meta http-equiv='Cache-Control' content='no-store'><title>Firmware update - Matrix Clock</title><style>*{box-sizing:border-box}html,body{min-height:100%;margin:0}body{font:16px -apple-system,BlinkMacSystemFont,Segoe UI,sans-serif;min-height:100vh;width:100%;padding:2em 1em;display:grid;place-items:center;background-color:#090e18;background-image:radial-gradient(circle at 20% 0%,#243653 0%,#101827 48%,#090e18 100%);background-repeat:no-repeat;background-size:cover;background-attachment:fixed;color:#eef4ff}.card{width:min(620px,100%);padding:1.5em;border:1px solid rgba(180,210,255,.2);border-radius:22px;background:rgba(24,37,59,.64);box-shadow:0 16px 40px rgba(0,0,0,.32);backdrop-filter:blur(18px)}h1{margin-top:0;color:#f7faff}.status{padding:.85em 1em;border-radius:12px;background:rgba(64,139,255,.16);border:1px solid rgba(120,180,255,.28);color:#dceaff;line-height:1.5}</style></head><body><main class='card'><h1>Firmware updated successfully</h1><p class='status'>The clock is rebooting...</p></main></body></html>");
        delay(500);
        ESP.restart();
    } else {
        String message = otaUploadError.length() ? otaUploadError : "Firmware upload failed.";
        web.send(400, "text/html", String("<!doctype html><html><head><meta name='viewport' content='width=device-width'><title>Update failed</title><style>a,a:visited,a:hover,a:active{color:#1677ff}</style></head><body><h1>Update failed</h1><p>") + message + "</p><p><a href='/update'>Try again</a></p></body></html>");
    }
    otaUploadStarted = false;
}

void sendRebootingPage() {
    web.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0", true);
    web.sendHeader("Pragma", "no-cache");
    web.sendHeader("Expires", "0");
    web.send(200, "text/html", "<!doctype html><html><head><meta name='viewport' content='width=device-width'><link rel='icon' href='/favicon.ico'><title>Matrix Clock</title><style>*{box-sizing:border-box}body{font:16px -apple-system,BlinkMacSystemFont,Segoe UI,sans-serif;min-height:100vh;margin:0;display:grid;place-items:center;padding:1.2em;background:radial-gradient(circle at 20% 0%,#243653 0%,#101827 48%,#090e18 100%);color:#eef4ff}.card{width:min(560px,100%);padding:2em;border:1px solid rgba(180,210,255,.2);border-radius:22px;background:rgba(24,37,59,.64);box-shadow:0 16px 40px rgba(0,0,0,.32);backdrop-filter:blur(18px)}h1{margin:0 0 .45em;font-weight:650;letter-spacing:-.03em;color:#f7faff}.status{padding:.8em 1em;border-radius:12px;background:rgba(64,139,255,.16);border:1px solid rgba(120,180,255,.28);color:#dceaff}</style></head><body><main class='card'><h1>Please wait</h1><p class='status'>The clock is rebooting...</p></main></body></html>");
}

void handleDriftCalibrationReset() {
    if (!requireWebAuth()) return;
    resetDriftCalibration(true);
    sendRebootingPage();
    delay(250);
    ESP.restart();
}

void handleWebSave() {
    if (web.hasArg("ssid_manual") && web.arg("ssid_manual").length() > 0)
        strlcpy(settings.ssid, web.arg("ssid_manual").c_str(), sizeof(settings.ssid));
    else if (web.hasArg("ssid") && web.arg("ssid") != "__manual__")
        strlcpy(settings.ssid, web.arg("ssid").c_str(), sizeof(settings.ssid));
    if (web.hasArg("password")) strlcpy(settings.password, web.arg("password").c_str(), sizeof(settings.password));
    if (web.hasArg("ntp")) {
        String ntp = web.arg("ntp");
        if (ntp == "custom" && web.hasArg("customntp")) ntp = web.arg("customntp");
        ntp.trim();
        if (ntp.length() > 0)
            strlcpy(settings.ntpServer, ntp.c_str(), sizeof(settings.ntpServer));
    }
    if (web.hasArg("timezone")) strlcpy(settings.timezone, web.arg("timezone").c_str(), sizeof(settings.timezone));
    if (web.hasArg("customtz")) strlcpy(settings.customTimezone, web.arg("customtz").c_str(), sizeof(settings.customTimezone));
    if (web.hasArg("restarthour"))
        settings.restartHour = constrain(web.arg("restarthour").toInt(), 0, 23);
    if (web.hasArg("restartminute"))
        settings.restartMinute = constrain(web.arg("restartminute").toInt(), 0, 55);
    if (web.hasArg("format")) settings.timeFormat = web.arg("format") == "12" ? 12 : 24;
    if (web.hasArg("brightness")) settings.brightness = constrain(web.arg("brightness").toInt(), 1, 5);
    if (web.hasArg("scroll")) settings.scrolling = constrain(web.arg("scroll").toInt(), 0, 3);
    if (web.hasArg("chronomode")) settings.chronographDisplayMode = constrain(web.arg("chronomode").toInt(), 0, 1);
    if (web.hasArg("colonblink")) settings.clockColonBlink = web.arg("colonblink") == "1" ? 1 : 0;
    if (web.hasArg("scrollspeed")) settings.scrollSpeed = constrain(web.arg("scrollspeed").toInt(), 0, 1);
    if (web.hasArg("transition")) settings.cleanTransitions = web.arg("transition") == "0";
    if (web.hasArg("offset"))
        settings.utcOffset = constrain(web.arg("offset").toInt(), -660, 660);
    if (web.hasArg("dst")) settings.daylightSaving = web.arg("dst").toInt();
    if (web.hasArg("apiscroll")) settings.apiScrollCount = constrain(web.arg("apiscroll").toInt(), 0, 5);
    commitDriftCalibrationIfReady();
    EEPROM.put(0, settings);
    EEPROM.commit();
    sendRebootingPage();
    delay(250);
    ESP.restart();
}

void eraseAllClockState() {
    EEPROM.begin(512);
    for (int i = 0; i < 512; i++)
        EEPROM.write(i, 0xFF);
    EEPROM.commit();
    clearDriftSessionCheckpoint();
    WiFi.disconnect(true); // erase SDK-stored station credentials too
    rtc_resetToBaseline(); // also clear the saved date/time in the DS3231
}

void handleFactoryReset() {
    eraseAllClockState();
    sendRebootingPage();
    delay(500);
    ESP.restart();
}

void handleSecuritySave() {
    if (!requireWebAuth()) return;
    String username = web.arg("username");
    String password = web.arg("password");
    String confirmation = web.arg("confirmation");
    if (username.length() >= sizeof(authSettings.username) ||
        password.length() >= sizeof(authSettings.password) ||
        confirmation.length() >= sizeof(authSettings.password) ||
        ((username.length() == 0) != (password.length() == 0))) {
        web.send(400, "text/plain", "Provide both fields, or leave both blank to disable protection.");
        return;
    }
    if (password != confirmation) {
        web.send(400, "text/plain", "Passwords do not match.");
        return;
    }
    memset(authSettings.username, 0, sizeof(authSettings.username));
    memset(authSettings.password, 0, sizeof(authSettings.password));
    strlcpy(authSettings.username, username.c_str(), sizeof(authSettings.username));
    strlcpy(authSettings.password, password.c_str(), sizeof(authSettings.password));
    authSettings.initialized = 1;
    saveAuthSettings();
    sendRebootingPage();
    delay(250);
    ESP.restart();
}

// GPIO0 is also the ESP8266 boot-strap pin.  After a confirmed physical
// factory reset, wait for release before rebooting or the clock would enter
// the UART bootloader instead of starting the firmware.
bool physicalFactoryHoldActive = false;
bool physicalFactoryWarningActive = false;
uint32_t physicalFactoryHoldStarted = 0;
int8_t physicalFactoryWarningPhase = -1;

bool servicePhysicalFactoryReset() {
    const bool pressed = digitalRead(0) == LOW;

    // Keep OTA writing protected from a destructive interruption.  The raw
    // factory-reset hold remains active in clock and chronograph modes.
    if (otaUploadStarted) {
        physicalFactoryHoldActive = false;
        physicalFactoryWarningActive = false;
        physicalFactoryWarningPhase = -1;
        return false;
    }

    uint32_t now = millis();
    if (!pressed) {
        // If release lands exactly on the threshold between input samples,
        // honour the completed twelve-and-a-half-second hold. GPIO0 is already high, so a
        // safe normal reboot can follow immediately.
        if (physicalFactoryHoldActive &&
            (uint32_t)(now - physicalFactoryHoldStarted) >= 12500UL) {
            showStaticStatus("RESET");
            eraseAllClockState();
            clear_Display();
            refresh_display();
            delay(50);
            ESP.restart();
            return true;
        }
        bool warningCancelled = false;
        if (physicalFactoryHoldActive && physicalFactoryWarningActive) {
            // Releasing during the warning cancels and consumes this press so
            // it cannot also trigger a date scroll or chronograph entry.
            warningCancelled = true;
            clear_Display();
            refresh_display();
        }
        physicalFactoryHoldActive = false;
        physicalFactoryWarningActive = false;
        physicalFactoryWarningPhase = -1;
        return warningCancelled;
    }

    if (!physicalFactoryHoldActive) {
        physicalFactoryHoldActive = true;
        physicalFactoryHoldStarted = now;
        physicalFactoryWarningActive = false;
        physicalFactoryWarningPhase = -1;
    }

    uint32_t heldMs = now - physicalFactoryHoldStarted;
    if (heldMs < 8000UL)
        return false;

    physicalFactoryWarningActive = true;
    int8_t phase;
    // Four half-second phases, a final half-second blank pause, then eight
    // quarter-second phases. Alternating blank/RESET doubles the rapid flashes.
    if (heldMs < 10000UL)
        phase = (int8_t)((heldMs - 8000UL) / 500UL);       // phases 0..3
    else if (heldMs < 10500UL)
        phase = 4;                                         // extra blank pause
    else {
        uint32_t fastPhase = (heldMs - 10500UL) / 250UL;
        if (fastPhase > 7UL)
            fastPhase = 7UL;
        phase = (int8_t)(5 + fastPhase);                   // phases 5..12
    }
    // The rapid section starts and ends visibly on RESET.  The final phase is
    // held on so the warning does not finish on a blank display.
    bool showReset = phase < 5 ? ((phase & 1) != 0)
                               : (((phase - 5) & 1) == 0 || phase == 12);

    if (phase != physicalFactoryWarningPhase) {
        physicalFactoryWarningPhase = phase;
        if (showReset)
            showStaticStatus("RESET");
        else {
            clear_Display();
            refresh_display();
        }
    }

    if (heldMs >= 12500UL) {
        // Confirm and erase at twelve-and-a-half seconds, then hold RESET visibly until the
        // boot-strap button has been released safely.
        showStaticStatus("RESET");
        eraseAllClockState();
        while (digitalRead(0) == LOW) {
            delay(5);
            yield();
        }
        clear_Display();
        refresh_display();
        delay(50);
        ESP.restart();
        return true;
    }
    return true;
}

void startWebInterface() {
    // Serve the clock icon at the conventional path so every page can use it,
    // including the static save/reset confirmation pages.
    web.on("/favicon.ico", HTTP_GET, []() {
        web.send(200, "image/svg+xml", "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'><circle cx='32' cy='32' r='28' fill='#1677ff'/><path d='M32 16v18l12 8' stroke='white' stroke-width='5' fill='none' stroke-linecap='round'/></svg>");
    });
    // ESP8266WebServer always collects Authorization itself; Cookie is the
    // one additional request header needed for browser sessions.
    web.collectHeaders("Cookie");
    web.on("/login", HTTP_ANY, handleLogin);
    web.on("/", HTTP_GET, handleWebRoot);
    web.on("/update", HTTP_GET, handleOtaPage);
    web.on("/update", HTTP_POST, handleOtaComplete, handleOtaUpload);
    web.on("/log", HTTP_GET, handleWebLog);
    web.on("/history/reset", HTTP_POST, handleStopwatchHistoryReset);
    web.on("/api/message", HTTP_POST, handleApiMessage);
    web.on("/api/cancel", HTTP_POST, handleApiCancel);
    web.on("/api/playback", HTTP_POST, handleApiPlaybackSave);
    web.on("/status", HTTP_GET, handleStatus);
    web.on("/calibration/status", HTTP_GET, handleDriftCalibrationStatus);
    web.on("/calibration/reset", HTTP_POST, handleDriftCalibrationReset);
    web.on("/security", HTTP_POST, handleSecuritySave);
    web.on("/save", HTTP_POST, []() { if (requireWebAuth()) handleWebSave(); });
    web.on("/reset", HTTP_POST, []() { if (requireWebAuth()) handleFactoryReset(); });
    web.onNotFound([]() { if (requireWebAuth()) web.send(404, "text/plain", "Not found"); });
    web.begin();
    Serial.println("Web interface started (OTA disabled)");
}

void startProvisioningAP() {
    provisioningMode = true;
    setupMessageX = -((int)(sizeof(setupMessage) - 1) * 6);
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("MatrixClock");
    // Scan asynchronously so loading the setup page never pauses the display.
    WiFi.scanNetworks(true);
    clear_Display();
    refresh_display();
    Serial.print("Setup AP started: MatrixClock at ");
    Serial.println(WiFi.softAPIP());
}


//months
char M_arr[12][5] = { { ' ', 'J', 'A', 'N', ' ' }, { ' ', 'F', 'E', 'B', ' ' },
        { ' ', 'M', 'A', 'R', ' ' }, { ' ', 'A', 'P', 'R', ' ' }, { ' ', 'M', 'A',
                'Y', ' ' }, { ' ', 'J', 'U', 'N', ' ' }, { ' ', 'J', 'U', 'L', ' ' }, {
                ' ', 'A', 'U', 'G', ' ' }, { ' ', 'S', 'E', 'P', ' ' }, { ' ', 'O', 'C',
                'T', ' ' }, { ' ', 'N', 'O', 'V', ' ' }, { ' ', 'D', 'E', 'C', ' ' } };
//days
char WT_arr[7][4] = { { 'S', 'U', 'N', ' ' }, { 'M', 'O', 'N', ' ' }, { 'T', 'U', 'E', ' ' }, {
        'W', 'E', 'D', ' ' }, { 'T', 'H', 'U', ' ' }, { 'F', 'R', 'I', ' ' }, { 'S', 'A', 'T', ' ' } };

// Zeichensatz 5x8 in einer 8x8 Matrix, 0,0 ist rechts oben
unsigned short const font1[96][9] = { { 0x07, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00 },   // 0x20, Space
        { 0x07, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04, 0x00 },   // 0x21, !
        { 0x07, 0x09, 0x09, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00 },   // 0x22, "
        { 0x07, 0x0a, 0x0a, 0x1f, 0x0a, 0x1f, 0x0a, 0x0a, 0x00 },   // 0x23, #
        { 0x07, 0x04, 0x0f, 0x14, 0x0e, 0x05, 0x1e, 0x04, 0x00 },   // 0x24, $
        { 0x07, 0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13, 0x00 },   // 0x25, %
        { 0x07, 0x04, 0x0a, 0x0a, 0x0a, 0x15, 0x12, 0x0d, 0x00 },   // 0x26, &
        { 0x07, 0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00 },   // 0x27, '
        { 0x07, 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02, 0x00 },   // 0x28, (
        { 0x07, 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08, 0x00 },   // 0x29, )
        { 0x07, 0x04, 0x15, 0x0e, 0x1f, 0x0e, 0x15, 0x04, 0x00 },   // 0x2a, *
        { 0x07, 0x00, 0x04, 0x04, 0x1f, 0x04, 0x04, 0x00, 0x00 },   // 0x2b, +
        { 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x02 },   // 0x2c, ,
        { 0x07, 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00, 0x00 },   // 0x2d, -
        { 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x03, 0x00 },   // 0x2e, .
        { 0x07, 0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10, 0x00 },   // 0x2f, /
        { 0x07, 0x0F, 0x09, 0x09, 0x09, 0x09, 0x09, 0x0F, 0x00 },   // 0x30, 0
        { 0x07, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x00 },   // 0x31, 1
        { 0x07, 0x0F, 0x01, 0x01, 0x0F, 0x08, 0x08, 0x0F, 0x00 },   // 0x32, 2
        { 0x07, 0x0F, 0x01, 0x01, 0x0F, 0x01, 0x01, 0x0F, 0x00 },   // 0x33, 3
        { 0x07, 0x09, 0x09, 0x09, 0x0F, 0x01, 0x01, 0x01, 0x00 },   // 0x34, 4
        { 0x07, 0x0F, 0x08, 0x08, 0x0F, 0x01, 0x01, 0x0F, 0x00 },   // 0x35, 5
        { 0x07, 0x0F, 0x08, 0x08, 0x0F, 0x09, 0x09, 0x0F, 0x00 },   // 0x36, 6
        { 0x07, 0x0F, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00 },   // 0x37, 7
        { 0x07, 0x0F, 0x09, 0x09, 0x0F, 0x09, 0x09, 0x0F, 0x00 },   // 0x38, 8
        { 0x07, 0x0F, 0x09, 0x09, 0x0F, 0x01, 0x01, 0x0F, 0x00 },   // 0x39, 9
        { 0x04, 0x00, 0x01, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00 },   // 0x3a, :
        { 0x07, 0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x04, 0x08, 0x00 },   // 0x3b, ;
        { 0x07, 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02, 0x00 },   // 0x3c, <
        { 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   // 0x3d, =
        { 0x07, 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08, 0x00 },   // 0x3e, >
        { 0x07, 0x0e, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04, 0x00 },   // 0x3f, ?
        { 0x07, 0x0e, 0x11, 0x17, 0x15, 0x17, 0x10, 0x0f, 0x00 },   // 0x40, @
        { 0x07, 0x04, 0x0a, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x00 },   // 0x41, A
        { 0x07, 0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e, 0x00 },   // 0x42, B
        { 0x07, 0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e, 0x00 },   // 0x43, C
        { 0x07, 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E, 0x00 },   // 0x44, D
        { 0x07, 0x1f, 0x10, 0x10, 0x1c, 0x10, 0x10, 0x1f, 0x00 },   // 0x45, E
        { 0x07, 0x1f, 0x10, 0x10, 0x1f, 0x10, 0x10, 0x10, 0x00 },   // 0x46, F
        { 0x07, 0x0e, 0x11, 0x10, 0x10, 0x13, 0x11, 0x0f, 0x00 },   // 0x37, G
        { 0x07, 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11, 0x00 },   // 0x48, H
        { 0x07, 0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e, 0x00 },   // 0x49, I
        { 0x07, 0x1f, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0c, 0x00 },   // 0x4a, J
        { 0x07, 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11, 0x00 },   // 0x4b, K
        { 0x07, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f, 0x00 },   // 0x4c, L
        { 0x07, 0x11, 0x1b, 0x15, 0x11, 0x11, 0x11, 0x11, 0x00 },   // 0x4d, M
        { 0x07, 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x00 },   // 0x4e, N
        { 0x07, 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e, 0x00 },   // 0x4f, O
        { 0x07, 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10, 0x00 },   // 0x50, P
        { 0x07, 0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d, 0x00 },   // 0x51, Q
        { 0x07, 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11, 0x00 },   // 0x52, R
        { 0x07, 0x0e, 0x11, 0x10, 0x0e, 0x01, 0x11, 0x0e, 0x00 },   // 0x53, S
        { 0x07, 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00 },   // 0x54, T
        { 0x07, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e, 0x00 },   // 0x55, U
        { 0x07, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04, 0x00 },   // 0x56, V
        { 0x07, 0x11, 0x11, 0x11, 0x15, 0x15, 0x1b, 0x11, 0x00 },   // 0x57, W
        { 0x07, 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11, 0x00 },   // 0x58, X
        { 0x07, 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04, 0x00 },   // 0x59, Y
        { 0x07, 0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f, 0x00 },   // 0x5a, Z
        { 0x07, 0x0e, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0e, 0x00 },   // 0x5b, [
        { 0x07, 0x10, 0x10, 0x08, 0x04, 0x02, 0x01, 0x01, 0x00 },   // 0x5c, '\'
        { 0x07, 0x0e, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0e, 0x00 },   // 0x5d, ]
        { 0x07, 0x04, 0x0a, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00 },   // 0x5e, ^
        { 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0x00 },   // 0x5f, _
        { 0x07, 0x04, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00 },   // 0x60, `
        { 0x07, 0x00, 0x0e, 0x01, 0x0d, 0x13, 0x13, 0x0d, 0x00 },   // 0x61, a
        { 0x07, 0x10, 0x10, 0x10, 0x1c, 0x12, 0x12, 0x1c, 0x00 },   // 0x62, b
        { 0x07, 0x00, 0x00, 0x0E, 0x10, 0x10, 0x10, 0x0E, 0x00 },   // 0x63, c
        { 0x07, 0x01, 0x01, 0x01, 0x07, 0x09, 0x09, 0x07, 0x00 },   // 0x64, d
        { 0x07, 0x00, 0x00, 0x0e, 0x11, 0x1f, 0x10, 0x0f, 0x00 },   // 0x65, e
        { 0x07, 0x06, 0x09, 0x08, 0x1c, 0x08, 0x08, 0x08, 0x00 },   // 0x66, f
        { 0x07, 0x00, 0x0e, 0x11, 0x13, 0x0d, 0x01, 0x01, 0x0e },   // 0x67, g
        { 0x07, 0x10, 0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x00 },   // 0x68, h
        { 0x05, 0x00, 0x02, 0x00, 0x06, 0x02, 0x02, 0x07, 0x00 },   // 0x69, i
        { 0x07, 0x00, 0x02, 0x00, 0x06, 0x02, 0x02, 0x12, 0x0c },   // 0x6a, j
        { 0x07, 0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12, 0x00 },   // 0x6b, k
        { 0x05, 0x06, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x00 },   // 0x6c, l
        { 0x07, 0x00, 0x00, 0x0a, 0x15, 0x15, 0x11, 0x11, 0x00 },   // 0x6d, m
        { 0x07, 0x00, 0x00, 0x16, 0x19, 0x11, 0x11, 0x11, 0x00 },   // 0x6e, n
        { 0x07, 0x00, 0x00, 0x0e, 0x11, 0x11, 0x11, 0x0e, 0x00 },   // 0x6f, o
        { 0x07, 0x00, 0x00, 0x1c, 0x12, 0x12, 0x1c, 0x10, 0x10 },   // 0x70, p
        { 0x07, 0x00, 0x00, 0x07, 0x09, 0x09, 0x07, 0x01, 0x01 },   // 0x71, q
        { 0x07, 0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10, 0x00 },   // 0x72, r
        { 0x07, 0x00, 0x00, 0x0f, 0x10, 0x0e, 0x01, 0x1e, 0x00 },   // 0x73, s
        { 0x07, 0x08, 0x08, 0x1c, 0x08, 0x08, 0x09, 0x06, 0x00 },   // 0x74, t
        { 0x07, 0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0d, 0x00 },   // 0x75, u
        { 0x07, 0x00, 0x00, 0x11, 0x11, 0x11, 0x0a, 0x04, 0x00 },   // 0x76, v
        { 0x07, 0x00, 0x00, 0x11, 0x11, 0x15, 0x15, 0x0a, 0x00 },   // 0x77, w
        { 0x07, 0x00, 0x00, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x00 },   // 0x78, x
        { 0x07, 0x00, 0x00, 0x11, 0x11, 0x0f, 0x01, 0x11, 0x0e },   // 0x79, y
        { 0x07, 0x00, 0x00, 0x1f, 0x02, 0x04, 0x08, 0x1f, 0x00 },   // 0x7a, z
        { 0x07, 0x06, 0x08, 0x08, 0x10, 0x08, 0x08, 0x06, 0x00 },   // 0x7b, {
        { 0x07, 0x04, 0x04, 0x04, 0x00, 0x04, 0x04, 0x04, 0x00 },   // 0x7c, |
        { 0x07, 0x0c, 0x02, 0x02, 0x01, 0x02, 0x02, 0x0c, 0x00 },   // 0x7d, }
        { 0x07, 0x08, 0x15, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00 },   // 0x7e, ~
        { 0x07, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x00 }    // 0x7f, DEL
};

// Zeichensatz 5x8 in einer 8x8 Matrix, 0,0 ist rechts oben
unsigned short const font2[96][9] = { { 0x07, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00 },   // 0x20, Space
        { 0x07, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04, 0x00 },   // 0x21, !
        { 0x07, 0x09, 0x09, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00 },   // 0x22, "
        { 0x07, 0x0a, 0x0a, 0x1f, 0x0a, 0x1f, 0x0a, 0x0a, 0x00 },   // 0x23, #
        { 0x07, 0x04, 0x0f, 0x14, 0x0e, 0x05, 0x1e, 0x04, 0x00 },   // 0x24, $
        { 0x07, 0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13, 0x00 },   // 0x25, %
        { 0x07, 0x04, 0x0a, 0x0a, 0x0a, 0x15, 0x12, 0x0d, 0x00 },   // 0x26, &
        { 0x07, 0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00 },   // 0x27, '
        { 0x07, 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02, 0x00 },   // 0x28, (
        { 0x07, 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08, 0x00 },   // 0x29, )
        { 0x07, 0x04, 0x15, 0x0e, 0x1f, 0x0e, 0x15, 0x04, 0x00 },   // 0x2a, *
        { 0x07, 0x00, 0x04, 0x04, 0x1f, 0x04, 0x04, 0x00, 0x00 },   // 0x2b, +
        { 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x02 },   // 0x2c, ,
        { 0x07, 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00, 0x00 },   // 0x2d, -
        { 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x03, 0x00 },   // 0x2e, .
        { 0x07, 0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10, 0x00 },   // 0x2f, /
        { 0x07, 0x00, 0x00, 0x07, 0x05, 0x05, 0x05, 0x07, 0x00 },   // 0x30, 0
        { 0x07, 0x00, 0x00, 0x02, 0x02, 0x02, 0x02, 0x02, 0x00 },   // 0x31, 1
        { 0x07, 0x00, 0x00, 0x07, 0x01, 0x07, 0x04, 0x07, 0x00 },   // 0x32, 2
        { 0x07, 0x00, 0x00, 0x07, 0x01, 0x07, 0x01, 0x07, 0x00 },   // 0x33, 3
        { 0x07, 0x00, 0x00, 0x05, 0x05, 0x07, 0x01, 0x01, 0x00 },   // 0x34, 4
        { 0x07, 0x00, 0x00, 0x07, 0x04, 0x07, 0x01, 0x07, 0x00 },   // 0x35, 5
        { 0x07, 0x00, 0x00, 0x07, 0x04, 0x07, 0x05, 0x07, 0x00 },   // 0x36, 6
        { 0x07, 0x00, 0x00, 0x07, 0x01, 0x01, 0x01, 0x01, 0x00 },   // 0x37, 7
        { 0x07, 0x00, 0x00, 0x07, 0x05, 0x07, 0x05, 0x07, 0x00 },   // 0x38, 8
        { 0x07, 0x00, 0x00, 0x07, 0x05, 0x07, 0x01, 0x07, 0x00 },   // 0x39, 9
        { 0x04, 0x00, 0x03, 0x03, 0x00, 0x03, 0x03, 0x00, 0x00 },   // 0x3a, :
        { 0x07, 0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x04, 0x08, 0x00 },   // 0x3b, ;
        { 0x07, 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02, 0x00 },   // 0x3c, <
        { 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   // 0x3d, =
        { 0x07, 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08, 0x00 },   // 0x3e, >
        { 0x07, 0x0e, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04, 0x00 },   // 0x3f, ?
        { 0x07, 0x0e, 0x11, 0x17, 0x15, 0x17, 0x10, 0x0f, 0x00 },   // 0x40, @
        { 0x07, 0x04, 0x0a, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x00 },   // 0x41, A
        { 0x07, 0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e, 0x00 },   // 0x42, B
        { 0x07, 0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e, 0x00 },   // 0x43, C
        { 0x07, 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E, 0x00 },   // 0x44, D
        { 0x07, 0x1f, 0x10, 0x10, 0x1c, 0x10, 0x10, 0x1f, 0x00 },   // 0x45, E
        { 0x07, 0x1f, 0x10, 0x10, 0x1f, 0x10, 0x10, 0x10, 0x00 },   // 0x46, F
        { 0x07, 0x0e, 0x11, 0x10, 0x10, 0x13, 0x11, 0x0f, 0x00 },   // 0x37, G
        { 0x07, 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11, 0x00 },   // 0x48, H
        { 0x07, 0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e, 0x00 },   // 0x49, I
        { 0x07, 0x1f, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0c, 0x00 },   // 0x4a, J
        { 0x07, 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11, 0x00 },   // 0x4b, K
        { 0x07, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f, 0x00 },   // 0x4c, L
        { 0x07, 0x11, 0x1b, 0x15, 0x11, 0x11, 0x11, 0x11, 0x00 },   // 0x4d, M
        { 0x07, 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x00 },   // 0x4e, N
        { 0x07, 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e, 0x00 },   // 0x4f, O
        { 0x07, 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10, 0x00 },   // 0x50, P
        { 0x07, 0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d, 0x00 },   // 0x51, Q
        { 0x07, 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11, 0x00 },   // 0x52, R
        { 0x07, 0x0e, 0x11, 0x10, 0x0e, 0x01, 0x11, 0x0e, 0x00 },   // 0x53, S
        { 0x07, 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00 },   // 0x54, T
        { 0x07, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e, 0x00 },   // 0x55, U
        { 0x07, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04, 0x00 },   // 0x56, V
        { 0x07, 0x11, 0x11, 0x11, 0x15, 0x15, 0x1b, 0x11, 0x00 },   // 0x57, W
        { 0x07, 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11, 0x00 },   // 0x58, X
        { 0x07, 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04, 0x00 },   // 0x59, Y
        { 0x07, 0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f, 0x00 },   // 0x5a, Z
        { 0x07, 0x0e, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0e, 0x00 },   // 0x5b, [
        { 0x07, 0x10, 0x10, 0x08, 0x04, 0x02, 0x01, 0x01, 0x00 },   // 0x5c, '\'
        { 0x07, 0x0e, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0e, 0x00 },   // 0x5d, ]
        { 0x07, 0x04, 0x0a, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00 },   // 0x5e, ^
        { 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0x00 },   // 0x5f, _
        { 0x07, 0x04, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00 },   // 0x60, `
        { 0x07, 0x00, 0x0e, 0x01, 0x0d, 0x13, 0x13, 0x0d, 0x00 },   // 0x61, a
        { 0x07, 0x10, 0x10, 0x10, 0x1c, 0x12, 0x12, 0x1c, 0x00 },   // 0x62, b
        { 0x07, 0x00, 0x00, 0x0E, 0x10, 0x10, 0x10, 0x0E, 0x00 },   // 0x63, c
        { 0x07, 0x01, 0x01, 0x01, 0x07, 0x09, 0x09, 0x07, 0x00 },   // 0x64, d
        { 0x07, 0x00, 0x00, 0x0e, 0x11, 0x1f, 0x10, 0x0f, 0x00 },   // 0x65, e
        { 0x07, 0x06, 0x09, 0x08, 0x1c, 0x08, 0x08, 0x08, 0x00 },   // 0x66, f
        { 0x07, 0x00, 0x0e, 0x11, 0x13, 0x0d, 0x01, 0x01, 0x0e },   // 0x67, g
        { 0x07, 0x10, 0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x00 },   // 0x68, h
        { 0x05, 0x00, 0x02, 0x00, 0x06, 0x02, 0x02, 0x07, 0x00 },   // 0x69, i
        { 0x07, 0x00, 0x02, 0x00, 0x06, 0x02, 0x02, 0x12, 0x0c },   // 0x6a, j
        { 0x07, 0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12, 0x00 },   // 0x6b, k
        { 0x05, 0x06, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x00 },   // 0x6c, l
        { 0x07, 0x00, 0x00, 0x0a, 0x15, 0x15, 0x11, 0x11, 0x00 },   // 0x6d, m
        { 0x07, 0x00, 0x00, 0x16, 0x19, 0x11, 0x11, 0x11, 0x00 },   // 0x6e, n
        { 0x07, 0x00, 0x00, 0x0e, 0x11, 0x11, 0x11, 0x0e, 0x00 },   // 0x6f, o
        { 0x07, 0x00, 0x00, 0x1c, 0x12, 0x12, 0x1c, 0x10, 0x10 },   // 0x70, p
        { 0x07, 0x00, 0x00, 0x07, 0x09, 0x09, 0x07, 0x01, 0x01 },   // 0x71, q
        { 0x07, 0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10, 0x00 },   // 0x72, r
        { 0x07, 0x00, 0x00, 0x0f, 0x10, 0x0e, 0x01, 0x1e, 0x00 },   // 0x73, s
        { 0x07, 0x08, 0x08, 0x1c, 0x08, 0x08, 0x09, 0x06, 0x00 },   // 0x74, t
        { 0x07, 0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0d, 0x00 },   // 0x75, u
        { 0x07, 0x00, 0x00, 0x11, 0x11, 0x11, 0x0a, 0x04, 0x00 },   // 0x76, v
        { 0x07, 0x00, 0x00, 0x11, 0x11, 0x15, 0x15, 0x0a, 0x00 },   // 0x77, w
        { 0x07, 0x00, 0x00, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x00 },   // 0x78, x
        { 0x07, 0x00, 0x00, 0x11, 0x11, 0x0f, 0x01, 0x11, 0x0e },   // 0x79, y
        { 0x07, 0x00, 0x00, 0x1f, 0x02, 0x04, 0x08, 0x1f, 0x00 },   // 0x7a, z
        { 0x07, 0x06, 0x08, 0x08, 0x10, 0x08, 0x08, 0x06, 0x00 },   // 0x7b, {
        { 0x07, 0x04, 0x04, 0x04, 0x00, 0x04, 0x04, 0x04, 0x00 },   // 0x7c, |
        { 0x07, 0x0c, 0x02, 0x02, 0x01, 0x02, 0x02, 0x0c, 0x00 },   // 0x7d, }
        { 0x07, 0x08, 0x15, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00 },   // 0x7e, ~
        { 0x07, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x00 }    // 0x7f, DEL
};
//************************automatic connection**************************************************************************
bool autoConfig()
{
    // Keep the radio awake while the startup marquee is running.  Modem
    // sleep can let the ESP8266 Wi-Fi task monopolise the CPU for a long
    // interval during association, leaving the MAX7219 showing one stale
    // frame (often the visible "ng...").  Normal clock operation remains
    // unaffected; this is only the short connection phase.
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    if (ssid[0] != '\0')
        WiFi.begin(ssid, pass);      // use credentials supplied in this build
    else
    {
        // If the application settings were wiped but the ESP8266 SDK still
        // has a saved station profile, let the SDK reconnect using it.  This
        // keeps a firmware update from unnecessarily losing network access.
        // With no saved profile, fall back to the setup AP below.
        if (WiFi.SSID().length() > 0) {
            Serial.println("No MatrixClock SSID; trying saved ESP8266 Wi-Fi profile");
            WiFi.begin();
        } else {
            delay(100);
            return false;
        }
    }

    int messagePosition = 0;
    uint32_t searchStarted = millis();
    uint32_t lastSearchReport = searchStarted;
    uint32_t lastWifiPoll = searchStarted - 250;
    unsigned long searchFrames = 0;
    wl_status_t cachedWifiStatus = WL_DISCONNECTED;
    const uint32_t searchFrameMs = settings.scrollSpeed == 1 ? 25 : 40;
    showStaticStatus("WiFi");
    while (millis() - searchStarted < 30000UL)
    {
       if (servicePhysicalFactoryReset()) {
           delay(1);
           yield();
           continue;
       }
       if (millis() - lastWifiPoll >= 250UL) {
           uint32_t pollStarted = millis();
           cachedWifiStatus = WiFi.status();
           uint32_t pollDuration = millis() - pollStarted;
           if (pollDuration > 50UL)
               Serial.printf("SEARCH WIFI POLL: %lu ms status=%d\n", pollDuration, (int)cachedWifiStatus);
           lastWifiPoll = millis();
       }
       // Check before drawing the next frame so a ready connection cannot
       // leave the final "ng..." frame visible.
       // Keep the short Wi-Fi status static.  The radio can temporarily
       // monopolise the ESP8266 during association, so an animated marquee
       // would visibly freeze on whichever frame was last drawn.
       if (cachedWifiStatus != WL_CONNECTED)
           messagePosition = 0;
       searchFrames = (millis() - searchStarted) / searchFrameMs;
       if (millis() - lastSearchReport >= 1000UL) {
           Serial.printf("SEARCH: %lu ms frame=%lu pos=%d WiFi=%d\n",
                         (unsigned long)(millis() - searchStarted), searchFrames,
                         messagePosition, (int)cachedWifiStatus);
           lastSearchReport = millis();
       }
       if (cachedWifiStatus == WL_CONNECTED)
       {
          Serial.printf("SEARCH CONNECTED: %lu ms frame=%lu pos=%d\n",
                        (unsigned long)(millis() - searchStarted), searchFrames,
                        messagePosition);
          Serial.println("AutoConfig Success");
          Serial.printf("SSID:%s\r\n", WiFi.SSID().c_str());
          Serial.println("PSW:[hidden]");
          showStaticStatus("OK!");

          Serial.println("WiFi connected");
          Serial.println(WiFi.localIP());
          Serial.println("Starting UDP");
          udp.begin(localPort);
          Serial.print("Local port: ");
          Serial.println(udp.localPort());
          return true;
       }
       delay(1);
       yield();
    }
    clear_Display();
    char2Arr('E', 25, 0);
    char2Arr('r', 19, 0);
    char2Arr('r', 12, 0);
    char2Arr('!', 6, 0);
    refresh_display(); 
    delay(1000);
    Serial.println("AutoConfig Failed!" );
    return false;
}
//**************************************************************************************************
void configureTimezone() {
    char tz[sizeof(settings.customTimezone)];
    if (settings.daylightSaving == 2 && settings.customTimezone[0] != '\0')
        strlcpy(tz, settings.customTimezone, sizeof(tz));
    else if (strcmp(settings.timezone, "Europe/London") == 0)
        strlcpy(tz, settings.daylightSaving ? "GMT0BST,M3.5.0/1,M10.5.0/2" : "GMT0", sizeof(tz));
    else if (strcmp(settings.timezone, "Europe/Paris") == 0)
        strlcpy(tz, settings.daylightSaving ? "CET-1CEST,M3.5.0/2,M10.5.0/3" : "CET-1", sizeof(tz));
    else if (strcmp(settings.timezone, "America/New_York") == 0)
        strlcpy(tz, settings.daylightSaving ? "EST5EDT,M3.2.0/2,M11.1.0/2" : "EST5", sizeof(tz));
    else if (strcmp(settings.timezone, "America/Los_Angeles") == 0)
        strlcpy(tz, settings.daylightSaving ? "PST8PDT,M3.2.0/2,M11.1.0/2" : "PST8", sizeof(tz));
    else if (strcmp(settings.timezone, "Australia/Sydney") == 0)
        strlcpy(tz, settings.daylightSaving ? "AEST-10AEDT,M10.1.0/2,M4.1.0/3" : "AEST-10", sizeof(tz));
    else if (strncmp(settings.timezone, "UTC", 3) == 0)
        // Offset presets are stored directly as POSIX UTC rules. POSIX uses
        // the opposite sign convention: UTC-1 means UTC+01:00 local time.
        strlcpy(tz, settings.timezone, sizeof(tz));
    else {
        int hours = settings.utcOffset / 60;
        snprintf(tz, sizeof(tz), "UTC%+d", -hours);
    }
    setenv("TZ", tz, 1);
    tzset();
}

bool readNtpMeasurement(NtpMeasurement& measurement, uint32_t timeoutUs) {
    if (WiFi.status() != WL_CONNECTED || WiFi.hostByName(ntpServerName, timeServerIP) != 1)
        return false;

    // Discard any stale datagram before making a fresh measurement.
    while (udp.parsePacket() > 0)
        while (udp.available()) udp.read();

    memset(packetBuffer, 0, NTP_PACKET_SIZE);
    packetBuffer[0] = 0b11100011;   // LI, Version, Mode
    packetBuffer[1] = 0;
    packetBuffer[2] = 6;
    packetBuffer[3] = 0xEC;
    packetBuffer[12] = 49;
    packetBuffer[13] = 0x4E;
    packetBuffer[14] = 49;
    packetBuffer[15] = 52;

    uint32_t requestStarted = micros();
    udp.beginPacket(timeServerIP, 123);
    udp.write(packetBuffer, NTP_PACKET_SIZE);
    udp.endPacket();

    int packetSize = 0;
    uint32_t receivedAt = 0;
    while ((uint32_t)(micros() - requestStarted) < timeoutUs) {
        if (servicePhysicalFactoryReset()) {
            delay(1);
            yield();
            continue;
        }
        packetSize = udp.parsePacket();
        if (packetSize >= NTP_PACKET_SIZE) {
            receivedAt = micros();
            break;
        }
        delay(5);
        yield();
    }
    if (packetSize < NTP_PACKET_SIZE ||
        udp.read(packetBuffer, NTP_PACKET_SIZE) < NTP_PACKET_SIZE)
        return false;
    while (udp.available()) udp.read();

    // NTP server replies use mode 4. Stratum 0 is a Kiss-o'-Death response,
    // and leap indicator 3 means the server is unsynchronised. Neither may
    // influence the clock or drift calibration.
    uint8_t leapIndicator = packetBuffer[0] >> 6;
    if ((packetBuffer[0] & 0x07) != 4 || packetBuffer[1] == 0 ||
        leapIndicator == 3)
        return false;

    uint32_t secondsSince1900 = ((uint32_t)word(packetBuffer[40], packetBuffer[41]) << 16) |
                                word(packetBuffer[42], packetBuffer[43]);
    const uint32_t seventyYears = 2208988800UL;
    if (secondsSince1900 < seventyYears)
        return false;
    uint32_t fractionWord = ((uint32_t)word(packetBuffer[44], packetBuffer[45]) << 16) |
                            word(packetBuffer[46], packetBuffer[47]);
    uint32_t roundTripUs = (uint32_t)(receivedAt - requestStarted);
    uint32_t serverFractionUs = (uint32_t)(((uint64_t)fractionWord * 1000000ULL) >> 32);
    uint64_t correctedUs = (uint64_t)serverFractionUs + (roundTripUs / 2UL);

    measurement.epoch = secondsSince1900 - seventyYears + (uint32_t)(correctedUs / 1000000ULL);
    measurement.fractionUs = (uint32_t)(correctedUs % 1000000ULL);
    measurement.referenceMicros = receivedAt;
    measurement.roundTripUs = roundTripUs;
    lastNtpStratum = packetBuffer[1];
    lastNtpLeapIndicator = leapIndicator;
    lastNtpServerAddress = timeServerIP;
    lastNtpSuccessEpoch = measurement.epoch;
    lastNtpMetadataValid = true;
    return true;
}

tm* connectNTP() { // Return local time when the startup NTP request succeeds.
    NtpMeasurement measurement;
    Serial.println("sending NTP packet...");
    if (!readNtpMeasurement(measurement, 1500000UL)) {
        Serial.println("No valid NTP reply; will retry");
        return NULL;
    }
    ntpEstimatedEpoch = measurement.epoch;
    ntpEstimatedFractionUs = measurement.fractionUs;
    ntpReferenceMicros = measurement.referenceMicros;
    lastNtpRoundTripUs = measurement.roundTripUs;
    Serial.printf("NTP sample: RTT=%lu us estimated=%lu.%06lu\n",
                  (unsigned long)lastNtpRoundTripUs,
                  (unsigned long)ntpEstimatedEpoch,
                  (unsigned long)ntpEstimatedFractionUs);
    epoch = ntpEstimatedEpoch + ((ntpEstimatedFractionUs >= 500000UL) ? 1UL : 0UL);
    configureTimezone();
    time_t t = epoch;
    return localtime(&t);
}

void runDriftCalibrationCheck() {
    struct DriftSample {
        int64_t offsetUs;
        uint32_t roundTripUs;
        uint64_t elapsedUs;
        uint64_t observedUs;
    } samples[DRIFT_SAMPLES_PER_CHECK];
    uint8_t sampleCount = 0;

    for (uint8_t attempt = 0; attempt < DRIFT_SAMPLES_PER_CHECK; attempt++) {
        NtpMeasurement measurement;
        if (readNtpMeasurement(measurement, DRIFT_NTP_TIMEOUT_US) &&
            measurement.roundTripUs <= DRIFT_MAX_RTT_US) {
            uint64_t elapsedUs = softwareClockElapsedAt(measurement.referenceMicros);
            int64_t predictedUs = (int64_t)softwareEpochBase * 1000000LL + (int64_t)elapsedUs;
            int64_t observedUs = (int64_t)measurement.epoch * 1000000LL + measurement.fractionUs;
            int64_t offsetUs = observedUs - predictedUs;
            if (offsetUs >= -DRIFT_MAX_OFFSET_US && offsetUs <= DRIFT_MAX_OFFSET_US) {
                samples[sampleCount++] = {offsetUs, measurement.roundTripUs, elapsedUs,
                                          (uint64_t)observedUs};
            }
        }
        if (attempt + 1 < DRIFT_SAMPLES_PER_CHECK) {
            delay(120);
            yield();
        }
    }

    if (sampleCount < DRIFT_MIN_SAMPLES) {
        driftLastCheckValid = false;
        lastNtpCheckResult = NTP_CHECK_NO_STABLE_REPLY;
        lastNtpCheckRttUs = 0;
        nextDriftCheckAt = millis() + 900000UL; // retry in 15 minutes
        return;
    }

    // Prefer the three lowest-latency replies, then take their median offset.
    // This removes both asymmetric-delay outliers and a single unusual sample.
    for (uint8_t i = 0; i + 1 < sampleCount; i++)
        for (uint8_t j = i + 1; j < sampleCount; j++)
            if (samples[j].roundTripUs < samples[i].roundTripUs) {
                DriftSample temporary = samples[i];
                samples[i] = samples[j];
                samples[j] = temporary;
            }
    const uint8_t selectedCount = DRIFT_MIN_SAMPLES;
    DriftSample selected[DRIFT_MIN_SAMPLES];
    for (uint8_t i = 0; i < selectedCount; i++)
        selected[i] = samples[i];
    for (uint8_t i = 0; i + 1 < selectedCount; i++)
        for (uint8_t j = i + 1; j < selectedCount; j++)
            if (selected[j].offsetUs < selected[i].offsetUs) {
                DriftSample temporary = selected[i];
                selected[i] = selected[j];
                selected[j] = temporary;
            }

    if (selected[selectedCount - 1].offsetUs - selected[0].offsetUs >
        DRIFT_MAX_SAMPLE_SPREAD_US) {
        driftLastCheckValid = false;
        lastNtpCheckResult = NTP_CHECK_INCONSISTENT_REPLIES;
        lastNtpCheckRttUs = 0;
        nextDriftCheckAt = millis() + 900000UL; // retry in 15 minutes
        return;
    }

    const DriftSample& medianSample = selected[selectedCount / 2];
    int64_t offsetUs = medianSample.offsetUs;
    uint64_t observedUs = medianSample.observedUs;
    driftLastCheckValid = true;
    driftLastOffsetUs = (int32_t)offsetUs;
    driftLastRoundTripUs = medianSample.roundTripUs;
    driftLastObservedUs = observedUs;
    lastNtpRoundTripUs = driftLastRoundTripUs;
    if (driftAcceptedChecks < 255)
        driftAcceptedChecks++;
    lastNtpCheckResult = NTP_CHECK_SUCCESS;
    lastNtpCheckRttUs = lastNtpRoundTripUs;

    // Use only the already-filtered median offset.  The correction is applied
    // gradually to the main software clock; the chronograph remains governed
    // solely by its saved oscillator-rate compensation.
    scheduleClockPhaseSlew(offsetUs);

    if (!driftReferenceValid) {
        driftReferenceValid = true;
        driftReferenceObservedUs = observedUs;
        driftReferenceOffsetUs = offsetUs;
    } else if (observedUs > driftReferenceObservedUs) {
        uint64_t elapsedSinceReferenceUs = observedUs - driftReferenceObservedUs;
        int64_t residualPpmMilli = ((offsetUs - driftReferenceOffsetUs) * 1000000000LL) /
                                   (int64_t)elapsedSinceReferenceUs;
        int64_t candidatePpmMilli = (int64_t)activeDriftPpmMilli + residualPpmMilli;
        if (candidatePpmMilli >= -DRIFT_MAX_RATE_PPM_MILLI &&
            candidatePpmMilli <= DRIFT_MAX_RATE_PPM_MILLI) {
            // Retain completed estimates across reset. Each new estimate is
            // folded into a bounded running mean; the active clock rate is
            // still left untouched until a later boot/restart commit.
            if (driftCandidateValid && driftRateEstimateCount > 0) {
                int64_t combined =
                    (int64_t)driftCandidatePpmMilli * driftRateEstimateCount +
                    candidatePpmMilli;
                driftCandidatePpmMilli =
                    (int32_t)(combined / (driftRateEstimateCount + 1));
            } else {
                driftCandidatePpmMilli = (int32_t)candidatePpmMilli;
            }
            driftCandidateValid = true;
            if (driftRateEstimateCount < 255)
                driftRateEstimateCount++;
        }
    }
    // Checkpoint every accepted check without touching flash.  The record
    // survives reset and is promoted to active EEPROM calibration only on a
    // later boot or an intentional commit/restart path.
    saveDriftSessionCheckpoint();
    nextDriftCheckAt = millis() + DRIFT_CHECK_INTERVAL_MS;
}
//**************************************************************************************************
void rtc_init(unsigned char sda, unsigned char scl) {
    Wire.begin(sda, scl);
    rtc_Write(controlREG, 0x00);
}
//**************************************************************************************************
// BCD Code
//**************************************************************************************************
unsigned char dec2bcd(unsigned char x) { //value 0...99
    unsigned char z, e, r;
    e = x % 10;
    z = x / 10;
    z = z << 4;
    r = e | z;
    return (r);
}
unsigned char bcd2dec(unsigned char x) { //value 0...99
    int z, e;
    e = x & 0x0F;
    z = x & 0xF0;
    z = z >> 4;
    z = z * 10;
    return (z + e);
}
//**************************************************************************************************
// RTC I2C Code
//**************************************************************************************************
unsigned char rtc_Read(unsigned char regaddress) {
    Wire.beginTransmission(DS3231_ADDRESS);
    Wire.write(regaddress);
    Wire.endTransmission();
    Wire.requestFrom((unsigned char) DS3231_ADDRESS, (unsigned char) 1);
    return (Wire.read());
}
void rtc_Write(unsigned char regaddress, unsigned char value) {
    Wire.beginTransmission(DS3231_ADDRESS);
    Wire.write(regaddress);
    Wire.write(value);
    Wire.endTransmission();
}
//**********************RTC read/write helpers (some variable names are German)****************************************************************************
unsigned char rtc_sekunde() {
    return (bcd2dec(rtc_Read(secondREG)));
}
unsigned char rtc_minute() {
    return (bcd2dec(rtc_Read(minuteREG)));
}
unsigned char rtc_stunde() {
    return (bcd2dec(rtc_Read(hourREG)));
}
unsigned char rtc_wochentag() {
    return (bcd2dec(rtc_Read(WTREG)));
}
unsigned char rtc_tag() {
    return (bcd2dec(rtc_Read(dateREG)));
}
unsigned char rtc_monat() {
    return (bcd2dec(rtc_Read(monthREG)));
}
unsigned char rtc_jahr() {
    return (bcd2dec(rtc_Read(yearREG)));
}
//******************************set RTC********************************************************************
void rtc_set(tm* tt) {
    // Write the complete DS3231 calendar in one I2C transaction.  Separate
    // writes left a window where seconds and minutes belonged to different
    // instants, which was the source of intermittent one-second starts.
    unsigned char weekday = (tt->tm_wday == 0) ? 7 : (unsigned char)tt->tm_wday;
    Wire.beginTransmission(DS3231_ADDRESS);
    Wire.write(secondREG);
    Wire.write(dec2bcd((unsigned char)tt->tm_sec));
    Wire.write(dec2bcd((unsigned char)tt->tm_min));
    Wire.write(dec2bcd((unsigned char)tt->tm_hour));
    Wire.write(dec2bcd(weekday));
    Wire.write(dec2bcd((unsigned char)tt->tm_mday));
    Wire.write(dec2bcd((unsigned char)tt->tm_mon + 1));
    Wire.write(dec2bcd((unsigned char)tt->tm_year - 100));
    Wire.endTransmission();
}

// Return the backup RTC to a known baseline when the user erases all clock
// settings.  A later NTP synchronisation will write the actual date/time.
void rtc_resetToBaseline() {
    tm baseline = {};
    baseline.tm_year = 100; // years since 1900: 2000
    baseline.tm_mon = 0;    // January
    baseline.tm_mday = 1;
    baseline.tm_wday = 6;   // Saturday
    rtc_set(&baseline);
}
//**************************************************************************************************
void rtc2mez() {
 
    unsigned short Jahr, Tag, Monat, WoTag, Stunde, Minute, Sekunde;

    Jahr = rtc_jahr();// year
    if (Jahr > 99)
        Jahr = 0;
    Monat = rtc_monat();// month
    if (Monat < 1 || Monat > 12)
        Monat = 1;
    Tag = rtc_tag();// day
    if (Tag < 1 || Tag > 31)
        Tag = 1;
    WoTag = rtc_wochentag();
    if (WoTag == 7)
        WoTag = 0;
    else if (WoTag > 6)
        WoTag = 0;
    Stunde = rtc_stunde();// hour
    if (Stunde > 23)
        Stunde = 0;
    currentHour24 = Stunde;
    if (settings.timeFormat == 12) {
        if (Stunde == 0) Stunde = 12;
        else if (Stunde > 12) Stunde -= 12;
    }
    Minute = rtc_minute();// minute
    if (Minute > 59)
        Minute = 0;
    Sekunde = rtc_sekunde();// second
    if (Sekunde > 59)
        Sekunde = 0;
    
    MEZ.WT = WoTag;          //So=0, Mo=1, Di=2 ...
    MEZ.sek1 = Sekunde % 10;
    MEZ.sek2 = Sekunde / 10;
    MEZ.sek12 = Sekunde;
    MEZ.min1 = Minute % 10;
    MEZ.min2 = Minute / 10;
    MEZ.min12 = Minute;
    MEZ.std1 = Stunde % 10;
    MEZ.std2 = Stunde / 10;
    MEZ.std12 = Stunde;
    MEZ.tag12 = Tag;
    MEZ.tag1 = Tag % 10;
    MEZ.tag2 = Tag / 10;
    MEZ.mon12 = Monat;
    MEZ.mon1 = Monat % 10;
    MEZ.mon2 = Monat / 10;
    MEZ.jahr12 = Jahr;
    MEZ.jahr1 = Jahr % 10;
    MEZ.jahr2 = Jahr / 10;
}

void softwareClock2mez() {
    if (!softwareClockValid) {
        rtc2mez();
        return;
    }
    // Accumulate wrap-safe 32-bit micros() deltas.  The ESP8266 micros()
    // counter wraps approximately every 71.6 minutes; subtracting two
    // uint32_t values remains correct across that rollover.
    uint32_t nowMicros = micros();
    uint32_t rawDeltaUs = (uint32_t)(nowMicros - softwareClockLastMicros);
    // Apply the previously measured oscillator-rate correction continuously.
    // The retained remainder preserves sub-microsecond fractions rather than
    // introducing a rounding error at each display update.
    uint64_t rateAdjustedUs = adjustedClockDeltaUs(rawDeltaUs, softwareClockRateRemainder);
    int64_t phaseAdjustedUs = phaseSlewDeltaUs(rawDeltaUs,
                                                softwareClockPhaseRemainingUs,
                                                softwareClockPhaseRemainder);
    softwareClockElapsedUs = (uint64_t)((int64_t)softwareClockElapsedUs +
                                         (int64_t)rateAdjustedUs + phaseAdjustedUs);
    softwareClockLastMicros = nowMicros;
    time_t now = softwareEpochBase +
                 (softwareClockElapsedUs / 1000000ULL);
    tm* current = localtime(&now);
    currentHour24 = current->tm_hour;
    unsigned short Jahr = current->tm_year - 100;
    unsigned short Monat = current->tm_mon + 1;
    unsigned short Tag = current->tm_mday;
    unsigned short WoTag = (current->tm_wday == 0) ? 0 : current->tm_wday;
    unsigned short Stunde = current->tm_hour;
    unsigned short Minute = current->tm_min;
    unsigned short Sekunde = current->tm_sec;
    if (settings.timeFormat == 12) {
        if (Stunde == 0) Stunde = 12;
        else if (Stunde > 12) Stunde -= 12;
    }
    MEZ.WT = WoTag;
    MEZ.sek1 = Sekunde % 10; MEZ.sek2 = Sekunde / 10; MEZ.sek12 = Sekunde;
    MEZ.min1 = Minute % 10; MEZ.min2 = Minute / 10; MEZ.min12 = Minute;
    MEZ.std1 = Stunde % 10; MEZ.std2 = Stunde / 10; MEZ.std12 = Stunde;
    MEZ.tag12 = Tag; MEZ.tag1 = Tag % 10; MEZ.tag2 = Tag / 10;
    MEZ.mon12 = Monat; MEZ.mon1 = Monat % 10; MEZ.mon2 = Monat / 10;
    MEZ.jahr12 = Jahr; MEZ.jahr1 = Jahr % 10; MEZ.jahr2 = Jahr / 10;
}

//***************************MAX7219 initial values**********************************************************************
const unsigned short InitArr[7][2] = { { 0x0C, 0x00 },    // display off
        { 0x00, 0xFF },    // no LEDtest
        { 0x09, 0x00 },    // BCD off
        { 0x0F, 0x00 },    // normal operation
        { 0x0B, 0x07 },    // start display
        { 0x0A, 0x04 },    // brightness
        { 0x0C, 0x01 }     // display on
};
//*********************************initialise MAX7219*****************************************************************
void max7219_init()  //all MAX7219 init
{
    unsigned short i, j;
    for (i = 0; i < 7; i++) {
        digitalWrite(CS, LOW);
        delayMicroseconds(1);
        for (j = 0; j < anzMAX; j++) {
            SPI.write(InitArr[i][0]);  //register
            SPI.write(InitArr[i][1]);  //value
        }
        digitalWrite(CS, HIGH);
    }
}
//*********************************set brightness*****************************************************************
void max7219_set_brightness(unsigned short br)  //brightness MAX7219
{
    unsigned short j;
    if (br < 16) {
        digitalWrite(CS, LOW);
        delayMicroseconds(1);
        for (j = 0; j < anzMAX; j++) {
            SPI.write(0x0A);  //register
            SPI.write(br);    //value
        }
        digitalWrite(CS, HIGH);
    }
}
//**************************************************************************************************
void helpArr_init(void)  //helperarray init
{
    unsigned short i, j, k;
    j = 0;
    k = 0;
    for (i = 0; i < anzMAX * 8; i++) {
        helpArrPos[i] = (1 << j);   //bitmask
        helpArrMAX[i] = k;
        j++;
        if (j > 7) {
            j = 0;
            k++;
        }
    }
}
//***********************************clear display***************************************************************
void clear_Display()   //clear all
{
    unsigned short i, j;
    for (i = 0; i < 8; i++)     //8 rows
    {
        digitalWrite(CS, LOW);
        delayMicroseconds(1);
        for (j = anzMAX; j > 0; j--) {
            LEDarr[j - 1][i] = 0;       //LEDarr clear
            SPI.write(i + 1);           //current row
            SPI.write(LEDarr[j - 1][i]);
        }
        digitalWrite(CS, HIGH);
    }
}
//*************************************rotate 90 degrees********************************************************************
void rotate_90() // for Generic displays
{
    for (uint8_t k = anzMAX; k > 0; k--) {

        uint8_t i, j, m, imask, jmask;
        uint8_t tmp[8]={0,0,0,0,0,0,0,0};
        for (  i = 0, imask = 0x01; i < 8; i++, imask <<= 1) {
          for (j = 0, jmask = 0x01; j < 8; j++, jmask <<= 1) {
            if (LEDarr[k-1][i] & jmask) {
              tmp[j] |= imask;
            }
          }
        }
        for(m=0; m<8; m++){
            LEDarr[k-1][m]=tmp[m];
        }
    }
}
//*****************************refresh display*********************************************************************
void refresh_display() //take info into LEDarr
{
    unsigned short i, j;
    // Apply the tilt-switch orientation to every render, including startup
    // and status messages that are shown before the normal clock loop begins.
    kk = !digitalRead(16);

#ifdef ROTATE_90
    rotate_90();
#endif

    for (i = 0; i < 8; i++)     //8 rows
    {
        digitalWrite(CS, LOW);
        delayMicroseconds(1);
        for (j = 1; j <= anzMAX; j++) 
        {
            SPI.write(i + 1);  //current row
            //if(kk==1){kk=0;
            if(kk){
              SPI.setBitOrder(LSBFIRST);      // bitorder for reverse columns
              SPI.write(LEDarr[4-j][7-i]);//SPI.write(LEDarr[j - 1][7-i]);
              SPI.setBitOrder(MSBFIRST);      // reset bitorder reset bit order
            }
            else {
              
            


            
#ifdef REVERSE_HORIZONTAL    //reverse horizontal direction 
            SPI.setBitOrder(LSBFIRST);      // bitorder for reverse columns
#endif

#ifdef REVERSE_VERTICAL     //reverse vertical direction
            SPI.write(LEDarr[4-j][7-i]);//SPI.write(LEDarr[j - 1][7-i]);
#else
            SPI.write(LEDarr[j - 1][i]);
#endif

#ifdef REVERSE_HORIZONTAL
            SPI.setBitOrder(MSBFIRST);      // reset bitorder reset bit order
#endif
        }}
        digitalWrite(CS, HIGH);
    }
}
//**************************************************************************************************
void char2Arr(unsigned short ch, int PosX, short PosY) { //characters into arr
    int i, j, k, l, m, o1, o2, o3, o4;  //in LEDarr
    PosX++;
    k = ch - 32;                        //ASCII position in font
    if ((k >= 0) && (k < 96))           //character found in font?
    {
        o4 = font1[k][0];                 //character width
        o3 = 1 << (o4 - 2);
        for (i = 0; i < o4; i++) {
            if (((PosX - i <= maxPosX) && (PosX - i >= 0))
                    && ((PosY > -8) && (PosY < 8))) //within matrix?
            {
                o1 = helpArrPos[PosX - i];
                o2 = helpArrMAX[PosX - i];
                for (j = 0; j < 8; j++) {
                    if (((PosY >= 0) && (PosY <= j)) || ((PosY < 0) && (j < PosY + 8))) //scroll vertical
                    {
                        l = font1[k][j + 1];
                        m = (l & (o3 >> i));  //e.g. o4=7  0zzzzz0, o4=4  0zz0
                        if (m > 0)
                            LEDarr[o2][j - PosY] = LEDarr[o2][j - PosY] | (o1);  //set point
                        else
                            LEDarr[o2][j - PosY] = LEDarr[o2][j - PosY] & (~o1); //clear point
                    }
                }
            }
        }
    }
}

void char22Arr(unsigned short ch, int PosX, short PosY) { //characters into arr
    int i, j, k, l, m, o1, o2, o3, o4;  //in LEDarr
    PosX++;
    k = ch - 32;                        //ASCII position in font
    if ((k >= 0) && (k < 96))           //character found in font?
    {
        o4 = font2[k][0];                 //character width
        o3 = 1 << (o4 - 2);
        for (i = 0; i < o4; i++) {
            if (((PosX - i <= maxPosX) && (PosX - i >= 0))
                    && ((PosY > -8) && (PosY < 8))) //within matrix?
            {
                o1 = helpArrPos[PosX - i];
                o2 = helpArrMAX[PosX - i];
                for (j = 0; j < 8; j++) {
                    if (((PosY >= 0) && (PosY <= j)) || ((PosY < 0) && (j < PosY + 8))) //scroll vertical
                    {
                        l = font2[k][j + 1];
                        m = (l & (o3 >> i));  //e.g. o4=7  0zzzzz0, o4=4  0zz0
                        if (m > 0)
                            LEDarr[o2][j - PosY] = LEDarr[o2][j - PosY] | (o1);  //set point
                        else
                            LEDarr[o2][j - PosY] = LEDarr[o2][j - PosY] & (~o1); //clear point
                    }
                }
            }
        }
    }
}

// Overlay variant used by the tightly packed Normal chronograph layout.
// Unlike the normal renderer it never clears blank glyph columns, preventing
// neighbouring digits from erasing one another at their shared boundaries.
void glyphOverlay(const unsigned short glyphs[96][9], unsigned short ch,
                  int PosX, short PosY) {
    PosX++;
    int k = ch - 32;
    if (k < 0 || k >= 96 || PosY <= -8 || PosY >= 8)
        return;
    int width = glyphs[k][0];
    int mask = 1 << (width - 2);
    for (int i = 0; i < width; i++) {
        if (PosX - i < 0 || PosX - i > maxPosX)
            continue;
        int o1 = helpArrPos[PosX - i];
        int o2 = helpArrMAX[PosX - i];
        for (int j = 0; j < 8; j++) {
            if ((PosY >= 0 && PosY <= j) ||
                (PosY < 0 && j < PosY + 8)) {
                if (glyphs[k][j + 1] & (mask >> i))
                    LEDarr[o2][j - PosY] |= o1;
            }
        }
    }
}

void char2Overlay(unsigned short ch, int PosX, short PosY) {
    glyphOverlay(font1, ch, PosX, PosY);
}

void char22Overlay(unsigned short ch, int PosX, short PosY) {
    glyphOverlay(font2, ch, PosX, PosY);
}

// Short, centred startup status.  These labels deliberately fit the 32-column
// matrix in the normal date font and remain stable while Wi-Fi/NTP work runs.
void showStaticStatus(const char* message) {
    int length = strlen(message);
    int pitch = 6;
    // char2Arr's PosX is the right edge of each glyph.  The display's
    // established wiring order requires characters to be supplied in reverse
    // order while their positions increase from left to right.
    // Account for the glyph overhang to the left of each PosX anchor; this
    // keeps short and long labels visually centred rather than pitch-centred.
    int position = ((int)maxPosX - ((length - 1) * pitch)) / 2 + 2;
    if (position < 0) position = 0;
    clear_Display();
    for (int i = 0; i < length; i++)
        char2Arr(message[(length - 1) - i], position + (i * pitch), 0);
    refresh_display();
}

void clearGlyphColumns(int PosX, int width) {
    PosX++;
    for (int i = 0; i < width; i++) {
        if (PosX - i < 0 || PosX - i > maxPosX)
            continue;
        int o1 = helpArrPos[PosX - i];
        int o2 = helpArrMAX[PosX - i];
        for (int j = 0; j < 8; j++)
            LEDarr[o2][j] &= (unsigned short)(~o1);
    }
}

// Four hour-elapsed indicators occupy the odd columns of the seven-column
// top row above the compact centiseconds.  The display refresh routine applies
// the tilt-switch orientation to these logical pixels just like all other
// display data.
void drawChronographHourIndicatorsAt(uint32_t elapsedMs, bool timerRunning,
                                     bool flashPhase, int anchor) {
    uint8_t elapsedHours = (uint8_t)((elapsedMs / 3600000UL) % 5UL);
    const uint8_t indicatorColumns[4] = {0, 2, 4, 6};
    int horizontalOffset = anchor - (int)maxPosX;
    for (uint8_t i = 0; i < 4; i++) {
        int column = indicatorColumns[i] + horizontalOffset;
        if (column < 0 || column > (int)maxPosX)
            continue;
        uint16_t mask = helpArrPos[column];
        uint8_t module = helpArrMAX[column];
        bool indicatorOn = false;
        if (!timerRunning) {
            // Paused: completed hours remain steady.  At hour four all
            // indicators remain on rather than flashing.
            indicatorOn = i < elapsedHours;
        } else if (elapsedHours >= 4) {
            // Hour four: all indicators flash with the stopwatch colon.
            indicatorOn = flashPhase;
        } else {
            // Running: completed hours are steady and the current hour's
            // indicator flashes with the stopwatch colon.
            indicatorOn = (i < elapsedHours) ||
                          (i == elapsedHours && flashPhase);
        }
        if (indicatorOn)
            LEDarr[module][0] |= mask;
    }
}

String formatChronographValue(uint32_t elapsedMs) {
    char value[24];
    uint32_t hours = elapsedMs / 3600000UL;
    uint32_t minutes = (elapsedMs / 60000UL) % 60UL;
    uint32_t seconds = (elapsedMs / 1000UL) % 60UL;
    uint32_t centiseconds = (elapsedMs / 10UL) % 100UL;
    snprintf(value, sizeof(value), "%02lu:%02lu:%02lu:%02lu",
             (unsigned long)hours, (unsigned long)minutes,
             (unsigned long)seconds,
             (unsigned long)centiseconds);
    return String(value);
}

void recordChronographStop(uint32_t elapsedMs) {
    if (elapsedMs == 0)
        return;
    for (int i = 2; i > 0; i--)
        chronographHistory[i] = chronographHistory[i - 1];
    chronographHistory[0] = formatChronographValue(elapsedMs);
}

// Compose either Normal HH:MM:SS or Precision MM:SS:CC at a movable anchor.
// Overlay drawing lets the chronograph and incoming clock share a transition
// frame without either glyph set erasing the other.
void drawChronographAt(uint32_t elapsedMs, bool showSeparator,
                       bool timerRunning, int anchor) {
    uint32_t minutes = (elapsedMs / 60000UL) % 60UL;
    uint32_t seconds = (elapsedMs / 1000UL) % 60UL;
    uint32_t centiseconds = (elapsedMs / 10UL) % 100UL;
    if (settings.chronographDisplayMode == 1) {
        // Normal layout: HH:MM SS.  The internal stopwatch still tracks
        // centiseconds, but they are intentionally hidden in this mode.
        uint32_t hours = (elapsedMs / 3600000UL) % 100UL;
        uint32_t minutes = (elapsedMs / 60000UL) % 60UL;
        uint32_t seconds = (elapsedMs / 1000UL) % 60UL;
        // The matrix wiring reverses logical column order.  Use the same
        // proven anchors as Precision mode, but assign fields in reverse so
        // the physical display reads HH:MM SS from left to right.
        // Draw compact seconds first because they are adjacent to the
        // minutes; the minute pair is redrawn last to preserve its edge.
        char22Overlay('0' + (seconds % 10), anchor - 27, 0);
        char22Overlay('0' + (seconds / 10), anchor - 23, 0);
        char2Overlay('0' + (hours % 10), anchor - 4, 0);
        char2Overlay('0' + (hours / 10), anchor + 1, 0);
        // Match Precision mode exactly: on while paused, and on for the
        // first half of each running second. Draw it first so the minute
        // glyphs restore their final edge column at the shared boundary.
        if (showSeparator)
            char2Overlay(':', anchor - 10, 0);
        // Draw the pair in reverse logical order for the reverse-wired
        // matrix so the physically right-hand minute digit restores its
        // complete final column instead of being cleared by overlap.
        char2Overlay('0' + (minutes / 10), anchor - 13, 0);
        char2Overlay('0' + (minutes % 10), anchor - 18, 0);
        // Hour-marker pixels are reserved for the precision layout only.
        return;
    }
    // Character positions produce MM:SS CC without introducing a second
    // colon, while keeping the compact centiseconds at the right-hand end.
    char22Overlay('0' + (centiseconds % 10), anchor - 27, 0);
    char22Overlay('0' + (centiseconds / 10), anchor - 23, 0);
    char2Overlay('0' + (seconds % 10), anchor - 18, 0);
    char2Overlay('0' + (seconds / 10), anchor - 13, 0);
    if (showSeparator)
        char2Overlay(':', anchor - 10, 0);
    char2Overlay('0' + (minutes % 10), anchor - 4, 0);
    char2Overlay('0' + (minutes / 10), anchor + 1, 0);
    drawChronographHourIndicatorsAt(elapsedMs, timerRunning, showSeparator,
                                    anchor);
}

// Render the stationary chronograph.  The display is transmitted at 50 Hz
// while the surrounding input loop may continue at a higher rate.
void showChronograph(uint32_t elapsedMs, bool showSeparator,
                     bool timerRunning) {
    static uint32_t lastChronographRefreshUs = 0;
    uint32_t nowUs = micros();
    bool refreshDue = (uint32_t)(nowUs - lastChronographRefreshUs) >= 20000UL;
    if (refreshDue) {
        clear_Display();
    } else {
        for (uint8_t module = 0; module < anzMAX; module++)
            for (uint8_t row = 0; row < 8; row++)
                LEDarr[module][row] = 0;
    }
    drawChronographAt(elapsedMs, showSeparator, timerRunning, maxPosX);
    // Keep SPI/display output at 50 Hz while the surrounding stopwatch loop
    // runs at 100 Hz for accurate button release timestamps.
    if (refreshDue) {
        refresh_display();
        lastChronographRefreshUs = nowUs;
    }
}

//**************************************************************************************************
void timer50ms() {
    static unsigned int cnt50ms = 0;
    // Poll the calibrated software clock frequently. The main loop only
    // redraws the clock state when its actual epoch second changes.
    f_tckr1s = true;
    cnt50ms++;
    if (cnt50ms == 20) {
        rtcSecondTickDue = true; // fallback when running from RTC only
        cnt50ms = 0;
    }
}
//**************************************************************************************************
//
// The setup function is called once when the clock starts.
void setup() {
    pinMode(16, INPUT);
    // DOWNLOAD/BOOT button is GPIO0, active low. Do not hold it during power-up
    // unless serial bootloader mode is intended.
    pinMode(0, INPUT_PULLUP);
    pinMode(CS, OUTPUT);
    digitalWrite(CS, HIGH);
    Serial.begin(115200);
    Serial.printf("Firmware build: %s\n", firmwareBuild);
    Serial.printf("BOOT reset=%s heap=%u chip=%08X flash=%u\n",
                  ESP.getResetReason().c_str(), ESP.getFreeHeap(),
                  ESP.getChipId(), ESP.getFlashChipRealSize());
    SPI.begin();
    helpArr_init();
    max7219_init();
    loadSettings();
    loadAuthSettings();
    loadDriftCalibration();
    max7219_set_brightness(settings.brightness * 3);// Map 1-5 levels to MAX7219 0-15
    rtc_init(SDA, SCL);
    clear_Display();
    refresh_display(); //take info into LEDarr
    //////////////////////////////////
    if (!autoConfig())//enter phone-based provisioning mode if Wi-Fi cannot connect
       startProvisioningAP();
    ///////////////////////////////////
    if (WiFi.status() == WL_CONNECTED) {
        // Keep the successful connection state visible before beginning the
        // longer NTP synchronisation phase.
        delay(1000);
        showStaticStatus("NTP");
    }
    int ntpSampleCount = 0;
    if (!provisioningMode) {
        for (int ntpAttempt = 0; ntpAttempt < 6 && ntpSampleCount < 3; ntpAttempt++) {
            if (ntpAttempt > 0)
                delay(500);
            if (connectNTP() != NULL)
                ntpSampleCount++;
        }
    }
    if (ntpSampleCount > 0) {
        // Carry the final sample forward from packet receipt to the current
        // instant, then wait for the next exact NTP second boundary.  The
        // previous code waited from a stale millisecond phase and could land
        // on either side of the boundary after a reset.
        uint64_t phaseUs = (uint64_t)ntpEstimatedFractionUs +
                           (uint32_t)(micros() - ntpReferenceMicros);
        unsigned long elapsedSeconds = (unsigned long)(phaseUs / 1000000ULL);
        unsigned long phaseRemainderUs = (unsigned long)(phaseUs % 1000000ULL);
        unsigned long boundaryWaitUs = 1000000UL - phaseRemainderUs;
        Serial.printf("NTP final: elapsed=%lu s phase=%lu us wait=%lu us\n",
                      elapsedSeconds, phaseRemainderUs, boundaryWaitUs);
        lastNtpCheckRttUs = lastNtpRoundTripUs;
        uint32_t boundaryStart = micros();
        while ((uint32_t)(micros() - boundaryStart) < boundaryWaitUs) {
            delay(1);
            yield();
        }
        uint32_t syncBoundaryMicros = micros();
        epoch = ntpEstimatedEpoch + elapsedSeconds + 1UL;
        configureTimezone();
        time_t stableEpoch = epoch;
        tm* stableLocal = localtime(&stableEpoch);
        rtc_set(stableLocal);
        softwareEpochBase = epoch;
        softwareClockElapsedUs = 0;
        softwareClockLastMicros = syncBoundaryMicros;
        softwareClockValid = true;
        nextDriftCheckAt = millis() + DRIFT_INITIAL_CHECK_DELAY_MS;
        Serial.printf("Clock base: epoch=%lu micros=%lu\n",
                      softwareEpochBase, (unsigned long)syncBoundaryMicros);
        lastNtpCheckResult = NTP_CHECK_SUCCESS;
        Serial.printf("RTC readback: %02u:%02u:%02u %02u/%02u/%04u\n",
                      (unsigned)rtc_stunde(), (unsigned)rtc_minute(), (unsigned)rtc_sekunde(),
                      (unsigned)rtc_tag(), (unsigned)rtc_monat(),
                      (unsigned)(rtc_jahr() + 2000));
    } else if (!provisioningMode) {
        Serial.println("no timepacket received");
        lastNtpCheckResult = NTP_CHECK_NO_VALID_REPLY;
        lastNtpCheckRttUs = 0;
    }
    // Hold a compact NTP result on the display for one second before the
    // normal clock renderer takes over.  Provisioning mode has no network
    // time source yet, so do not show a misleading Err! status there.
    if (!provisioningMode) {
        showStaticStatus(ntpSampleCount > 0 ? "OK!" : "Err!");
        delay(1000);
    }
    // Start the display ticker only after NTP has established the software
    // clock boundary.  Starting it before Wi-Fi/NTP setup left its one-second
    // phase unrelated to the actual clock and could make the display appear
    // almost one second late on every boot.
    f_tckr1s = false;
    displayTickDue = false;
    tckr.attach(0.05, timer50ms);
    setDisplayTicker(false);
    // On a successful normal boot, identify the clock once before handing
    // control to the time renderer.  This is intentionally separate from
    // API/date marquees so the physical button cannot cancel it.
    if (!provisioningMode && ntpSampleCount > 0 && WiFi.status() == WL_CONNECTED) {
        startupIpMarqueeMessage = WiFi.localIP().toString();
        startupIpMarqueeX = -((int)startupIpMarqueeMessage.length() * 6);
        startupIpMarqueeActive = true;
    }
    // Consume one immediate clock update when loop() starts.  Without this,
    // its zero-initialised digit buffers briefly rendered 00:00 00 after NTP.
    f_tckr1s = true;
    displayTickDue = true;
    if (WiFi.status() == WL_CONNECTED || (WiFi.getMode() & WIFI_AP))
        startWebInterface();
}
//**************************************************************************************************
// Main device loop.
void loop() {
    unsigned int sek1 = 0, sek2 = 0, min1 = 0, min2 = 0, std1 = 0, std2 = 0;
    unsigned int sek11 = 0, sek12 = 0, sek21 = 0, sek22 = 0;
    unsigned int min11 = 0, min12 = 0, min21 = 0, min22 = 0;
    unsigned int std11 = 0, std12 = 0, std21 = 0, std22 = 0;
    signed int x = 0; //x1,x2;
    signed int y = 0, y1 = 0, y2 = 0, y3=0;
    bool updown = false;
    unsigned int sc1 = 0, sc2 = 0, sc3 = 0, sc4 = 0, sc5 = 0, sc6 = 0;
    unsigned int f_scroll_x = false;
    bool downloadButtonWasPressed = false;
    bool buttonScrollLock = false;
    bool clockDisplayed = false;
    bool chronographMode = false;
    bool clockMarquee = false;
    bool chronographExitMarquee = false;
    bool chronographRunning = false;
    bool chronographStarted = false;
    bool longPressHandled = false;
    bool consumeButtonUntilRelease = false;
    bool apiButtonHoldLock = false;
    uint32_t buttonPressStarted = 0;
    uint32_t chronographStartedAtMicros = 0;
    uint32_t chronographElapsed = 0;
    uint32_t chronographPressElapsed = 0;
    uint32_t chronographExitElapsed = 0;
    uint8_t chronographExitProgress = 0;
    int64_t chronographRateRemainder = 0;
    uint32_t chronographLastActivity = 0;
    uint32_t buttonDisabledUntil = 0;
    bool dailyRestartPending = false;
    uint32_t mainClockResumeAt = 0;
    bool displayedSoftwareSecondValid = false;
    uint32_t displayedSoftwareEpoch = 0;

    z_PosX = maxPosX;
    d_PosX = -8;
    //  x=0; x1=0; x2=0;

    refresh_display();
    updown = true;
    if (updown == false) {
        y2 = -9;
        y1 = 8;
    }
    if (updown == true) { //scroll  up to down
        y2 = 8;
        y1 = -8;
    }
    // Seed transition history from the current clock value. Zero-initialised
    // history makes a non-zero hour tens digit sweep on boot and can briefly
    // render an all-zero frame.
    softwareClock2mez();
    mainColonVisible = (MEZ.sek12 % 2) == 0;
    sek1 = sek11 = sek12 = MEZ.sek1;
    sek2 = sek21 = sek22 = MEZ.sek2;
    min1 = min11 = min12 = MEZ.min1;
    min2 = min21 = min22 = MEZ.min2;
    std1 = std11 = std12 = MEZ.std1;
    std2 = std21 = std22 = MEZ.std2;
    while (true) {
        yield();  //keep ESP8266 background tasks running during long loops.
        if (servicePhysicalFactoryReset()) {
            // A cancelled warning consumes the held press.  Existing display
            // ownership (AP, date, API or clock marquee) resumes on the next
            // frame without receiving a release action.
            if (digitalRead(0) != LOW) {
                downloadButtonWasPressed = false;
                longPressHandled = false;
                consumeButtonUntilRelease = false;
                apiButtonHoldLock = false;
            }
            continue;
        }
        apiBlockedByChronograph = chronographMode;
        web.handleClient();
        bool restartTimeReached = currentHour24 == settings.restartHour &&
                                  MEZ.min12 == settings.restartMinute &&
                                  MEZ.sek12 == 0;
        if (chronographMode) {
            // Never interrupt a stopwatch session.  Remember that the
            // scheduled restart was missed and defer it until normal clock
            // mode has been visible for 15 uninterrupted minutes.
            if (restartTimeReached) {
                dailyRestartPending = true;
                mainClockResumeAt = 0;
            }
        } else if (dailyRestartPending) {
            if (!clockDisplayed || clockMarquee) {
                mainClockResumeAt = 0;
            } else if (mainClockResumeAt == 0) {
                mainClockResumeAt = millis();
            } else if ((uint32_t)(millis() - mainClockResumeAt) >= 900000UL) {
                commitDriftCalibrationIfReady();
                clear_Display();
                delay(500);
                ESP.restart();
            }
        } else if (restartTimeReached) {
            commitDriftCalibrationIfReady();
            clear_Display();
            delay(500);
            ESP.restart();
        }
        // Calibration is deliberately deferred while the user is timing or a
        // marquee owns the display. A valid result may then slew the live
        // wall clock smoothly; it never steps the display or rewrites the RTC.
        if (softwareClockValid && nextDriftCheckAt != 0 &&
            (int32_t)(millis() - nextDriftCheckAt) >= 0 &&
            !chronographMode && !apiMessageActive && !apiMessagePending &&
            !f_scroll_x && !clockMarquee) {
            runDriftCalibrationCheck();
        }
        if (!provisioningMode && f_tckr1s == true) {
            f_tckr1s = false;
            bool renderNewSecond = false;
            if (softwareClockValid) {
                uint32_t polledEpoch = softwareClockEpochNow();
                renderNewSecond = !displayedSoftwareSecondValid ||
                                  polledEpoch != displayedSoftwareEpoch;
            } else {
                // With no valid NTP source, retain the original one-second
                // RTC display cadence rather than repeatedly reading the RTC.
                renderNewSecond = rtcSecondTickDue;
            }

            if (renderNewSecond) {
                rtcSecondTickDue = false;
                if(!digitalRead(16)){kk=1;}else kk=0;
            softwareClock2mez();
            if (softwareClockValid) {
                displayedSoftwareEpoch = softwareEpochBase +
                    (uint32_t)(softwareClockElapsedUs / 1000000ULL);
                displayedSoftwareSecondValid = true;
            }
            // Latch the main-clock colon phase once per second.  This avoids
            // basing the blink on transitional digit buffers.
            mainColonVisible = (MEZ.sek12 % 2) == 0;
            sek1 = MEZ.sek1;
            sek2 = MEZ.sek2;
            min1 = MEZ.min1;
            min2 = MEZ.min2;
            std1 = MEZ.std1;
            std2 = MEZ.std2;
            y = y2;                 //scroll updown
            // MEZ already contains the current NTP-aligned time. Animate
            // only digits that changed; do not manually advance one second.
            sc1 = (sek1 != sek12);
            sc2 = (sek2 != sek22);
            sc3 = (min1 != min12);
            sc4 = (min2 != min22);
            sc5 = (std1 != std12);
            sc6 = (std2 != std22);

            // Clean digit changes: keep the date/message marquee, but render
            // changed clock digits directly instead of sweeping them vertically.
            if (settings.cleanTransitions) {
                sc1 = 0;
                sc2 = 0;
                sc3 = 0;
                sc4 = 0;
                sc5 = 0;
                sc6 = 0;
                y = 0;
                // Instant mode renders the actual clock values without any
                // transition frames.
                sek1 = MEZ.sek1;
                sek2 = MEZ.sek2;
                min1 = MEZ.min1;
                min2 = MEZ.min2;
                std1 = MEZ.std1;
                std2 = MEZ.std2;
            }

            sek11 = sek12;
            sek12 = sek1;
            sek21 = sek22;
            sek22 = sek2;
            min11 = min12;
            min12 = min1;
            min21 = min22;
            min22 = min2;
            std11 = std12;
            std12 = std1;
            std21 = std22;
            std22 = std2;
            // Automatic date scrolling is available only after a successful
            // NTP synchronisation.  The RTC still drives the normal clock
            // display when NTP is unavailable, but we do not start a date
            // marquee from potentially stale calendar data.
            // Do not let an automatic date event interrupt a startup/API/NTP
            // return animation.  Its scheduled instant is simply skipped if
            // the clock has not yet settled into its static display.
            bool dateIntervalTrigger = softwareClockValid && !clockMarquee &&
                !chronographExitMarquee &&
                !startupIpMarqueeActive &&
                ((settings.scrolling == 1 && MEZ.sek12 == 46) ||
                 (settings.scrolling == 2 && (MEZ.min12 % 15) == 14 && MEZ.sek12 == 46) ||
                 (settings.scrolling == 3 && MEZ.min12 == 59 && MEZ.sek12 == 46));
            if (dateIntervalTrigger)
                f_scroll_x = true;//marquee enable flag
            // When startup NTP synchronisation failed, periodically remind
            // the user to configure a valid server.  Keep this separate from
            // the normal date marquee, which remains disabled in this state.
            if (!softwareClockValid && (MEZ.sek12 == 1 || MEZ.sek12 == 31) &&
                !chronographMode && !apiMessageActive && !apiMessagePending &&
                !ntpFailureMarqueeActive && !ntpClockExitMarquee &&
                !f_scroll_x && !clockMarquee && !chronographExitMarquee) {
                ntpClockExitMarquee = true;
                ntpFailureMarqueeMessage = String("Set NTP @ ") + WiFi.localIP().toString();
                z_PosX = maxPosX;
                buttonScrollLock = true;
            }
            } // end calibrated clock-second update
        } // end clock poll
        if (displayTickDue) { // Run at the active display interval.
            displayTickDue = false;
            if (provisioningMode) {
                static uint32_t setupLastStep = 0;
                clear_Display();
                for (size_t i = 0; i < sizeof(setupMessage) - 1; i++)
                    // Matrix columns are horizontally reversed on this build.
                    char2Arr(setupMessage[(sizeof(setupMessage) - 2) - i], setupMessageX + (int)(i * 6), 0);
                // The setup instruction has one fixed speed, independent of
                // the user-selectable normal date/message speed.
                uint32_t setupNow = millis();
                if (setupLastStep == 0)
                    setupLastStep = setupNow - 30;
                if ((uint32_t)(setupNow - setupLastStep) >= 30) {
                    setupMessageX++;
                    setupLastStep = setupNow;
                }
                if (setupMessageX > 31)
                    setupMessageX = -((int)(sizeof(setupMessage) - 1) * 6);
                refresh_display();
                continue;
            }
            if (startupIpMarqueeActive) {
                // This boot-only confirmation is deliberately handled before
                // button processing, so it always completes once and cannot
                // accidentally open the date or chronograph functions.
                clear_Display();
                int messageLength = startupIpMarqueeMessage.length();
                for (int i = 0; i < messageLength; i++)
                    char2Arr(startupIpMarqueeMessage[(messageLength - 1) - i],
                             startupIpMarqueeX + (i * 6), 0);
                refresh_display();
                startupIpMarqueeX++;
                if (startupIpMarqueeX > (int)maxPosX + 8) {
                    startupIpMarqueeActive = false;
                    startupIpMarqueeX = 0;
                    // Bring the clock in using its standard marquee motion.
                    z_PosX = 0;
                    d_PosX = -200;
                    clockMarquee = true;
                    buttonScrollLock = true;
                }
                continue;
            }
            bool downloadButtonPressed = (digitalRead(0) == LOW);
            uint32_t buttonNow = millis();
            if (downloadButtonPressed && !downloadButtonWasPressed) {
                buttonPressStarted = buttonNow;
                longPressHandled = (int32_t)(buttonNow - buttonDisabledUntil) < 0;
                if ((apiCancelAfterCurrent || apiGracefulReturn) &&
                    (int32_t)(buttonNow - apiCancelDeadline) < 0) {
                    // A second press overrides graceful cancellation and
                    // restores the clock immediately without a marquee.
                    apiMessageActive = false;
                    apiCancelAfterCurrent = false;
                    apiGracefulReturn = false;
                    apiCancelDeadline = 0;
                    apiMessagePending = false;
                    apiMessageQueueCount = 0;
                    apiMessageQueueHead = apiMessageQueueTail = 0;
                    z_PosX = maxPosX;
                    d_PosX = -8;
                    clockMarquee = false;
                    clockDisplayed = true;
                    buttonScrollLock = false;
                    buttonDisabledUntil = buttonNow + 100UL;
                    longPressHandled = true;
                }
            }
            // Do not allow a hold begun during (or carried into) the date
            // marquee to enter chronograph mode.  Releasing the button resets
            // this guard so the next press is a clean attempt.
            if (downloadButtonPressed && f_scroll_x)
                longPressHandled = true;
            if (apiMessageActive && downloadButtonPressed)
                apiButtonHoldLock = true;
            // Enter as soon as the one-second threshold is crossed.  Consume
            // the eventual release so this same hold cannot immediately start
            // or exit the chronograph.  The independent factory-reset timer
            // continues if the button remains held.
            if (downloadButtonPressed &&
                !chronographMode && clockDisplayed &&
                !apiMessageActive &&
                !apiButtonHoldLock &&
                !buttonScrollLock && !longPressHandled &&
                (uint32_t)(buttonNow - buttonPressStarted) >= 1000UL &&
                (uint32_t)(buttonNow - buttonPressStarted) < 8000UL) {
                chronographMode = true;
                chronographRunning = false;
                chronographStarted = false;
                chronographElapsed = 0;
                chronographRateRemainder = 0;
                chronographLastActivity = buttonNow;
                longPressHandled = true;
                consumeButtonUntilRelease = true;
                f_scroll_x = false;
                buttonScrollLock = true;
                setDisplayTicker(false);
                // Preserve the physical pressed state so the continuous hold
                // can still reach the factory-reset warning and threshold.
                downloadButtonWasPressed = downloadButtonPressed;
                showChronograph(0, true, false);
                continue;
            }
            if (chronographMode) {
                uint32_t chronographLimit = chronographLimitMs();
                uint32_t chronoDisplayMs = chronographElapsed;
                if (chronographRunning) {
                    int64_t previewRemainder = chronographRateRemainder;
                    uint32_t runningMs = adjustedChronographDeltaMs(
                        chronographStartedAtMicros, micros(), previewRemainder);
                    if (chronographElapsed >= chronographLimit ||
                        runningMs >= chronographLimit - chronographElapsed) {
                        chronographElapsed = chronographLimit;
                        chronoDisplayMs = chronographLimit;
                        chronographRunning = false;
                        // Keep the session marked as started so a short press
                        // cannot restart a timer that has reached its cap.
                        chronographStarted = true;
                        chronographLastActivity = buttonNow;
                        recordChronographStop(chronographElapsed);
                        setDisplayTicker(false);
                    } else {
                        chronoDisplayMs += runningMs;
                    }
                }
                if (!chronographRunning &&
                    (uint32_t)(buttonNow - chronographLastActivity) >= 900000UL) {
                    chronographExitElapsed = chronographElapsed;
                    chronographExitProgress = 0;
                    chronographExitMarquee = true;
                    chronographMode = false;
                    chronographRunning = false;
                    chronographStarted = false;
                    buttonScrollLock = false;
                    clockDisplayed = false;
                    f_tckr1s = true;
                    buttonDisabledUntil = buttonNow + 50UL;
                    longPressHandled = true;
                    setDisplayTicker(false);
                    z_PosX = 0;
                    d_PosX = -200;
                    clockMarquee = false;
                    buttonScrollLock = true;
                    continue;
                }
                // A press that begins while running stops on release.  Once
                // stopped, a one-second hold either resets a non-zero value or
                // exits immediately at zero; that hold is then consumed.
                if (downloadButtonPressed && !downloadButtonWasPressed && !longPressHandled) {
                    chronographLastActivity = buttonNow;
                    chronographPressElapsed = chronographElapsed;
                    if (chronographRunning) {
                        int64_t previewRemainder = chronographRateRemainder;
                        chronographPressElapsed += adjustedChronographDeltaMs(
                            chronographStartedAtMicros, micros(), previewRemainder);
                    }
                }
                if (downloadButtonPressed && !chronographRunning && !longPressHandled &&
                    (uint32_t)(buttonNow - buttonPressStarted) >= 1000UL) {
                    longPressHandled = true;
                    consumeButtonUntilRelease = true;
                    if (chronographPressElapsed != 0) {
                        chronographRunning = false;
                        chronographStarted = false;
                        chronographElapsed = 0;
                        chronographRateRemainder = 0;
                    } else {
                        // At zero, exit immediately at the one-second mark
                        // while the button is still held.  Its later release
                        // is consumed by the normal clock-mode input path.
                        chronographExitElapsed = chronographElapsed;
                        chronographExitProgress = 0;
                        chronographExitMarquee = true;
                        chronographMode = false;
                        chronographRunning = false;
                        chronographStarted = false;
                        clockDisplayed = false;
                        f_tckr1s = true;
                        setDisplayTicker(false);
                        z_PosX = 0;
                        d_PosX = -200;
                        clockMarquee = false;
                        buttonScrollLock = true;
                        downloadButtonWasPressed = downloadButtonPressed;
                        continue;
                    }
                }
                if (!downloadButtonPressed && downloadButtonWasPressed) {
                    bool wasLongPress = longPressHandled;
                    chronographLastActivity = buttonNow;
                    // Short-press actions execute on release.  This also
                    // cleanly separates them from the long-press action.
                    if (!longPressHandled) {
                        if (!chronographStarted) {
                            chronographStarted = true;
                            chronographRunning = true;
                            chronographStartedAtMicros = micros();
                            chronographRateRemainder = 0;
                            setDisplayTicker(true);
                        } else if (chronographRunning) {
                            chronographElapsed += adjustedChronographDeltaMs(
                                chronographStartedAtMicros, micros(), chronographRateRemainder);
                            if (chronographElapsed > chronographLimit)
                                chronographElapsed = chronographLimit;
                            chronographRunning = false;
                            recordChronographStop(chronographElapsed);
                            setDisplayTicker(false);
                        } else {
                            if (chronographElapsed < chronographLimit) {
                                chronographRunning = true;
                                chronographStartedAtMicros = micros();
                                chronographRateRemainder = 0;
                                setDisplayTicker(true);
                            }
                        }
                    }
                    if (wasLongPress)
                        buttonDisabledUntil = buttonNow + 50UL;
                    consumeButtonUntilRelease = false;
                    longPressHandled = false;
                }
                downloadButtonWasPressed = downloadButtonPressed;
                chronoDisplayMs = chronographElapsed;
                if (chronographRunning) {
                    int64_t previewRemainder = chronographRateRemainder;
                    chronoDisplayMs = min(chronographLimit, chronoDisplayMs +
                                          adjustedChronographDeltaMs(
                                              chronographStartedAtMicros, micros(),
                                              previewRemainder));
                }
                if (chronographMode)
                    showChronograph(chronoDisplayMs,
                                    !chronographRunning || ((chronoDisplayMs / 10UL) % 100UL < 50UL),
                                    chronographRunning);
                continue;
            }
            // A short press still starts the configured date scroll, but only
            // after release so a one-second hold can enter chronograph mode.
            if (!downloadButtonPressed && downloadButtonWasPressed &&
                !longPressHandled && clockDisplayed &&
                !apiMessageActive && !apiCancelAfterCurrent && !apiMessagePending &&
                !buttonScrollLock && (int32_t)(buttonNow - buttonDisabledUntil) >= 0 &&
                (uint32_t)(buttonNow - buttonPressStarted) < 1000UL) {
                f_scroll_x = true;
                d_PosX = -8;
                buttonScrollLock = true;
            }
            if (!downloadButtonPressed && downloadButtonWasPressed) {
                longPressHandled = false;
                consumeButtonUntilRelease = false;
            }
            bool buttonReleased = !downloadButtonPressed && downloadButtonWasPressed;
            downloadButtonWasPressed = downloadButtonPressed;
            if (buttonReleased && (apiMessageActive || apiMessagePending)) {
                if (apiMessageActive) {
                    apiCancelAfterCurrent = true;
                    apiCancelDeadline = buttonNow + 1000UL;
                    buttonScrollLock = true;
                } else {
                    apiMessagePending = false;
                    buttonScrollLock = false;
                }
                apiMessageQueueCount = 0;
                apiMessageQueueHead = apiMessageQueueTail = 0;
                z_PosX = maxPosX;
                d_PosX = -8;
                clockMarquee = false;
                buttonDisabledUntil = buttonNow + 100UL;
                clockDisplayed = true;
                longPressHandled = false;
            }
            if (buttonReleased)
                apiButtonHoldLock = false;
            if (apiMessagePending && clockDisplayed && !chronographMode &&
                !clockMarquee && !f_scroll_x && !apiMessageActive &&
                !apiClockExitMarquee && !ntpClockExitMarquee) {
                apiMessagePending = false;
                apiMessage = apiMessageQueue[apiMessageQueueHead];
                apiMessageQueue[apiMessageQueueHead] = "";
                apiMessageQueueHead = (apiMessageQueueHead + 1) % API_MESSAGE_QUEUE_SIZE;
                if (apiMessageQueueCount > 0)
                    apiMessageQueueCount--;
                apiMessagePending = apiMessageQueueCount > 0;
                apiMessageActive = false;
                apiMessagePass = 0;
                apiClockExitMarquee = true;
                z_PosX = maxPosX;
                buttonScrollLock = true;
            }
            if (apiClockExitMarquee || ntpClockExitMarquee) {
                // Match the date marquee's clock-out phase before bringing a
                // message in. The display is horizontally reversed, so
                // increasing z_PosX moves the clock off-screen.
                z_PosX++;
                if (z_PosX > (unsigned int)(maxPosX + 32)) {
                    z_PosX = maxPosX;
                    if (apiClockExitMarquee) {
                        apiClockExitMarquee = false;
                        apiMessageActive = true;
                        apiMessageX = -((int)apiMessage.length() * 6);
                    }
                    if (ntpClockExitMarquee) {
                        ntpClockExitMarquee = false;
                        ntpFailureMarqueeActive = true;
                        ntpFailureMarqueeX = -((int)ntpFailureMarqueeMessage.length() * 6);
                    }
                }
            }
            if (apiMessageActive) {
                clear_Display();
                int messageLength = apiMessage.length();
                for (int i = 0; i < messageLength; i++)
                    char2Arr(apiMessage[(messageLength - 1) - i], apiMessageX + (i * 6), 0);
                refresh_display();
                apiMessageX++;
                // Allow the complete glyph width to clear the right edge
                // before either starting the next pass or restoring the
                // clock.  The number of passes is user-configurable.
                if (apiMessageX > (int)maxPosX + 8) {
                if (apiCancelAfterCurrent) {
                        apiCancelAfterCurrent = false;
                        apiCancelDeadline = 0;
                        apiMessageActive = false;
                        apiMessageX = 0;
                        apiMessagePass = 0;
                        z_PosX = 0;
                        d_PosX = -200;
                        clockMarquee = true;
                        apiGracefulReturn = true;
                        buttonScrollLock = true;
                    } else if (apiMessageScrollCount == 0 ||
                    apiMessagePass + 1 < apiMessageScrollCount) {
                        apiMessagePass++;
                        apiMessageX = -((int)apiMessage.length() * 6);
                    } else {
                        apiMessageActive = false;
                        apiMessageX = 0;
                        apiMessagePass = 0;
                        if (apiMessageScrollCount == 0) {
                            apiMessageActive = true;
                            apiMessagePass = 0;
                            apiMessageX = -((int)apiMessage.length() * 6);
                            buttonScrollLock = true;
                        } else if (apiMessageQueueCount > 0) {
                            // Keep notifications contiguous while messages
                            // remain queued; do not show the clock between
                            // individual API messages.
                            apiMessagePending = true;
                            buttonScrollLock = true;
                        } else {
                            // Bring the normal clock back with its
                            // established left-to-right marquee, after the
                            // final message has fully cleared the display.
                            z_PosX = 0;
                            d_PosX = -200;
                            clockMarquee = true;
                            buttonScrollLock = true;
                        }
                    }
                }
                continue;
            }
            if (ntpFailureMarqueeActive) {
                clear_Display();
                int messageLength = ntpFailureMarqueeMessage.length();
                for (int i = 0; i < messageLength; i++)
                    // Matrix columns are horizontally reversed on this build.
                    char2Arr(ntpFailureMarqueeMessage[(messageLength - 1) - i],
                             ntpFailureMarqueeX + (i * 6), 0);
                refresh_display();
                ntpFailureMarqueeX++;
                if (ntpFailureMarqueeX > (int)maxPosX + 8) {
                    ntpFailureMarqueeActive = false;
                    ntpFailureMarqueeX = 0;
                    // Return to the RTC-backed clock with the normal clock
                    // marquee after the reminder has fully cleared.
                    z_PosX = 0;
                    d_PosX = -200;
                    clockMarquee = true;
                    buttonScrollLock = true;
                }
                continue;
            }
            if (chronographExitMarquee) {
                // Compose both displays in one frame: the frozen chronograph
                // moves out while the live clock enters at the same speed,
                // separated by the same eight blank columns as clock/date.
                clear_Display();
                z_PosX = chronographExitProgress >= CHRONOGRAPH_CLOCK_LEAD_COLUMNS
                    ? chronographExitProgress - CHRONOGRAPH_CLOCK_LEAD_COLUMNS
                    : 0;
            } else if (clockMarquee) {
                z_PosX++;
                if (z_PosX >= maxPosX) {
                    z_PosX = maxPosX;
                    clockMarquee = false;
                    apiGracefulReturn = false;
                    apiCancelDeadline = 0;
                    d_PosX = -8;
                    buttonScrollLock = false;
                }
            } else if (f_scroll_x == true) {
                z_PosX++;
                d_PosX++;
                if (d_PosX == 101)
                    z_PosX = 0;
                if (z_PosX == maxPosX) {
                    f_scroll_x = false;
                    d_PosX = -8;
                    buttonScrollLock = false;
                }
            }
            if (sc1 == 1) {
                if (updown == 1)
                    y--;
                else
                    y++;
               y3 = y;
               if (y3 > 0) {
                y3 = 0;
                }     
               char22Arr(48 + sek12, z_PosX - 27, y3);
               char22Arr(48 + sek11, z_PosX - 27, y + y1);
                if (y == 0) {
                    sc1 = 0;
                }
            }
            else
                char22Arr(48 + sek1, z_PosX - 27, 0);

            if (sc2 == 1) {
                char22Arr(48 + sek22, z_PosX - 23, y3);
                char22Arr(48 + sek21, z_PosX - 23, y + y1);
                if (y == 0)
                    sc2 = 0;
            }
            else
              char22Arr(48 + sek2, z_PosX - 23, 0);

            if (sc3 == 1) {
                char2Arr(48 + min12, z_PosX - 18, y);
                char2Arr(48 + min11, z_PosX - 18, y + y1);
                if (y == 0)
                    sc3 = 0;
            }
            else
                char2Arr(48 + min1, z_PosX - 18, 0);

            if (sc4 == 1) {
                char2Arr(48 + min22, z_PosX - 13, y);
                char2Arr(48 + min21, z_PosX - 13, y + y1);
                if (y == 0)
                    sc4 = 0;
            }
            else
                char2Arr(48 + min2, z_PosX - 13, 0);

              if (!settings.clockColonBlink || mainColonVisible)
                  char2Arr(':', z_PosX - 10 + x, 0);
              else
                  clearGlyphColumns(z_PosX - 10 + x, 4);

            if (sc5 == 1) {
                char2Arr(48 + std12, z_PosX - 4, y);
                char2Arr(48 + std11, z_PosX - 4, y + y1);
                if (y == 0)
                    sc5 = 0;
            }
            else
                char2Arr(48 + std1, z_PosX - 4, 0);

            if (sc6 == 1) {
                char2Arr(48 + std22, z_PosX + 1, y);
                char2Arr(48 + std21, z_PosX + 1, y + y1);
                if (y == 0)
                    sc6 = 0;
            }
            else
                char2Arr(48 + std2, z_PosX + 1, 0);

            if (!apiClockExitMarquee && !ntpClockExitMarquee) {
            char2Arr(' ', d_PosX+5, 0);        //day of the week
            char2Arr(WT_arr[MEZ.WT][0], d_PosX - 1, 0);        //day of the week
            char2Arr(WT_arr[MEZ.WT][1], d_PosX - 7, 0);
            char2Arr(WT_arr[MEZ.WT][2], d_PosX - 13, 0);
            char2Arr(WT_arr[MEZ.WT][3], d_PosX - 19, 0);
            char2Arr(48 + MEZ.tag2, d_PosX - 24, 0);           //day
            char2Arr(48 + MEZ.tag1, d_PosX - 30, 0);
            char2Arr(M_arr[MEZ.mon12 - 1][0], d_PosX - 39, 0); //month
            char2Arr(M_arr[MEZ.mon12 - 1][1], d_PosX - 43, 0);
            char2Arr(M_arr[MEZ.mon12 - 1][2], d_PosX - 49, 0);
            char2Arr(M_arr[MEZ.mon12 - 1][3], d_PosX - 55, 0);
            char2Arr(M_arr[MEZ.mon12 - 1][4], d_PosX - 61, 0);
            char2Arr('2', d_PosX - 68, 0);                     //year
            char2Arr('0', d_PosX - 74, 0);
            char2Arr(48 + MEZ.jahr2, d_PosX - 80, 0);
            char2Arr(48 + MEZ.jahr1, d_PosX - 86, 0);
            }

            if (chronographExitMarquee) {
                // Keep the incoming clock completely hidden during its
                // eight-column lead-in; z_PosX is unsigned and cannot carry
                // the negative anchor used by the date marquee.
                if (chronographExitProgress < CHRONOGRAPH_CLOCK_LEAD_COLUMNS)
                    clear_Display();
                drawChronographAt(chronographExitElapsed, true, false,
                                  (int)maxPosX + chronographExitProgress);
            }

            refresh_display(); // Refresh at the configured display interval.
            if (chronographExitMarquee) {
                if (chronographExitProgress >=
                    maxPosX + CHRONOGRAPH_CLOCK_LEAD_COLUMNS) {
                    chronographExitMarquee = false;
                    chronographExitProgress = 0;
                    z_PosX = maxPosX;
                    d_PosX = -8;
                    buttonScrollLock = false;
                    buttonDisabledUntil = millis() + 50UL;
                } else {
                    chronographExitProgress++;
                }
            }
            if (softwareClockValid && !chronographExitMarquee)
                clockDisplayed = true;
        } // End configured display interval.
    }  //end while(true)
}


