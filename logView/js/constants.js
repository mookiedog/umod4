// constants.js - LOGID definitions and lookup tables
// Exported from ECU_log.h, EP_log.h, WP_log.h, log_base.h

export const LOGID = {
    // Base addresses
    GEN_BASE: 0x00,
    ECU_BASE: 0x10,
    EP_BASE: 0xD0,
    WP_BASE: 0xE0,

    // General IDs
    GEN_ECU_LOG_VER: 0x01,
    GEN_EP_LOG_VER: 0x02,
    GEN_WP_LOG_VER: 0x03,

    // ECU IDs (complete list from ECU_log.h)
    ECU_CPU_EVENT: 0x10,
    ECU_T1_OFLO: 0x12,
    ECU_L4000_EVENT: 0x18,
    ECU_T1_HOFLO: 0x1E,
    ECU_F_INJ_ON: 0x20,
    ECU_F_INJ_DUR: 0x22,
    ECU_R_INJ_ON: 0x24,
    ECU_R_INJ_DUR: 0x26,
    ECU_F_COIL_ON: 0x30,
    ECU_F_COIL_OFF: 0x32,
    ECU_R_COIL_ON: 0x34,
    ECU_R_COIL_OFF: 0x36,
    ECU_F_COIL_MAN_ON: 0x38,
    ECU_F_COIL_MAN_OFF: 0x3A,
    ECU_R_COIL_MAN_ON: 0x3C,
    ECU_R_COIL_MAN_OFF: 0x3E,
    ECU_F_IGN_DLY: 0x40,
    ECU_R_IGN_DLY: 0x42,
    ECU_5MILLISEC_EVENT: 0x48,
    ECU_CRANK_P6_MAX: 0x49,
    ECU_FUEL_PUMP: 0x4A,
    ECU_ERROR_L000C: 0x50,
    ECU_ERROR_L000D: 0x51,
    ECU_ERROR_L000E: 0x52,
    ECU_ERROR_L000F: 0x53,
    ECU_RAW_VTA: 0x60,
    ECU_RAW_MAP: 0x62,
    ECU_RAW_AAP: 0x63,
    ECU_RAW_THW: 0x64,
    ECU_RAW_THA: 0x65,
    ECU_RAW_VM: 0x66,
    ECU_PORTG_DB: 0x67,
    ECU_CRANKREF_START: 0x70,
    ECU_CRANKREF_ID: 0x72,
    ECU_CAM_ERR: 0x73,
    ECU_CAMSHAFT: 0x74,
    ECU_SPRK_X1: 0x80,
    ECU_SPRK_X2: 0x82,
    ECU_NOSPARK: 0x84,

    // EP IDs
    EP_LOAD_NAME: 0xD0,
    EP_FIND_NAME: 0xD1,
    EP_LOAD_ADDR: 0xD2,
    EP_LOAD_LEN: 0xD4,
    EP_LOAD_ERR: 0xD6,

    // WP IDs (complete list from WP_log.h)
    WP_CSECS: 0xE1,
    WP_SECS: 0xE2,
    WP_MINS: 0xE3,
    WP_HOURS: 0xE4,
    WP_DATE: 0xE5,
    WP_MONTH: 0xE6,
    WP_YEAR: 0xE7,
    WP_FIXTYPE: 0xE8,
    WP_GPS_POSN: 0xE9,
    WP_GPS_VELO: 0xEA,
    WP_GPS_PPS: 0xEB,
    WP_WR_TIME: 0xEC,
    WP_SYNC_TIME: 0xED,
};

// CPU Event names
export const ECU_CPU_EVENT_NAMES = {
    0x0: 'RTI',
    0x1: 'IRQ',
    0x2: 'XIRQ',
    0x3: 'SWI',
    0x4: 'IOP',
    0x5: 'COP',
    0x6: 'CMF',
    0x7: 'RESET',
    0x8: 'OC5F',
    0x9: 'OC4F',
    0xA: 'OC3F',
    0xB: 'IC3'
};

// Error code names for EP_LOAD_ERR
export const EP_LOAD_ERR_NAMES = {
    0x00: 'ERR_NOERR',
    0x01: 'ERR_NOTFOUND',
    0x02: 'ERR_NONAME',
    0x03: 'ERR_CKSUMERR',
    0x04: 'ERR_VERIFYERR',
    0x05: 'ERR_BADOFFSET',
};

// Binary display configuration
export const BYTES_PER_LINE = 4;  // Number of bytes to display per line in binary mode
