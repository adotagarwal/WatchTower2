// INSTRUCTIONS
// - set the PIN_ANTENNA to desired output pin
// - download and run the code on your device
// - connect your phone to "WWVB" to set the wifi config for the device

// Designed for the following, but should be easily
// transferable to other components:
// - ESP32: https://www.adafruit.com/product/5395
// - DRV8833 breakout: https://www.adafruit.com/product/3297

#include <WiFiManager.h>
#include "time.h"
#include "esp_sntp.h"
#include "time-services.h"
#include <Preferences.h>
#include <ESPUI.h>

enum WWVB_T {
  ZERO = 0,
  ONE = 1,
  MARK = 2,
};

// Set these values to the pin numbers corresponding to those your antenna and onboard led are connected
const int PIN_ANTENNA = 23;
const int PIN_LED = 2;

//STATE VARIABLES
WiFiManager wifiManager;
Preferences thePreferences;
bool logicValue = 0; // TODO rename
uint64_t logicBits = 0;
struct timeval lastSync;
uint16_t liveLogHandle;
uint16_t liveLogicBitsHandle;
String logBuffer = "";

// PREFERENCE VARIABLES
bool enableFlashing = false;
enum time_service theService = WWVB;
String theNtpServer;
String theTimezone;
String theHostname;

// TIMEZONES: https://gist.github.com/alwynallan/24d96091655391107939

// A tricky way to force arduino to reboot
// by accessing a protected memory address
void(* forceReboot) (void) = 0;

// A callback that tracks when we last sync'ed the
// time with the ntp server
void time_sync_notification_cb(struct timeval *tv) {
    lastSync = *tv;
}

// A callback that is called when the device
// starts up an access point for wifi configuration.
// This is called when the device cannot connect to wifi.
void accesspointCallback(WiFiManager*) {
    Serial.println("Connect to WWVB with another device to set wifi configuration.");
}

void readPreferences() { 
    thePreferences.begin("watchtower", true);
    Serial.println("Reading preferences");
    theNtpServer = thePreferences.getString("ntp-Server", "pool.ntp.org");
    theTimezone = thePreferences.getString("timezone", "EST5EDT,M3.2.0,M11.1.0");
    theHostname = thePreferences.getString("hostname", "wwvb");
    const char *myServiceString = thePreferences.getString("timeService", "AAIOT-WWVB").c_str();
    theService = getServiceForString(myServiceString);
    enableFlashing = thePreferences.getBool("enableFlashing", true);

    Serial.printf("Preferences read: {ntp server: %s, timezone: %s, hostname: %s, service: %d, enableFlashing: %d}\n",
        theNtpServer,
        theTimezone.c_str(),
        theHostname,
        theService,
        enableFlashing
        );
    thePreferences.end();
}

void savePreferences() {
    thePreferences.begin("watchtower", false);
    String myService = getStringForService(theService);
    thePreferences.putString("ntp-Server", theNtpServer);
    thePreferences.putString("timezone", theTimezone);
    thePreferences.putString("timeService", myService);
    thePreferences.putString("hostname", theHostname);
    thePreferences.putBool("enableFlashing", enableFlashing);
    thePreferences.end();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Starting...");
  
  pinMode(PIN_ANTENNA, OUTPUT);
  pinMode(PIN_LED, OUTPUT);

  // hack for this on esp32 qt py?
  // E (14621) rmt: rmt_new_tx_channel(269): not able to power down in light sleep
  digitalWrite(PIN_ANTENNA, 0);
  //digitalWrite(PIN_LED, 0);

  readPreferences();

  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);

  Serial.println("Connecting to Wi-Fi...");

  // Connect to WiFi using // https://github.com/tzapu/WiFiManager 
  // If no wifi, start up an SSID called "WWVB" so
  // the user can configure wifi using their phone.
  wifiManager.setAPCallback(accesspointCallback);
  wifiManager.autoConnect(theHostname.c_str());

  Serial.println("Syncing NTP time...");

  // Connect to network time server
  // By default, it will resync every hour
  sntp_set_time_sync_notification_cb(time_sync_notification_cb);
  configTime(0, 0, theNtpServer.c_str());
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    flashFailure();
    delay(3000);
    forceReboot();
  }
  Serial.println("Got the time from NTP");
  flashSuccess();

  // Now set the timezone.
  // We broadcast in UTC, but we need the timezone for the is_dst bit
  updateTimezone();

  // Start the 60khz carrier signal using 8-bit (0-255) resolution
  int myFrequency = getFrequencyForService(theService);

  ledcAttach(PIN_ANTENNA, myFrequency, 8);
  
  createUi();

  char timeStringBuff[100]; // Buffer to hold the formatted time string
  strftime(timeStringBuff, sizeof(timeStringBuff), "%A, %B %d %Y %H:%M:%S", &timeinfo);
  appendToLogFormat("Started up at %s", timeStringBuff);

  // green means go
  flashSuccess();
}

void attachPwmPin() {
  int myFrequency = getFrequencyForService(theService);
  appendToLogFormat("using frequency: %d", myFrequency);
  ledcAttach(PIN_ANTENNA, myFrequency, 8);
}

void detachPwmPin() {
    appendToLog("detaching pwm pin");
    digitalWrite(PIN_ANTENNA, 0);
    ledcDetach(PIN_ANTENNA);
}

void updateTimezone() { 
    setenv("TZ",theTimezone.c_str(),1);
    tzset();
}

void createUi() { 
    ControlColor myControlColor = ControlColor::Dark;
    uint16_t mySettingsTab = ESPUI.addControl(ControlType::Tab, "Settings", "Settings");
    uint16_t myLogTab = ESPUI.addControl(ControlType::Tab, "Log", "Log");

    // host name
    ESPUI.addControl(ControlType::Text, "Hostname:", theHostname.c_str(), myControlColor, mySettingsTab, &setHostname);

    // ntp server
    ESPUI.addControl(ControlType::Text, "NTP Server:", theNtpServer.c_str(), myControlColor, mySettingsTab, &setNtpServer);

    // time zone string
    ESPUI.addControl(ControlType::Text, "Time Zone String:", theTimezone.c_str(), myControlColor, mySettingsTab, &setTimezoneFromSelect);

    const char* myServiceString = getStringForService(theService);

    // time service
    uint16_t myServiceSelect
        = ESPUI.addControl(ControlType::Select, "Time Service:", myServiceString, myControlColor, mySettingsTab, &setServiceFromSelect);
    ESPUI.addControl(ControlType::Option, "WWVB (USA)", "WWVB", ControlColor::None, myServiceSelect);
    ESPUI.addControl(ControlType::Option, "DCF77 (EU)", "DCF77", ControlColor::None, myServiceSelect);
    ESPUI.addControl(ControlType::Option, "JJY40 (ASIA)", "JJY40", ControlColor::None, myServiceSelect);
    ESPUI.addControl(ControlType::Option, "JJY60 (ASIA)", "JJY60", ControlColor::None, myServiceSelect);
    ESPUI.addControl(ControlType::Option, "MSF (UK)", "MSF", ControlColor::None, myServiceSelect);
    ESPUI.addControl(ControlType::Option, "Legacy Mode (OG Code)", "LEGACY", ControlColor::None, myServiceSelect);

    // enable flashing
    ESPUI.addControl(ControlType::Switcher, "Enable Flashing", enableFlashing ? "1" : "0", myControlColor, mySettingsTab, &setEnableFlashing);
    
    // write preferences to nvs
    ESPUI.addControl(ControlType::Button, "Persist Settings", "Save", ControlColor::Wetasphalt, mySettingsTab, &handlePersistSettings);

    // reboot
    ESPUI.addControl(ControlType::Button, "Reboot Device", "Reboot", ControlColor::Wetasphalt, mySettingsTab, &handleReboot);

    // logs
    liveLogHandle = ESPUI.addControl(ControlType::Label, "Log Output", "", myControlColor, myLogTab);
    liveLogicBitsHandle = ESPUI.addControl(ControlType::Label, "Current Logic Bits", "", myControlColor, myLogTab);

    ESPUI.beginLITTLEFS("Radio Controlled Watch Tower Administration");
}

int theLogCounter = 0;
void appendToLog(const String& message) {
    Serial.println(message);
    logBuffer += message + "\n";
    // Optionally, limit the size of logBuffer to prevent memory issues
    if (logBuffer.length() > 512) { // Example limit
        logBuffer = logBuffer.substring(logBuffer.indexOf('\n') + 1);
    }

    if (true || ++theLogCounter % 10 == 0) {
        theLogCounter = 0;
        ESPUI.updateText(liveLogHandle, logBuffer); // Assuming "logLabel" is the ID of your label
    }
}

void appendToLogFormat(const char * format, ...) {
    static char buffer[256]; // A static buffer to store the formatted string
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args); // Use vsnprintf for safety
    va_end(args);
    
    appendToLog(buffer);
}

void updateCurrentLogicBitsLabel() { 
    // convert logicBits to string
    // set string
    String myString = "";
    for (int i = 63; i >= 0; --i) {
        if ((logicBits >> i) & 1) {
            myString += '1';
        } else {
            myString += '0';
        }
    }
    ESPUI.updateText(liveLogicBitsHandle, myString);
}

void setHostname(Control* sender, int type) {
    appendToLogFormat("Updating hostname based on value: %s", sender->value);
    theHostname = sender->value.c_str();
    appendToLogFormat("Updated hostname to: %s", theHostname);
}

void setNtpServer(Control* sender, int type) {
    appendToLogFormat("Updating ntp server based on value: %s", sender->value);
    theNtpServer = sender->value.c_str();
    appendToLogFormat("Updated ntp server to: %s", theNtpServer);
}

void setEnableFlashing(Control* sender, int value) {
    switch (value)
    {
    case S_ACTIVE:
        appendToLog("Enable flashing");
        enableFlashing = true;
        break;

    case S_INACTIVE:
        appendToLog("Disable flashing");
        enableFlashing = false;
        break;
    }
}

void setServiceFromSelect(Control* sender, int value)
{
    appendToLogFormat("Updating service based on value: %s", sender->value);
    theService = getServiceForString(sender->value.c_str());
    detachPwmPin();
    attachPwmPin();
    appendToLogFormat("Updated time service to: %d", theService);
}

void setTimezoneFromSelect(Control* sender, int value)
{
    appendToLogFormat("Updating timezone based on value: %s", sender->value);
    theTimezone = sender->value;
    updateTimezone();
    appendToLogFormat("Updated timezone to: %s", theTimezone);
}

void handlePersistSettings(Control* sender, int type) {
    if (type == B_UP) {
        appendToLog("Persisting settings to NVS");
        savePreferences();
    }
}

void handleReboot(Control* sender, int type) {
    if (type == B_UP) {
        appendToLog("Forced reboot NOW");
        forceReboot();
    }
}

void loop() {
  struct timeval now; // current time in seconds / millis
  time_t nowRounded; // current time in hour, minute (no seconds)
  struct tm buf_now_utc; // current time in UTC
  struct tm buf_now_local; // current time in localtime
  struct tm buf_today_start, buf_tomorrow_start; // start of today and tomrrow in localtime

  gettimeofday(&now,NULL);
  nowRounded = now.tv_sec - now.tv_sec % 60;
  localtime_r(&now.tv_sec, &buf_now_local);

  // DEBUGGING Optionally muck with buf_now_local
  // to make it easier to see if your watch has been set
  if (false) {
    // I like to adjust the time to something I can tell was 
    // set by the Watch Tower and not by Fort Collins.

    // If you set the time/date ahead, be aware that the
    // code will reboot if you set the time more than 4 hours
    // ahead of lastSync.

    // If you hardcode to a fixed date, be aware that some watches
    // may not sync the date every night (presumably to save battery
    // or speed up the sync process), so the date may not always be
    // what you expect even though the watch says it sync'd.

    // Subtract two weeks from today's date. mktime will do the right thing.
    buf_now_local.tm_mday -= 14;
    
    // write your changes back to now and buf_now_local
    now.tv_sec = mktime(&buf_now_local);
    localtime_r(&now.tv_sec, &buf_now_local);
  }

  gmtime_r(&now.tv_sec, &buf_now_utc); 

  // compute start of today for dst
  struct timeval today_start = now;
  today_start.tv_usec = 0;
  today_start.tv_sec = (today_start.tv_sec / 86400) * 86400; // This is not exact but close enough
  localtime_r(&today_start.tv_sec, &buf_today_start);

  // compute start of tomorrow for dst
  struct timeval tomorrow_start = now;
  tomorrow_start.tv_usec = 0;
  tomorrow_start.tv_sec = ((tomorrow_start.tv_sec / 86400) + 1) * 86400; // again, close enough
  localtime_r(&tomorrow_start.tv_sec, &buf_tomorrow_start);

  const bool prevLogicValue = logicValue;
  const uint64_t prevLogicBits = logicBits;

  if (theService == LEGACY) {
    logicValue = wwvbLogicSignal(
        buf_now_utc.tm_hour,
        buf_now_utc.tm_min,
        buf_now_utc.tm_sec, 
        now.tv_usec/1000,
        buf_now_utc.tm_yday+1,
        buf_now_utc.tm_year+1900,
        buf_today_start.tm_isdst,
        buf_tomorrow_start.tm_isdst
        );
  } else {
    logicBits = prepareMinute(theService, nowRounded);
    int myModulation = getModulationForSecond(theService, logicBits, buf_now_utc.tm_sec);
    logicValue =  getLogicForMillisecond(theService, myModulation, now.tv_usec/1000);
  }

  if( logicValue != prevLogicValue ) {
    ledcWrite(PIN_ANTENNA, dutyCycle(logicValue));  // Update the duty cycle of the PWM

    // light up the pixel if desired
    if( logicValue == 1 && enableFlashing) {
      digitalWrite(PIN_LED, HIGH);
    } else {
      digitalWrite(PIN_LED, LOW);
    }

    // do any logging after we set the bit to not slow anything down,
    // serial port I/O is slow!
    char timeStringBuff[100]; // Buffer to hold the formatted time string
    strftime(timeStringBuff, sizeof(timeStringBuff), "%A, %B %d %Y %H:%M:%S", &buf_now_local);

    char lastSyncStringBuff[100]; // Buffer to hold the formatted time string
    struct tm buf_lastSync;
    localtime_r(&lastSync.tv_sec, &buf_lastSync);
    strftime(lastSyncStringBuff, sizeof(lastSyncStringBuff), "%b %d %Y %H:%M", &buf_lastSync);
    Serial.printf("%s.%03d (%s) [last sync %s]: %s\n",timeStringBuff, now.tv_usec/1000, buf_now_local.tm_isdst ? "DST" : "STD", lastSyncStringBuff, logicValue ? "1" : "0");

    long uptime = millis()/1000;

    // Reboot once a day at noon to address any wifi hiccoughs.
    // (specifically, reboot any time after 12pm as long as it's been at least 20 hours since the last reboot)
    if( uptime > 20*60*60  && buf_now_local.tm_hour >= 12) {
      appendToLog("Initiating daily reboot");
      flashSuccess();
      delay(1000);
      forceReboot();
    }

    // If no sync in last 4h, set the pixel to red and reboot
    if(uptime > 4*60*60 && now.tv_sec - lastSync.tv_sec > 60 * 60 * 4 ) {
      appendToLog("Last sync more than four hours ago, rebooting.");
      flashFailure();
      delay(3000);
      forceReboot();
    }
  }

  if (logicBits != prevLogicBits) {
    updateCurrentLogicBitsLabel();
  }
}

// Convert a logical bit into a PWM pulse width.
// Returns 50% duty cycle (128) for high, 0% for low
static inline short dutyCycle(bool logicValue) {
  return logicValue ? (256*0.5) : 0; // 128 == 50% duty cycle
}


static inline void flashSuccess() {
    for (int i = 0; i < 3; i++) {
        digitalWrite(PIN_LED, HIGH);
        delay(100);
        digitalWrite(PIN_LED, LOW);
        delay(100);
    }
}

static inline void flashFailure() {
    for (int i = 0; i < 7; i++) {
        digitalWrite(PIN_LED, HIGH);
        delay(500);
        digitalWrite(PIN_LED, LOW);
        delay(500);
    }
    digitalWrite(PIN_LED, HIGH);
}


// Returns a logical high or low to indicate whether the
// PWM signal should be high or low based on the current time
// https://www.nist.gov/pml/time-and-frequency-division/time-distribution/radio-station-wwvb/wwvb-time-code-format
bool wwvbLogicSignal(
    int hour,                // 0 - 23
    int minute,              // 0 - 59
    int second,              // 0 - 59 (leap 60)
    int millis,
    int yday,                // days since January 1 eg. Jan 1 is 0
    int year,                // year since 0, eg. 2025
    int today_start_isdst,   // was this morning DST?
    int tomorrow_start_isdst // is tomorrow morning DST?
) {
    int leap = is_leap_year(year);
    
    WWVB_T bit;
    switch (second) {
        case 0: // mark
            bit = WWVB_T::MARK;
            break;
        case 1: // minute 40
            bit = (WWVB_T)(((minute / 10) >> 2) & 1);
            break;
        case 2: // minute 20
            bit = (WWVB_T)(((minute / 10) >> 1) & 1);
            break;
        case 3: // minute 10
            bit = (WWVB_T)(((minute / 10) >> 0) & 1);
            break;
        case 4: // blank
            bit = WWVB_T::ZERO;
            break;
        case 5: // minute 8
            bit = (WWVB_T)(((minute % 10) >> 3) & 1);
            break;
        case 6: // minute 4
            bit = (WWVB_T)(((minute % 10) >> 2) & 1);
            break;
        case 7: // minute 2
            bit = (WWVB_T)(((minute % 10) >> 1) & 1);
            break;
        case 8: // minute 1
            bit = (WWVB_T)(((minute % 10) >> 0) & 1);
            break;
        case 9: // mark
            bit = WWVB_T::MARK;
            break;
        case 10: // blank
            bit = WWVB_T::ZERO;
            break;
        case 11: // blank
            bit = WWVB_T::ZERO;
            break;
        case 12: // hour 20
            bit = (WWVB_T)(((hour / 10) >> 1) & 1);
            break;
        case 13: // hour 10
            bit = (WWVB_T)(((hour / 10) >> 0) & 1);
            break;
        case 14: // blank
            bit = WWVB_T::ZERO;
            break;
        case 15: // hour 8
            bit = (WWVB_T)(((hour % 10) >> 3) & 1);
            break;
        case 16: // hour 4
            bit = (WWVB_T)(((hour % 10) >> 2) & 1);
            break;
        case 17: // hour 2
            bit = (WWVB_T)(((hour % 10) >> 1) & 1);
            break;
        case 18: // hour 1
            bit = (WWVB_T)(((hour % 10) >> 0) & 1);
            break;
        case 19: // mark
            bit = WWVB_T::MARK;
            break;
        case 20: // blank
            bit = WWVB_T::ZERO;
            break;
        case 21: // blank
            bit = WWVB_T::ZERO;
            break;
        case 22: // yday of year 200
            bit = (WWVB_T)(((yday / 100) >> 1) & 1);
            break;
        case 23: // yday of year 100
            bit = (WWVB_T)(((yday / 100) >> 0) & 1);
            break;
        case 24: // blank
            bit = WWVB_T::ZERO;
            break;
        case 25: // yday of year 80
            bit = (WWVB_T)((((yday / 10) % 10) >> 3) & 1);
            break;
        case 26: // yday of year 40
            bit = (WWVB_T)((((yday / 10) % 10) >> 2) & 1);
            break;
        case 27: // yday of year 20
            bit = (WWVB_T)((((yday / 10) % 10) >> 1) & 1);
            break;
        case 28: // yday of year 10
            bit = (WWVB_T)((((yday / 10) % 10) >> 0) & 1);
            break;
        case 29: // mark
            bit = WWVB_T::MARK;
            break;
        case 30: // yday of year 8
            bit = (WWVB_T)(((yday % 10) >> 3) & 1);
            break;
        case 31: // yday of year 4
            bit = (WWVB_T)(((yday % 10) >> 2) & 1);
            break;
        case 32: // yday of year 2
            bit = (WWVB_T)(((yday % 10) >> 1) & 1);
            break;
        case 33: // yday of year 1
            bit = (WWVB_T)(((yday % 10) >> 0) & 1);
            break;
        case 34: // blank
            bit = WWVB_T::ZERO;
            break;
        case 35: // blank
            bit = WWVB_T::ZERO;
            break;
        case 36: // UTI sign +
            bit = WWVB_T::ONE;
            break;
        case 37: // UTI sign -
            bit = WWVB_T::ZERO;
            break;
        case 38: // UTI sign +
            bit = WWVB_T::ONE;
            break;
        case 39: // mark
            bit = WWVB_T::MARK;
            break;
        case 40: // UTI correction 0.8
            bit = WWVB_T::ZERO;
            break;
        case 41: // UTI correction 0.4
            bit = WWVB_T::ZERO;
            break;
        case 42: // UTI correction 0.2
            bit = WWVB_T::ZERO;
            break;
        case 43: // UTI correction 0.1
            bit = WWVB_T::ZERO;
            break;
        case 44: // blank
            bit = WWVB_T::ZERO;
            break;
        case 45: // year 80
            bit = (WWVB_T)((((year / 10) % 10) >> 3) & 1);
            break;
        case 46: // year 40
            bit = (WWVB_T)((((year / 10) % 10) >> 2) & 1);
            break;
        case 47: // year 20
            bit = (WWVB_T)((((year / 10) % 10) >> 1) & 1);
            break;
        case 48: // year 10
            bit = (WWVB_T)((((year / 10) % 10) >> 0) & 1);
            break;
        case 49: // mark
            bit = WWVB_T::MARK;
            break;
        case 50: // year 8
            bit = (WWVB_T)(((year % 10) >> 3) & 1);
            break;
        case 51: // year 4
            bit = (WWVB_T)(((year % 10) >> 2) & 1);
            break;
        case 52: // year 2
            bit = (WWVB_T)(((year % 10) >> 1) & 1);
            break;
        case 53: // year 1
            bit = (WWVB_T)(((year % 10) >> 0) & 1);
            break;
        case 54: // blank
            bit = WWVB_T::ZERO;
            break;
        case 55: // leap year
            bit = leap ? WWVB_T::ONE : WWVB_T::ZERO;
            break;
        case 56: // leap second
            bit = WWVB_T::ZERO;
            break;
        case 57: // dst bit 1
            bit = today_start_isdst ? WWVB_T::ONE : WWVB_T::ZERO;
            break;
        case 58: // dst bit 2
            bit = tomorrow_start_isdst ? WWVB_T::ONE : WWVB_T::ZERO;
            break;
        case 59: // mark
            bit = WWVB_T::MARK;
            break;
    }

    // Convert a wwvb zero, one, or mark to the appropriate pulse width
    // zero: low 200ms, high 800ms
    // one: low 500ms, high 500ms
    // mark low 800ms, high 200ms
    if (bit == WWVB_T::ZERO) {
      return millis >= 200;
    } else if (bit == WWVB_T::ONE) {
      return millis >= 500;
    } else {
      return millis >= 800;
    }
}

static inline int is_leap_year(int year) {
    return (year % 4 == 0) && (year % 100 != 0 || year % 400 == 0);
}
