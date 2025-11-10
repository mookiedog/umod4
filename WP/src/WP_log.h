#if !defined WP_LOG_H
#define WP_LOG_H

#include "log_base.h"

#define   LOGID_GEN_WP_LOG_VER_V0                   (0x00)                          // Version 0

#define LOGID_WP_CSECS_TYPE_U8                           ((LOGID_WP_BASE) + 0x01)
#define LOGID_WP_CSECS_DLEN                         1

#define LOGID_WP_SECS_TYPE_U8                            ((LOGID_WP_BASE) + 0x02)
#define LOGID_WP_SECS_DLEN                          1

#define LOGID_WP_MINS_TYPE_U8                            ((LOGID_WP_BASE) + 0x03)
#define LOGID_WP_MINS_DLEN                          1

#define LOGID_WP_HOURS_TYPE_U8                           ((LOGID_WP_BASE) + 0x04)
#define LOGID_WP_HOURS_DLEN                         1

#define LOGID_WP_DATE_TYPE_U8                            ((LOGID_WP_BASE) + 0x05)
#define LOGID_WP_DATE_DLEN                          1

#define LOGID_WP_MONTH_TYPE_U8                           ((LOGID_WP_BASE) + 0x06)
#define LOGID_WP_MONTH_DLEN                         1

#define LOGID_WP_YEAR_TYPE_U8                            ((LOGID_WP_BASE) + 0x07)
#define LOGID_WP_YEAR_DLEN                          1

#define LOGID_WP_FIXTYPE_TYPE_U8                         ((LOGID_WP_BASE) + 0x08)
#define LOGID_WP_FIXTYPE_DLEN                       1

// This is packed as 1 big message to save 20 bytes per second getting logged
// by not storing separate headers for lat/lon/velo at 10 Hz report rate.
// That might be sort of pointless in the grand scheme of things though.
// 4-byte lat, 4-byte lon, 2 byte velo
#define LOGID_WP_PV                                 ((LOGID_WP_BASE) + 0x09)
#define LOGID_WP_PV_DLEN                            (4+4+2)

// Log filesystem IO operations.
// Time will be stored as 2 bytes representing milliseconds.
// Times >= 65535 mSec will be clamped to 65535 mSec, or just over a minute.
//
// It is not required to log every filesystem operation, but it might be useful to track the max times.
#define LOGID_WP_WR_TIME_U16                        ((LOGID_WP_BASE) + 0x0A)
#define LOGID_WP_WR_TIME_DLEN                       2
#define LOGID_WP_SYNC_TIME_U16                      ((LOGID_WP_BASE) + 0x0B)
#define LOGID_WP_SYNC_TIME_DLEN                     2

#endif
