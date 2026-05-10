#ifndef GPS_H
#define GPS_H

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "Psm.h"
#include "Uart.h"

#include <stdint.h>

typedef enum {
    GPS_NOT_PRESENT,    // GPIO reads low on every timeout — nothing driving the line
    GPS_MAYBE_PRESENT,  // GPIO reads high on timeout — something is driving the line
    GPS_PRESENT,        // received at least one byte from the UART
} GpsPresence;

struct GpsHealth {
    GpsPresence presence;
    uint32_t rx_errors;         // UART framing/parity errors
    uint32_t cksum_errors;      // UBX checksum failures
    uint32_t tim_tp_count;
    uint32_t nav_timels_count;
    uint32_t nav_pvt_count;
    uint32_t unknown_count;
    uint32_t tim_tp_age_ms;     // ms since last TIM-TP; UINT32_MAX if never received
    uint32_t nav_timels_age_ms;
    uint32_t nav_pvt_age_ms;
};

class Gps : public Psm
{
  public:
    Gps(Uart* uart);

    bool getLocation(float* latitude_degrees, float* longitude_degrees);
    float getSpeedMph() const { return speed_mph_; }
    void getHealth(GpsHealth* out) const;

    void sendUbxMsg(uint8_t ubxClass, uint8_t ubxId, const uint8_t* payload, uint16_t payloadLength);
    void sendUbxMsg(uint8_t* buffer, uint16_t bufferLen);

    int32_t waitForAckNak();

    void setUbxOnlyMode(uint32_t baudRate);
    void setMeasurementRate(uint16_t mSec);
    void setNavReportRate();
    void setTimePulseReportRate();
    void setTimelsReportRate();
    void setStationaryPlatformModel();
    void setAutomotivePlatformModel();
    void setPowerUp();
    void setPowerDown(uint32_t duration_msec);
    void setAntennaPower(bool powerOn);

    void rxTask();

    // These implement the Psm interface
    void run();
    void sleep();
    void deepSleep() {sleep();}
    void powerOff() {sleep();}

  private:
    void setBaud();
    void config();

    float latitude_degrees;
    float longitude_degrees;
    bool locationKnown;
    float speed_mph_;

    Uart* uart;
    TaskHandle_t gps_taskHandle;

    bool ubxAck;
    bool ubxNak;

    // The length of this buffer defines the max length of a UBX msg that we can receive.
    uint8_t ubxBuffer[128];
    uint8_t ubxClass;
    uint8_t ubxId;
    uint8_t* ubxP;
    uint16_t ubxLen;
    uint16_t ubxPayloadCount;
    uint8_t ubxCkA, ubxCkB;

    void tx(uint8_t byte);


    uint8_t get_uint8_t(uint8_t* buffer, uint16_t offset);
    int8_t  get_int8_t(uint8_t* buffer, uint16_t offset);

    uint32_t get_uint16_t(uint8_t* buffer, uint16_t offset);
    int32_t  get_int16_t(uint8_t* buffer, uint16_t offset);

    uint32_t get_uint32_t(uint8_t* buffer, uint16_t offset);
    int32_t  get_int32_t(uint8_t* buffer, uint16_t offset);

    void processUbxBuffer();
    void process_TIM_TP(uint8_t* payload);
    void process_NAV_TIMELS(uint8_t* payload);
    void process_NAV_PVT(uint8_t* payload);

    // A collection of state variables to help decide when to log certain kinds of data
    // If moving is != 0, we are moving.
    uint32_t moving;

    // A velocity at or above this value will be considered as "moving"
    #define MIN_MOVEMENT_VELOCITY_MPH   1.0f

    // The number of times in a row we need to think we are stopped before entering the non-moving state
    // We get positions at 10 Hz, so this means stopped for 1 second:
    #define STOP_CNT 10

    int32_t fixType;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hours;
    uint8_t mins;
    uint8_t csecs;
    uint8_t secs;
    int32_t nanos;
    uint32_t itow;

    // Health metrics
    GpsPresence m_presence;
    uint32_t m_rx_errors;
    uint32_t m_cksum_errors;
    uint32_t m_tim_tp_count;
    uint32_t m_nav_timels_count;
    uint32_t m_nav_pvt_count;
    uint32_t m_unknown_count;
    uint32_t m_tim_tp_last_ms;      // 0 = never received
    uint32_t m_nav_timels_last_ms;
    uint32_t m_nav_pvt_last_ms;
};

#endif