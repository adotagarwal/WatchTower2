#include <wwvb-impl.h>

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
    int leap = (year % 4 == 0) && (year % 100 != 0 || year % 400 == 0);
    
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