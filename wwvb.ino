// Derived from https://github.com/anishathalye/micro-wwvb/blob/master/src/microwwvb.c
// https://www.instructables.com/WWVB-radio-time-signal-generator-for-ATTINY45-or-A/
// https://en.wikipedia.org/wiki/WWVB
// https://www.nist.gov/pml/time-and-frequency-division/time-distribution/radio-station-wwvb/wwvb-time-code-format
//
// Simulator: https://wokwi.com/projects/431240334467357697
//      (make sure to comment out the wifi bits)
//      (may also need to change the antenna pin)
//

#include <WiFiManager.h>
#include "time.h"

enum WWVB_T {
  ZERO = 0,
  ONE = 1,
  MARK = 2,
};

const int PIN_ANTENNA = 13;
const int KHZ_60 = 60000;
const int PIN_LED = LED_BUILTIN; // for visual confirmation

// Set to your timezone.
// This is needed for computing DST if applicable
const char *timezone = "PST8PDT,M3.2.0,M11.1.0"; // America/Los_Angeles

WiFiManager wifiManager;
const char* ntpServer = "pool.ntp.org";
bool logicValue = 0; // TODO rename


void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(PIN_ANTENNA, OUTPUT);
  pinMode(PIN_LED, OUTPUT);

  // Connect to WiFi using // https://github.com/tzapu/WiFiManager 
  // If no wifi, start up an SSID called "WWVB" so
  // the user can configure wifi using their phone.
  wifiManager.autoConnect("WWVB");

  // Connect to network time server
  configTime(0, 0, ntpServer);
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return; // TODO abort
  }
  Serial.println("Got the time from NTP");

  // Now we can set the real timezone.
  // We broadcast in UTC, but we need the timezone for the is_dst bit
  setenv("TZ",timezone,1);  //  Now adjust the TZ.  Clock settings are adjusted to show the new local time
  tzset();

  // Start the 60khz carrier signal using 8-bit (0-255) resolution
  ledcAttach(PIN_ANTENNA, KHZ_60, 8);
}

void loop() {
  // TODO add sleep for planet earth

  // now and buf_now are used for the current time.
  struct timeval now, today_start, tomorrow_start;
  struct tm buf_now_utc, buf_now_local, buf_today_start, buf_tomorrow_start;
  gettimeofday(&now,NULL);
  gmtime_r(&now.tv_sec, &buf_now_utc); 
  localtime_r(&now.tv_sec, &buf_now_local);

  today_start = now;
  today_start.tv_usec = 0;
  today_start.tv_sec = (today_start.tv_sec / 86400) * 86400; // This is not exact but close enough
  localtime_r(&today_start.tv_sec, &buf_today_start);

  tomorrow_start = now;
  tomorrow_start.tv_usec = 0;
  tomorrow_start.tv_sec = ((tomorrow_start.tv_sec / 86400) + 1) * 86400; // again, close enough
  localtime_r(&tomorrow_start.tv_sec, &buf_tomorrow_start);


  // DEBUGGING
  // If you're not sure if your clock is being set,
  // you can uncomment these lines to offset the
  // time by 30 minutes
  // buf_now_utc.tm_min = buf_now_local.tm_min = (buf_now_utc.tm_min + 30) % 60;

  const bool prevLogicValue = logicValue;

  logicValue = wwvbPinState(
    buf_now_utc.tm_hour,
    buf_now_utc.tm_min,
    buf_now_utc.tm_sec,
    now.tv_usec/1000,
    buf_now_utc.tm_yday+1,
    buf_now_utc.tm_year+1900,
    buf_today_start.tm_isdst,
    buf_tomorrow_start.tm_isdst
    );

  if( logicValue != prevLogicValue ) {
    ledcWrite(PIN_ANTENNA, dutyCycle(logicValue));  // Update the duty cycle of the PWM
    digitalWrite(PIN_LED, logicValue);

    // do any logging after we set the bit to not slow anything down,
    // serial port I/O is slow!
    char timeStringBuff[100]; // Buffer to hold the formatted time string
    strftime(timeStringBuff, sizeof(timeStringBuff), "%A, %B %d %Y %H:%M:%S", &buf_now_local);
    Serial.printf("%s.%03d (%s): %s\n",timeStringBuff, now.tv_usec/1000, buf_now_local.tm_isdst ? "DST" : "STD", logicValue ? "1" : "0");
  }
}

// Convert a logical bit into a PWM pulse width.
// Returns 50% duty cycle (128) for high, 0% for low
static inline short dutyCycle(bool logicValue) {
  return logicValue ? (256*0.5) : 0; // 128 == 50% duty cycle
}


// Returns a logical high or low to indicate whether the
// PWM signal should be high or low based on the current time
// https://www.nist.gov/pml/time-and-frequency-division/time-distribution/radio-station-wwvb/wwvb-time-code-format
bool wwvbPinState(
    int hour,
    int minute,
    int second,
    int millis,
    int yday,
    int year,
    int today_start_isdst, // was this morning DST?
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

