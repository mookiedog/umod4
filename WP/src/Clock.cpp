#include "Clock.h"

#include <string.h>
#include <math.h>

#include "TimeUtils.h"

#if 0
const char* Clock::dstDecoder[] = {
  "??",
  "DS",
  "ST"
};
#endif

//--------------------------------------------------------------------------------
Clock::Clock()

{
  set = false;
}

//--------------------------------------------------------------------------------
bool Clock::presetUtcTime(cTime_t* timeP)
{
  return false;
}


//--------------------------------------------------------------------------------
void Clock::setFromPreset()
{
}

#if 0
//--------------------------------------------------------------------------------
tzOffset_t Clock::getTimeZoneOffset()
{
  // DST moves the clock forward by 1 hour as compared to ST
  int32_t dstCorrection = (dstState == DST) ? 60 : 0;
  return(timezone + dstCorrection);
}


//--------------------------------------------------------------------------------
bool Clock::setTimeZoneOffset(tzOffset_t proposedOffset)
{
  if (!timezoneValid(proposedOffset)) {
    return(false);
  }

  // Only update the timezone if the offset is valid
  timezone = proposedOffset;
  return(true);
}

//--------------------------------------------------------------------------------
// The timezone offset is constrained to be in 30 minute increments, limited to +-12 hours.
bool Clock::timezoneValid(tzOffset_t offset)
{
  if ((offset % 30) != 0) {
    return false;
  }

  if ((offset > (12*60)) || (offset < (-12*60))) {
    return false;
  }

  return true;
}
#endif
