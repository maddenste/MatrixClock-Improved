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

MatrixClock Improved Firmware v3.1.2

This firmware is a modified and extended build of the original HACK LABS
MatrixClock project attributed above.

Modified, integrated, and maintained by:
Steve Madden
steve@maddenuk.net

Modified build date: 2026-09-11

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
#define MATRIXCLOCK_FIRMWARE_VERSION "v3.1.2"

#include <SPI.h>
#include <Ticker.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Updater.h>
#include <EEPROM.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <time.h>
#include <core_esp8266_features.h> // ESP8266 monotonic 64-bit microsecond timer

// Timezone presets and strict POSIX validation adapted from the CH-899 project.
// Methods stay inside a class to avoid Arduino-generated prototype ordering
// problems. Existing ClockSettings fields and EEPROM offsets remain unchanged.
class MatrixClockTimezone {
public:
    struct Preset {
        char id[32];
        char label[56];
        char standard[16];
        char automatic[40];
    };
    static constexpr size_t COUNT = 40;
    static constexpr size_t RULE_CAPACITY = 64;
    static bool preset(size_t index, Preset& result) {
        // Single source of truth: id, label, standard-time rule, automatic rule.
        // Regional rules reflect IANA 2026.3; future legislation needs updates.
        static const Preset PRESETS[] PROGMEM = {
            {"baker", "(UTC-12:00) Baker Island", "<-12>12", "<-12>12"},
            {"american_samoa", "(UTC-11:00) American Samoa", "SST11", "SST11"},
            {"honolulu", "(UTC-10:00) Honolulu", "HST10", "HST10"},
            {"anchorage", "(UTC-09:00) Anchorage", "AKST9", "AKST9AKDT,M3.2.0,M11.1.0"},
            {"los_angeles", "(UTC-08:00) Los Angeles", "PST8", "PST8PDT,M3.2.0,M11.1.0"},
            {"phoenix_vancouver", "(UTC-07:00) Phoenix, Vancouver", "MST7", "MST7"},
            {"denver", "(UTC-07:00) Denver", "DEN7", "MST7MDT,M3.2.0,M11.1.0"},
            {"chicago", "(UTC-06:00) Chicago", "CHI6", "CST6CDT,M3.2.0,M11.1.0"},
            {"mexico_city", "(UTC-06:00) Mexico City", "CST6", "CST6"},
            {"new_york", "(UTC-05:00) New York", "EST5", "EST5EDT,M3.2.0,M11.1.0"},
            {"lima", "(UTC-05:00) Lima", "<-05>5", "<-05>5"},
            {"halifax", "(UTC-04:00) Halifax", "AST4", "AST4ADT,M3.2.0,M11.1.0"},
            {"santiago", "(UTC-04:00) Santiago", "<-04>4", "<-04>4<-03>,M9.1.6/24,M4.1.6/24"},
            {"newfoundland", "(UTC-03:30) Newfoundland", "NST3:30", "NST3:30NDT,M3.2.0,M11.1.0"},
            {"buenos_aires", "(UTC-03:00) Buenos Aires", "<-03>3", "<-03>3"},
            {"south_georgia", "(UTC-02:00) South Georgia", "<-02>2", "<-02>2"},
            {"azores", "(UTC-01:00) Azores", "<-01>1", "<-01>1<+00>,M3.5.0/0,M10.5.0/1"},
            {"utc_reykjavik", "(UTC+00:00) UTC / Reykjavik", "UTC0", "UTC0"},
            {"london", "(UTC+00:00) London (GMT/BST)", "GMT0", "GMT0BST,M3.5.0/1,M10.5.0/2"},
            {"paris_berlin", "(UTC+01:00) Paris, Berlin", "CET-1", "CET-1CEST,M3.5.0,M10.5.0/3"},
            {"athens", "(UTC+02:00) Athens", "EET-2", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
            {"johannesburg", "(UTC+02:00) Johannesburg", "SAST-2", "SAST-2"},
            {"moscow_nairobi", "(UTC+03:00) Moscow, Nairobi", "MSK-3", "MSK-3"},
            {"tehran", "(UTC+03:30) Tehran", "<+0330>-3:30", "<+0330>-3:30"},
            {"dubai", "(UTC+04:00) Dubai, Abu Dhabi", "<+04>-4", "<+04>-4"},
            {"karachi", "(UTC+05:00) Karachi, Islamabad", "PKT-5", "PKT-5"},
            {"almaty", "(UTC+05:00) Almaty", "<+05>-5", "<+05>-5"},
            {"india_sri_lanka", "(UTC+05:30) India, Sri Lanka", "IST-5:30", "IST-5:30"},
            {"dhaka", "(UTC+06:00) Dhaka", "<+06>-6", "<+06>-6"},
            {"yangon", "(UTC+06:30) Yangon", "<+0630>-6:30", "<+0630>-6:30"},
            {"bangkok_jakarta", "(UTC+07:00) Bangkok, Jakarta", "<+07>-7", "<+07>-7"},
            {"singapore_beijing", "(UTC+08:00) Singapore, Beijing", "<+08>-8", "<+08>-8"},
            {"tokyo_seoul", "(UTC+09:00) Tokyo, Seoul", "JST-9", "JST-9"},
            {"darwin", "(UTC+09:30) Darwin", "ACST-9:30", "ACST-9:30"},
            {"sydney_melbourne", "(UTC+10:00) Sydney, Melbourne", "AEST-10", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
            {"solomon", "(UTC+11:00) Solomon Islands", "<+11>-11", "<+11>-11"},
            {"auckland", "(UTC+12:00) Auckland", "NZST-12", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
            {"fiji", "(UTC+12:00) Fiji", "<+12>-12", "<+12>-12"},
            {"samoa_tonga", "(UTC+13:00) Samoa, Tonga", "<+13>-13", "<+13>-13"},
            {"kiritimati", "(UTC+14:00) Kiritimati", "<+14>-14", "<+14>-14"},
        };
        static_assert(sizeof(PRESETS) / sizeof(PRESETS[0]) == COUNT, "Timezone count mismatch");
        if (index >= COUNT) return false;
        memcpy_P(&result, &PRESETS[index], sizeof(result));
        return true;
    }
private:
    class Reader {
    public:
        Reader(const char* text, size_t length) : text_(text), length_(length), pos_(0) {}
        char current() const { return pos_ < length_ ? text_[pos_] : '\0'; }
        bool done() const { return pos_ == length_; }
        void advance() { if (pos_ < length_) ++pos_; }
        bool take(char c) {
            if (done() || current() != c) return false;
            advance();
            return true;
        }
        bool number(unsigned maximum, unsigned maxDigits, unsigned& value) {
            unsigned result = 0, count = 0;
            while (digit(current())) {
                if (++count > maxDigits) return false;
                result = result * 10 + static_cast<unsigned>(current() - '0');
                if (result > maximum) return false;
                advance();
            }
            if (!count) return false;
            value = result;
            return true;
        }
    private:
        const char* text_;
        size_t length_, pos_;
    };
    static bool digit(char c) { return c >= '0' && c <= '9'; }
    static bool alpha(char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    }
    static bool abbreviation(Reader& reader) {
        const bool quoted = reader.take('<');
        const unsigned maximum = quoted ? 8 : 10;
        unsigned count = 0;
        while (alpha(reader.current()) ||
               (quoted && (digit(reader.current()) || reader.current() == '+' || reader.current() == '-'))) {
            if (++count > maximum) return false;
            reader.advance();
        }
        return count >= 3 && (!quoted || reader.take('>'));
    }
    static bool clockTime(Reader& reader, unsigned maxHour, unsigned hourDigits) {
        unsigned value = 0;
        if (!reader.number(maxHour, hourDigits, value)) return false;
        if (reader.take(':')) {
            if (!reader.number(59, 2, value)) return false;
            if (reader.take(':') && !reader.number(59, 2, value)) return false;
        }
        return true;
    }
    static bool offset(Reader& reader) {
        if (reader.current() == '+' || reader.current() == '-') reader.advance();
        // Unlike CH-899's minute-only MM32 protocol, preserve second-resolution
        // offsets in MatrixClock's existing custom POSIX rules.
        return clockTime(reader, 24, 2);
    }
    static bool transition(Reader& reader) {
        unsigned value = 0;
        if (reader.take('M')) {
            if (!reader.number(12, 2, value) || value == 0 || !reader.take('.')) return false;
            if (!reader.number(5, 1, value) || value == 0 || !reader.take('.')) return false;
            if (!reader.number(6, 1, value)) return false;
        } else if (reader.take('J')) {
            if (!reader.number(365, 3, value) || value == 0) return false;
        } else if (!reader.number(365, 3, value)) return false;
        return !reader.take('/') || clockTime(reader, 167, 3);
    }
public:
    // Complete-input grammar for ESP8266 core 3.1.2's newlib-xtensa parser.
    // Explicit DST dates are required; /24 is supported, /2u and negative
    // transition times are not. This validates syntax, not geographical accuracy.
    static bool validRule(const char* text, size_t length) {
        if (!text || !length || length >= RULE_CAPACITY) return false;
        for (size_t i = 0; i < length; ++i) {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            if (c <= 0x20 || c >= 0x7f) return false;
        }
        Reader reader(text, length);
        if (!abbreviation(reader) || !offset(reader)) return false;
        if (reader.done()) return true;
        if (!abbreviation(reader)) return false;
        if (reader.current() != ',' && !offset(reader)) return false;
        return reader.take(',') && transition(reader) &&
               reader.take(',') && transition(reader) && reader.done();
    }
    static bool fixedUtc(const char* id) {
        if (strncmp(id, "UTC", 3) != 0 || !validRule(id, strlen(id))) return false;
        Reader reader(id + 3, strlen(id + 3));
        return offset(reader) && reader.done();
    }
    static bool find(const char* id, Preset& result) {
        // Unambiguous old regional identifiers remain compatible after OTA.
        if (strcmp(id, "Europe/London") == 0) id = "london";
        else if (strcmp(id, "Europe/Paris") == 0) id = "paris_berlin";
        else if (strcmp(id, "America/New_York") == 0) id = "new_york";
        else if (strcmp(id, "America/Los_Angeles") == 0) id = "los_angeles";
        else if (strcmp(id, "Australia/Sydney") == 0) id = "sydney_melbourne";
        else if (strcmp(id, "UTC0") == 0) id = "utc_reykjavik";
        for (size_t i = 0; i < COUNT; ++i) {
            preset(i, result);
            if (strcmp(result.id, id) == 0) return true;
        }
        return false;
    }
    static bool validSelection(const char* id, const char* custom, uint8_t mode) {
        Preset entry;
        if (mode > 2 || (!find(id, entry) && !fixedUtc(id))) return false;
        const size_t length = strlen(custom);
        if (length && !validRule(custom, length)) return false;
        return mode != 2 || length != 0;
    }
    static bool resolve(const char* id, const char* custom, uint8_t mode,
                        char* rule, size_t capacity) {
        if (!validSelection(id, custom, mode)) return false;
        Preset entry;
        const char* selected = custom;
        if (mode != 2) {
            if (find(id, entry)) selected = mode == 1 ? entry.automatic : entry.standard;
            else selected = id; // Legacy fixed offset: never guess a city.
        }
        return strlcpy(rule, selected, capacity) < capacity;
    }
    static bool normalize(char* id, char* custom, uint8_t& mode, int legacyOffset,
                          bool& recovered) {
        // Called only on terminated ClockSettings buffers: 32-byte id, 64-byte rule.
        bool changed = false;
        if (mode > 2) { mode = 0; recovered = changed = true; }
        if (custom[0] && !validRule(custom, strlen(custom))) {
            custom[0] = '\0';
            recovered = changed = true;
        }
        if (mode == 2 && !custom[0]) {
            mode = 1; // Match the previous empty-custom fallback to Automatic.
            recovered = changed = true;
        }
        Preset entry;
        if (find(id, entry)) {
            if (strcmp(id, entry.id) != 0) {
                strlcpy(id, entry.id, 32);
                changed = true;
            }
        } else if (!fixedUtc(id)) {
            // Preserve the old unknown-identifier fallback, including its
            // whole-hour truncation. Never infer a region from an offset.
            const int bounded = legacyOffset < -660 || legacyOffset > 660 ? 0 : legacyOffset;
            snprintf(id, 32, "UTC%+d", -(bounded / 60));
            recovered = changed = true;
        }
        return changed;
    }
    static void fixedLabel(const char* id, char* label, size_t capacity) {
        // fixedUtc() has already validated this string; POSIX signs are reversed.
        const char* number = id + 3;
        const bool east = *number == '-';
        if (*number == '-' || *number == '+') ++number;
        unsigned hours = 0, minutes = 0, seconds = 0;
        sscanf(number, "%u:%u:%u", &hours, &minutes, &seconds);
        if (seconds)
            snprintf(label, capacity, "UTC%c%02u:%02u:%02u (saved fixed offset)",
                     east ? '+' : '-', hours, minutes, seconds);
        else
            snprintf(label, capacity, "UTC%c%02u:%02u (saved fixed offset)",
                     east ? '+' : '-', hours, minutes);
    }
};
bool timezoneSettingsRecovered = false;

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
const char firmwareBuild[] = "MatrixClock Improved " MATRIXCLOCK_FIRMWARE_VERSION;
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
uint64_t ntpReferenceMicros = 0;
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
uint64_t softwareClockLastMicros = 0;
uint64_t softwareClockElapsedUs = 0;
bool softwareClockValid = false;
// The software clock can run from a plausible RTC seed while awaiting NTP.
// Keep this separate from its validity so the UI and reminder remain honest.
bool networkTimeSynchronized = false;
enum NtpCheckResult : uint8_t {
    NTP_CHECK_AWAITING,
    NTP_CHECK_SUCCESS,
    NTP_CHECK_NO_STABLE_REPLY,
    NTP_CHECK_INCONSISTENT_REPLIES,
    NTP_CHECK_NO_VALID_REPLY,
    NTP_CHECK_OFFSET_REJECTED
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
uint8_t ntpFailureMarqueePass = 0;
const uint8_t NTP_FAILURE_MARQUEE_SCROLLS = 2;
bool startupNtpFailureIntroPending = false;
uint32_t startupNtpFailureClockVisibleAt = 0;
const uint32_t STARTUP_NTP_FAILURE_CLOCK_HOLD_MS = 5000UL;
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
void max7219_set_brightness(unsigned short br);

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
const unsigned char temperatureMsbREG = 0x11;

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

// The live preview is capped at two browser viewers. This leaves room for the
// normal page and one brief browser/probe request without allowing unbounded
// 20 Hz display/status polling on this small MCU.
const uint32_t MAIN_PAGE_VIEWER_TIMEOUT_MS = 15000UL;
const uint8_t MAIN_PAGE_VIEWER_LIMIT = 2;
struct MainPageViewer {
    String token;
    uint32_t lastSeen = 0;
};
MainPageViewer mainPageViewers[MAIN_PAGE_VIEWER_LIMIT];

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
static_assert(sizeof(settings.timezone) == 32 &&
              sizeof(settings.customTimezone) == MatrixClockTimezone::RULE_CAPACITY,
              "Timezone migration must match the existing EEPROM field sizes");

// Keep the original ClockSettings layout stable for OTA compatibility.  The
// live-preview preference uses spare EEPROM space instead of moving the
// calibration/authentication records that follow it.
const uint32_t LIVE_DISPLAY_SETTINGS_MAGIC = 0x4D434C44; // "MCLD"
const uint8_t LIVE_DISPLAY_SETTINGS_VERSION = 1;
const uint16_t LIVE_DISPLAY_SETTINGS_OFFSET = 360;
enum LiveDisplayColour : uint8_t {
    LIVE_DISPLAY_RED = 0,
    LIVE_DISPLAY_GREEN = 1,
    LIVE_DISPLAY_WHITE = 2,
    LIVE_DISPLAY_BLUE = 3
};
struct LiveDisplaySettings {
    uint32_t magic;
    uint8_t version;
    uint8_t colour;
    uint16_t reserved;
    uint32_t checksum;
};
static_assert(sizeof(LiveDisplaySettings) == 12,
              "Live display settings layout must remain stable");
static_assert(LIVE_DISPLAY_SETTINGS_OFFSET >= sizeof(ClockSettings) + 24,
              "Live display settings must not overlap drift calibration");
LiveDisplaySettings liveDisplaySettings = {};

// Keep the Arduino sketch preprocessor from emitting this prototype before
// LiveDisplaySettings has been declared.
uint32_t liveDisplaySettingsChecksum(const LiveDisplaySettings& source);

// Keep the flash record exactly the same size: authentication and product
// records follow it in EEPROM. Version 1 rates remain usable as provisional
// starting points; version 2 carries a checksum and raw-timer calibration.
const uint32_t DRIFT_CALIBRATION_MAGIC = 0x4D434452;
const uint16_t DRIFT_CALIBRATION_VERSION = 2;
const uint16_t DRIFT_CALIBRATION_OFFSET = sizeof(ClockSettings);
const uint32_t DRIFT_INITIAL_CHECK_DELAY_MS = 300000UL;
const uint32_t DRIFT_CHECK_INTERVAL_MS = 7200000UL;
const uint32_t DRIFT_RETRY_MS = 900000UL;
const uint32_t DRIFT_SAVE_INTERVAL_MS = 86400000UL;
const uint32_t DRIFT_NTP_TIMEOUT_US = 300000UL;
// Preserve distant-server time-sync compatibility; rate learning has a
// separate uncertainty gate and may need much longer on a high-latency path.
const uint32_t DRIFT_MAX_RTT_US = 250000UL;
const int32_t DRIFT_MAX_OFFSET_US = 5000000L;
const uint32_t DRIFT_MAX_SAMPLE_SPREAD_US = 10000UL;
const int32_t DRIFT_MAX_RATE_PPM_MILLI = 100000L;
const int32_t DRIFT_RATE_AGREEMENT_PPM_MILLI = 5000L;
const uint32_t DRIFT_MAX_UNCERTAINTY_PPM_MILLI = 2000UL;
const int32_t DRIFT_MAX_RATE_STEP_PPM_MILLI = 2000L;
const int32_t DRIFT_MIN_CHANGE_PPM_MILLI = 100L;
const uint64_t DRIFT_MIN_RATE_INTERVAL_US = 5400000000ULL; // 90 minutes
const uint64_t DRIFT_MAX_RATE_INTERVAL_US = 604800000000ULL; // one week
const uint8_t DRIFT_SAMPLES_PER_CHECK = 5;
const uint8_t DRIFT_MIN_SAMPLES = 3;
const uint8_t DRIFT_MIN_RATE_ESTIMATES = 2;
const int32_t PHASE_SLEW_IGNORE_US = 10000L;
const int32_t PHASE_SLEW_MAX_OFFSET_US = 500000L;
const int32_t PHASE_SLEW_RATE_PPM_MILLI = 200000L;

struct DriftCalibration {
    uint32_t magic;
    uint16_t version;
    int32_t ratePpmMilli;
    uint32_t updatedEpoch;
    int32_t lastOffsetUs;
    uint8_t acceptedChecks;
    uint8_t reserved[3]; // 24-bit checksum in v2; layout unchanged
};
static_assert(sizeof(DriftCalibration) == 24, "Preserve EEPROM layout");

const uint32_t DRIFT_SESSION_MAGIC = 0x4D435253;
const uint16_t DRIFT_SESSION_VERSION = 3;
// Offset is in 4-byte words. The first 128 bytes belong to the OTA bootloader.
const uint32_t DRIFT_SESSION_RTC_OFFSET = 32;
struct DriftSessionCheckpoint {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t checksum;
    int32_t storedRateSnapshot;
    uint32_t storedEpochSnapshot;
    int32_t activeRatePpmMilli;
    uint32_t activeUpdatedEpoch;
    int32_t candidateRatePpmMilli;
    uint32_t rateEstimateCount;
    uint32_t flags;
};
static_assert(sizeof(DriftSessionCheckpoint) % 4 == 0, "RTC alignment");
static_assert(DRIFT_SESSION_RTC_OFFSET * 4 + sizeof(DriftSessionCheckpoint) <= 512,
              "Calibration checkpoint must fit in RTC user memory");

struct NtpMeasurement {
    uint32_t epoch;
    uint32_t fractionUs;
    uint64_t referenceMicros;
    uint32_t roundTripUs;
    IPAddress server;
    uint8_t stratum;
    uint8_t leapIndicator;
};

DriftCalibration driftCalibration;
int32_t activeDriftPpmMilli = 0;
bool activeDriftVerified = false;
uint32_t activeDriftUpdatedEpoch = 0;
int64_t softwareClockRateRemainder = 0;
int64_t softwareClockPhaseRemainingUs = 0;
int64_t softwareClockPhaseRemainder = 0;
uint32_t nextDriftCheckAt = 0;
uint32_t nextDriftCheckEpoch = 0;
uint32_t lastDriftSaveAttemptAt = 0;
bool driftStorageFault = false;
bool driftCheckpointFault = false;
bool driftReferenceValid = false;
uint64_t driftReferenceRawUs = 0;
uint64_t driftReferenceObservedUs = 0;
uint32_t driftReferenceUncertaintyUs = 0;
IPAddress driftReferenceServer;
uint16_t driftAcceptedChecks = 0;
bool driftLastCheckValid = false;
int32_t driftLastOffsetUs = 0;
uint32_t driftLastRoundTripUs = 0;
uint64_t driftLastObservedUs = 0;
bool driftCandidateValid = false;
int32_t driftCandidatePpmMilli = 0;
uint8_t driftRateEstimateCount = 0;
bool driftRateRejected = false;
bool driftAwaitingLongerInterval = false;

// Explicit declarations keep Arduino's generated prototypes below custom types.
uint32_t driftSessionChecksum(const DriftSessionCheckpoint& source);
uint32_t driftCalibrationChecksum(const DriftCalibration& source);
bool readDriftSessionCheckpoint(DriftSessionCheckpoint& record);
bool clearDriftSessionCheckpoint();
bool saveDriftSessionCheckpoint();
bool resetDriftCalibration(bool persist);
bool commitDriftCalibrationIfReady();
void advanceSoftwareClockTo(uint64_t nowMicros);
uint64_t ntpObservedUs(const NtpMeasurement& measurement);
bool readNtpMeasurement(NtpMeasurement& measurement, const IPAddress& server,
                        uint32_t timeoutUs);
bool collectNtpMeasurement(NtpMeasurement& selected, uint32_t& spreadUs,
                          uint32_t timeoutUs);
void publishNtpMeasurement(const NtpMeasurement& measurement);
void acceptDriftReference(const NtpMeasurement& measurement, uint32_t uncertaintyUs);
String ntpNextCheckText();
String ntpNextCheckAtText();
String ntpLastSuccessText();
void scheduleNextDriftCheck(uint32_t delayMs);

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
static_assert(DRIFT_CALIBRATION_OFFSET + sizeof(DriftCalibration) <= AUTH_OFFSET,
              "Calibration must not overlap authentication");
static_assert(AUTH_OFFSET + sizeof(AuthSettings) <= PRODUCT_MARKER_OFFSET,
              "Authentication record must fit before product marker");
static_assert(LIVE_DISPLAY_SETTINGS_OFFSET + sizeof(LiveDisplaySettings) <= AUTH_OFFSET,
              "Live display settings must fit before authentication");
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

String mainPageViewerCookie() {
    if (!web.hasHeader("Cookie")) return String();
    const String cookie = web.header("Cookie");
    const String key = F("mc_viewer=");
    int start = cookie.indexOf(key);
    while (start >= 0) {
        if (start == 0 || cookie[start - 1] == ';' || cookie[start - 1] == ' ') {
            int end = cookie.indexOf(';', start);
            if (end < 0) end = cookie.length();
            return cookie.substring(start + key.length(), end);
        }
        start = cookie.indexOf(key, start + 1);
    }
    return String();
}

String newMainPageViewerToken() {
    return String(ESP.getChipId(), HEX) + String(ESP.random(), HEX) +
           String(ESP.random(), HEX);
}

bool claimMainPageViewer() {
    String token = mainPageViewerCookie();
    if (token.length() == 0) {
        token = newMainPageViewerToken();
        web.sendHeader("Set-Cookie", String("mc_viewer=") + token +
                       "; HttpOnly; SameSite=Strict; Path=/", true);
    }
    const uint32_t now = millis();
    for (uint8_t i = 0; i < MAIN_PAGE_VIEWER_LIMIT; i++) {
        MainPageViewer& viewer = mainPageViewers[i];
        if (viewer.token.length() != 0 &&
            (uint32_t)(now - viewer.lastSeen) >= MAIN_PAGE_VIEWER_TIMEOUT_MS)
            viewer.token = "";
        if (viewer.token == token) {
            viewer.lastSeen = now;
            return true;
        }
    }
    for (uint8_t i = 0; i < MAIN_PAGE_VIEWER_LIMIT; i++) {
        MainPageViewer& viewer = mainPageViewers[i];
        if (viewer.token.length() == 0) {
            viewer.token = token;
            viewer.lastSeen = now;
            return true;
        }
    }
    return false;
}

bool refreshMainPageViewer() {
    const String token = mainPageViewerCookie();
    if (token.length() == 0) return false;
    for (uint8_t i = 0; i < MAIN_PAGE_VIEWER_LIMIT; i++) {
        MainPageViewer& viewer = mainPageViewers[i];
        if (viewer.token == token) {
            viewer.lastSeen = millis();
            return true;
        }
    }
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

// A separate record uses previously unused EEPROM bytes. Never extend
// ClockSettings: that would move calibration and reinterpret existing data.
const uint32_t REBOOT_SETTINGS_MAGIC = 0x4D435242; // "MCRB"
const uint8_t REBOOT_SETTINGS_VERSION = 1;
const uint16_t REBOOT_SETTINGS_OFFSET = 488;
struct RebootSettings {
    uint32_t magic;
    uint8_t version;
    uint8_t dailyEnabled;
    uint8_t largeErrorEnabled;
    uint8_t reserved;
    uint32_t checksum;
};
static_assert(sizeof(RebootSettings) == 12, "Reboot settings layout");
static_assert(REBOOT_SETTINGS_OFFSET >= PRODUCT_MARKER_OFFSET + sizeof(ProductMarker),
              "Reboot settings must not overlap product marker");
static_assert(REBOOT_SETTINGS_OFFSET + sizeof(RebootSettings) <= 512,
              "Reboot settings must fit in EEPROM");
RebootSettings rebootSettings = {};
uint32_t rebootSettingsChecksum(const RebootSettings& record);

uint32_t rebootSettingsChecksum(const RebootSettings& record) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&record);
    uint32_t hash = 2166136261UL;
    for (size_t i = 0; i < offsetof(RebootSettings, checksum); i++)
        hash = (hash ^ bytes[i]) * 16777619UL;
    return hash;
}

void prepareRebootSettings() {
    rebootSettings.magic = REBOOT_SETTINGS_MAGIC;
    rebootSettings.version = REBOOT_SETTINGS_VERSION;
    rebootSettings.reserved = 0;
    rebootSettings.checksum = rebootSettingsChecksum(rebootSettings);
}

void loadRebootSettings() {
    EEPROM.get(REBOOT_SETTINGS_OFFSET, rebootSettings);
    bool valid = rebootSettings.magic == REBOOT_SETTINGS_MAGIC &&
                 rebootSettings.version == REBOOT_SETTINGS_VERSION &&
                 rebootSettings.dailyEnabled <= 1 &&
                 rebootSettings.largeErrorEnabled <= 1 &&
                 rebootSettings.reserved == 0 &&
                 rebootSettings.checksum == rebootSettingsChecksum(rebootSettings);
    if (valid) return;
    rebootSettings = {};
    rebootSettings.dailyEnabled = 0; // reboot only when recovery is needed by default
    rebootSettings.largeErrorEnabled = 1;
    prepareRebootSettings();
    EEPROM.put(REBOOT_SETTINGS_OFFSET, rebootSettings);
    if (!EEPROM.commit()) Serial.println("Unable to save default reboot settings");
}

bool largeClockErrorDetected() {
    // Only accepted, filtered measurements can request recovery. A rejected
    // packet must not cause reboots, and a later good offset clears the need.
    return softwareClockValid && driftLastCheckValid &&
           (driftLastOffsetUs > PHASE_SLEW_MAX_OFFSET_US ||
            driftLastOffsetUs < -PHASE_SLEW_MAX_OFFSET_US);
}

bool scheduledRebootWanted() {
    return rebootSettings.dailyEnabled ||
           (rebootSettings.largeErrorEnabled && largeClockErrorDetected());
}

bool scheduledRebootTimeReached() {
    if (provisioningMode) return false;
    static uint32_t observedDay = 0;
    static bool consideredToday = false;
    uint32_t today = (uint32_t)MEZ.jahr12 * 10000UL +
                     (uint32_t)MEZ.mon12 * 100UL + MEZ.tag12;
    uint16_t minuteNow = currentHour24 * 60U + MEZ.min12;
    uint16_t scheduledMinute = settings.restartHour * 60U + settings.restartMinute;
    if (today != observedDay) {
        // A boot after today's slot waits until tomorrow, preventing a loop
        // of restarts while the selected minute is still on the display.
        consideredToday = observedDay == 0 && minuteNow >= scheduledMinute;
        observedDay = today;
    }
    if (consideredToday || minuteNow < scheduledMinute) return false;
    consideredToday = true; // at most once per local date, including DST fallback
    return true; // crossing the time also survives brief web/NTP servicing delays
}

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
    if (MatrixClockTimezone::normalize(settings.timezone, settings.customTimezone,
                                       settings.daylightSaving, settings.utcOffset,
                                       timezoneSettingsRecovered)) {
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

uint32_t liveDisplaySettingsChecksum(const LiveDisplaySettings& source) {
    LiveDisplaySettings record = source;
    record.checksum = 0;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&record);
    uint32_t hash = 2166136261UL;
    for (size_t i = 0; i < sizeof(record); i++) {
        hash ^= bytes[i];
        hash *= 16777619UL;
    }
    return hash;
}

void prepareLiveDisplaySettings() {
    liveDisplaySettings.magic = LIVE_DISPLAY_SETTINGS_MAGIC;
    liveDisplaySettings.version = LIVE_DISPLAY_SETTINGS_VERSION;
    liveDisplaySettings.reserved = 0;
    liveDisplaySettings.checksum = liveDisplaySettingsChecksum(liveDisplaySettings);
}

void loadLiveDisplaySettings() {
    EEPROM.get(LIVE_DISPLAY_SETTINGS_OFFSET, liveDisplaySettings);
    const bool valid = liveDisplaySettings.magic == LIVE_DISPLAY_SETTINGS_MAGIC &&
        liveDisplaySettings.version == LIVE_DISPLAY_SETTINGS_VERSION &&
        liveDisplaySettings.colour <= LIVE_DISPLAY_BLUE &&
        liveDisplaySettings.checksum == liveDisplaySettingsChecksum(liveDisplaySettings);
    if (!valid) {
        memset(&liveDisplaySettings, 0, sizeof(liveDisplaySettings));
        liveDisplaySettings.colour = LIVE_DISPLAY_RED;
        prepareLiveDisplaySettings();
        EEPROM.put(LIVE_DISPLAY_SETTINGS_OFFSET, liveDisplaySettings);
        EEPROM.commit();
    }
}

const char* liveDisplayColourName() {
    switch (liveDisplaySettings.colour) {
        case LIVE_DISPLAY_GREEN: return "green";
        case LIVE_DISPLAY_BLUE: return "blue";
        case LIVE_DISPLAY_WHITE: return "white";
        default: return "red";
    }
}

String formatDriftPpm(int32_t ratePpmMilli) {
    char text[28];
    int32_t magnitude = ratePpmMilli < 0 ? -ratePpmMilli : ratePpmMilli;
    snprintf(text, sizeof(text), "%c%ld.%03ld ppm", ratePpmMilli < 0 ? '-' : '+',
             (long)(magnitude / 1000L), (long)(magnitude % 1000L));
    return String(text);
}

String formatDriftOffset(int32_t offsetUs) {
    char text[40];
    uint32_t tenthsMs = (uint32_t)(offsetUs < 0 ? -offsetUs : offsetUs);
    tenthsMs = (tenthsMs + 50UL) / 100UL;
    if (tenthsMs == 0) return F("Less than 0.1 ms");
    snprintf(text, sizeof(text), "%lu.%lu ms %s",
             (unsigned long)(tenthsMs / 10), (unsigned long)(tenthsMs % 10),
             offsetUs > 0 ? "behind NTP" : "ahead of NTP");
    return String(text);
}

uint32_t driftCalibrationChecksum(const DriftCalibration& source) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&source);
    uint32_t hash = 2166136261UL;
    // Ignore compiler padding and the checksum itself, hash the actual fields.
    for (size_t i = 0; i < offsetof(DriftCalibration, reserved); i++) {
        if (i == 6 || i == 7) continue;
        hash = (hash ^ bytes[i]) * 16777619UL;
    }
    return hash & 0xFFFFFFUL;
}

uint32_t driftSessionChecksum(const DriftSessionCheckpoint& source) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&source);
    uint32_t hash = 2166136261UL;
    for (size_t i = 0; i < sizeof(source); i++) {
        if (i >= offsetof(DriftSessionCheckpoint, checksum) &&
            i < offsetof(DriftSessionCheckpoint, checksum) + 4) continue;
        hash = (hash ^ bytes[i]) * 16777619UL;
    }
    return hash;
}

bool clearDriftSessionCheckpoint() {
    DriftSessionCheckpoint blank = {};
    driftCheckpointFault = !ESP.rtcUserMemoryWrite(DRIFT_SESSION_RTC_OFFSET,
                              reinterpret_cast<uint32_t*>(&blank), sizeof(blank));
    return !driftCheckpointFault;
}

bool saveDriftSessionCheckpoint() {
    DriftSessionCheckpoint record = {};
    record.magic = DRIFT_SESSION_MAGIC;
    record.version = DRIFT_SESSION_VERSION;
    record.size = sizeof(record);
    record.storedRateSnapshot = driftCalibration.ratePpmMilli;
    record.storedEpochSnapshot = driftCalibration.updatedEpoch;
    record.activeRatePpmMilli = activeDriftPpmMilli;
    record.activeUpdatedEpoch = activeDriftUpdatedEpoch;
    record.candidateRatePpmMilli = driftCandidatePpmMilli;
    record.rateEstimateCount = driftRateEstimateCount;
    record.flags = (activeDriftVerified ? 1UL : 0UL) |
                   (driftCandidateValid ? 2UL : 0UL);
    record.checksum = driftSessionChecksum(record);
    driftCheckpointFault = !ESP.rtcUserMemoryWrite(DRIFT_SESSION_RTC_OFFSET,
                              reinterpret_cast<uint32_t*>(&record), sizeof(record));
    return !driftCheckpointFault;
}

bool readDriftSessionCheckpoint(DriftSessionCheckpoint& record) {
    if (!ESP.rtcUserMemoryRead(DRIFT_SESSION_RTC_OFFSET,
                              reinterpret_cast<uint32_t*>(&record), sizeof(record)))
        return false;
    return record.magic == DRIFT_SESSION_MAGIC &&
           record.version == DRIFT_SESSION_VERSION &&
           record.size == sizeof(record) &&
           record.checksum == driftSessionChecksum(record) &&
           record.storedRateSnapshot == driftCalibration.ratePpmMilli &&
           record.storedEpochSnapshot == driftCalibration.updatedEpoch &&
           record.activeRatePpmMilli >= -DRIFT_MAX_RATE_PPM_MILLI &&
           record.activeRatePpmMilli <= DRIFT_MAX_RATE_PPM_MILLI &&
           record.candidateRatePpmMilli >= -DRIFT_MAX_RATE_PPM_MILLI &&
           record.candidateRatePpmMilli <= DRIFT_MAX_RATE_PPM_MILLI &&
           record.rateEstimateCount <= 4 && (record.flags & ~3UL) == 0 &&
           ((record.flags & 2UL) != 0) == (record.rateEstimateCount != 0) &&
           (!(record.flags & 1UL) || record.activeUpdatedEpoch != 0);
}

bool resetDriftCalibration(bool persist) {
    DriftCalibration blank = {};
    blank.magic = DRIFT_CALIBRATION_MAGIC;
    blank.version = DRIFT_CALIBRATION_VERSION;
    uint32_t checksum = driftCalibrationChecksum(blank);
    for (uint8_t i = 0; i < 3; i++) blank.reserved[i] = checksum >> (8 * i);
    if (persist) {
        EEPROM.put(DRIFT_CALIBRATION_OFFSET, blank);
        if (!EEPROM.commit()) {
            EEPROM.put(DRIFT_CALIBRATION_OFFSET, driftCalibration);
            driftStorageFault = true;
            return false;
        }
    }
    advanceSoftwareClockTo(micros64());
    driftCalibration = blank;
    activeDriftPpmMilli = 0;
    activeDriftVerified = false;
    activeDriftUpdatedEpoch = 0;
    softwareClockRateRemainder = 0;
    softwareClockPhaseRemainingUs = 0;
    softwareClockPhaseRemainder = 0;
    driftReferenceValid = false;
    driftAcceptedChecks = 0;
    driftLastCheckValid = false;
    driftLastOffsetUs = 0;
    driftLastRoundTripUs = 0;
    driftLastObservedUs = 0;
    driftCandidateValid = false;
    driftCandidatePpmMilli = 0;
    driftRateEstimateCount = 0;
    driftRateRejected = false;
    driftAwaitingLongerInterval = false;
    driftStorageFault = false;
    clearDriftSessionCheckpoint();
    return true;
}

void loadDriftCalibration() {
    static_assert(DRIFT_CALIBRATION_OFFSET + sizeof(DriftCalibration) <= 512,
                  "Drift calibration must fit in EEPROM");
    EEPROM.get(DRIFT_CALIBRATION_OFFSET, driftCalibration);
    uint32_t checksum = (uint32_t)driftCalibration.reserved[0] |
                        ((uint32_t)driftCalibration.reserved[1] << 8) |
                        ((uint32_t)driftCalibration.reserved[2] << 16);
    bool legacy = driftCalibration.version == 1;
    bool valid = driftCalibration.magic == DRIFT_CALIBRATION_MAGIC &&
                 driftCalibration.ratePpmMilli >= -DRIFT_MAX_RATE_PPM_MILLI &&
                 driftCalibration.ratePpmMilli <= DRIFT_MAX_RATE_PPM_MILLI &&
                 (legacy || (driftCalibration.version == DRIFT_CALIBRATION_VERSION &&
                             checksum == driftCalibrationChecksum(driftCalibration)));
    if (!valid) {
        if (!resetDriftCalibration(true)) {
            resetDriftCalibration(false);
            driftStorageFault = true;
        }
        return;
    }
    activeDriftPpmMilli = driftCalibration.ratePpmMilli;
    activeDriftVerified = !legacy && driftCalibration.updatedEpoch != 0;
    activeDriftUpdatedEpoch = driftCalibration.updatedEpoch;
    lastDriftSaveAttemptAt = millis();
    DriftSessionCheckpoint checkpoint;
    if (readDriftSessionCheckpoint(checkpoint)) {
        activeDriftPpmMilli = checkpoint.activeRatePpmMilli;
        activeDriftVerified = (checkpoint.flags & 1UL) != 0;
        activeDriftUpdatedEpoch = checkpoint.activeUpdatedEpoch;
        driftCandidateValid = (checkpoint.flags & 2UL) != 0;
        driftCandidatePpmMilli = checkpoint.candidateRatePpmMilli;
        driftRateEstimateCount = checkpoint.rateEstimateCount;
    } else {
        clearDriftSessionCheckpoint();
    }
    // Raw timestamps and offsets NEVER span a boot. Only completed rate
    // estimates survive; the page waits for a fresh measurement after restart.
    driftReferenceValid = false;
    driftLastCheckValid = false;
}

uint64_t adjustedClockDeltaUs(uint64_t rawDeltaUs, int64_t& remainder) {
    const int64_t scale = 1000000000LL;
    // Splitting the whole and fractional parts avoids multiplying a long
    // chronograph interval by one billion (which would overflow at ~2.5 hours).
    int64_t correction = (int64_t)(rawDeltaUs / scale) * activeDriftPpmMilli;
    int64_t numerator = (int64_t)(rawDeltaUs % scale) * activeDriftPpmMilli + remainder;
    correction += numerator / scale;
    remainder = numerator % scale;
    return (uint64_t)((int64_t)rawDeltaUs + correction);
}

int64_t phaseSlewDeltaUs(uint64_t rawDeltaUs, int64_t& remainingUs,
                         int64_t& remainder) {
    if (remainingUs == 0) return 0;
    const int64_t scale = 1000000000LL;
    const int64_t rate = remainingUs > 0 ? PHASE_SLEW_RATE_PPM_MILLI
                                         : -PHASE_SLEW_RATE_PPM_MILLI;
    int64_t numerator = (int64_t)(rawDeltaUs % scale) * rate + remainder;
    int64_t correctionUs = (int64_t)(rawDeltaUs / scale) * rate + numerator / scale;
    remainder = numerator % scale;
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

void advanceSoftwareClockTo(uint64_t nowMicros) {
    if (!softwareClockValid || nowMicros < softwareClockLastMicros) return;
    uint64_t rawDeltaUs = nowMicros - softwareClockLastMicros;
    uint64_t rateAdjustedUs = adjustedClockDeltaUs(rawDeltaUs, softwareClockRateRemainder);
    int64_t phaseAdjustedUs = phaseSlewDeltaUs(rawDeltaUs, softwareClockPhaseRemainingUs,
                                            softwareClockPhaseRemainder);
    softwareClockElapsedUs = (uint64_t)((int64_t)softwareClockElapsedUs +
                                      (int64_t)rateAdjustedUs + phaseAdjustedUs);
    softwareClockLastMicros = nowMicros;
}

void scheduleClockPhaseSlew(int64_t offsetUs) {
    // The caller first advances time with the OLD rate and slew target.
    if (offsetUs > -PHASE_SLEW_IGNORE_US && offsetUs < PHASE_SLEW_IGNORE_US) {
        softwareClockPhaseRemainingUs = 0;
        softwareClockPhaseRemainder = 0;
    } else if (offsetUs >= -PHASE_SLEW_MAX_OFFSET_US &&
               offsetUs <= PHASE_SLEW_MAX_OFFSET_US) {
        softwareClockPhaseRemainingUs = offsetUs;
        softwareClockPhaseRemainder = 0;
    }
}

// Rate stays fixed for the complete chronograph session. micros64() makes
// uninterrupted multi-hour intervals safe across any number of 32-bit wraps.
uint32_t adjustedChronographDeltaMs(uint64_t startMicros, uint64_t nowMicros,
                                    int64_t& remainder) {
    return (uint32_t)(adjustedClockDeltaUs(nowMicros - startMicros, remainder) / 1000ULL);
}

uint64_t softwareClockElapsedAt(uint64_t referenceMicros) {
    if (referenceMicros < softwareClockLastMicros) return softwareClockElapsedUs;
    uint64_t rawDeltaUs = referenceMicros - softwareClockLastMicros;
    int64_t rateRemainder = softwareClockRateRemainder;
    int64_t phaseRemaining = softwareClockPhaseRemainingUs;
    int64_t phaseRemainder = softwareClockPhaseRemainder;
    return (uint64_t)((int64_t)softwareClockElapsedUs +
        (int64_t)adjustedClockDeltaUs(rawDeltaUs, rateRemainder) +
        phaseSlewDeltaUs(rawDeltaUs, phaseRemaining, phaseRemainder));
}

uint32_t softwareClockEpochNow() {
    if (!softwareClockValid) return 0;
    return softwareEpochBase + (uint32_t)(softwareClockElapsedAt(micros64()) / 1000000ULL);
}

void scheduleNextDriftCheck(uint32_t delayMs) {
    nextDriftCheckAt = millis() + delayMs;
    nextDriftCheckEpoch = softwareClockValid
        ? softwareClockEpochNow() + (delayMs + 999UL) / 1000UL
        : 0;
}

bool commitDriftCalibrationIfReady() {
    if (!activeDriftVerified) return true;
    lastDriftSaveAttemptAt = millis();
    int32_t difference = activeDriftPpmMilli - driftCalibration.ratePpmMilli;
    if (driftCalibration.version == DRIFT_CALIBRATION_VERSION &&
        driftCalibration.updatedEpoch != 0 &&
        difference > -DRIFT_MIN_CHANGE_PPM_MILLI && difference < DRIFT_MIN_CHANGE_PPM_MILLI) {
        driftStorageFault = false;
        return true;
    }
    DriftCalibration record = {};
    record.magic = DRIFT_CALIBRATION_MAGIC;
    record.version = DRIFT_CALIBRATION_VERSION;
    record.ratePpmMilli = activeDriftPpmMilli;
    record.updatedEpoch = softwareClockEpochNow();
    record.lastOffsetUs = driftLastOffsetUs;
    record.acceptedChecks = driftAcceptedChecks > 255 ? 255 : driftAcceptedChecks;
    uint32_t checksum = driftCalibrationChecksum(record);
    for (uint8_t i = 0; i < 3; i++) record.reserved[i] = checksum >> (8 * i);
    EEPROM.put(DRIFT_CALIBRATION_OFFSET, record);
    if (!EEPROM.commit()) {
        EEPROM.put(DRIFT_CALIBRATION_OFFSET, driftCalibration);
        driftStorageFault = true;
        saveDriftSessionCheckpoint();
        return false;
    }
    driftCalibration = record;
    driftStorageFault = false;
    saveDriftSessionCheckpoint(); // new flash snapshot, preserve recent estimates
    return true;
}

void applyDriftCandidate() {
    if (!driftCandidateValid || driftRateEstimateCount < DRIFT_MIN_RATE_ESTIMATES ||
        apiBlockedByChronograph) return;
    int32_t target = driftCandidatePpmMilli;
    if (activeDriftVerified) {
        int32_t step = (target - activeDriftPpmMilli) / 2;
        step = constrain(step, -DRIFT_MAX_RATE_STEP_PPM_MILLI, DRIFT_MAX_RATE_STEP_PPM_MILLI);
        if (step > -DRIFT_MIN_CHANGE_PPM_MILLI && step < DRIFT_MIN_CHANGE_PPM_MILLI) return;
        target = activeDriftPpmMilli + step;
    }
    advanceSoftwareClockTo(micros64()); // no past interval is recalculated
    activeDriftPpmMilli = target;
    activeDriftVerified = true;
    activeDriftUpdatedEpoch = softwareClockEpochNow();
    // Keep the fractional remainder: changing speed must not discard elapsed time.
}

String driftStoredText() {
    if (!softwareClockValid) return F("Inactive - using RTC");
    if (!activeDriftVerified && activeDriftPpmMilli == 0) return F("Not calibrated");
    return formatDriftPpm(activeDriftPpmMilli);
}

String driftEstimateText() {
    return driftCandidateValid ? formatDriftPpm(driftCandidatePpmMilli) : String(F("Insufficient data"));
}

String driftLatestCheckText() {
    return driftLastCheckValid ? formatDriftOffset(driftLastOffsetUs) : String(F("Awaiting first measurement"));
}

String driftSampleText() {
    if (!softwareClockValid) return F("Unavailable in RTC mode");
    if (activeDriftVerified) return F("Validated");
    const uint8_t completed = driftRateEstimateCount < DRIFT_MIN_RATE_ESTIMATES
        ? driftRateEstimateCount : DRIFT_MIN_RATE_ESTIMATES;
    return String(completed) + F(" of ") + String(DRIFT_MIN_RATE_ESTIMATES) +
           F(" required estimates");
}

String driftStatusText() {
    if (!softwareClockValid) return F("RTC fallback");
    if (!networkTimeSynchronized)
        return activeDriftVerified ? F("RTC fallback - active")
                                   : F("Using RTC time - awaiting NTP");
    if (driftStorageFault) return activeDriftVerified ? F("Correction active - unable to save")
                                                     : F("Unable to save calibration");
    if (driftCheckpointFault) return F("Reset recovery unavailable");
    if (lastNtpCheckResult != NTP_CHECK_SUCCESS && lastNtpCheckResult != NTP_CHECK_AWAITING)
        return F("Check rejected - retry scheduled");
    if (nextDriftCheckAt && (int32_t)(millis() - nextDriftCheckAt) >= 0)
        return F("Waiting for clock mode");
    if (driftRateRejected) return F("Rate changed - checking agreement");
    if (driftAwaitingLongerInterval) return F("Collecting a longer measurement");
    if (!driftReferenceValid) return F("Initial stabilisation");
    if (activeDriftVerified) return F("Active");
    if (activeDriftUpdatedEpoch) return F("Revalidating saved compensation");
    return F("Measuring drift");
}

String driftPhaseText() {
    if (!softwareClockValid) return F("Unavailable in RTC mode");
    int64_t remaining = softwareClockPhaseRemainingUs;
    int64_t fraction = softwareClockPhaseRemainder;
    phaseSlewDeltaUs(micros64() - softwareClockLastMicros, remaining, fraction);
    if (remaining != 0) return F("Time correction in progress - ") + formatDriftOffset((int32_t)remaining);
    if (driftLastCheckValid &&
        (driftLastOffsetUs > PHASE_SLEW_MAX_OFFSET_US || driftLastOffsetUs < -PHASE_SLEW_MAX_OFFSET_US))
        return F("Large offset - automatic adjustment withheld");
    return F("No adjustment pending");
}

String driftSavedText() {
    if (!driftCalibration.updatedEpoch) return F("Not saved");
    time_t epoch = driftCalibration.updatedEpoch;
    tm* local = localtime(&epoch);
    if (!local) return F("Saved");
    char text[24];
    strftime(text, sizeof(text), "%d %b %Y, %H:%M:%S", local);
    return String(text);
}

String driftLastCheckTimeText() {
    if (!driftLastCheckValid) return F("No measurement this boot");
    time_t epoch = (time_t)(driftLastObservedUs / 1000000ULL);
    tm* local = localtime(&epoch);
    if (!local) return F("Unavailable");
    char text[24];
    strftime(text, sizeof(text), "%d %b %Y, %H:%M:%S", local);
    return String(text);
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
    web.sendContent_P(PSTR("</fieldset><p class='notice'>Once connected to your home Wi-Fi, open the IP address shown on the clock's startup display.</p><button type='submit'>Save and reboot</button></form>"
                      "<form method='POST' action='/reset' onsubmit=\"return confirm('Erase all saved settings and Wi-Fi credentials, then reboot?')\"><button class='danger' type='submit'>Factory reset</button></form></main></body></html>"));
    Serial.printf("WEB / setup complete ms=%lu heap=%u networks=%d\n",
                  (unsigned long)(millis() - webStartedAt), ESP.getFreeHeap(), networkCount);
}

void sendMainPageViewerBusy() {
    web.sendHeader("Cache-Control", "no-store");
    web.send(423, "text/html",
             "<!doctype html><html><head><meta name='viewport' content='width=device-width'>"
             "<title>Matrix Clock</title><style>body{min-height:100vh;margin:0;display:grid;place-items:center;"
             "padding:1em;background:#090e18;color:#eef4ff;font:16px -apple-system,BlinkMacSystemFont,Segoe UI,sans-serif}"
             "main{max-width:420px;padding:1.5em;border:1px solid rgba(180,210,255,.2);border-radius:18px;"
             "background:rgba(24,37,59,.8)}h1{margin-top:0}p{color:#b8c7de;line-height:1.5}</style></head><body>"
             "<main><h1>Live display limit reached</h1><p>The main Matrix Clock page is already open on two devices. "
             "Close one, then refresh this page.</p></main></body></html>");
}

void handleWebRoot() {
    const uint32_t webStartedAt = millis();
    const bool settingsPage = web.uri() == "/settings";
    Serial.printf("WEB / start heap=%u mode=%d wifi=%d provisioning=%d\n",
                  ESP.getFreeHeap(), (int)WiFi.getMode(), (int)WiFi.status(),
                  provisioningMode ? 1 : 0);
    if (provisioningMode) {
        handleSetupWebRoot(webStartedAt);
        return;
    }
    if (!requireWebAuth()) return;
    if (!settingsPage && !claimMainPageViewer()) {
        sendMainPageViewerBusy();
        return;
    }
    String ipAddress;

    // Stream static content from flash and generate each dynamic option only
    // when it is about to be sent. This keeps peak heap use bounded.
    web.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0", true);
    web.sendHeader("Pragma", "no-cache");
    web.sendHeader("Expires", "0");
    web.setContentLength(CONTENT_LENGTH_UNKNOWN);
    web.send(200, "text/html", "");

    // Send the page frame directly from flash so CSS cannot disappear when
    // memory is tight. The dynamic form fields below remain streamed.
    web.sendContent_P(PSTR("<!doctype html><html><head><meta name='viewport' content='width=device-width'><link rel='icon' href='/favicon.ico'><title>Matrix Clock - " MATRIXCLOCK_FIRMWARE_VERSION "</title><style>*{box-sizing:border-box}body{font:16px -apple-system,BlinkMacSystemFont,Segoe UI,sans-serif;max-width:720px;margin:0 auto;padding:2em 1em;background:#090e18 radial-gradient(circle at 20% 0%,#243653,#101827 48%,#090e18) no-repeat fixed;background-size:cover;color:#eef4ff}.card{padding:1.4em;margin:1em 0;border:1px solid rgba(180,210,255,.2);border-radius:22px;background:rgba(24,37,59,.64);box-shadow:0 16px 40px rgba(0,0,0,.32);backdrop-filter:blur(18px)}a,a:visited,a:hover,a:active{color:#1677ff}a.button-link{display:block;width:100%;min-height:2.6em;padding:.7em;text-align:center;text-decoration:none;font-weight:600;color:#fff;border-radius:12px;background:#1677ff;box-shadow:0 5px 14px rgba(22,119,255,.25)}a.button-link:active{transform:translateY(2px) scale(.99);filter:brightness(.82)}h1{font-weight:650;letter-spacing:-.03em;color:#f7faff}fieldset{margin:1em 0;padding:1em;border:1px solid rgba(180,210,255,.2);border-radius:15px;background:rgba(10,18,32,.3)}label{display:block;margin:.8em 0;color:#dbe7fb}.small{font-size:.9em;color:#aebfda;line-height:1.5}input,select,button{width:100%;min-height:2.6em;font:inherit;padding:.6em .7em;border-radius:10px}input,select{border:1px solid rgba(180,210,255,.25);background:rgba(7,14,26,.62);color:#f1f6ff}button{font-weight:600;color:#fff;border:0;border-radius:12px;background:#1677ff;box-shadow:0 5px 14px rgba(22,119,255,.25);transition:transform .08s ease,filter .08s ease}button:not(:disabled):active{transform:translateY(2px) scale(.99);filter:brightness(.82)}button.danger{margin-top:.8em;background:#c43d59}.api-cancel{margin-top:.7em;background:#c43d59}.calibration-reset{background:#c43d59}.settings-save-action{margin-top:1em;padding-top:1em;border-top:1px solid rgba(180,210,255,.18)}.maintenance-actions{display:grid;gap:.65em}.maintenance-actions form{margin:0}.maintenance-actions button,.maintenance-actions button.danger{margin:0}.settings-navigation{margin:1em 0 .6em}.firmware-link{margin:.6em 0 0}.status-grid,.inline-fields,.calibration-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:.65em}.status-grid{grid-template-columns:repeat(3,minmax(0,1fr));margin:1em 0}.status-item,.calibration-grid>div{padding:.75em .85em;border:1px solid rgba(180,210,255,.16);border-radius:13px;background:rgba(255,255,255,.06)}.status-label,.calibration-grid span{display:block;font-size:.76em;letter-spacing:.04em;text-transform:uppercase;color:#9eb1ce;margin-bottom:.25em}.status-value,.calibration-grid strong{display:block;color:#f1f6ff;font-weight:650}.event-list{display:grid;gap:.45em}.event-row,.event-heading{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:1em;padding:.55em .65em;border-radius:9px;background:rgba(255,255,255,.045);font:12px ui-monospace,monospace}.event-heading{margin-top:.65em;color:#9eb1ce;font-weight:650;border-bottom:1px solid rgba(180,210,255,.18);background:transparent}.event-label{color:#aebfda}.event-value{color:#f1f6ff;text-align:right}.api-row{display:flex;align-items:flex-end;gap:.65em}.api-row label{flex:1;margin:.8em 0 0}.api-row button{width:auto;white-space:nowrap}.api-status{display:block;min-height:1.4em;margin-top:.35em;color:#b9d5ff}.api-help,.calibration-help{color:#dbe7fb;line-height:1.5}.api-help small{display:block;margin-top:.45em;color:#aebfda}.api-playback{margin-top:1.1em;padding:1em;border:1px solid rgba(180,210,255,.16);border-radius:14px;background:rgba(255,255,255,.045)}.api-playback-title{font-weight:650;margin-bottom:.65em}.calibration-card details{margin-top:.8em}.calibration-card summary{cursor:pointer;color:#b9d5ff}.calibration-card summary:focus-visible{outline:2px solid #1677ff;outline-offset:4px}.calibration-card details p{margin-bottom:0}.reboot-toggle{display:flex;align-items:flex-start;gap:.65em}.reboot-toggle input[type=checkbox]{width:1.2em;height:1.2em;min-height:0;padding:0;margin:.15em 0 0;flex:0 0 auto;accent-color:#1677ff}.reboot-toggle span{min-width:0;line-height:1.5}.calibration-actions{margin-top:.8em}.calibration-actions form{margin:0}@media(max-width:520px){.status-grid,.inline-fields,.calibration-grid{grid-template-columns:1fr}.event-row,.event-heading{grid-template-columns:1fr;gap:.15em}.event-value{text-align:left}.api-row{align-items:stretch;flex-direction:column}.api-row button{width:100%}}</style></head><body><div class='card'>"));
    if (settingsPage)
        web.sendContent_P(PSTR("<h1>Settings</h1>"));
    else
        web.sendContent_P(PSTR("<h1>Matrix Clock - " MATRIXCLOCK_FIRMWARE_VERSION "</h1>"));
    String pageChunk;
    pageChunk.reserve(640);

    if (!settingsPage) {
        web.sendContent_P(PSTR("<style>.live-display{margin:0 0 1em;padding:1em;border:1px solid rgba(180,210,255,.2);border-radius:15px;background:rgba(10,18,32,.3)}.live-display-title{display:block;font-size:.76em;letter-spacing:.04em;text-transform:uppercase;color:#9eb1ce}.live-display-pixels{display:grid;grid-template-columns:repeat(32,6px);gap:2px;width:max-content;margin:.7em auto 0;padding:.7em;border-radius:10px;background:#03070d}.live-pixel{width:6px;height:6px;border-radius:50%;background:#151b25}.live-display[data-colour=red] .live-pixel.on{background:#ff3b4f;box-shadow:0 0 4px rgba(255,59,79,.75)}.live-display[data-colour=green] .live-pixel.on{background:#4fd47c;box-shadow:0 0 4px rgba(79,212,124,.75)}.live-display[data-colour=white] .live-pixel.on{background:#fff;box-shadow:0 0 4px rgba(255,255,255,.65)}</style><section id='liveDisplay' class='live-display' data-colour='"));
        web.sendContent(liveDisplayColourName());
        web.sendContent_P(PSTR("' aria-label='Live display preview'><span class='live-display-title'>Live display</span><div id='liveDisplayPixels' class='live-display-pixels' role='img' aria-label='Current 32 by 8 clock display'></div></section>"));
        web.sendContent_P(PSTR("<style>.live-display[data-colour=blue] .live-pixel.on{background:#4da3ff;box-shadow:0 0 4px rgba(77,163,255,.75)}</style>"));
        const String safeSsid = escapeHtml(WiFi.SSID());
        ipAddress = WiFi.localIP().toString();
        const int currentRssi = WiFi.RSSI();
        pageChunk = F("<div class='status-grid'><div class='status-item'><span class='status-label'>Wi-Fi</span><span class='status-value'>");
        pageChunk += safeSsid;
        pageChunk += F("</span></div><div class='status-item'><span class='status-label'>IP address</span><span class='status-value'>");
        pageChunk += ipAddress;
        pageChunk += F("</span></div><div class='status-item'><span class='status-label'>Wi-Fi signal</span><span id='signalStatus' class='status-value'>");
        pageChunk += signalQuality(currentRssi);
        pageChunk += F(" <small>(");
        pageChunk += currentRssi;
        pageChunk += F(" dBm)</small></span></div></div>");
        flushWebChunk(pageChunk);
    }

    if (settingsPage) {
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
    pageChunk = F("<form method='POST' action='/save'>");
    flushWebChunk(pageChunk);

    pageChunk += F("<fieldset id='displaySettings'><legend>Display settings <span style='display:inline-block;margin-left:.45em;padding:.18em .5em;border-radius:999px;font-size:.7em;vertical-align:middle;background:rgba(55,190,135,.16);color:#9cf0c8'>No reboot &middot; save below</span></legend><div class='inline-fields'><label>Brightness <select name='brightness'>");
    appendWebOption(pageChunk, "1", "20%", settings.brightness == 1);
    appendWebOption(pageChunk, "2", "40%", settings.brightness == 2);
    appendWebOption(pageChunk, "3", "60%", settings.brightness == 3);
    appendWebOption(pageChunk, "4", "80%", settings.brightness == 4);
    appendWebOption(pageChunk, "5", "100%", settings.brightness == 5);
    pageChunk += F("</select></label><label>Scroll speed <select name='scrollspeed'>");
    appendWebOption(pageChunk, "0", "Slow", settings.scrollSpeed == 0);
    appendWebOption(pageChunk, "1", "Fast", settings.scrollSpeed == 1);
    pageChunk += F("</select></label></div><div class='inline-fields'><label>HH(:)MM separator <select name='colonblink'>");
    flushWebChunk(pageChunk);

    appendWebOption(pageChunk, "0", "Solid", settings.clockColonBlink == 0);
    appendWebOption(pageChunk, "1", "Blink", settings.clockColonBlink == 1);
    pageChunk += F("</select></label><label>Digit transition <select name='transition'>");
    appendWebOption(pageChunk, "1", "Vertical sweep", !settings.cleanTransitions);
    appendWebOption(pageChunk, "0", "Instant change", settings.cleanTransitions);
    pageChunk += F("</select></label></div><div class='inline-fields'><label>Date scrolling interval <select name='scroll'>");
    appendWebOption(pageChunk, "1", "Every minute", settings.scrolling == 1);
    appendWebOption(pageChunk, "2", "Every 15 Minutes", settings.scrolling == 2);
    appendWebOption(pageChunk, "3", "Every hour", settings.scrolling == 3);
    appendWebOption(pageChunk, "0", "Off", settings.scrolling == 0);
    pageChunk += F("</select></label><label>Number of message scrolls <select name='apiscroll'>");
    appendWebOption(pageChunk, "0", "Continuous", settings.apiScrollCount == 0);
    for (int count = 1; count <= 5; count++) {
        char countText[2];
        snprintf(countText, sizeof(countText), "%d", count);
        appendNumericWebOption(pageChunk, count, countText,
                               settings.apiScrollCount == count);
    }
    pageChunk += F("</select></label></div><div class='inline-fields'><label>Chronograph mode <select name='chronomode'>");
    appendWebOption(pageChunk, "0", "Precision (MM:SS:cc)",
                    settings.chronographDisplayMode == 0);
    appendWebOption(pageChunk, "1", "Normal mode (HH:MM:ss)",
                    settings.chronographDisplayMode == 1);
    pageChunk += F("</select></label><label>Live display colour <select name='livecolour'>");
    appendWebOption(pageChunk, "0", "Red", liveDisplaySettings.colour == LIVE_DISPLAY_RED);
    appendWebOption(pageChunk, "1", "Green", liveDisplaySettings.colour == LIVE_DISPLAY_GREEN);
    appendWebOption(pageChunk, "3", "Blue", liveDisplaySettings.colour == LIVE_DISPLAY_BLUE);
    appendWebOption(pageChunk, "2", "White", liveDisplaySettings.colour == LIVE_DISPLAY_WHITE);
    pageChunk += F("</select></label></div><button id='saveDisplaySettingsButton' type='button'>Save display settings (no reboot)</button><small id='displaySettingsStatus' class='api-status' aria-live='polite'></small></fieldset>");
    flushWebChunk(pageChunk);
    web.sendContent_P(PSTR("<script>(()=>{const box=document.getElementById('displaySettings'),status=document.getElementById('displaySettingsStatus'),controls=[...box.querySelectorAll('select')],displaySave=document.getElementById('saveDisplaySettingsButton');let clearStatus=0;controls.forEach(e=>e.dataset.saved=e.value);box.addEventListener('change',()=>{clearTimeout(clearStatus);status.textContent='Unsaved changes'});displaySave.addEventListener('click',async()=>{clearTimeout(clearStatus);const body=new URLSearchParams(),rebootSave=box.closest('form').querySelector(\"button[type='submit']\");controls.forEach(e=>{body.set(e.name,e.value);e.disabled=true});displaySave.disabled=true;if(rebootSave)rebootSave.disabled=true;status.textContent='Saving...';try{let r=await fetch('/display/settings',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});let x=await r.json();if(!r.ok)throw new Error(x.error||'Unable to save settings');controls.forEach(e=>e.dataset.saved=e.value);status.textContent='Settings saved'}catch(error){controls.forEach(e=>e.value=e.dataset.saved);status.textContent=error.message||'Unable to save settings'}finally{controls.forEach(e=>e.disabled=false);displaySave.disabled=false;if(rebootSave)rebootSave.disabled=false;clearStatus=setTimeout(()=>status.textContent='',2000)}});})();</script>"));

    pageChunk = F("<fieldset><legend>Time settings <span style='display:inline-block;margin-left:.45em;padding:.18em .5em;border-radius:999px;font-size:.7em;vertical-align:middle;background:rgba(255,180,70,.16);color:#ffd28a'>Reboot required</span></legend><label>Time format <select name='format'>");
    appendWebOption(pageChunk, "24", "24-hour", settings.timeFormat == 24);
    appendWebOption(pageChunk, "12", "12-hour", settings.timeFormat == 12);
    pageChunk += F("</select></label><label>Timezone <select name='timezone'>");
    flushWebChunk(pageChunk);

    MatrixClockTimezone::Preset timezonePreset;
    const bool savedRegionalTimezone = MatrixClockTimezone::find(settings.timezone, timezonePreset);
    if (!savedRegionalTimezone && MatrixClockTimezone::fixedUtc(settings.timezone)) {
        char legacyLabel[64];
        MatrixClockTimezone::fixedLabel(settings.timezone, legacyLabel, sizeof(legacyLabel));
        appendWebOption(pageChunk, settings.timezone, legacyLabel, true);
    }
    for (size_t i = 0; i < MatrixClockTimezone::COUNT; ++i) {
        MatrixClockTimezone::preset(i, timezonePreset);
        appendWebOption(pageChunk, timezonePreset.id, timezonePreset.label,
                        strcmp(settings.timezone, timezonePreset.id) == 0);
        if ((i % 4) == 3) flushWebChunk(pageChunk);
    }
    flushWebChunk(pageChunk);

    pageChunk = F("</select></label>");
    if (timezoneSettingsRecovered)
        pageChunk += F("<p role='alert'>An invalid saved timezone setting was recovered. Please check your timezone and daylight-saving selection.</p>");
    if (!savedRegionalTimezone)
        pageChunk += F("<p>Your saved fixed UTC offset has been preserved. Select a location above to enable its automatic daylight-saving rules.</p>");
    pageChunk += F("<label>Daylight saving <select name='dst'>");
    appendWebOption(pageChunk, "1", "Automatic", settings.daylightSaving == 1);
    appendWebOption(pageChunk, "0", "Disabled", settings.daylightSaving == 0);
    appendWebOption(pageChunk, "2", "Custom rule", settings.daylightSaving == 2);
    pageChunk += F("</select></label><small>Automatic follows the selected location's seasonal changes; Disabled keeps its standard time all year. Fixed-offset locations do not change. Presets need firmware updates if local laws change.</small><label>Custom POSIX rule (used in Custom rule mode) <input name='customtz' maxlength='63' value='");
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
    pageChunk += F("' placeholder='URL / IP Address'></label></fieldset>");
    flushWebChunk(pageChunk);

    pageChunk += F("<fieldset class='reboot-settings'><legend>Scheduled reboot <span style='display:inline-block;margin-left:.45em;padding:.18em .5em;border-radius:999px;font-size:.7em;vertical-align:middle;background:rgba(255,180,70,.16);color:#ffd28a'>Reboot required</span></legend><input type='hidden' name='rebootsettings' value='1'><label>Reboot mode <select name='rebootmode'>");
    appendWebOption(pageChunk, "daily", "Daily reboot",
                    rebootSettings.dailyEnabled);
    appendWebOption(pageChunk, "error", "Only if time error exceeds 0.5 seconds",
                    !rebootSettings.dailyEnabled);
    pageChunk += F("</select></label><details class='small'><summary>How scheduled reboots work</summary><p>Daily reboot restarts the clock at the selected time every day. Error recovery restarts it at that time only if the last accepted NTP check shows an error greater than 0.5 seconds. Chronograph mode postpones either reboot until 15 minutes back in clock mode.</p></details><div class='inline-fields'><label>Reboot hour <select name='restarthour'>");
    flushWebChunk(pageChunk);

    for (int hour = 0; hour < 24; hour++) {
        char hourText[3];
        snprintf(hourText, sizeof(hourText), "%02d", hour);
        appendNumericWebOption(pageChunk, hour, hourText,
                               settings.restartHour == hour);
    }
    flushWebChunk(pageChunk);
    web.sendContent_P(PSTR("</select></label><label>Reboot minute <select name='restartminute'>"));
    for (int minute = 0; minute < 60; minute += 5) {
        char minuteText[3];
        snprintf(minuteText, sizeof(minuteText), "%02d", minute);
        appendNumericWebOption(pageChunk, minute, minuteText,
                               settings.restartMinute == minute);
    }
    pageChunk += F("</select></label></div></fieldset><div class='settings-save-action'><button type='submit'>Save and reboot</button></div></form>");
    flushWebChunk(pageChunk);
    web.sendContent_P(PSTR("<script>(()=>{const mode=document.querySelector('select[name=dst]'),rule=document.querySelector('input[name=customtz]');if(!mode||!rule)return;function update(){const custom=mode.value==='2';rule.disabled=!custom;rule.required=custom}mode.addEventListener('change',update);update()})();</script>"));
    }

    if (!settingsPage) {
    sendDeviceInfoMarkup();
    web.sendContent_P(PSTR("<fieldset class='calibration-card'><legend>Clock accuracy calibration</legend><p class='calibration-help'>Automatically measures oscillator drift against NTP and applies validated compensation without restarting.</p><div class='calibration-grid'><div><span>Active compensation</span><strong id='calibrationActive'>"));
    web.sendContent(driftStoredText());
    web.sendContent_P(PSTR("</strong></div><div><span>Calibration status</span><strong id='calibrationStatus'>"));
    web.sendContent(driftStatusText());
    web.sendContent_P(PSTR("</strong></div><div><span>Estimated compensation</span><strong id='calibrationEstimate'>"));
    web.sendContent(driftEstimateText());
    web.sendContent_P(PSTR("</strong></div><div><span>Last NTP offset</span><strong id='calibrationOffset'>"));
    web.sendContent(driftLatestCheckText());
    web.sendContent_P(PSTR("</strong></div><div><span>Calibration progress</span><strong id='calibrationProgress'>"));
    web.sendContent(driftSampleText());
    web.sendContent_P(PSTR("</strong></div><div><span>Next NTP check</span><strong id='calibrationNext'>"));
    web.sendContent(ntpNextCheckText());
    web.sendContent_P(PSTR("</strong></div></div><p class='small'>Time alignment: <span id='calibrationPhase'>"));
    web.sendContent(driftPhaseText());
    web.sendContent_P(PSTR("</span><br>Last measurement: <span id='calibrationChecked'>"));
    web.sendContent(driftLastCheckTimeText());
    web.sendContent_P(PSTR("</span><br>Correction saved: <span id='calibrationSaved'>"));
    web.sendContent(driftSavedText());
    web.sendContent_P(PSTR("</span></p><details class='small'><summary>How calibration works</summary><p>The first check is after five minutes, then every two hours. Two consistent rate readings are needed: about four hours from a fresh start with a stable, low-latency connection; noisy or distant servers can take longer. Checks wait for normal clock mode, so an active chronograph is never retimed.</p><p>Positive ppm speeds up the timer; negative ppm slows it down. The time error is a measurement from the last accepted check, not a live precision guarantee. Small offsets are corrected gently without jumping the clock.</p><p>Completed rate readings survive a hardware reset in RTC memory, but not a power loss. The first reliable correction is saved to flash promptly, then changes are saved at most daily during normal running or through Save and reboot.</p></details></fieldset><fieldset><legend>Home Assistant / API integration</legend><p class='api-help'>Send a scrolling message with:<br><code>POST http://"));
    web.sendContent(ipAddress);
    web.sendContent_P(PSTR("/api/message</code><br><code>Content-Type: application/x-www-form-urlencoded</code><br><code>message=MatrixClock " MATRIXCLOCK_FIRMWARE_VERSION "</code><br><code>&amp;scrolls=1-5</code></p><details class='small'><summary>How API messages work</summary><p>Use the form below for a quick test. Home Assistant can call the same address with a REST command. Add an optional scrolls value to override this clock's saved setting for one message. Messages are limited to 64 printable characters and are unavailable while the chronograph is open.</p></details><form action='/api/message' method='POST' onsubmit=\"event.preventDefault();let f=this,b=document.getElementById('sendMessageButton'),s=document.getElementById('sendMessageStatus');b.disabled=true;s.textContent='Sending...';fetch(f.action,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(new FormData(f))}).then(r=>r.json()).then(x=>{s.textContent=x.accepted?'Message sent':'Message rejected';f.reset()}).catch(()=>{s.textContent='Unable to contact clock'}).finally(()=>{b.disabled=false})\"><div class='api-row'><label>Send message to clock <input name='message' maxlength='64' required placeholder='MatrixClock " MATRIXCLOCK_FIRMWARE_VERSION "'></label><button id='sendMessageButton' type='submit'>Send</button></div></form><button type='button' class='api-cancel' onclick=\"this.disabled=true;fetch('/api/cancel',{method:'POST'}).then(()=>{document.getElementById('sendMessageStatus').textContent='Cancellation requested'}).catch(()=>{document.getElementById('sendMessageStatus').textContent='Unable to contact clock'}).finally(()=>setTimeout(()=>this.disabled=false,1000))\" ondblclick=\"return false\">Cancel message</button><small id='sendMessageStatus' class='api-status' aria-live='polite'></small></fieldset><script>document.addEventListener('DOMContentLoaded',()=>{let i=document.querySelector('input[name=message]');if(i)i.removeAttribute('required')});document.addEventListener('submit',e=>{let f=e.target,i=f.querySelector&&f.querySelector('input[name=message]');if(i&&!i.value.trim())i.value='MatrixClock " MATRIXCLOCK_FIRMWARE_VERSION "'},true)</script>"));
    web.sendContent_P(PSTR("<script>document.addEventListener('DOMContentLoaded',()=>{let i=document.querySelector('input[name=message]');if(i){let d='MatrixClock " MATRIXCLOCK_FIRMWARE_VERSION "';i.value=i.defaultValue='';i.placeholder=d}})</script>"));
    web.sendContent_P(PSTR("<script>(()=>{let timers={};new MutationObserver(m=>m.forEach(x=>{let e=x.target;if(!e.id||!e.classList.contains('api-status'))return;clearTimeout(timers[e.id]);if(e.textContent)timers[e.id]=setTimeout(()=>e.textContent='',2000)})).observe(document.body,{subtree:true,childList:true,characterData:true})})();</script>"));
    web.sendContent_P(PSTR("<p><a class='button-link' href='/settings'>Settings</a></p><script>async function updateSignal(){let e=document.getElementById('signalStatus');if(!e)return;try{let r=await fetch('/status');let x=await r.json();e.innerHTML=x.quality+' <small>('+x.rssi+' dBm)</small>'}catch(e){}}updateSignal();setInterval(updateSignal,5000);</script>"));
    } else {
    web.sendContent_P(PSTR("<fieldset class='maintenance-actions'><legend>Maintenance</legend><button type='button' onclick=\"openSecurityModal(this)\">Change username and password</button><div id='securityModal' role='dialog' aria-modal='true' aria-labelledby='securityTitle' style='display:none;position:fixed;inset:0;overflow:hidden;background:rgba(0,0,0,.62);z-index:9999'><div class='card' style='position:fixed;top:50%;left:50%;transform:translate(-50%,-50%);margin:0;width:min(460px,calc(100vw - 2em));max-height:calc(100vh - 2em);overflow:auto'><h2 id='securityTitle'>Change username and password</h2><p class='small'>Leave both username and password blank to disable security.</p><form method='POST' action='/security' onsubmit=\"if(this.password.value!==this.confirmation.value){alert('Passwords do not match');return false}return true\"><label>Username<input name='username' maxlength='19' autocomplete='username'></label><label>New password<input name='password' type='password' maxlength='39' autocomplete='new-password'></label><label>Confirm password<input name='confirmation' type='password' maxlength='39' autocomplete='new-password'></label><button type='submit'>Save settings and reboot</button></form><button type='button' style='margin-top:.7em;background:#52627a' onclick=\"closeSecurityModal()\">Cancel</button></div></div><form method='POST' action='/calibration/reset' onsubmit=\"return confirm('Reset the drift calibration and collected measurements, then reboot?')\"><button type='submit' class='calibration-reset'>Reset drift calibration and reboot</button></form><form method='POST' action='/reset' onsubmit=\"return confirm('Erase all saved settings and Wi-Fi credentials, then reboot into setup mode?')\"><button class='danger' type='submit'>Factory reset</button></form></fieldset><p class='settings-navigation'><a class='button-link' href='/'>Back to clock</a></p><p class='firmware-link'><a href='/update'>Firmware update</a></p><script>let securityOpener=null;function openSecurityModal(b){let m=document.getElementById('securityModal');securityOpener=b;document.body.dataset.securityScrollY=window.scrollY;document.body.style.overflow='hidden';m.style.display='block';let i=m.querySelector('input[name=username]');if(i)i.focus()}function closeSecurityModal(){let m=document.getElementById('securityModal');m.style.display='none';document.body.style.overflow='';if(securityOpener)securityOpener.focus()}document.addEventListener('keydown',e=>{let m=document.getElementById('securityModal');if(!m||m.style.display==='none')return;if(e.key==='Escape'){e.preventDefault();closeSecurityModal();return}if(e.key==='Tab'){let a=[...m.querySelectorAll('input,button')].filter(x=>!x.disabled);if(!a.length)return;let n=a.indexOf(document.activeElement);if(e.shiftKey&&n<=0){e.preventDefault();a[a.length-1].focus()}else if(!e.shiftKey&&n===a.length-1){e.preventDefault();a[0].focus()}}});</script>"));
    web.sendContent_P(PSTR("<script>(()=>{const m=document.getElementById('securityModal'),c=m&&m.querySelector('.card');if(!m||!c)return;c.style.top='0';c.style.transform='translateX(-50%)';const show=window.openSecurityModal;window.openSecurityModal=function(button){c.style.top='0';c.style.transform='translateX(-50%)';show(button)};})();</script>"));
    }
    web.sendContent_P(PSTR("</div></body></html>"));
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
    web.sendContent_P(PSTR("<fieldset><legend>Device info</legend><div class='event-list'><div class='event-row'><span class='event-label'>Uptime</span><span id='infoUptime' class='event-value'>Loading...</span></div><div class='event-row'><span class='event-label'>RTC temperature</span><span id='infoRtcTemperature' class='event-value'>Loading...</span></div><div class='event-heading'><span>NTP status</span><span></span></div><div class='event-row'><span class='event-label'>Server</span><span id='infoServer' class='event-value'>Loading...</span></div><div class='event-row'><span class='event-label'>Resolved IP</span><span id='infoResolvedIp' class='event-value'>Loading...</span></div><div class='event-row'><span class='event-label'>Stratum</span><span id='infoStratum' class='event-value'>Loading...</span></div><div class='event-row'><span class='event-label'>Status</span><span id='infoStatus' class='event-value'>Loading...</span></div><div class='event-row'><span class='event-label'>Response time</span><span id='infoResponseTime' class='event-value'>Loading...</span></div><div class='event-row'><span class='event-label'>Last successful check</span><span id='infoLastSuccess' class='event-value'>Loading...</span></div><div class='event-heading'><span>Chronograph history</span><span>HH:MM:ss:cc</span></div><div class='event-row'><span class='event-label'>Last 1</span><span id='infoLast1' class='event-value'>Loading...</span></div><div class='event-row'><span class='event-label'>Last 2</span><span id='infoLast2' class='event-value'>Loading...</span></div><div class='event-row'><span class='event-label'>Last 3</span><span id='infoLast3' class='event-value'>Loading...</span></div></div></fieldset><script>function setDeviceInfoValue(id,value){let e=document.getElementById(id);if(e&&e.textContent!==value)e.textContent=value}let deviceInfoBusy=false,liveDisplayBusy=false,deviceInfoTimer=0,liveDisplayTimer=0,nextDisplayDelay=50;function renderLiveDisplay(frame,tilted){let grid=document.getElementById('liveDisplayPixels');if(!grid||!frame||frame.length!==64)return;if(!grid.children.length){let fragment=document.createDocumentFragment();for(let i=0;i<256;i++){let dot=document.createElement('span');dot.className='live-pixel';fragment.appendChild(dot)}grid.appendChild(fragment)}let dot=0;for(let row=0;row<8;row++)for(let byte=0;byte<4;byte++){let value=parseInt(frame.slice((row*4+byte)*2,(row*4+byte)*2+2),16);for(let bit=0;bit<8;bit++){let target=tilted==='1'?255-dot:dot;grid.children[target].classList.toggle('on',!!(value&(1<<(7-bit))));dot++}}}async function updateLog(){if(deviceInfoBusy)return;deviceInfoBusy=true;let controller=new AbortController(),timeout=setTimeout(()=>controller.abort(),4000);try{let r=await fetch('/log',{cache:'no-store',signal:controller.signal});if(r.redirected){location.href=r.url;return}if(!r.ok)throw 0;let x=await r.json();for(let k in x)setDeviceInfoValue(k.startsWith('calibration')?k:'info'+k[0].toUpperCase()+k.slice(1),x[k])}catch(e){}finally{clearTimeout(timeout);deviceInfoBusy=false;clearTimeout(deviceInfoTimer);deviceInfoTimer=setTimeout(updateLog,1000)}}async function updateLiveDisplay(){if(liveDisplayBusy)return;liveDisplayBusy=true;let controller=new AbortController(),timeout=setTimeout(()=>controller.abort(),2000);try{let r=await fetch('/display/frame',{cache:'no-store',signal:controller.signal});if(r.redirected){location.href=r.url;return}if(!r.ok)throw 0;let x=await r.json();renderLiveDisplay(x.frame,x.tilted);nextDisplayDelay=Math.max(20,Math.min(70,Number(x.nextMs)||50))}catch(e){}finally{clearTimeout(timeout);liveDisplayBusy=false;clearTimeout(liveDisplayTimer);liveDisplayTimer=setTimeout(updateLiveDisplay,nextDisplayDelay)}}updateLog();updateLiveDisplay();</script>"));
    web.sendContent_P(PSTR("<script>(()=>{let last=document.getElementById('infoLastSuccess').closest('.event-row');last.insertAdjacentHTML('afterend',\"<div class='event-row'><span class='event-label'>Next check at</span><span id='infoNextCheck' class='event-value'>Loading...</span></div>\");let history=document.getElementById('infoLast3').closest('.event-row');history.insertAdjacentHTML('afterend',\"<div style='display:flex;align-items:center;justify-content:flex-end;gap:.7em;min-height:2.2em'><span id='historyResetStatus' style='color:#9eb1ce;font:12px ui-monospace,monospace'></span><button id='historyResetButton' type='button' style='width:auto;min-height:2.2em;padding:.35em .7em;font-size:.82em' onclick='resetStopwatchHistory()'>Reset history</button></div>\")})();async function resetStopwatchHistory(){let b=document.getElementById('historyResetButton'),s=document.getElementById('historyResetStatus');b.disabled=true;try{let r=await fetch('/history/reset',{method:'POST',cache:'no-store'});if(!r.ok)throw 0;let x=await r.json();setDeviceInfoValue('infoLast1',x.last1);setDeviceInfoValue('infoLast2',x.last2);setDeviceInfoValue('infoLast3',x.last3);s.textContent='History cleared';setTimeout(()=>s.textContent='',2000)}catch(e){s.textContent='Unable to clear'}setTimeout(()=>b.disabled=false,300)}</script>"));
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
    if (lastNtpCheckResult == NTP_CHECK_OFFSET_REJECTED)
        return "Check rejected - implausible time";
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
    if (!softwareClockValid || nextDriftCheckAt == 0 || nextDriftCheckEpoch == 0)
        return "Awaiting time sync";

    int32_t remainingMs = (int32_t)(nextDriftCheckAt - millis());
    if (remainingMs <= 0)
        return "Waiting for clock mode...";

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

String ntpNextCheckAtText() {
    if (!softwareClockValid || nextDriftCheckAt == 0 || nextDriftCheckEpoch == 0)
        return "Awaiting time sync";

    if ((int32_t)(nextDriftCheckAt - millis()) <= 0)
        return "Waiting for clock mode...";

    time_t checkTime = (time_t)nextDriftCheckEpoch;
    tm* localCheck = localtime(&checkTime);
    if (!localCheck)
        return "Unavailable";
    char text[12];
    snprintf(text, sizeof(text), "%02d:%02d:%02d",
             localCheck->tm_hour, localCheck->tm_min, localCheck->tm_sec);
    return String(text);
}

String deviceUptimeText() {
    // micros64() also avoids the 49-day rollover of a millis()-based uptime.
    uint64_t seconds = micros64() / 1000000ULL;
    uint32_t days = (uint32_t)(seconds / 86400ULL);
    uint32_t hours = (uint32_t)((seconds / 3600ULL) % 24ULL);
    uint32_t minutes = (uint32_t)((seconds / 60ULL) % 60ULL);
    char text[40];
    snprintf(text, sizeof(text), "%lu %s %02lu:%02lu:%02lu",
             (unsigned long)days, days == 1 ? "day" : "days",
             (unsigned long)hours, (unsigned long)minutes,
             (unsigned long)(seconds % 60ULL));
    return String(text);
}

bool readRtcTemperatureQuarterDegrees(int16_t& value) {
    // The DS3231 refreshes these registers about once per minute. Cache the
    // native quarter-degree value so the web page and API can use different
    // presentation resolutions without creating extra I2C traffic.
    static bool valid = false;
    static int16_t quarterDegrees = 0;
    static uint32_t lastReadAt = 0;
    uint32_t now = millis();

    if (!valid || (uint32_t)(now - lastReadAt) >= 60000UL) {
        lastReadAt = now;
        Wire.beginTransmission(DS3231_ADDRESS);
        Wire.write(temperatureMsbREG);
        if (Wire.endTransmission() == 0 &&
            Wire.requestFrom((unsigned char)DS3231_ADDRESS,
                             (unsigned char)2) == 2) {
            int8_t wholeDegrees = (int8_t)Wire.read();
            uint8_t fraction = ((uint8_t)Wire.read() >> 6) & 0x03;
            quarterDegrees = (int16_t)wholeDegrees * 4 + fraction;
            valid = true;
        }
    }

    if (valid)
        value = quarterDegrees;
    return valid;
}

String rtcTemperatureText() {
    int16_t quarterDegrees;
    if (!readRtcTemperatureQuarterDegrees(quarterDegrees))
        return "Unavailable";

    // Convert quarter-degree units to half-degree units, rounding midpoint
    // values away from zero, then present a consistent single decimal place.
    int16_t halfDegrees = (quarterDegrees >= 0)
        ? (quarterDegrees + 1) / 2
        : (quarterDegrees - 1) / 2;
    int16_t magnitude = halfDegrees < 0 ? -halfDegrees : halfDegrees;
    char text[16];
    snprintf(text, sizeof(text), "%s%d.%u " "\xC2\xB0" "C",
             halfDegrees < 0 ? "-" : "",
             magnitude / 2,
             (unsigned)((magnitude % 2) * 5));
    return String(text);
}

// Encode the frame exactly as it is physically shown: 8 rows of four bytes,
// represented as 64 hexadecimal characters for the lightweight web preview.
String liveDisplayFrameText() {
    static const char hex[] = "0123456789abcdef";
    char frame[65];
    uint8_t out = 0;
    const bool tilted = kk;
    for (uint8_t physicalRow = 0; physicalRow < 8; physicalRow++) {
        uint8_t logicalRow = tilted ? 7 - physicalRow : physicalRow;
        for (uint8_t byteIndex = 0; byteIndex < 4; byteIndex++) {
            uint8_t value = 0;
            for (uint8_t bitIndex = 0; bitIndex < 8; bitIndex++) {
                uint8_t physicalColumn = byteIndex * 8 + bitIndex;
                uint8_t logicalColumn = tilted
                    ? physicalColumn : (uint8_t)(31 - physicalColumn);
                uint8_t module = logicalColumn >> 3;
                uint8_t moduleBit = logicalColumn & 7;
                if (LEDarr[module][logicalRow] & (1 << moduleBit))
                    value |= 1 << (7 - bitIndex);
            }
            frame[out++] = hex[value >> 4];
            frame[out++] = hex[value & 15];
        }
    }
    frame[out] = '\0';
    return String(frame);
}

uint16_t nextLiveDisplayPollDelayMs() {
    uint64_t elapsedUs = softwareClockValid
        ? softwareClockElapsedAt(micros64()) : (uint64_t)millis() * 1000ULL;
    const uint32_t framePeriodUs = 50000UL; // 20 Hz
    uint32_t intoFrameUs = (uint32_t)(elapsedUs % framePeriodUs);
    // Poll shortly after each 50 ms boundary so the browser samples
    // the completed physical frame rather than the outgoing one.
    return (uint16_t)(((framePeriodUs - intoFrameUs) / 1000UL) + 8UL);
}

void handleWebLog() {
    if (!requireWebAuth()) return;
    if (!refreshMainPageViewer()) {
        web.send(423, "application/json", "{\"error\":\"Live display open on another device\"}");
        return;
    }
    String payload;
    payload.reserve(1520);
    payload += '{';
    auto appendValue = [&payload](const char* key, const String& value, bool first) {
        if (!first) payload += ',';
        payload += '\"';
        payload += key;
        payload += F("\":\"");
        payload += escapeJson(value);
        payload += '\"';
    };
    appendValue("uptime", deviceUptimeText(), true);
    appendValue("rtcTemperature", rtcTemperatureText(), false);
    appendValue("server", ntpServerDisplayText(), false);
    appendValue("resolvedIp", lastNtpMetadataValid
                ? lastNtpServerAddress.toString() : String("Awaiting valid reply"), false);
    appendValue("stratum", ntpStratumText(), false);
    appendValue("status", ntpReplyStatusText(), false);
    appendValue("responseTime", ntpResponseTimeText(), false);
    appendValue("lastSuccess", ntpLastSuccessText(), false);
    appendValue("nextCheck", ntpNextCheckAtText(), false);
    appendValue("calibrationActive", driftStoredText(), false);
    appendValue("calibrationStatus", driftStatusText(), false);
    appendValue("calibrationEstimate", driftEstimateText(), false);
    appendValue("calibrationOffset", driftLatestCheckText(), false);
    appendValue("calibrationProgress", driftSampleText(), false);
    appendValue("calibrationNext", ntpNextCheckText(), false);
    appendValue("calibrationPhase", driftPhaseText(), false);
    appendValue("calibrationChecked", driftLastCheckTimeText(), false);
    appendValue("calibrationSaved", driftSavedText(), false);
    appendValue("last1", chronographHistory[0], false);
    appendValue("last2", chronographHistory[1], false);
    appendValue("last3", chronographHistory[2], false);
    payload += '}';
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, "application/json", payload);
}

void handleLiveDisplayFrame() {
    if (!requireWebAuth()) return;
    if (!refreshMainPageViewer()) {
        web.send(423, "application/json", "{\"error\":\"Live display open on another device\"}");
        return;
    }
    String payload;
    payload.reserve(112);
    payload = F("{\"frame\":\"");
    payload += liveDisplayFrameText();
    payload += F("\",\"tilted\":\"");
    payload += kk ? '1' : '0';
    payload += F("\",\"nextMs\":");
    payload += String(nextLiveDisplayPollDelayMs());
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

bool parseDisplaySetting(const char* name, uint8_t minimum,
                         uint8_t maximum, uint8_t& result) {
    if (!web.hasArg(name)) return false;
    String value = web.arg(name);
    if (value.length() != 1 || value[0] < '0' || value[0] > '9')
        return false;
    uint8_t parsed = (uint8_t)(value[0] - '0');
    if (parsed < minimum || parsed > maximum)
        return false;
    result = parsed;
    return true;
}

void handleDisplaySettingsSave() {
    if (!requireWebAuth()) return;

    uint8_t brightnessValue, scrollSpeedValue, colonBlinkValue;
    uint8_t transitionValue, scrollingValue, chronographValue, apiScrollValue;
    uint8_t liveDisplayColourValue;
    if (!parseDisplaySetting("brightness", 1, 5, brightnessValue) ||
        !parseDisplaySetting("scrollspeed", 0, 1, scrollSpeedValue) ||
        !parseDisplaySetting("colonblink", 0, 1, colonBlinkValue) ||
        !parseDisplaySetting("transition", 0, 1, transitionValue) ||
        !parseDisplaySetting("scroll", 0, 3, scrollingValue) ||
        !parseDisplaySetting("chronomode", 0, 1, chronographValue) ||
        !parseDisplaySetting("apiscroll", 0, 5, apiScrollValue) ||
        !parseDisplaySetting("livecolour", LIVE_DISPLAY_RED,
                             LIVE_DISPLAY_BLUE, liveDisplayColourValue)) {
        web.send(400, "application/json", "{\"error\":\"Invalid display settings\"}");
        return;
    }

    // Changing layout or limit part-way through an active chronograph could
    // reinterpret the current elapsed value. Other display choices remain
    // live, but this particular change waits until normal clock mode.
    if (apiBlockedByChronograph &&
        chronographValue != settings.chronographDisplayMode) {
        web.send(409, "application/json", "{\"error\":\"Close chronograph first\"}");
        return;
    }

    const ClockSettings previousSettings = settings;
    const LiveDisplaySettings previousLiveDisplaySettings = liveDisplaySettings;
    settings.brightness = brightnessValue;
    settings.scrollSpeed = scrollSpeedValue;
    settings.clockColonBlink = colonBlinkValue;
    settings.cleanTransitions = transitionValue == 0;
    settings.scrolling = scrollingValue;
    settings.chronographDisplayMode = chronographValue;
    settings.apiScrollCount = apiScrollValue;
    liveDisplaySettings.colour = liveDisplayColourValue;
    prepareLiveDisplaySettings();

    max7219_set_brightness(settings.brightness * 3);
    setDisplayTicker(apiBlockedByChronograph);
    EEPROM.put(0, settings);
    EEPROM.put(LIVE_DISPLAY_SETTINGS_OFFSET, liveDisplaySettings);
    if (!EEPROM.commit()) {
        settings = previousSettings;
        liveDisplaySettings = previousLiveDisplaySettings;
        max7219_set_brightness(settings.brightness * 3);
        setDisplayTicker(apiBlockedByChronograph);
        EEPROM.put(0, settings);
        EEPROM.put(LIVE_DISPLAY_SETTINGS_OFFSET, liveDisplaySettings);
        EEPROM.commit();
        web.send(503, "application/json", "{\"error\":\"Unable to save settings\"}");
        return;
    }

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

void handleApiTemperature() {
    if (!requireWebAuth()) return;
    int16_t quarterDegrees;
    if (!readRtcTemperatureQuarterDegrees(quarterDegrees)) {
        web.sendHeader("Cache-Control", "no-store");
        web.send(503, "application/json", "{\"error\":\"RTC temperature unavailable\"}");
        return;
    }

    // Preserve the DS3231's native 0.25-degree resolution for Home Assistant;
    // only the human-facing Device info value is rounded to 0.5 degrees.
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, "application/json",
             String("{\"temperature_c\":") +
             String((float)quarterDegrees / 4.0f, 2) + "}");
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
                  "font:inherit;font-weight:600;color:white;border:0;border-radius:12px;background:#1677ff;transition:transform .08s ease,filter .08s ease,box-shadow .08s ease;box-shadow:0 5px 14px rgba(22,119,255,.25)}button.danger{background:#c43d59;box-shadow:0 5px 14px rgba(196,61,89,.25)}button:not(:disabled):active{transform:translateY(2px) scale(.99);filter:brightness(.82);box-shadow:0 1px 4px rgba(0,0,0,.3)}"
                  "a,a:visited,a:hover,a:active{color:#1677ff}a.button-link{display:block;width:100%;min-height:2.6em;padding:.6em .7em;text-align:center;text-decoration:none;font-weight:600;color:#fff;border-radius:12px;background:#1677ff;box-shadow:0 5px 14px rgba(22,119,255,.25)}a.button-link:active{transform:translateY(2px) scale(.99);filter:brightness(.82)}.small{color:#aebfda;line-height:1.5}</style></head><body><main class='card'><h1>Firmware update</h1>"
                  "<p class='notice'>Current firmware: <strong>" MATRIXCLOCK_FIRMWARE_VERSION "</strong></p>"
                  "<p class='small'>Check GitHub for a newer release, download its OTA firmware file, then return here to upload it. Do not use the Factory 4 MB image on this page.</p>"
                  "<p><a href='https://github.com/maddenste/MatrixClock-Improved/releases' target='_blank' rel='noopener'>Check for updates on GitHub</a></p>"
                  "<p class='notice'>Choose an OTA firmware file, or drag it into the box below</p>"
                  "<form method='POST' action='/update' enctype='multipart/form-data' onsubmit=\"if(!confirm('Upload this OTA firmware file and reboot the clock? Do not switch off power or close this page while the update is in progress.'))return false;this.querySelector('button').disabled=true\">"
                  "<input type='file' name='firmware' accept='.bin,application/octet-stream' required>"
                  "<button type='submit' class='danger'>Upload and reboot</button></form><p><a class='button-link' href='/settings'>Return to settings</a></p>"
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
    if (!resetDriftCalibration(true)) {
        web.send(503, "text/plain", "Unable to save calibration reset. Please retry.");
        return;
    }
    sendRebootingPage();
    delay(250);
    ESP.restart();
}

void handleWebSave() {
    // Validate before changing ANY settings or committing calibration.
    // AP setup omits these fields and preserves the loaded timezone.
    String timezoneId = web.hasArg("timezone") ? web.arg("timezone") : String(settings.timezone);
    String customRule = web.hasArg("customtz") ? web.arg("customtz") : String(settings.customTimezone);
    String dstMode = web.hasArg("dst") ? web.arg("dst") : String(settings.daylightSaving);
    timezoneId.trim();
    customRule.trim();
    dstMode.trim();
    if (timezoneId.length() >= sizeof(settings.timezone) ||
        customRule.length() >= sizeof(settings.customTimezone) ||
        timezoneId.length() != strlen(timezoneId.c_str()) ||
        customRule.length() != strlen(customRule.c_str()) ||
        (dstMode != "0" && dstMode != "1" && dstMode != "2") ||
        !MatrixClockTimezone::validSelection(timezoneId.c_str(), customRule.c_str(),
                                             static_cast<uint8_t>(dstMode.toInt()))) {
        web.send(400, "text/plain",
                 "Invalid timezone settings. Choose a listed timezone and daylight-saving mode. "
                 "Custom rule mode requires a valid POSIX rule of at most 63 characters. "
                 "Nothing was saved. Go back to correct the settings.");
        return;
    }
    const ClockSettings previousSettings = settings;
    const RebootSettings previousRebootSettings = rebootSettings;
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
    strlcpy(settings.timezone, timezoneId.c_str(), sizeof(settings.timezone));
    strlcpy(settings.customTimezone, customRule.c_str(), sizeof(settings.customTimezone));
    settings.daylightSaving = static_cast<uint8_t>(dstMode.toInt());
    bool unusedRecovery = false;
    MatrixClockTimezone::normalize(settings.timezone, settings.customTimezone,
                                   settings.daylightSaving, settings.utcOffset, unusedRecovery);
    // AP setup does not contain these controls, so it must leave them alone.
    // Hidden marker distinguishes the settings form from the AP setup form.
    if (web.hasArg("rebootsettings")) {
        if (web.hasArg("rebootmode")) {
            String rebootMode = web.arg("rebootmode");
            if (rebootMode == "daily") {
                rebootSettings.dailyEnabled = 1;
                rebootSettings.largeErrorEnabled = 0;
            } else if (rebootMode == "error") {
                rebootSettings.dailyEnabled = 0;
                rebootSettings.largeErrorEnabled = 1;
            }
        } else {
            // Accept the previous checkbox form during a rolling update.
            rebootSettings.dailyEnabled = web.arg("dailyreboot") == "1";
            rebootSettings.largeErrorEnabled = web.arg("errorreboot") == "1";
        }
    }
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
    if (web.hasArg("apiscroll")) settings.apiScrollCount = constrain(web.arg("apiscroll").toInt(), 0, 5);
    commitDriftCalibrationIfReady();
    prepareRebootSettings();
    EEPROM.put(0, settings);
    EEPROM.put(REBOOT_SETTINGS_OFFSET, rebootSettings);
    if (!EEPROM.commit()) {
        settings = previousSettings;
        rebootSettings = previousRebootSettings;
        EEPROM.put(0, settings);
        EEPROM.put(REBOOT_SETTINGS_OFFSET, rebootSettings);
        web.send(503, "text/plain", "Unable to save settings. No reboot was requested. Please retry.");
        return;
    }
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
    web.on("/settings", HTTP_GET, handleWebRoot);
    web.on("/update", HTTP_GET, handleOtaPage);
    web.on("/update", HTTP_POST, handleOtaComplete, handleOtaUpload);
    web.on("/log", HTTP_GET, handleWebLog);
    web.on("/display/frame", HTTP_GET, handleLiveDisplayFrame);
    web.on("/history/reset", HTTP_POST, handleStopwatchHistoryReset);
    web.on("/api/message", HTTP_POST, handleApiMessage);
    web.on("/api/cancel", HTTP_POST, handleApiCancel);
    web.on("/api/temperature", HTTP_GET, handleApiTemperature);
    web.on("/api/playback", HTTP_POST, handleApiPlaybackSave);
    web.on("/display/settings", HTTP_POST, handleDisplaySettingsSave);
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
    // Advertise a clear, unique DHCP hostname before associating.  ESP's
    // factory default is ESP-<chip-id>; this gives each MatrixClock a stable
    // UniFi/DHCP label such as MatrixClock-20A117 instead.
    char matrixClockHostname[24];
    snprintf(matrixClockHostname, sizeof(matrixClockHostname),
             "MatrixClock-%06X", ESP.getChipId());
    WiFi.hostname(matrixClockHostname);

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
    if (!MatrixClockTimezone::resolve(settings.timezone, settings.customTimezone,
                                      settings.daylightSaving, tz, sizeof(tz))) {
        // Defensive fallback only: load and save paths validate settings first.
        strlcpy(tz, "UTC0", sizeof(tz));
        timezoneSettingsRecovered = true;
    }
    setenv("TZ", tz, 1);
    tzset();
}

uint64_t ntpObservedUs(const NtpMeasurement& measurement) {
    return (uint64_t)measurement.epoch * 1000000ULL + measurement.fractionUs;
}

uint64_t decodeNtpTimestampUs(const byte* value) {
    uint32_t seconds = ((uint32_t)value[0] << 24) | ((uint32_t)value[1] << 16) |
                       ((uint32_t)value[2] << 8) | value[3];
    uint32_t fraction = ((uint32_t)value[4] << 24) | ((uint32_t)value[5] << 16) |
                        ((uint32_t)value[6] << 8) | value[7];
    if (seconds == 0 && fraction == 0) return 0;
    // Select the modern NTP era, including the 2036 timestamp rollover.
    uint64_t fullSeconds = seconds;
    if (seconds < 2208988800UL) fullSeconds += (1ULL << 32);
    return (fullSeconds - 2208988800ULL) * 1000000ULL +
           (((uint64_t)fraction * 1000000ULL) >> 32);
}

bool readNtpMeasurement(NtpMeasurement& measurement, const IPAddress& server,
                        uint32_t timeoutUs) {
    while (udp.parsePacket() > 0)
        while (udp.available()) udp.read();
    memset(packetBuffer, 0, NTP_PACKET_SIZE);
    packetBuffer[0] = 0x23; // NTP v4 client, no leap warning
    packetBuffer[2] = 6;
    packetBuffer[3] = 0xEC;
    // The server must echo this per-request nonce in its origin timestamp.
    // This also works at cold boot, before a UTC timestamp is available.
    uint32_t nonce[2] = {ESP.random(), ESP.random()};
    nonce[1] |= 1UL;
    memcpy(packetBuffer + 40, nonce, sizeof(nonce));
    uint64_t requestStarted = micros64();
    if (!udp.beginPacket(server, 123)) return false;
    if (udp.write(packetBuffer, NTP_PACKET_SIZE) != NTP_PACKET_SIZE || !udp.endPacket())
        return false;
    while (micros64() - requestStarted < timeoutUs) {
        if (servicePhysicalFactoryReset()) { delay(1); continue; }
        int packetSize = udp.parsePacket();
        if (packetSize >= NTP_PACKET_SIZE) {
            uint64_t receivedAt = micros64();
            bool expectedPeer = udp.remoteIP() == server && udp.remotePort() == 123;
            int bytesRead = udp.read(packetBuffer, NTP_PACKET_SIZE);
            while (udp.available()) udp.read();
            uint8_t version = (packetBuffer[0] >> 3) & 7;
            uint8_t leap = packetBuffer[0] >> 6;
            uint8_t stratum = packetBuffer[1];
            if (!expectedPeer || bytesRead != NTP_PACKET_SIZE ||
                memcmp(packetBuffer + 24, nonce, sizeof(nonce)) != 0 ||
                (packetBuffer[0] & 7) != 4 || (version != 3 && version != 4) ||
                stratum < 1 || stratum > 15 || leap == 3)
                continue;
            uint64_t receivedByServer = decodeNtpTimestampUs(packetBuffer + 32);
            uint64_t sentByServer = decodeNtpTimestampUs(packetBuffer + 40);
            uint64_t localRoundTrip = receivedAt - requestStarted;
            if (!receivedByServer || sentByServer < receivedByServer ||
                sentByServer - receivedByServer > localRoundTrip ||
                localRoundTrip > timeoutUs) continue;
            uint32_t networkRoundTrip = (uint32_t)(localRoundTrip -
                                                  (sentByServer - receivedByServer));
            // Four-timestamp delay: remove server processing, then estimate
            // the return path. Residual Wi-Fi asymmetry remains measurement noise.
            uint64_t arrivalUs = sentByServer + networkRoundTrip / 2;
            measurement.epoch = (uint32_t)(arrivalUs / 1000000ULL);
            measurement.fractionUs = arrivalUs % 1000000ULL;
            measurement.referenceMicros = receivedAt;
            measurement.roundTripUs = networkRoundTrip;
            measurement.server = server;
            measurement.stratum = stratum;
            measurement.leapIndicator = leap;
            return true;
        }
        if (packetSize > 0) while (udp.available()) udp.read();
        delay(1); // avoid the old 5 ms receive-poll quantisation
    }
    return false;
}

bool collectNtpMeasurement(NtpMeasurement& selected, uint32_t& spreadUs,
                          uint32_t timeoutUs) {
    if (WiFi.status() != WL_CONNECTED) {
        lastNtpCheckResult = NTP_CHECK_NO_VALID_REPLY;
        return false;
    }
    // Keep a responsive server for this boot. Resolving a pool on every check
    // could switch servers, restart each baseline and prevent qualification.
    // After a failed check, resolve again on the scheduled retry for recovery.
    if (lastNtpMetadataValid && lastNtpCheckResult == NTP_CHECK_SUCCESS) {
        timeServerIP = lastNtpServerAddress;
    } else if (WiFi.hostByName(ntpServerName, timeServerIP, 1000) != 1) {
        lastNtpCheckResult = NTP_CHECK_NO_VALID_REPLY;
        return false;
    }
    NtpMeasurement samples[DRIFT_SAMPLES_PER_CHECK];
    uint8_t count = 0;
    for (uint8_t attempt = 0; attempt < DRIFT_SAMPLES_PER_CHECK; attempt++) {
        NtpMeasurement sample;
        if (readNtpMeasurement(sample, timeServerIP, timeoutUs) &&
            sample.roundTripUs <= DRIFT_MAX_RTT_US) samples[count++] = sample;
        if (attempt + 1 < DRIFT_SAMPLES_PER_CHECK) delay(120);
    }
    if (count < DRIFT_MIN_SAMPLES) {
        lastNtpCheckResult = NTP_CHECK_NO_STABLE_REPLY;
        return false;
    }
    for (uint8_t i = 0; i + 1 < count; i++)
        for (uint8_t j = i + 1; j < count; j++)
            if (samples[j].roundTripUs < samples[i].roundTripUs) {
                NtpMeasurement temporary = samples[i]; samples[i] = samples[j]; samples[j] = temporary;
            }
    // All five requests use one resolved server. Compare the three fastest
    // replies against raw elapsed time, never against the rate/slew-adjusted clock.
    for (uint8_t i = 0; i + 1 < DRIFT_MIN_SAMPLES; i++)
        for (uint8_t j = i + 1; j < DRIFT_MIN_SAMPLES; j++)
            if ((int64_t)ntpObservedUs(samples[j]) - (int64_t)samples[j].referenceMicros <
                (int64_t)ntpObservedUs(samples[i]) - (int64_t)samples[i].referenceMicros) {
                NtpMeasurement temporary = samples[i]; samples[i] = samples[j]; samples[j] = temporary;
            }
    int64_t spread = ((int64_t)ntpObservedUs(samples[DRIFT_MIN_SAMPLES - 1]) -
                     (int64_t)samples[DRIFT_MIN_SAMPLES - 1].referenceMicros) -
                    ((int64_t)ntpObservedUs(samples[0]) - (int64_t)samples[0].referenceMicros);
    if (spread < 0 || spread > DRIFT_MAX_SAMPLE_SPREAD_US) {
        lastNtpCheckResult = NTP_CHECK_INCONSISTENT_REPLIES;
        return false;
    }
    spreadUs = (uint32_t)spread;
    selected = samples[DRIFT_MIN_SAMPLES / 2];
    return true;
}

void publishNtpMeasurement(const NtpMeasurement& measurement) {
    // Publish metadata only after the complete measurement passes filtering.
    lastNtpStratum = measurement.stratum;
    lastNtpLeapIndicator = measurement.leapIndicator;
    lastNtpServerAddress = measurement.server;
    lastNtpSuccessEpoch = measurement.epoch;
    lastNtpMetadataValid = true;
    lastNtpRoundTripUs = measurement.roundTripUs;
    lastNtpCheckRttUs = measurement.roundTripUs;
    lastNtpCheckResult = NTP_CHECK_SUCCESS;
    networkTimeSynchronized = true;
}

// Start a rate-compensated software clock from the RTC's local calendar when
// startup NTP fails. This is deliberately limited to a verified saved rate and
// a plausible post-firmware-era calendar: an erased RTC baseline must never be
// presented as a synchronised clock or used to calculate drift.
bool seedSoftwareClockFromRtc() {
    uint16_t year = 0;
    uint8_t month = 0, day = 0, hour = 0, minute = 0, second = 0;
    bool stableSnapshot = false;
    for (uint8_t attempt = 0; attempt < 2 && !stableSnapshot; ++attempt) {
        const uint8_t secondsBefore = rtc_sekunde();
        year = (uint16_t)rtc_jahr() + 2000U;
        month = rtc_monat();
        day = rtc_tag();
        hour = rtc_stunde();
        minute = rtc_minute();
        second = rtc_sekunde();
        stableSnapshot = secondsBefore == second;
    }
    if (!activeDriftVerified || year < 2024 || year > 2099 ||
        !stableSnapshot ||
        month < 1 || month > 12 || day < 1 || day > 31 ||
        hour > 23 || minute > 59 || second > 59)
        return false;

    tm local = {};
    local.tm_year = (int)year - 1900;
    local.tm_mon = month - 1;
    local.tm_mday = day;
    local.tm_hour = hour;
    local.tm_min = minute;
    local.tm_sec = second;
    local.tm_isdst = -1; // Let the selected POSIX rule determine DST.
    time_t localEpoch = mktime(&local);
    if (localEpoch <= 0) return false;

    softwareEpochBase = (uint32_t)localEpoch;
    softwareClockElapsedUs = 0;
    softwareClockLastMicros = micros64();
    softwareClockRateRemainder = 0;
    softwareClockPhaseRemainingUs = 0;
    softwareClockPhaseRemainder = 0;
    softwareClockValid = true;
    networkTimeSynchronized = false;
    driftReferenceValid = false;
    scheduleNextDriftCheck(DRIFT_INITIAL_CHECK_DELAY_MS);
    return true;
}

// A good reply received during RTC holdover is an authoritative recovery, not
// a drift measurement against a potentially stale RTC phase. Re-anchor first,
// write the corrected RTC calendar, then collect the next rate interval as
// normal without discarding the saved active correction.
void recoverSoftwareClockFromNtp(const NtpMeasurement& measurement,
                                 uint32_t uncertaintyUs) {
    softwareEpochBase = measurement.epoch;
    softwareClockElapsedUs = measurement.fractionUs;
    softwareClockLastMicros = measurement.referenceMicros;
    softwareClockRateRemainder = 0;
    softwareClockPhaseRemainingUs = 0;
    softwareClockPhaseRemainder = 0;
    softwareClockValid = true;
    advanceSoftwareClockTo(micros64());
    time_t recoveredEpoch = softwareClockEpochNow();
    tm recoveredLocal = {};
    localtime_r(&recoveredEpoch, &recoveredLocal);
    rtc_set(&recoveredLocal);
    driftLastCheckValid = false;
    driftReferenceValid = false;
    acceptDriftReference(measurement, uncertaintyUs);
    publishNtpMeasurement(measurement);
    scheduleNextDriftCheck(DRIFT_CHECK_INTERVAL_MS);
    saveDriftSessionCheckpoint();
}

void acceptDriftReference(const NtpMeasurement& measurement, uint32_t uncertaintyUs) {
    driftReferenceValid = true;
    driftReferenceRawUs = measurement.referenceMicros;
    driftReferenceObservedUs = ntpObservedUs(measurement);
    driftReferenceUncertaintyUs = uncertaintyUs;
    driftReferenceServer = measurement.server;
}

void runDriftCalibrationCheck() {
    NtpMeasurement measurement;
    uint32_t spreadUs = 0;
    if (!collectNtpMeasurement(measurement, spreadUs, DRIFT_NTP_TIMEOUT_US)) {
        scheduleNextDriftCheck(DRIFT_RETRY_MS);
        return; // retain the last successful result, clearly mark the failed attempt
    }
    uint32_t uncertaintyUs = max<uint32_t>(500, max<uint32_t>(measurement.roundTripUs / 2, spreadUs));
    if (!networkTimeSynchronized) {
        recoverSoftwareClockFromNtp(measurement, uncertaintyUs);
        return;
    }
    uint64_t observedUs = ntpObservedUs(measurement);
    uint64_t elapsedAtMeasurement = softwareClockElapsedAt(measurement.referenceMicros);
    int64_t offsetUs = (int64_t)observedUs -
                      ((int64_t)softwareEpochBase * 1000000LL + (int64_t)elapsedAtMeasurement);
    if (offsetUs < -DRIFT_MAX_OFFSET_US || offsetUs > DRIFT_MAX_OFFSET_US) {
        lastNtpCheckResult = NTP_CHECK_OFFSET_REJECTED;
        scheduleNextDriftCheck(DRIFT_RETRY_MS);
        return;
    }
    publishNtpMeasurement(measurement);
    driftLastCheckValid = true;
    driftLastOffsetUs = (int32_t)offsetUs;
    driftLastRoundTripUs = measurement.roundTripUs;
    driftLastObservedUs = observedUs;
    if (driftAcceptedChecks < 65535) driftAcceptedChecks++;
    driftRateRejected = false;
    driftAwaitingLongerInterval = false;

    // Acknowledge every already-applied correction before replacing the slew
    // target. Carry the selected packet's time forward to this exact instant.
    uint64_t nowUs = micros64();
    advanceSoftwareClockTo(nowUs);
    uint64_t sinceReceipt = nowUs - measurement.referenceMicros;
    int64_t temporaryRemainder = 0;
    int64_t currentOffsetUs = (int64_t)observedUs +
        (int64_t)adjustedClockDeltaUs(sinceReceipt, temporaryRemainder) -
        ((int64_t)softwareEpochBase * 1000000LL + (int64_t)softwareClockElapsedUs);
    scheduleClockPhaseSlew(currentOffsetUs);

    bool serverChanged = driftReferenceValid && measurement.server != driftReferenceServer;
    if (!driftReferenceValid || serverChanged) {
        if (serverChanged) { driftCandidateValid = false; driftRateEstimateCount = 0; }
        acceptDriftReference(measurement, uncertaintyUs);
    } else {
        uint64_t rawDeltaUs = measurement.referenceMicros - driftReferenceRawUs;
        if (rawDeltaUs > DRIFT_MAX_RATE_INTERVAL_US || observedUs <= driftReferenceObservedUs) {
            driftCandidateValid = false;
            driftRateEstimateCount = 0;
            driftRateRejected = true;
            acceptDriftReference(measurement, uncertaintyUs);
        } else if (rawDeltaUs >= DRIFT_MIN_RATE_INTERVAL_US) {
            uint64_t observedDeltaUs = observedUs - driftReferenceObservedUs;
            int64_t rateErrorUs = (int64_t)observedDeltaUs - (int64_t)rawDeltaUs;
            uint64_t uncertainty = ((uint64_t)uncertaintyUs + driftReferenceUncertaintyUs) *
                                   1000000000ULL / rawDeltaUs;
            if (rateErrorUs > (int64_t)(rawDeltaUs / 10000ULL) ||
                rateErrorUs < -(int64_t)(rawDeltaUs / 10000ULL)) {
                driftRateRejected = true;
                driftCandidateValid = false;
                driftRateEstimateCount = 0;
                acceptDriftReference(measurement, uncertaintyUs);
            } else if (uncertainty > DRIFT_MAX_UNCERTAINTY_PPM_MILLI) {
                driftAwaitingLongerInterval = true; // retain baseline to reduce uncertainty
            } else {
                int32_t rate = (int32_t)(rateErrorUs * 1000000000LL / (int64_t)rawDeltaUs);
                int32_t difference = rate - driftCandidatePpmMilli;
                if (!driftCandidateValid ||
                    difference > DRIFT_RATE_AGREEMENT_PPM_MILLI ||
                    difference < -DRIFT_RATE_AGREEMENT_PPM_MILLI) {
                    driftRateRejected = driftCandidateValid;
                    driftCandidatePpmMilli = rate;
                    driftRateEstimateCount = 1;
                } else {
                    uint8_t weight = driftRateEstimateCount < 4 ? driftRateEstimateCount : 3;
                    driftCandidatePpmMilli = (int32_t)(((int64_t)driftCandidatePpmMilli * weight + rate) /
                                                     (weight + 1));
                    if (driftRateEstimateCount < 4) driftRateEstimateCount++;
                }
                driftCandidateValid = true;
                // Start a new independent interval; no boot baseline reused forever.
                acceptDriftReference(measurement, uncertaintyUs);
                applyDriftCandidate();
            }
        } else {
            driftAwaitingLongerInterval = true;
        }
    }
    saveDriftSessionCheckpoint();
    // Save the first trusted correction promptly; thereafter at most daily
    // during normal running, or through an intentional save/reboot.
    if (activeDriftVerified &&
        ((driftCalibration.version != DRIFT_CALIBRATION_VERSION || !driftCalibration.updatedEpoch) ?
         (!driftStorageFault || (uint32_t)(millis() - lastDriftSaveAttemptAt) >= DRIFT_RETRY_MS) :
         (uint32_t)(millis() - lastDriftSaveAttemptAt) >= DRIFT_SAVE_INTERVAL_MS))
        commitDriftCalibrationIfReady();
    scheduleNextDriftCheck(DRIFT_CHECK_INTERVAL_MS);
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
    advanceSoftwareClockTo(micros64());
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
    loadLiveDisplaySettings();
    loadRebootSettings();
    loadAuthSettings();
    loadDriftCalibration();
    max7219_set_brightness(settings.brightness * 3);// Map 1-5 levels to MAX7219 0-15
    rtc_init(SDA, SCL);
    configureTimezone(); // needed if a verified correction enters RTC holdover
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
        // Fast local servers can answer almost immediately.  Keep the phase
        // label visible long enough to be read before showing OK! or Err!.
        delay(1000);
    }
    int ntpSampleCount = 0;
    if (!provisioningMode) {
        NtpMeasurement measurement;
        uint32_t spreadUs = 0;
        if (collectNtpMeasurement(measurement, spreadUs, 1500000UL)) {
            publishNtpMeasurement(measurement);
            ntpEstimatedEpoch = measurement.epoch;
            ntpEstimatedFractionUs = measurement.fractionUs;
            ntpReferenceMicros = measurement.referenceMicros;
            ntpSampleCount = DRIFT_MIN_SAMPLES;
        }
    }
    if (ntpSampleCount > 0) {
        // Anchor at packet reception; carry its fraction forward without
        // a delayed boundary wait changing the software clock's phase.
        uint64_t syncBoundaryMicros = ntpReferenceMicros;
        epoch = ntpEstimatedEpoch;
        configureTimezone();
        softwareEpochBase = epoch;
        softwareClockElapsedUs = ntpEstimatedFractionUs;
        softwareClockLastMicros = syncBoundaryMicros;
        softwareClockValid = true;
        uint64_t rtcBoundary = (softwareClockElapsedAt(micros64()) / 1000000ULL + 1ULL) * 1000000ULL;
        while (softwareClockElapsedAt(micros64()) < rtcBoundary) { delay(1); yield(); }
        time_t stableEpoch = softwareClockEpochNow();
        tm* stableLocal = localtime(&stableEpoch);
        rtc_set(stableLocal);
        scheduleNextDriftCheck(DRIFT_INITIAL_CHECK_DELAY_MS);
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
        if (seedSoftwareClockFromRtc())
            Serial.println("RTC holdover started with saved drift correction");
        // After Err!, show the clock before the explanatory marquee. This
        // applies to both direct RTC fallback and compensated RTC holdover.
        startupNtpFailureIntroPending = true;
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
    // On every normal Wi-Fi boot, identify the clock once before handing
    // control to the time renderer. This follows both NTP OK! and Err!, and
    // remains separate from API/date marquees so the physical button cannot
    // cancel it.
    if (!provisioningMode && WiFi.status() == WL_CONNECTED) {
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
    uint64_t chronographStartedAtMicros = 0;
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
    if (startupNtpFailureIntroPending) {
        // Err! has just completed. Bring the RTC-backed clock in before
        // holding it static for five seconds.
        z_PosX = 0;
        d_PosX = -200;
        clockMarquee = true;
        buttonScrollLock = true;
    }
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
        bool restartTimeReached = scheduledRebootTimeReached();
        bool restartWanted = scheduledRebootWanted();
        if (!restartWanted) {
            dailyRestartPending = false;
            mainClockResumeAt = 0;
        } else if (chronographMode) {
            // Both reboot policies defer until the chronograph is closed.
            // Re-entering the chronograph starts a fresh 15-minute grace period.
            mainClockResumeAt = 0;
            if (restartTimeReached) dailyRestartPending = true;
        } else if (dailyRestartPending) {
            if (!clockDisplayed || clockMarquee || chronographExitMarquee) {
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
            !f_scroll_x && !clockMarquee && !chronographExitMarquee &&
            !startupIpMarqueeActive && !ntpFailureMarqueeActive &&
            !ntpClockExitMarquee && !apiClockExitMarquee) {
            runDriftCalibrationCheck();
        }
        // The first NTP failure gets one deliberate startup sequence:
        // Err! -> incoming clock -> five static seconds -> two message passes.
        if (startupNtpFailureClockVisibleAt != 0 &&
            (uint32_t)(millis() - startupNtpFailureClockVisibleAt) >=
                STARTUP_NTP_FAILURE_CLOCK_HOLD_MS &&
            !networkTimeSynchronized && !clockMarquee && !f_scroll_x &&
            !ntpFailureMarqueeActive && !ntpClockExitMarquee &&
            !chronographExitMarquee && !apiMessageActive && !apiMessagePending) {
            startupNtpFailureClockVisibleAt = 0;
            ntpClockExitMarquee = true;
            ntpFailureMarqueeMessage = F("No sync-Set NTP");
            ntpFailureMarqueePass = 0;
            z_PosX = maxPosX;
            buttonScrollLock = true;
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
            // The DS3231 stores a complete date and time, so its battery-backed
            // calendar follows the same automatic date-scroll schedule during
            // NTP fallback. Its accuracy still depends on the last valid sync.
            // Do not let an automatic date event interrupt a startup/API/NTP
            // return animation.  Its scheduled instant is simply skipped if
            // the clock has not yet settled into its static display.
            bool dateIntervalTrigger = !clockMarquee &&
                !chronographExitMarquee &&
                !startupIpMarqueeActive &&
                !startupNtpFailureIntroPending &&
                startupNtpFailureClockVisibleAt == 0 &&
                !ntpClockExitMarquee && !ntpFailureMarqueeActive &&
                ((settings.scrolling == 1 && MEZ.sek12 == 46) ||
                 (settings.scrolling == 2 && (MEZ.min12 % 15) == 14 && MEZ.sek12 == 46) ||
                 (settings.scrolling == 3 && MEZ.min12 == 59 && MEZ.sek12 == 46));
            if (dateIntervalTrigger)
                f_scroll_x = true;//marquee enable flag
            // When startup NTP synchronisation failed, remind the user once
            // per hour. Keep this separate from the normal date marquee.
            if (!networkTimeSynchronized && MEZ.min12 == 0 && MEZ.sek12 == 1 &&
                !chronographMode && !apiMessageActive && !apiMessagePending &&
                !ntpFailureMarqueeActive && !ntpClockExitMarquee &&
                !startupNtpFailureIntroPending && startupNtpFailureClockVisibleAt == 0 &&
                !f_scroll_x && !clockMarquee && !chronographExitMarquee) {
                ntpClockExitMarquee = true;
                ntpFailureMarqueeMessage = F("No sync-Set NTP");
                ntpFailureMarqueePass = 0;
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
                        chronographStartedAtMicros, micros64(), previewRemainder);
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
                            chronographStartedAtMicros, micros64(), previewRemainder);
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
                            chronographStartedAtMicros = micros64();
                            chronographRateRemainder = 0;
                            setDisplayTicker(true);
                        } else if (chronographRunning) {
                            chronographElapsed += adjustedChronographDeltaMs(
                                chronographStartedAtMicros, micros64(), chronographRateRemainder);
                            if (chronographElapsed > chronographLimit)
                                chronographElapsed = chronographLimit;
                            chronographRunning = false;
                            recordChronographStop(chronographElapsed);
                            setDisplayTicker(false);
                        } else {
                            if (chronographElapsed < chronographLimit) {
                                chronographRunning = true;
                                chronographStartedAtMicros = micros64();
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
                                              chronographStartedAtMicros, micros64(),
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
                !apiClockExitMarquee && !ntpClockExitMarquee &&
                !startupNtpFailureIntroPending && startupNtpFailureClockVisibleAt == 0) {
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
                        ntpFailureMarqueePass = 0;
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
                    if (++ntpFailureMarqueePass < NTP_FAILURE_MARQUEE_SCROLLS) {
                        // A two-pass reminder is continuous: do not bring the
                        // RTC clock back between passes.
                        ntpFailureMarqueeX = -((int)ntpFailureMarqueeMessage.length() * 6);
                    } else {
                        ntpFailureMarqueeActive = false;
                        ntpFailureMarqueeX = 0;
                        ntpFailureMarqueePass = 0;
                        // Return to the RTC-backed clock with the normal clock
                        // marquee after the final pass has fully cleared.
                        z_PosX = 0;
                        d_PosX = -200;
                        clockMarquee = true;
                        buttonScrollLock = true;
                    }
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
                    if (startupNtpFailureIntroPending) {
                        startupNtpFailureIntroPending = false;
                        startupNtpFailureClockVisibleAt = millis();
                        buttonScrollLock = true;
                    } else {
                        buttonScrollLock = false;
                    }
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


