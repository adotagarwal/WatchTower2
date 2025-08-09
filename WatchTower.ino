// TODO move to readme
// Online simulator: https://wokwi.com/projects/431240334467357697
//      (make sure to comment out the wifi bits)
//      (may also need to change the antenna pin)
//      (run the simulator and stop it, and download the logic analyser output and view in something like PulseView)

//
// INSTRUCTIONS
// - set the PIN_ANTENNA to desired output pin
// - set the timezone as desired
// - download and run the code on your device
// - connect your phone to "WWVB" to set the wifi config for the device

// TODO reboot daily in case of wifi issues

#include <WiFiManager.h>
#include <Adafruit_NeoPixel.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "time.h"
#include "esp_sntp.h"

enum WWVB_T {
  ZERO = 0,
  ONE = 1,
  MARK = 2,
};

const int PIN_ANTENNA = 13;
const int KHZ_60 = 60000;

// Set to your timezone.
// This is needed for computing DST if applicable
// https://gist.github.com/alwynallan/24d96091655391107939
const char *timezone = "PST8PDT,M3.2.0,M11.1.0"; // America/Los_Angeles
const char* ntpServer = "pool.ntp.org";

WiFiManager wifiManager;
Adafruit_NeoPixel pixels(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
bool logicValue = 0; // TODO rename
struct timeval lastSync;

const uint32_t COLOR_READY = pixels.Color(0, 60, 0);
const uint32_t COLOR_LOADING = pixels.Color(32, 20, 0);
const uint32_t COLOR_ERROR = pixels.Color(150, 0, 0);
const uint32_t COLOR_TRANSMIT = pixels.Color(32, 20, 0);


// Optional I2c display 
// https://www.adafruit.com/product/326
// Does nothing if no display present.
// Can be removed if not using.
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3D /// See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);


// A tricky way to force arduino to reboot
// by accessing a protected memory address
void(* forceReboot) (void) = 0;

// A callback that tracks when we last sync'ed the
// time with the ntp server
void time_sync_notification_cb(struct timeval *tv) {
  lastSync = *tv;
}

void accesspointCallback(WiFiManager*) {
  Serial.println("Connect to WWVB with another device to set wifi configuration.");
  display.clearDisplay();
  display.setCursor(0, 0);     // Start at top-left corner
  display.println("Connect to WWVB");
  display.println("with your phone");
  display.println("to configure WiFi.");
  display.display();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  pixels.begin();

  pinMode(PIN_ANTENNA, OUTPUT);
  pixels.setPixelColor(0, COLOR_LOADING );
  pixels.show();

  // Initialize optional I2c display
  display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);
  display.setTextSize(1);      // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE); // Draw white text
  display.cp437(true);         // Use full 256 char 'Code Page 437' font

  // hack for this on esp32 qt py?
  // E (14621) rmt: rmt_new_tx_channel(269): not able to power down in light sleep
  digitalWrite(PIN_ANTENNA, 0);

  display.clearDisplay();
  display.setCursor(0, 0);     // Start at top-left corner
  display.println("Connecting...");
  display.display();

  // https://github.com/tzapu/WiFiManager/issues/1426
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);

  // Connect to WiFi using // https://github.com/tzapu/WiFiManager 
  // If no wifi, start up an SSID called "WWVB" so
  // the user can configure wifi using their phone.
  wifiManager.setAPCallback(accesspointCallback);
  wifiManager.autoConnect("WWVB");

  display.clearDisplay();
  display.setCursor(0, 0);     // Start at top-left corner
  display.println("Syncing time...");
  display.display();

  // Connect to network time server
  // By default, it will resync every hour
  // Register a callback to be informed when we sync
  sntp_set_time_sync_notification_cb(time_sync_notification_cb);
  configTime(0, 0, ntpServer);
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    pixels.setPixelColor(0, COLOR_ERROR );
    pixels.show();
    delay(3000);
    forceReboot();
  }
  Serial.println("Got the time from NTP");

  // Now we can set the real timezone.
  // We broadcast in UTC, but we need the timezone for the is_dst bit
  setenv("TZ",timezone,1);  //  Now adjust the TZ.  Clock settings are adjusted to show the new local time
  tzset();

  // Start the 60khz carrier signal using 8-bit (0-255) resolution
  ledcAttach(PIN_ANTENNA, KHZ_60, 8);

  // green means go
  pixels.setPixelColor(0, COLOR_READY );
  pixels.show();
  delay(3000);

  pixels.clear();  
  pixels.show();
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
  // or set the date to dec 31
  // buf_now_utc.tm_yday = buf_now_local.tm_yday = 365;
  // or set the time randomly every 10 minutes
  // int mod_min = buf_now_utc.tm_min % 10;
  // randomSeed(buf_now_utc.tm_min / 10); // pick a new random time every 10m
  // buf_now_utc.tm_hour = buf_now_local.tm_hour = random(0,24);
  // buf_now_utc.tm_min = buf_now_local.tm_min = (random(0,60) + mod_min) % 60;


  const bool prevLogicValue = logicValue;

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

  if( logicValue != prevLogicValue ) {
    ledcWrite(PIN_ANTENNA, dutyCycle(logicValue));  // Update the duty cycle of the PWM

    // light up the pixel if desired
    // if( logicValue == 1 ) {
    //   pixels.setPixelColor(0, COLOR_TRANSMIT ); // don't call show yet, the color may change
    // } else {
    //   pixels.clear();
    // }

    // do any logging after we set the bit to not slow anything down,
    // serial port I/O is slow!
    char timeStringBuff[100]; // Buffer to hold the formatted time string
    strftime(timeStringBuff, sizeof(timeStringBuff), "%A, %B %d %Y %H:%M:%S", &buf_now_local);

    char lastSyncStringBuff[100]; // Buffer to hold the formatted time string
    struct tm buf_lastSync;
    localtime_r(&lastSync.tv_sec, &buf_lastSync);
    strftime(lastSyncStringBuff, sizeof(lastSyncStringBuff), "%b %d %Y %H:%M", &buf_lastSync);
    Serial.printf("%s.%03d (%s) [last sync %s]: %s\n",timeStringBuff, now.tv_usec/1000, buf_now_local.tm_isdst ? "DST" : "STD", lastSyncStringBuff, logicValue ? "1" : "0");

    // update the optional I2c display for debugging
    // Does nothing if no display connected
    char line1Buf[100];
    strftime(line1Buf, sizeof(line1Buf), "%b %d %Y %H:%M", &buf_now_local);
    display.clearDisplay();
    display.setTextSize(1);      // Normal 1:1 pixel scale
    display.setTextColor(SSD1306_WHITE); // Draw white text
    display.setCursor(0, 0);     // Start at top-left corner
    display.cp437(true);         // Use full 256 char 'Code Page 437' font
    display.println(line1Buf);
    display.display();    

    // Reboot once a day at noon to address any wifi hiccoughs.
    // (specifically, reboot any time after 12pm as long as it's been at least 20 hours since the last reboot)
    long uptime = millis()/1000;
    if( uptime > 20*60*60  && buf_now_local.tm_hour >= 12) {
      Serial.println("Initiating daily reboot");
      delay(1000);
      forceReboot();
    }

    // If no sync in last 4h, set the pixel to red and reboot
    if( now.tv_sec - lastSync.tv_sec > 60 * 60 * 4 ) {
      pixels.setPixelColor(0, COLOR_ERROR );
      delay(3000);
      forceReboot();
    }

    pixels.show();  
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

