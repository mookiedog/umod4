#if !defined WP_LOG_H
#define WP_LOG_H

// Terrible hack, for now:
//#include "ECU_log.h"
#define LOG_WP_STRT_ADDR   (0xE0)

#define LOG_CSECS       ((LOG_WP_STRT_ADDR) + 0x0000)
#define LOG_CSECS_LEN   1

#define LOG_SECS        ((LOG_WP_STRT_ADDR) + 0x0001)
#define LOG_SECS_LEN    1

#define LOG_MINS        ((LOG_WP_STRT_ADDR) + 0x0002)
#define LOG_MINS_LEN    1

#define LOG_HOURS       ((LOG_WP_STRT_ADDR) + 0x0003)
#define LOG_HOURS_LEN   1

#define LOG_DATE        ((LOG_WP_STRT_ADDR) + 0x0004)
#define LOG_DATE_LEN    1

#define LOG_MONTH       ((LOG_WP_STRT_ADDR) + 0x0005)
#define LOG_MONTH_LEN   1

#define LOG_YEAR        ((LOG_WP_STRT_ADDR) + 0x0006)
#define LOG_YEAR_LEN    1

#define LOG_FIXTYPE     ((LOG_WP_STRT_ADDR) + 0x0007)
#define LOG_FIXTYPE_LEN 1

// This is packed as 1 big message to save 20 bytes per second getting logged
// by not storing separate headers for lat/lon/velo at 10 Hz report rate.
// That might be sort of pointless in the grand scheme of things though.
// 4-byte lat, 4-byte lon, 2 byte velo
#define LOG_PV          ((LOG_WP_STRT_ADDR) + 0x0008)
#define LOG_PV_LEN      (4+4+2)

// Log filesystem IO operations.
// Time will be stored as 2 bytes representing milliseconds.
// Times >= 65535 mSec will be clamped to 65535 mSec, or just over a minute.
//
// It is not required to log every filesystem operation, but it might be useful to track the max times.
#define LOG_WR_TIME     ((LOG_WP_STRT_ADDR) + 0x0010)
#define LOG_WR_TIME_LEN 2
#define LOG_SYNC_TIME   ((LOG_WP_STRT_ADDR) + 0x0011)
#define LOG_SYNC_TIME_LEN 2


#endif
