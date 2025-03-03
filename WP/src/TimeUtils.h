#ifndef TIMEUTILS_H
#define TIMEUTILS_H

#include "stdint.h"

#include "Clock.h"

// This might be better served as a variable so it could be changed on the fly like if there
// were a switch to disable DST or something
#define DST_START_MONTH   3
#define DST_END_MONTH     11

class TimeUtils
{
  public:
    static bool isLeapYear(uint32_t year);

    static bool GT(const cTime_t& a, const cTime_t& b);
    static bool GTEQ(const cTime_t& a, const cTime_t& b);

    // Convert a date of the form Y/M/D into a Rata Die date number
    static uint32_t toRataDie(uint32_t Y, uint32_t M, uint32_t D);

    // Convert the date inside a cTime_t struct to its ratadie
    static uint32_t toRataDie(const cTime_t& theDate);

    // Convert a Rata Die date number to Y/M/D
    static void fromRataDie(const uint32_t rataDie, uint32_t &Y, uint32_t &M, uint32_t &D);

    // Convert a Rata Die date number to a cTime_t
    // Only the year/month/dates are touched in the cTime_t param
    static void fromRataDie(const uint32_t rataDie, cTime_t& cTime);

    // Return the number of whole days based on how much datespec time has
    // passed between prev and now.
    //static uint32_t fullDaysBetweenDates(dateSpec_t &now, dateSpec_t prev)

    // Convert a yyyy/mm/dd to a numeric day of the week [0..6] where 0==Sun, 1=Mon, ...
    static uint32_t dayOfWeek(uint32_t y, uint32_t m, uint32_t d);
    // Convert a cTime_t object's yyyy/mm/dd fields to a numeric day of the week [0..6] where 0==Sun, 1=Mon, ...
    static uint32_t dayOfWeek(const cTime_t& t);

    // Convert a day of the week [0==Sun, 1==Mon, ...]  to a 3-character string
    static const char* dayOfWeekToString(uint32_t dayOfWeek);

    // Convert a month number [1==Jan, 2==Feb, ...] to a 3-character string
    static const char* monthToString(uint32_t month);

    // An 'ordinal' date starts at 1 on 1-Jan and counts up to 365 on Dec 31; 366 if it was a leap year.
    static uint32_t date2ordinalDate(int16_t year, int16_t month, int16_t date);
    static bool ordinalDate2date(uint32_t ordinalDate, int16_t year, int16_t* month, int16_t* date);

    // These routines take care of converting between UTC and Local times
    static bool timezoneValid(tzOffset_t offset);
    //static bool local2utc(const cTime_t& localTime, cTime_t* utcTime);

    // The param 'permitDstChanges' will account for local time changes due to changing to and from DST
    // Localities that do not change times should leave this false, and set the localTimeZone to
    // whatever constant UTC timezone offset they have decided to stick with.
    static void utc2local(const cTime_t& utcTime, tzOffset_t localTimezone, bool permitDstChanges, cTime_t& localTime);

    // The localTime will be converted back to UTC.
    static bool local2utc(const cTime_t& localTime, cTime_t& utcTime);

    // Calculate the length of the current day in seconds, where 0 seconds is 12:00:00AM, 1==12:00:01AM, etc.up to 86399 seconds
    // at 11:59:59PM.  The potential effects of leapseconds are ignored.
    static int32_t lengthOfDayInSeconds(const cTime_t& t);

  private:
    static const char* dayDecoder[];
    static const char* monthDecoder[];

    static bool adjust(cTime_t& t, tzOffset_t tzOffset);
};

#endif
