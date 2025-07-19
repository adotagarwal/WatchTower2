// Derived from https://github.com/anishathalye/micro-wwvb/blob/master/src/microwwvb.c
// https://www.instructables.com/WWVB-radio-time-signal-generator-for-ATTINY45-or-A/
// https://en.wikipedia.org/wiki/WWVB
// https://www.nist.gov/pml/time-and-frequency-division/time-distribution/radio-station-wwvb/wwvb-time-code-format
//
// Simulator: https://wokwi.com/projects/431240334467357697
//      (make sure to comment out the wifi bits)
//      (may also need to change the antenna pin)
//
// Working
// - verified 2025-09-15 18:42 using oscilloscope with 100%/0% duty cycles and the
//   waves look exactly like the NIST example (except 2025 instead of 2001)
// - the watches sense the carrier wave since they move to "Working" (both for 20" straight and canaduino antennas)
//   but they can't turn that into a time
// - try 80% duty cycle?
// - WOOO! I set the junghans with the straight antenna!! Not the casio though.
//   https://photos.app.goo.gl/R9BfENnQBhxtWPzTA (junghans is -30m which is my debugging value)
//   I think it will work better once i have the amplifier circuit and the canaduino antenna
// 
// Hypotheses
// - maybe 3.3v isn't enough? https://www.instructables.com/WWVB-radio-time-signal-generator-for-ATTINY45-or-A/ uses 5v
//   https://github.com/anishathalye/micro-wwvb doesn't say but it uses mini-usb which is 5v
//   - Principle: A transistor, specifically a BJT or MOSFET, can act as a switch, amplifying the ESP32's 3.3V signal to control the 5V LED circuit.
//   - try using an amplifer circuit
// - maybe the antenna has too much induction? try a 20" flat wire?
// - try a different watch
// - are the watches getting confused by the hardcoded minute?


#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include "time.h"

enum WWVB_T {
  ZERO = 0,
  ONE = 1,
  MARK = 2,
};

const int PIN_ANTENNA = A5;
const int KHZ_60 = 60000;
const int PIN_LED = LED_BUILTIN; // for visual confirmation
const int RESOLUTION = 8; // 8-bit is 0-255
const char *timezone = "PST8PDT,M3.2.0,M11.1.0"; // America/Los_Angeles


WiFiManager wifiManager;
bool logicValue = 0;
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 0;
const int   daylightOffset_sec = 3600;



// void IRAM_ATTR timerHandler0(void)
// {
//   // TODO move to an ISR, but localtime is crashing in ISR
//   // will getLocalTime also crash?
//   struct timeval now;
//   gettimeofday(&now,NULL);
//   struct tm buf;
//   // localtime_r(&now.tv_sec, &buf);
//   gmtime_r(&now.tv_sec, &buf); // TODO debugging
//   const bool prevPinValue = logicValue;
//   logicValue = wwvbPinState(buf.tm_hour,buf.tm_min,buf.tm_sec,now.tv_usec/1000,buf.tm_yday,buf.tm_mday,buf.tm_mon,buf.tm_year,buf.tm_isdst);
//   if( logicValue != prevPinValue ) {
//     // digitalWrite(PIN_ANTENNA, pinValue);
//     if(PIN_LED!=NULL) {
//       // digitalWrite(PIN_LED, pinValue);
//     }
//   }
// }

void setup() {
  Serial.begin(115200);
  pinMode(PIN_ANTENNA, OUTPUT);
  if( PIN_LED!=NULL ) {
    pinMode(PIN_LED, OUTPUT);
  }
  wifiManager.autoConnect("WWVB");

  configTime(0, 0, ntpServer);
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println("Got the time from NTP");
  // Now we can set the real timezone
  setenv("TZ",timezone,1);  //  Now adjust the TZ.  Clock settings are adjusted to show the new local time
  tzset();

  // auto timer = timerBegin(1000000); // 1Mhz
  // timerAttachInterrupt(timer, &timerHandler0);
  // timerAlarm(timer, 100000, true, 0); // every 100ms

  // Start the 60khz carrier signal
  ledcAttach(PIN_ANTENNA, KHZ_60, RESOLUTION);

}

void loop() {
  // TODO move to an ISR, but localtime is crashing in ISR
  // will getLocalTime also crash?
  struct timeval now;
  gettimeofday(&now,NULL);
  struct tm buf;
  // localtime_r(&now.tv_sec, &buf);
  gmtime_r(&now.tv_sec, &buf); // TODO debugging
  buf.tm_min = (buf.tm_min + 30 ) % 60; // TODO debugging
  // TODO debugging https://www.nist.gov/pml/time-and-frequency-division/time-distribution/radio-station-wwvb/wwvb-time-code-format
  // buf.tm_year = 101; // 2001
  // buf.tm_year = 125; // 2025
  // buf.tm_mon = 8; // sep
  // buf.tm_mday = 15; 
  // buf.tm_yday = 258;
  // buf.tm_hour = 18;
  // buf.tm_min = 42;

  const bool prevLogicValue = logicValue;
  logicValue = wwvbPinState(buf.tm_hour,buf.tm_min,buf.tm_sec,now.tv_usec/1000,buf.tm_yday,buf.tm_year+1900,buf.tm_isdst);
  if( logicValue != prevLogicValue ) {
    ledcWrite(PIN_ANTENNA, dutyCycle(logicValue));  // Update the duty cycle of the PWM
    if(PIN_LED!=NULL) {
      digitalWrite(PIN_LED, logicValue);
    }

    // do any logging after we set the bit to not slow anything down
    char timeStringBuff[64]; // Buffer to hold the formatted time string
    strftime(timeStringBuff, sizeof(timeStringBuff), "%A, %B %d %Y %H:%M:%S", &buf);
    Serial.printf("%s %03d: %s\n",timeStringBuff, now.tv_usec/1000, logicValue ? "1" : "0");
  }
}

// Returns 50% duty cycle (128) for high, 0% for low
static inline short dutyCycle(bool logicValue) {
  return logicValue ? (256*0.5) : 0; // 128 == 50% duty cycle
}


// Returns high or low to indicate whether the
// wwvb signal should be high or low based on the current time
bool wwvbPinState(
    int hour,
    int minute,
    int second,
    int millis,
    int yday,
    int year,
    int dst
) {
    int leap = is_leap_year(year);
    
    // compute bit
    // https://www.nist.gov/pml/time-and-frequency-division/time-distribution/radio-station-wwvb/wwvb-time-code-format
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
            bit = dst ? WWVB_T::ONE : WWVB_T::ZERO; // XXX this isn't exactly correct
            break;
        case 58: // dst bit 2
            bit = dst ? WWVB_T::ONE : WWVB_T::ZERO;
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

