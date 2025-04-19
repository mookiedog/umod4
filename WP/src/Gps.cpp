// The modules are marked NEO8-M8N-0-10
// uBlox indicates that NEO8-M8N-0 corresponds to: u-blox M8 GNSS module, Flash, TCXO, SAW, LNA LCC

#include "Gps.h"

#include "stdio.h"
#include <string.h>
//#include <math.h>

#include "Clock.h"
#include "TimeUtils.h"

#include "hardware/gpio.h"
#include "umod4_WP.h"

#include "pico/time.h"

#include "Logger.h"
#include "WP_log.h"

uint32_t msgCount;
uint32_t cksumErrorCount;

static uint32_t dbg = 0;

// Defining this next symbol tells the GPS to disable NMEA and communicate via UBX only
#define UBX_ONLY_MODE

#define MSECS_PER_DAY 86400000

// ----------------------------------------------------------------------------------
const char* decode_srcOfCurrLs(uint8_t src)
{
  switch (src) {
    case 0: return "Default";
    case 1: return "GLONASS-GPS";
    case 2: return "GPS";
    case 3: return "SBAS";
    case 4: return "Beidou";
    case 5: return "Galileo";
    case 6: return "AidedData";
    case 7: return "Configured";
  }
  return "Unknown";
}



// ----------------------------------------------------------------------------------
const char* decode_srcOfLsChange(uint8_t src)
{
  switch (src) {
    case 0: return "No Info";
    case 2: return "GPS";
    case 3: return "SBAS";
    case 4: return "Beidou";
    case 5: return "Galileo";
    case 6: return "GLONASS";
  }
  return "Unknown";
}


// ----------------------------------------------------------------------------------
extern "C" void start_gps_rxTask(void *pvParameters);

void start_gps_rxTask(void *pvParameters)
{
  // The task parameter is the specific Gps object we should be using in the ISR
  Gps* gps = static_cast<Gps*>(pvParameters);

  // This allows us to invoke the task method on the correct Gps instance
  gps->rxTask();
  panic(LOCATION("Should never return"));
}


// ----------------------------------------------------------------------------------
Gps::Gps(Uart* _uart) /*: UartCallback()*/
{
  uart = _uart;

  locationKnown = false;
  latitude_degrees = 0.0;
  longitude_degrees = 0.0;
  xTaskCreate(start_gps_rxTask, "Gps", 2048 /* words */, this, TASK_HIGH_PRIORITY, &gps_taskHandle);

  uart->notifyOnRx(gps_taskHandle);

  // Drive this pin high to signal the scope
  gpio_init(SPARE1_PIN);
  gpio_put(SPARE1_PIN, 0);
  gpio_set_dir(SPARE1_PIN, GPIO_OUT);

  // Assume we have no fix yet
  fixType = -1;

  // Set time/date info to illegal values to trigger reloading them once they are known:
  year = 0;
  month = 0;
  day = 0;
  hours = 255;
  mins = 255;
  secs = 255;
  nanos = 0;

  moving = false;
}


// ----------------------------------------------------------------------------------
void Gps::sleep()
{
  // The only way I can get the GPS to go into its low power mode is to ask for an
  // infinite duration powerDown.
  setPowerDown(0);
}

// ----------------------------------------------------------------------------------
void Gps::run()
{
  // The only way to wake the GPS from an infinite duration powerDown is to
  // ask for a short duration powerdown, which overrides any previous infinite
  // powerdown request.
  setPowerDown(1);
}

// ----------------------------------------------------------------------------------
bool Gps::getLocation(float* latitudeP, float* longitudeP)
{
  bool result = false;

  if (locationKnown) {
    if (latitudeP) {
      *latitudeP = latitude_degrees;
    }
    if (longitudeP) {
      *longitudeP = longitude_degrees;
    }
    result = true;
  }

  return result;
}


// ----------------------------------------------------------------------------------
uint8_t Gps::get_uint8_t(uint8_t* buffer, uint16_t offset)
{
  return(buffer[offset]);
}

int8_t Gps::get_int8_t(uint8_t* buffer, uint16_t offset)
{
  return((int8_t)buffer[offset]);
}

// ----------------------------------------------------------------------------------
uint32_t Gps::get_uint16_t(uint8_t* buffer, uint16_t offset)
{
  return(buffer[offset] + (buffer[offset+1]<<8));
}

int32_t Gps::get_int16_t(uint8_t* buffer, uint16_t offset)
{
  return (int32_t)get_uint32_t(buffer, offset);
}


// ----------------------------------------------------------------------------------
uint32_t Gps::get_uint32_t(uint8_t* buffer, uint16_t offset)
{
  return(buffer[offset] + (buffer[offset+1]<<8) + (buffer[offset+2]<<16) + (buffer[offset+3]<<24));
}

int32_t Gps::get_int32_t(uint8_t* buffer, uint16_t offset)
{
  return (int32_t)get_uint32_t(buffer, offset);
}

// ----------------------------------------------------------------------------------
// TIM-TP.  See the protocol manual pg 72-73 for the relationship between UTC and the PPS TimePulse
void Gps::process_TIM_TP(uint8_t* payload)
{
  // The short story is that this packet represents the UTC time-of-week at the next PPS event.
  if (dbg) printf("TIM-TP: ");
  uint32_t tow = get_uint32_t(payload, 0);
  uint32_t wkNum = get_uint16_t(payload, 12);
  uint8_t flags = get_uint8_t(payload, 14);
  char timeBase = (flags & 1) ? 'U' : 'G';
  char utcAvail = (flags & 2) ? 'U' : '-';

  cTime_t timeAtNextPps;

  timeAtNextPps.tzOffset  = 0;
  timeAtNextPps.millisecs = tow % 1000;
  timeAtNextPps.secs      = (tow/(1000)) % 60;
  timeAtNextPps.mins      = (tow/(1000*60)) % 60;
  timeAtNextPps.hours     = (tow/(1000*3600)) % 24;

  #if 0
    uint32_t gpsEpoch_ratadie = TimeUtils::ymdToRataDie(1980, 1, 6);
  #else
    uint32_t gpsEpoch_ratadie = 722820;   // This is a constant so it doesn't need to be calculated each time
  #endif

  uint32_t daysSinceGpsEpoch = (wkNum*7) + (tow / MSECS_PER_DAY);
  uint32_t today_ratadie = gpsEpoch_ratadie + daysSinceGpsEpoch;
  TimeUtils::fromRataDie(today_ratadie, timeAtNextPps);

  // Notify the rtc what the UTC time will be at the next PPS event
  #warning "FIX ME"
  #if 0
  {
    extern ArtemisRtc* rtc;
    rtc->presetUtcTime(&timeAtNextPps);
  }
  #endif

  if (dbg) {
    printf("Wk:%lu TOW:%lu %c%c [%02u/%s/%04u %s %02u:%02u:%02u.%03u UTC]\n",
          wkNum, tow, timeBase, utcAvail,
          timeAtNextPps.date,
          TimeUtils::monthToString(timeAtNextPps.month),
          timeAtNextPps.years,
          TimeUtils::dayOfWeekToString(TimeUtils::dayOfWeek(timeAtNextPps)),
          timeAtNextPps.hours, timeAtNextPps.mins, timeAtNextPps.secs, timeAtNextPps.millisecs
    );
  }
}

// ----------------------------------------------------------------------------------
// NAV-TIMELS
void Gps::process_NAV_TIMELS(uint8_t* payload)
{
  uint32_t version = get_uint32_t(payload, 4);
  if (version == 0) {
    uint8_t srcOfCurrLs = get_uint8_t(payload, 8);
    uint8_t currLs = get_uint8_t(payload, 9);
    uint8_t srcOfLsChange = get_uint8_t(payload, 10);
    uint8_t lsChange = get_uint8_t(payload, 11);
    int32_t timeToLsEvent = get_int32_t(payload, 12);
    uint16_t dateOfLsGpsWn = get_uint16_t(payload, 16);
    uint16_t dateOfLsGpsDn = get_uint16_t(payload, 18);
    uint8_t valid = get_uint8_t(payload, 23);
    bool validCurrLs = (valid & 0x01) != 0;
    bool validTimeToLsEvent = (valid & 0x02) != 0;

    if (dbg) {
      printf("NAV-TIMELS: V%d", version);
      if (validCurrLs) {
        printf(" CurrLsSrc: %s", decode_srcOfCurrLs(srcOfCurrLs));
        printf(" CurrLs: %d", currLs);
      }
      if (validTimeToLsEvent) {
        printf(" lsChgSrc: %s", decode_srcOfLsChange(srcOfLsChange));
        printf(" lsChg: %d", lsChange);
        if (lsChange != 0) {
          printf(" timeToLsEvent: %ld", timeToLsEvent);
          printf(" dateOfLsGpsWn: %lu", dateOfLsGpsWn);
          printf(" dateOfLsGpsDn: %lu", dateOfLsGpsDn);
        }
      }
      printf("\n");
    }
  }
  else {
    if (dbg) printf("NAV-TIMELS: unknown version 0x%04X\n", version);
  }
}

// ----------------------------------------------------------------------------------
void Gps::process_NAV_PVT(uint8_t* payload)
{
  // NAV-PVT
  bool dateValid;
  bool timeValid;
  bool fullyResolved;
  uint8_t _fixType;
  uint16_t _year;
  uint8_t _month;
  uint8_t _day;
  uint8_t _hours;
  uint8_t _mins;
  uint8_t _secs;
  int32_t _nanos;
  uint32_t _itow;

  if (dbg) printf("NAV-PVT: ");
  _year = _month = _day = 0;
  uint8_t validFlags = get_uint8_t(payload, 11);
  dateValid = (validFlags & 0x01);
  if (dateValid) {
    //if (dbg) printf("Date is valid\n");
    _year = get_uint16_t(payload, 4);
    _month = get_uint8_t(payload, 6);
    _day   = get_uint8_t(payload, 7);
  }

  _hours = _mins = _secs = 0;
  timeValid = (validFlags & 0x02);
  if (timeValid) {
    //if (dbg) printf("Time is valid\n");
    _hours = get_uint8_t(payload, 8);
    _mins  = get_uint8_t(payload, 9);
    _secs  = get_uint8_t(payload, 10);
    _nanos = get_int32_t(payload, 16);
  }

  if (timeValid && dateValid) {
    bool doRest = false;
    if (logger) {
      if (year != _year) {
        year = _year;
        int8_t yr = year - 2000;
        logger->logData(LOG_YEAR, LOG_YEAR_LEN, (uint8_t*)&yr);
        doRest = true;
      }

      if ((month != _month) || doRest) {
        month = _month;
        logger->logData(LOG_MONTH, LOG_MONTH_LEN, &month);
        doRest = true;
      }

      if ((day != _day) || doRest) {
        day = _day;
        logger->logData(LOG_DATE, LOG_DATE_LEN, &day);
        doRest = true;
      }

      if ((hours != _hours) || doRest) {
        hours = _hours;
        logger->logData(LOG_HOURS, LOG_HOURS_LEN, &hours);
        doRest = true;
      }

      if ((mins != _mins) || doRest) {
        mins = _mins;
        logger->logData(LOG_MINS, LOG_MINS_LEN, &mins);
        doRest = true;
      }

      if ((secs != _secs) || doRest) {
        secs = _secs;
        logger->logData(LOG_SECS, LOG_SECS_LEN, &secs);
        doRest = true;
      }

      if (moving && (fixType>=2)) {
        nanos = _nanos;
        int8_t centis = (nanos+5000000)/10000000;
        logger->logData(LOG_CSECS, LOG_CSECS_LEN, (uint8_t*)&centis);
      }
    }
  }

  fullyResolved = (validFlags & 0x04);
  if (fullyResolved) {
    //if (dbg) printf("Fully Resolved\n");
    // "Fully resolved" means something to do with having accounted for the difference in total leap seconds between GPS time and UTC time.
    // Apparently, the GPS firmware is originally built with the current total number of leap seconds, but that can/will change over time.
    // The satellites send the current leap second info over a 12.5 minute period, so the actual leap second count might
    // jump a few seconds when new leap second data is received that may differ from the built-in leap second info.
    // See: https://portal.u-blox.com/s/question/0D52p00008HKCbFCAX/what-is-time-error-if-utc-fully-resolved-flag-not-asserted
  }

  if (dbg) {
    printf("%c%c%c  ", timeValid ? 'T' : '-', dateValid ? 'D' : '-', fullyResolved ? 'R' : '-');
    if (dateValid) printf("%04u/%02u/%02u ", _year, _month, _day);
    if (timeValid) printf("%02u:%02u:%02u %09ld", _hours, _mins, _secs, _nanos);
  }

  _fixType = get_uint8_t(payload, 20);
  if (dbg) printf(" F%u ", _fixType);

  if (_fixType != fixType) {
    // Log all changes to fixType, upgrades or downgrades:
    if (logger) logger->logData(LOG_FIXTYPE, LOG_FIXTYPE_LEN, &_fixType);

    if ((fixType<2) && (_fixType >= 2)) {
      // Trigger logging our position now that we have a 2D or 3D fix even if we are not moving.
      moving = STOP_CNT;
    }
    fixType = _fixType;
  }

  if ((_fixType == 2) || (_fixType == 3)) {
    int32_t rawLat = get_int32_t(payload, 28);
    int32_t rawLon = get_int32_t(payload, 24);

    double lat = rawLat * 1.0e-7;
    double lon = rawLon * 1.0e-7;

    int32_t gSpeed_mm_per_sec = get_int32_t(payload, 60);
    float gSpeed_mph = gSpeed_mm_per_sec / 447.04f;

    // We will log our current velocity as an integer in terms of tenths of MPH, so 128 means 12.8 MPH
    int16_t velocity = (gSpeed_mph + 0.05) * 10;

    if (gSpeed_mph >= MIN_MOVEMENT_VELOCITY_MPH) {
      moving = STOP_CNT;
    }
    else if (moving) {
      moving -= 1;
      if (moving == 0) {
        // We just stopped moving
        //printf("Log: stopped moving\n");
      }
    }

    latitude_degrees = lat;
    longitude_degrees = lon;
    locationKnown = true;
    if (dbg) printf("%10.6lf, %10.6lf (%.1f mph)\n", lat, lon, gSpeed_mph);

    if (moving) {
      if (dbg) printf("Log: %10.6lf, %10.6lf (%.1f mph)\n", lat, lon, gSpeed_mph);
      uint8_t b[10];
      b[0] = (rawLat >> 0) & 0xFF;
      b[1] = (rawLat >> 8) & 0xFF;
      b[2] = (rawLat >>16) & 0xFF;
      b[3] = (rawLat >>24) & 0xFF;
      b[4] = (rawLon >> 0) & 0xFF;
      b[5] = (rawLon >> 8) & 0xFF;
      b[6] = (rawLon >>16) & 0xFF;
      b[7] = (rawLon >>24) & 0xFF;
      b[8] = (velocity >> 0) & 0xFF;
      b[9] = (velocity >> 8) & 0xFF;


      if (logger) logger->logData(LOG_PV, LOG_PV_LEN, b);
    }
  }
  else {
    if (dbg) printf("\n");
  }

  /*
  _itow = get_uint32_t(payload, 0);
  if (dbg) printf("ITOW=%lu\n", _itow);
  */

  if (((_fixType == 2) || (_fixType == 3)) && timeValid) {
    // The GPS is reporting a proper time.
    // Set the RTC if needed

  }
}

// ----------------------------------------------------------------------------------
void Gps::processUbxBuffer()
{
  uint8_t* payload = &ubxBuffer[4];

  msgCount++;

  if ((ubxClass == 0x05) && (ubxId == 0x01)) {
    // ACK-ACK
    if (dbg) printf("%s: UBX ACK-ACK received\n", __FUNCTION__);
    ubxAck = true;
  }
  else if ((ubxClass == 0x05) && (ubxId == 0x00)) {
    // ACK-NAK
    if (dbg) printf("%s: UBX ACK-NAK received\n", __FUNCTION__);
    ubxNak = true;
  }
  else if ((ubxClass == 0x0D) && (ubxId == 0x01)) {
    process_TIM_TP(payload);
  }
  else if ((ubxClass == 0x01) && (ubxId == 0x26)) {
    process_NAV_TIMELS(payload);
  }
  else if ((ubxClass == 0x01) && (ubxId == 0x07)) {
    process_NAV_PVT(payload);
  }
  else {
    printf("%s: Unknown UBX Message received: %02X-%02X\n", __FUNCTION__, ubxClass, ubxId);
  }
}

// ----------------------------------------------------------------------------------
void Gps::tx(uint8_t byte)
{
  if (dbg >= 2) {
    printf("%02X ", byte);
  }
  uart->tx(byte);
}


// ----------------------------------------------------------------------------------
// This version assumes that the class and ID are the first two bytes followed immediately
// by the payload
void Gps::sendUbxMsg(uint8_t* buffer, uint16_t bufferLength)
{
  uint8_t ckA, ckB;
  uint16_t payloadLength;

  if (bufferLength<2) {
    panic("Buffer too small!");
  }
  payloadLength = bufferLength-2;

  tx(0xB5);
  tx(0x62);

  ckA = ckB = 0;

  // Send the class & ID (first two bytes in buffer)
  tx(*buffer);
  ckA += *buffer++;
  ckB += ckA;
  tx(*buffer);
  ckA += *buffer++;
  ckB += ckA;

  // Send the payload length, LSB first
  tx(payloadLength & 0xFF);
  ckA += payloadLength & 0xFF;
  ckB += ckA;

  tx((payloadLength>>8) & 0xFF);
  ckA += (payloadLength>>8) & 0xFF;
  ckB += ckA;

  // Send the payload (if any)
  for (uint32_t i=0; i<payloadLength; i++) {
    tx(*buffer);
    ckA += *buffer;
    ckB += ckA;
    buffer++;
  }

  // Always send the checksum bytes
  tx(ckA);
  tx(ckB);

  if (dbg >= 2) {
    printf("\n");
  }
}


// ----------------------------------------------------------------------------------
void Gps::sendUbxMsg(uint8_t ubxClass, uint8_t ubxId, const uint8_t* payload, uint16_t payloadLength)
{
  uint8_t ckA, ckB;

  ubxAck = ubxNak = false;

  tx(0xB5);
  tx(0x62);

  ckA = ckB = 0;

  tx(ubxClass);
  ckA += ubxClass;
  ckB += ckA;

  tx(ubxId);
  ckA += ubxId;
  ckB += ckA;

  tx(payloadLength & 0xFF);
  ckA += payloadLength & 0xFF;
  ckB += ckA;

  tx((payloadLength>>8) & 0xFF);
  ckA += (payloadLength>>8) & 0xFF;
  ckB += ckA;

  for (uint32_t i=0; i<payloadLength; i++) {
    tx(*payload);
    ckA += *payload;
    ckB += ckA;
    payload++;
  }

  tx(ckA);
  tx(ckB);

  if (dbg >= 2) {
    printf("\n");
  }
}

// ----------------------------------------------------------------------------------
void Gps::setUbxOnlyMode(uint32_t baudRate)
{
  #ifdef UBX_ONLY_MODE
  const uint8_t cl = 0x06;
  const uint8_t id = 0x00;

  uint8_t payload[20] = {
    0x01,                     // port id
    0x00,                     // reserved
    0x00, 0x00,               // TX ready
    0xC0, 0x08, 0x00, 0x00,   // mode
    0x00, 0x00, 0x00, 0x00,   // baud rate
    0x07, 0x00,               // in protocol
    0x01, 0x00,               // out protocol is UBX-only
    0x00, 0x00,               // flags
    0x00, 0x00,               // reserved[2]
  };

  payload[8]  = (baudRate>>0)  & 0xFF;
  payload[9]  = (baudRate>>8)  & 0xFF;
  payload[10] = (baudRate>>16) & 0xFF;
  payload[11] = (baudRate>>24) & 0xFF;

  if (dbg) {printf("%s: GPS UBX: Setting UBX-only reporting mode at baud rate: %u\n", __FUNCTION__, baudRate);}
  sendUbxMsg(cl, id, payload, sizeof(payload));

  // Because this msg can change the baud rate, we wait for the msg to be completely
  // transmitted before returning.
  uint32_t t0_usec = time_us_32();
  do {
  } while (uart->txBusy());
  if (dbg) printf("%s: %.2f mSec to complete message transmission\n", __FUNCTION__, (time_us_32() - t0_usec)/1000.0);


  #else
    if (dbg) {printf("%s: GPS_UBX: NMEA messages are active!\n", __FUNCTION__);}
  #endif
}

// ----------------------------------------------------------------------------------
// Set the measurement rate. In essence, this sets the basic navigation report rate.
// To set a measurement rate of 1 Hz, use 1000 mSec. For 10 Hz, use 100 mSec.
void Gps::setMeasurementRate(uint16_t mSec)
{
  const uint8_t cl = 0x06;    // CFG
  const uint8_t id = 0x08;    // RATE

  uint8_t payload[6];

  payload[0] = mSec & 0xFF;
  payload[1] = (mSec>>8) & 0xFF;

  // Generate a navigation solution every 1 measurement cycle
  payload[2] = 1;
  payload[3] = 0;

  // Measurement cycles will be aligned to UTC time:
  payload[4] = 0;
  payload[5] = 0;

  printf("%s: GPS UBX: Setting CFG-MEAS measurement rate\n", __FUNCTION__);
  sendUbxMsg(cl, id, payload, sizeof(payload));
}


// ----------------------------------------------------------------------------------
// Setting the 'report rate' is not completely intuitive.
// Setting the report rate to '1' means to send a NAV/PVT
// message on every navigation solution. Setting the rate to '2' would mean to send
// the NAV/PVT every other navigation solution.
void Gps::setNavReportRate()
{
  const uint8_t cl = 0x06;    // CFG
  const uint8_t id = 0x01;    // MSG

  const uint8_t payload[] = {
    0x01,                     // set rate for: message class: NAV
    0x07,                     // message ID: PVT
    0x01                      // NAV/PVT message rate will be once per navigation solution sending on this port
  };

  printf("%s: GPS UBX: Setting NAV-PVT report rate\n", __FUNCTION__);
  sendUbxMsg(cl, id, payload, sizeof(payload));
}


// ----------------------------------------------------------------------------------
void Gps::setTimelsReportRate()
{
  const uint8_t cl = 0x06;    // CFG
  const uint8_t id = 0x01;    // MSG

  const uint8_t payload[] = {
    0x01,                     // set rate for: message class: NAV
    0x26,                     // message ID: TIMELS
    0x01                      // message rate will be once per navigation solution sending on this port
  };

  printf("%s: GPS UBX: Setting NAV-TIMELS report rate\n", __FUNCTION__);
  sendUbxMsg(cl, id, payload, sizeof(payload));
}


// ----------------------------------------------------------------------------------
void Gps::setTimePulseReportRate()
{
  const uint8_t cl = 0x06;    // CFG
  const uint8_t id = 0x01;    // MSG

  const uint8_t payload[] = {
    0x0D,                     // set rate for: message class: TIM
    0x01,                     // message ID: TP
    0x01                      // message rate will be once per navigation solution sending on this port
  };

  printf("%s: GPS UBX: Setting TIM-TP report rate\n", __FUNCTION__);
  sendUbxMsg(cl, id, payload, sizeof(payload));
}

// ----------------------------------------------------------------------------------
void Gps::setStationaryPlatformModel()
{
  const uint8_t cl = 0x06;    // CFG
  const uint8_t id = 0x24;    // NAV5

  // This is a cheat since we only need to define the first 3 bytes, that's all we do.
  // The mask prevents setting any of the other fields which would be zeroed filled.
  const uint8_t payload[36] = {
    0x01, 0x00,               // mask bits (set dynamic model only)
    0x02,                     // Use "stationary" dynamic platform model
  };

  printf("%s: GPS UBX: Setting platform model to 'stationary'\n", __FUNCTION__);
  sendUbxMsg(cl, id, payload, sizeof(payload));
}

// ----------------------------------------------------------------------------------
void Gps::setAutomotivePlatformModel()
{
  const uint8_t cl = 0x06;    // CFG
  const uint8_t id = 0x24;    // NAV5

  // This is a cheat since we only need to define the first 3 bytes, that's all we do.
  // The mask prevents setting any of the other fields which would be zeroed filled.
  const uint8_t payload[36] = {
    0x01, 0x00,               // mask bits (set dynamic model only)
    0x04,                     // Use "automotive" dynamic platform model
  };

  printf("%s: GPS UBX: Setting platform model to 'automotive'\n", __FUNCTION__);
  sendUbxMsg(cl, id, payload, sizeof(payload));
}

// ----------------------------------------------------------------------------------
// This appears to only enable the potential for the antenna to be powere off.
// From the protocol manual:
//    "It can be used to turn off the supply to the antenna in the event of
//     a short circuit (for example) or to manage power consumption in power save."
// This makes it sound as though this routine enables the system to switch off the antenna
// when it goes into power save mode.

void Gps::setAntennaPower(bool powerOn)
{
  const uint8_t cl = 0x06;    // CFG
  const uint8_t id = 0x13;    // ANT

  uint8_t payload[8];

  // Zeroing the whole payload ensures that the flag bit which controls
  // the antenna control pin assignments will not allow any changes
  // to the pin assignments.
  memset(payload, 0, sizeof(payload));
  payload[0] = powerOn ? 0x01 : 0x00;

  printf("%s: GPS UBX: Setting antenna power to %s\n", __FUNCTION__, powerOn ? "ON" : "OFF");
  sendUbxMsg(cl, id, payload, sizeof(payload));
}


// ----------------------------------------------------------------------------------
// On a Neo8-M8N, the average operating power consumption was about 42 mA.
// Calling this routing cut power consumption by about 33 mA.
// Even in power down mode, the GPS is still drawing 9.4 mA.
// That is surprisingly high, in my mind.
void Gps::setPowerDown(uint32_t duration_ms)
{
  const uint8_t cl = 0x02;    // RXM
  const uint8_t id = 0x41;    // PMREQ

  #if 0
  // Sadly, this version of the UBX command flat-out doesn't work. It never enters power save
  // even if you configure it for no wakeup sources and infinite duration sleep.
  const uint32_t durationOffset = 4;
  const uint8_t _payload[16] = {
    0x00,                     // message version 0
    0x00, 0x00, 0x00,         // reserved[3]
    0x00, 0x00, 0x00, 0x00,   // sleep duration in msec (to be filled in with duration_ms)
    //0x02, 0x00, 0x00, 0x00,   // flags: enter backup mode
    0x06, 0x00, 0x00, 0x00,   // flags: force entry into backup mode, even if USB is connected
    //0x08, 0x00, 0x00, 0x00,   // Wakeup sources: any edge on RX pin
    0x00, 0x00, 0x00, 0x00,   // Wakeup sources: none
  };
  #else
  // This version of the command does work, but it only sleeps for a duration with no wakeup on IO pin events.
  const uint32_t durationOffset = 0;
  const uint8_t _payload[8] = {
    0x00, 0x00, 0x00, 0x00,   // sleep duration in msec (to be filled in with duration_ms)
    0x02, 0x00, 0x00, 0x00,   // flags: enter backup mode
  };
  #endif

  // Copy the ROM string to RAM so we can fill in the duration parameter
  uint8_t payload[sizeof(_payload)];
  memcpy(payload, _payload, sizeof(_payload));
  payload[durationOffset+0] = duration_ms & 0xFF;
  payload[durationOffset+1] = (duration_ms>>8) & 0xFF;
  payload[durationOffset+2] = (duration_ms>>16) & 0xFF;
  payload[durationOffset+3] = (duration_ms>>24) & 0xFF;

  //printf("GPS UBX: Putting GPS to sleep\n");
  sendUbxMsg(cl, id, payload, sizeof(payload));
}



// ----------------------------------------------------------------------------------
void Gps::setBaud()
{
  // The GPS defaults to 9600 baud mode after powering up.
  // That's too slow for anything more than about a 2 Hz report rate.
  // However, the realities of life mean that we can't be 100% sure if the GPS
  // is operating at 9600 or the desired GPS_BAUD_RATE if the PicoW reset for any reason
  // during system operation.

  // So: We set our UART to 9600 baud, then send a command to config the GPS for the desired GPS_BAUD_RATE.
  // If the UART was operating at 9600 baud, this will switch it to GPS_BAUD_RATE.
  // If the UART was already operating at the desired GPS_BAUD_RATE, it will just see a garbled mess and ignore it.
  //
  // WARNING: According to uBlox doc:
  //  "As of Protocol version 18+, the UART RX interface will be disabled when more than 100
  //   frame errors are detected during a one-second period. This can happen if the wrong
  //   baud rate is used or the UART RX pin is grounded."

  uint32_t tempBaud = 9600;
  if (dbg) printf("%s: Setting UART baud rate %d\n", __FUNCTION__, tempBaud);
  uart->configBaud(tempBaud);
  setUbxOnlyMode(GPS_BAUD_RATE);

  if (dbg) printf("%s: Setting UART baud rate %d\n", __FUNCTION__, GPS_BAUD_RATE);
  uart->configBaud(GPS_BAUD_RATE);
  setUbxOnlyMode(GPS_BAUD_RATE);

  // In theory, our uart and the GPS are operating at the desired GPS_BAUD_RATE now.
}

// ----------------------------------------------------------------------------------
// We assume that both the PicoW and the GPS are operating in sync at the system's target baud rate GPS_BAUD_RATE.
// It appears that the NEO-8 responds to each command with ACK-ACK or ACK-NAK within about
// 6 milliseconds after receiving it.
#define ACK_ACK_DELAY_MS 20
void Gps::config()
{
  int32_t ack;

  // In theory, we don't actually need to do this again because we already did it when we called configBaud().
  // It's harmless though.
  setUbxOnlyMode(GPS_BAUD_RATE);
  vTaskDelay(pdMS_TO_TICKS(ACK_ACK_DELAY_MS));

  // Setting the measurement rate has the side effect of setting
  // the basic rate that navigation solutions get generated.
  setMeasurementRate(GPS_MEASUREMENT_PERIOD_MS);
  vTaskDelay(pdMS_TO_TICKS(ACK_ACK_DELAY_MS));

  setNavReportRate();
  vTaskDelay(pdMS_TO_TICKS(ACK_ACK_DELAY_MS));

  setTimelsReportRate();
  vTaskDelay(pdMS_TO_TICKS(ACK_ACK_DELAY_MS));

  setTimePulseReportRate();
  vTaskDelay(pdMS_TO_TICKS(ACK_ACK_DELAY_MS));

  setAutomotivePlatformModel();
  vTaskDelay(pdMS_TO_TICKS(ACK_ACK_DELAY_MS));
}

// ----------------------------------------------------------------------------------
void Gps::rxTask()
{
  uint8_t b;
  typedef enum {SYNC_ST,
    NMEA_H1_ST, NMEA_G_ST, NMEA_P_ST, NMEA_GP_ST,
    UBX_62_ST, UBX_CLASS_ST, UBX_ID_ST, UBX_LENLO_ST, UBX_LENHI_ST, UBX_PAYLOAD_ST, UBX_CKA_ST, UBX_CKB_ST,
    } state_t;

  state_t state = SYNC_ST;

  // Enable a pulldown on the pin that the GPS uses to transmit to us.
  // If the GPS is present, it will override our pulldown whenever the UART is idle.
  gpio_pull_down(GPS_RX_PIN);

  // There are a couple of annoying possibilities at this point:
  // 1) the GPS may be running at its default baud rate instead of the faster rate required to support 10Hz position reports
  // 2) it is possible that while the baud rate is correct, the GPS is mis-configured. It might happen by
  //    stopping a debugging session in the middle of configuration, or if the WP crashes during configuration.
  // Either way, we should not trust the GPS to be properly configured in any way at this point.
  setBaud();
  config();

  while (1) {
    uint16_t c;

    // Get the next RX char from the RX queue, but with a timeout.
    // Under normal circumstances, the timeout will never trigger since the GPS should be
    // reporting wads of UBS messages at 10 Hz.
    // If the GPS resets itself or messes up its baud rate or is not even present (as might
    // happen during bench testing) the 1/2 second timeout on receiving valid serial data will trigger.
    BaseType_t rval = uart->rx(c, pdMS_TO_TICKS(500));
    if (rval != pdPASS) {
      /// This test crudely avoids constantly talking to a GPS module that is not present.
      // With no GPS, the GPS TX input will always report as '0' due to our port-based pulldown.
      if (gpio_get(GPS_RX_PIN) != 0) {
        // The GPIO is being driven high, so a GPS might be present.
        // Try to reconfigure it:
        setBaud();
        config();
      }
    }
    else {
      // We successfully retrieved a character to process.
      // The data bits are the low order 8 bits, and the error flags are the higher-order bits.
      bool err = c >= 0x100;
      if (err) {
        if (dbg) printf("%s: err bits during receive: %02X\n", __FUNCTION__, c>>8);
      }
      if (err) {
        // We don't care what the error was - we just resync our UBX stream:
        state = SYNC_ST;
      }

      // Strip off the error bits:
      b = c & 0xFF;

      switch (state) {
        case SYNC_ST:
          // throw away all data until we see the start of a UBX msg or the start of a NMEA sentence
          if (b == '$') {
            state = NMEA_H1_ST;
          }
          else if (b == 0xB5) {
            state = UBX_62_ST;
          }
          break;

        case NMEA_H1_ST:
          if (b == 'G') {
            state = NMEA_G_ST;
          }
          else if (b == 'P') {
            state = NMEA_P_ST;
          }
          else {
            state = SYNC_ST;
          }
          break;

        case NMEA_G_ST:
          if (b == 'P') {
            state = NMEA_GP_ST;
          }
          else {
            state = SYNC_ST;
          }
          break;

        case NMEA_GP_ST:
        case NMEA_P_ST:
          printf("%s: NMEA message detected!\n", __FUNCTION__);
          // Reconfigure the GPS
          // The baud rate must be OK so we only need to reconfig the UBX stuff
          config();
          state = SYNC_ST;
          break;

        case UBX_62_ST:
          if (b == 0x62) {
            state = UBX_CLASS_ST;
            ubxP = ubxBuffer;
            memset(ubxBuffer, 0, sizeof(ubxBuffer));
            ubxCkA = ubxCkB = 0;
          }
          else {
            state = SYNC_ST;
          }
          break;

        case UBX_CLASS_ST:
          *ubxP++ = b;
          ubxCkA += b;
          ubxCkB += ubxCkA;

          ubxClass = b;
          state = UBX_ID_ST;
          break;

        case UBX_ID_ST:
          *ubxP++ = b;
          ubxCkA += b;
          ubxCkB += ubxCkA;

          ubxId = b;
          state = UBX_LENLO_ST;
          break;


        case UBX_LENLO_ST:
          *ubxP++ = b;
          ubxCkA += b;
          ubxCkB += ubxCkA;

          ubxLen = b;
          state = UBX_LENHI_ST;
          break;

        case UBX_LENHI_ST:
          *ubxP++ = b;
          ubxCkA += b;
          ubxCkB += ubxCkA;

          ubxLen += (b<<8);
          if (dbg) printf("Incoming UBX %02X-%02X (len %d)\n", ubxBuffer[0], ubxBuffer[1], ubxLen);
          if (ubxLen > 0) {
            // Check if the message is too long to fit in our buffer (-6 due to needing room for storing class/id/lenLo,lenHi,ckA,ckB)
            if (ubxLen > (sizeof(ubxBuffer)-6)) {
              printf("%s: ubx msg too long [%d] for buffer - ignored\n", __FUNCTION__, ubxLen);
              state = SYNC_ST;
            }
            else {
              ubxPayloadCount = 0;
              state = UBX_PAYLOAD_ST;
            }
          }
          else {
            // for a zero-length payload, go straight to reading the checksum
            state = UBX_CKA_ST;
          }
          break;

        case UBX_PAYLOAD_ST:
          // We know the message will fit so we can process each byte without checking for overflow
          *ubxP++ = b;
          ubxCkA += b;
          ubxCkB += ubxCkA;

          ubxPayloadCount++;
          if (ubxPayloadCount == ubxLen) {
            state = UBX_CKA_ST;
          }
          break;

        case UBX_CKA_ST:
        *ubxP++ = b;
        if (b == ubxCkA) {
            state = UBX_CKB_ST;
          }
          else {
            cksumErrorCount++;
            printf("%s: UBX checksum A error (err #%d) saw: %02X, expected %02X\n", __FUNCTION__, cksumErrorCount, b, ubxCkA);
            state = SYNC_ST;
          }
          break;

        case UBX_CKB_ST:
          *ubxP++ = b;
          if (b == ubxCkB) {
            // message received, checksum is good
            processUbxBuffer();
          }
          else {
            cksumErrorCount++;
            printf("%s: UBX checksum B error (%d)\n", __FUNCTION__, cksumErrorCount);
          }
          state = SYNC_ST;
          break;

        default:
        state = SYNC_ST;
      }
    }
  }
}
