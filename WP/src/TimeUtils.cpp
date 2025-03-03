#include "TimeUtils.h"

const char* TimeUtils::dayDecoder[] = {
  "Sun",
  "Mon",
  "Tue",
  "Wed",
  "Thu",
  "Fri",
  "Sat"
  };


const char* TimeUtils::monthDecoder[] = {
  "Jan",
  "Feb",
  "Mar",
  "Apr",
  "May",
  "Jun",
  "Jly",
  "Aug",
  "Sep",
  "Oct",
  "Nov",
  "Dec"
};

// Tables used to convert dates to ordinalDates and vice versa.
static const uint32_t normalYear[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
static const uint32_t   leapYear[] = {0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335};

static const uint32_t shiftedMonthDays[] = {0, 306, 337, 0, 31, 61, 92, 122, 153, 184, 214, 245, 275};

// --------------------------------------------------------------------------------
bool TimeUtils::isLeapYear(uint32_t year)
{
  return ((!(year % 4) && (year % 100)) || !(year % 400));
}


// --------------------------------------------------------------------------------
// Another way to do this would be to convert each time to a 64 bit milliseconds value
// and then just compare them.  It might be possible to work in timezones into that
// calculation which would be neat to compare different timezones but still get the right answers.
bool TimeUtils::GT(const cTime_t& a, const cTime_t& b)
{
  if (a.years != b.years) {
    return a.years > b.years;
  }
  if (a.month != b.month) {
    return a.month > b.month;
  }
  if (a.date != b.date) {
    return a.date > b.date;
  }
  if (a.hours != b.hours) {
    return a.hours > b.hours;
  }
  if (a.mins != b.mins) {
    return a.mins > b.mins;
  }
  if (a.secs != b.secs) {
    return a.secs > b.secs;
  }
  return a.millisecs > b.millisecs;
}


// --------------------------------------------------------------------------------
bool TimeUtils::GTEQ(const cTime_t& a, const cTime_t& b)
{
  if (a.years != b.years) {
    return a.years >= b.years;
  }
  if (a.month != b.month) {
    return a.month >= b.month;
  }
  if (a.date != b.date) {
    return a.date >= b.date;
  }
  if (a.hours != b.hours) {
    return a.hours >= b.hours;
  }
  if (a.mins != b.mins) {
    return a.mins >= b.mins;
  }
  if (a.secs != b.secs) {
    return a.secs >= b.secs;
  }
  return a.millisecs >= b.millisecs;
}



// --------------------------------------------------------------------------------
// Convert a date of the form Y/M/D into a Rata Die date number
uint32_t TimeUtils::toRataDie(uint32_t Y, uint32_t M, uint32_t D)
{
  #if 0
    if (M < 3) {
      M += 12;
      Y--;
    }
    double temp = D + ((153 * M - 457)/5) + (365*Y) + (Y/4) - (Y/100) + (Y/400) - 306;
    uint32_t result = temp;
    return result;
  #else
    // Shift the beginning of the year to March 1, so a leap day (if any) becomes the very last day of the year
    uint32_t z = (M < 3) ? Y - 1 : Y;
    uint32_t mdays = shiftedMonthDays[M];
    return D + mdays + (365*z) + (z/4) - (z/100) + (z/400) - 306;
  #endif
}


// --------------------------------------------------------------------------------
// Convert a date of the form Y/M/D into a Rata Die date number
uint32_t TimeUtils::toRataDie(const cTime_t& theDate)
{
  return toRataDie(theDate.years, theDate.month, theDate.date);
}

// Rata Die algorithm by Peter Baum. Scraped from GitHub.
// Don't ask me how it works.
void TimeUtils::fromRataDie(const uint32_t rdn, uint32_t &Y, uint32_t &M, uint32_t &D)
{
  uint32_t Z, H, A, B;
  uint32_t y, m, d;

  Z = rdn + 306;
  H = 100 * Z - 25;
  A = H / 3652425;
  B = A - (A >> 2);
  y = (100 * B + H) / 36525;
  d = B + Z - (1461 * y >> 2);
  m = (535 * d + 48950) >> 14;
  if (m > 12) {
    y++;
    m -= 12;
  }

  Y = y;
  M = m;
  D = d - shiftedMonthDays[m];
}


// --------------------------------------------------------------------------------
void TimeUtils::fromRataDie(const uint32_t rataDie, cTime_t& cTime)
{
  uint32_t y,m,d;

  fromRataDie(rataDie, y, m, d);

  cTime.years = y;
  cTime.month = m;
  cTime.date = d;
}


#if 0
// --------------------------------------------------------------------------------
// Return the number of whole days based on how much datespec time has
// passed between prev and now.
uint32_t TimeUtils::fullDaysBetweenDates(dateSpec_t &now, dateSpec_t prev)
{
  int32_t startDay = toRataDie(prev.year, prev.month, prev.day);
  int32_t endDay   = toRataDie(now.year, now.month, now.day);
  int32_t deltaDays = endDay-startDay;
  if (deltaDays < 0) {
    panic();
  }
  return deltaDays;
}
#endif


//--------------------------------------------------------------------------------
// As per Wikipedia, here is an algorithm to convert a yyyy/mm/dd to the day of the week [0..6] where 0==Sunday, 1=Monday, ...
uint32_t TimeUtils::dayOfWeek(uint32_t y, uint32_t m, uint32_t d)
{
  static int32_t t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  int32_t dayOfWeek;

  y -= (m < 3);
  dayOfWeek = (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;

  return(dayOfWeek);
}

//--------------------------------------------------------------------------------
uint32_t TimeUtils::dayOfWeek(const cTime_t& t)
{
  return dayOfWeek(t.years, t.month, t.date);
}


//--------------------------------------------------------------------------------
// DayOfWeek is zero-based: 0==Sunday, 1==Monday, ...
const char* TimeUtils::dayOfWeekToString(uint32_t dayOfWeek)
{
  if (dayOfWeek >= 7) {
    return nullptr;
  }

  return dayDecoder[dayOfWeek];
}


//--------------------------------------------------------------------------------
// Month is one-based: 1==Jan, 2=Feb, ...
const char* TimeUtils::monthToString(uint32_t month)
{
  const char* name = nullptr;

  if (--month <= 11) {
    name = monthDecoder[month];
  }
  return name;
}

#if 0
//--------------------------------------------------------------------------------
bool TimeUtils::isLeapYear(uint32_t year)
{
  // Take care of the exceptions
  if ((year % 400) == 0) {
    return true;
  }
  else if ((year % 100) == 0) {
    return false;
  }

  // If no exceptions apply, then use the general rule
  return ((year % 4) == 0);
}
#endif

//--------------------------------------------------------------------------------
// Convert a YYYY-MM-DD to the ordinal date.  The ordinal date is the day of the year,
// starting at 1 for Jan 1, and ending at 366 or 365 for 31-Dec, depending on whether the year
// in question is a leap year or not, respectively.
uint32_t TimeUtils::date2ordinalDate(int16_t year, int16_t month, int16_t date)
{
  const uint32_t* p = isLeapYear(year) ? leapYear : normalYear;
  uint32_t ordinalDate = p[month-1] + date;
  return ordinalDate;
}


//--------------------------------------------------------------------------------
bool TimeUtils::ordinalDate2date(uint32_t ordinalDate, int16_t year, int16_t* month, int16_t* date)
{
  uint32_t i = 0;

  const uint32_t* p = isLeapYear(year) ? leapYear : normalYear;

  while (i<11) {
    if (ordinalDate <= p[i+1]) {
      // We found our month
      *month = i+1;
      *date = ordinalDate - p[i];
      return true;
    }
    i++;
  }
  return false;
}

#if 0
//--------------------------------------------------------------------------------
void TimeUtils::utcToLocalHrsMins(float UTC, int32_t &hrs, int32_t &mins)
{
  float localT;
  float minutes, hours;

  localT = UTC + (localTime->getTimeZoneOffset() / 60.0);
  if (localT < 0) localT += 24.0;
  if (localT >= 24.0) localT -= 24.0;

  // MODFF returns the fractional part, sets param 2 to the integral part
  minutes = modff(localT, &hours)*60;

  hrs = hours;
  mins = round(minutes);

  // Because of the round() operation, it is possible that the minutes rounded to 60
  if (mins == 60) {
    mins = 0;
    hrs+=1;
    if (hrs == 24) {
      hrs = 0;
    }
  }
}
#endif

//--------------------------------------------------------------------------------
// Adjust a time to another timezone relative to its own.
// For example, adjusting by -60 minutes converts the time to one timezone west.
// This is simple math that accounts for changing of dates etc, but ignores the effects of
// DST/ST changes!
bool TimeUtils::adjust(cTime_t& t, tzOffset_t tzOffset)
{
  uint32_t years, month, date, ratadie;

  if ((tzOffset < (-12*60)) || (tzOffset > (12*60))) {
    // Timezone is out of range of -12 to +12 hours
    return false;
  }

  // Start by applying the basic tz adjustment
  t.mins  += (tzOffset % 60);
  t.hours += (tzOffset / 60);

  // See if the adjusted minutes have slopped into the hour on either side.
  // If so, get the minutes back into range and adjust the hours accordingly.
  if (t.mins < 0) {
    t.mins += 60;
    t.hours -= 1;
  }
  else if (t.mins >= 60) {
    t.mins -= 60;
    t.hours += 1;
  }

  // See if our adjusted hours have slopped into the day on either side
  // If so, get the hours back into range and adjust the Y/M/D accordingly.
  if (t.hours < 0) {
    t.hours += 24;
    ratadie = toRataDie(t);
    ratadie--;
    fromRataDie(ratadie, years, month, date);
    t.years = years;
    t.month = month;
    t.date = date;
  }
  else if (t.hours >= 24) {
    t.hours -= 24;
    ratadie = toRataDie(t);
    ratadie++;
    fromRataDie(ratadie, years, month, date);
    t.years = years;
    t.month = month;
    t.date = date;
  }
  return true;
}


//--------------------------------------------------------------------------------
void TimeUtils::utc2local(
  const cTime_t& utc,
  tzOffset_t tzOffset,      // This is the basic ST timezone offset for the target local time
  bool permitDstChanges,    // if true, we need to account for changes to the tzOffset param if DST is active
  cTime_t& localTime
  )
{
  // The hard part about converting a UTC time to a Local time is knowing if DST is active or not.
  // We start by applying the basic timezone correction
  localTime = utc;
  adjust(localTime, tzOffset);
  localTime.tzOffset = tzOffset;
  localTime.dst = false;

  // At this point, the localTime is correct if DST is not permitted
  if (permitDstChanges) {
    // We need to figure out if DST is active or not
    if ((localTime.month < DST_START_MONTH) || (localTime.month > DST_END_MONTH)) {
      // DST can't be possibly be active during these months
    }
    else if ((localTime.month > DST_START_MONTH) && (localTime.month < DST_END_MONTH)) {
      // DST must be active during these months
      localTime.dst = true;
    }
    else if (localTime.month == DST_START_MONTH) {
      // ST changes to DST on the second Sunday in March.
      // The second Sunday cannot possibly occur before Mar 8.
      // Calculate how many days to wait starting from the 8th until we would see a Sunday:
      uint32_t d = TimeUtils::dayOfWeek(localTime.years, 3, 8);
      uint32_t dateOfSwitchover = 8 + ((7-d) % 7);

      // endSt starts off representing the very last moment of Local ST within this month.
      cTime_t endSt;
      endSt.tzOffset = 0;
      endSt.years = localTime.years;
      endSt.month = localTime.month;
      endSt.date = dateOfSwitchover;
      endSt.hours = 1;
      endSt.mins = 59;
      endSt.secs = 59;
      endSt.millisecs = 999;

      // Convert endSt to mark the UTC time corresponding to the very last moment of Local ST in the current timezone
      adjust(endSt, -tzOffset);

      // If the UTC time being converted > the UTC time marking the end of ST in the local timezone, it must be DST
      localTime.dst = TimeUtils::GT(utc, endSt);
    }
    else if (localTime.month == DST_END_MONTH) {
      // DST switches back to ST on the first Sunday in November.
      // The first Sunday must be in the range Nov 1..8.
      // Calculate how many days to wait starting from the 1st until we would see a Sunday:
      uint32_t d = TimeUtils::dayOfWeek(localTime.years, 11, 1);
      uint32_t dateOfSwitchover = 1 + ((7-d) % 7);

      // beginSt gets set to the UTC time corresponding to the very first moment of ST within this month.
      // That would be 1:00:00.000AM ST
      cTime_t beginSt;
      beginSt.tzOffset = 0;
      beginSt.years = localTime.years;
      beginSt.month = localTime.month;
      beginSt.date = dateOfSwitchover;
      beginSt.hours = 1;
      beginSt.mins = 0;
      beginSt.secs = 0;
      beginSt.millisecs = 0;

      // Convert beginSt to mark the UTC time corresponding to the very first moment of Local ST in the current timezone
      adjust(beginSt, -tzOffset);

      // If the UTC clock is reporting a time >= the UTC time marking the start of ST, it must be ST
      localTime.dst = !TimeUtils::GTEQ(utc, beginSt);
    }

    // If DST is active right now, adjust our original conversion to account for it
    if (localTime.dst) {
      localTime.tzOffset += 60;
      adjust(localTime, 60);
    }
  }
}

//--------------------------------------------------------------------------------
bool TimeUtils::local2utc(const cTime_t& localTime, cTime_t& utcTime)
{
  // Since the tzOffset always defines the offset from UTC (regardless if dst is set or not),
  // conversion to UTC is a simple matter of removing the tzOffset:
  utcTime = localTime;
  adjust(utcTime, -localTime.tzOffset);

  return true;
}

//--------------------------------------------------------------------------------
int32_t TimeUtils::lengthOfDayInSeconds(const cTime_t& t)
{
  return (t.hours * 3600) + (t.mins * 60) + t.secs;
}
