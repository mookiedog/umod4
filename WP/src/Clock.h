#ifndef CLOCK_H
#define CLOCK_H

#include <stdint.h>
//#include "main.h"

//----------------------------------------------------------------------------------------------------
typedef int16_t tzOffset_t;

// A clock time struct contains all the info that a timekeeping system would need to
// completely describe a UTC or local time so that conversions between timezones or DST/ST determinations can be performed.
//
// The 'dst' field defines if daylight savings is active.
// Localities that do not perform dst switching should leave the dst field 'false' all the time.
// The dst field is required in order to resolve the ambiguity when the time shifts backwards in the fall
// andthe times between 1AM and 2AM occur twice in a row, the first time with DST=true, and then again with DST=false.
//
// The tzOffset always represents the amount that local time is ahead (or behind) the UTC time.
//
// Examples:
// A cTime_t representing a UTC time would have a tzOffset of  0, dst='false' always
// A cTime_t representing a GMT time would have a tzOffset of  0, dst='false'
// A cTime_t representing a BST time would have a tzOffset of 60, dst='true'
// A cTime_t representing a PST time would have a tzOffset of (-8*60),    dst='false'
// A cTime_t representing a PDT time would have a tzOffset of (-8*60)+60, dst='true'
// A cTime_t representing year-round MST time in most of Arizona would have a tzOffset of (-7*60), dst='false' always
// A cTime_t representing 01:30:00.000AM PDT would have a tzOffset of (-7*60), dst='true'
// A cTime_t representing 01:30:00.000AM PST (exactly 1 hour after the example directly above) would have a tzOffset of (-8*60), dst='false'
typedef struct {
  tzOffset_t tzOffset;        // Timezone offset, in minutes to allow for places that have 30 minute TZ offset
  int16_t millisecs;          // 0..999
  int16_t years;              // the 4-digit year
  int8_t month;               // 1..12
  int8_t date;                // 1..31
  int8_t hours;               // 0..24
  int8_t mins;                // 0..59
  int8_t secs;                // 0..60
  bool dst;
} cTime_t;

//--------------------------------------------------------------------------------
// This abstract class is meant to provide a consistent abstraction for some mechanism that
// keeps a time-of-day style of time.
// It could be implemented on top of whatever RTC hardware exists inside this processor
// or it could be a complete software implementation based on some sort of tick interrupt.
//
// Clock objects keep the time as 24-hour UTC time for a few reasons:
//  - UTC handles leap seconds in a well-defined fashion
//  - UTC does not perform daylight savings changes
//  - 24 hour time is one less source of confusion
//
// There are mechanisms to convert the underlying UTC clock into a local time by specifying a timezone offset
// and if DST is allowed or not.

class Clock
{
  public:
    Clock();


    // Start and stop the RTC oscillator
    virtual void start() = 0;
    virtual void stop() = 0;

    virtual bool getUtcTime(cTime_t* timeP) = 0;
    virtual bool setUtcTime(cTime_t* timeP) = 0;

    // Preset allows a Clock to do all the error checking and convertion from a
    // cTime_t to whatever the hardware actually wants. It must be followed by a set()
    // to move the converted hardware contents into the hardware registers.
    // It is presumed that a set would be performed on something like a GPS PPS event
    // indicating the start of a second.
    virtual bool presetUtcTime(cTime_t* timeP);
    virtual void setFromPreset();

    virtual bool isSet() = 0;

    // Timezone offset is measured in MINUTES, not hours.
    // This allows for timezones with 30 minute offsets as in Newfoundland for example.
    tzOffset_t getTimeZoneOffset();

  private:
    bool set;      // True if the clock has been set
};

#endif
