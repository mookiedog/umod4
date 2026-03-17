#include <stddef.h>
#include "log_meta.h"
#include "log_ids.h"

// Helper macros to keep the table concise
#define META_DISP(n, u)  { (n), (u), true  }
#define META_HIDE(n)     { (n), NULL, false }

// 256-entry metadata table, indexed by log ID byte.
// C99 designated initialisers — all unspecified entries are zero-initialised
// {NULL, NULL, false}, meaning "undefined log ID".
const log_id_meta_t g_log_id_meta[256] = {

    // ── GEN ──────────────────────────────────────────────────────────────
    [LOGID_GEN_ECU_LOG_VER_TYPE_U8]  = META_HIDE("ECU Log Version"),
    [LOGID_GEN_EP_LOG_VER_TYPE_U8]   = META_HIDE("EP Log Version"),
    [LOGID_GEN_WP_LOG_VER_TYPE_U8]   = META_HIDE("WP Log Version"),

    // ── ECU — timing / events (not suitable for live numeric display) ─────
    [LOGID_ECU_CPU_EVENT_TYPE_U8]        = META_HIDE("CPU Event"),
    [LOGID_ECU_T1_OFLO_TYPE_TS]          = META_HIDE("Timer Overflow"),
    [LOGID_ECU_F_INJ_ON_TYPE_PTS]        = META_HIDE("Front Inj ON"),
    [LOGID_ECU_R_INJ_ON_TYPE_PTS]        = META_HIDE("Rear Inj ON"),
    [LOGID_ECU_F_COIL_ON_TYPE_PTS]       = META_HIDE("Front Coil ON"),
    [LOGID_ECU_F_COIL_OFF_TYPE_PTS]      = META_HIDE("Front Coil OFF"),
    [LOGID_ECU_R_COIL_ON_TYPE_PTS]       = META_HIDE("Rear Coil ON"),
    [LOGID_ECU_R_COIL_OFF_TYPE_PTS]      = META_HIDE("Rear Coil OFF"),
    [LOGID_ECU_CRANKREF_START_TYPE_TS]   = META_HIDE("Crank Ref Start"),
    [LOGID_ECU_CAMSHAFT_TYPE_TS]         = META_HIDE("Camshaft Event"),
    [LOGID_ECU_SPRK_X1_TYPE_PTS]         = META_HIDE("Spark X1"),
    [LOGID_ECU_SPRK_X2_TYPE_PTS]         = META_HIDE("Spark X2"),

    // ── ECU — ignition timing ─────────────────────────────────────────────
    [LOGID_ECU_F_IGN_DLY_TYPE_0P8]  = META_DISP(LOGID_ECU_F_IGN_DLY_NAME, LOGID_ECU_F_IGN_DLY_UNITS),
    [LOGID_ECU_R_IGN_DLY_TYPE_0P8]  = META_DISP(LOGID_ECU_R_IGN_DLY_NAME, LOGID_ECU_R_IGN_DLY_UNITS),

    // ── ECU — error flags ─────────────────────────────────────────────────
    [LOGID_ECU_ECU_ERROR_L000C_TYPE_U8] = META_DISP(LOGID_ECU_ECU_ERROR_L000C_NAME, LOGID_ECU_ECU_ERROR_L000C_UNITS),
    [LOGID_ECU_ECU_ERROR_L000D_TYPE_U8] = META_DISP(LOGID_ECU_ECU_ERROR_L000D_NAME, LOGID_ECU_ECU_ERROR_L000D_UNITS),
    [LOGID_ECU_ECU_ERROR_L000E_TYPE_U8] = META_DISP(LOGID_ECU_ECU_ERROR_L000E_NAME, LOGID_ECU_ECU_ERROR_L000E_UNITS),
    [LOGID_ECU_ECU_ERROR_L000F_TYPE_U8] = META_DISP(LOGID_ECU_ECU_ERROR_L000F_NAME, LOGID_ECU_ECU_ERROR_L000F_UNITS),

    // ── ECU — trim pots ───────────────────────────────────────────────────
    [LOGID_ECU_TP_CO1_DB_TYPE_U8]   = META_DISP(LOGID_ECU_TP_CO1_DB_NAME, LOGID_ECU_TP_CO1_DB_UNITS),
    [LOGID_ECU_TP_CO2_DB_TYPE_U8]   = META_DISP(LOGID_ECU_TP_CO2_DB_NAME, LOGID_ECU_TP_CO2_DB_UNITS),

    // ── ECU — sensors ─────────────────────────────────────────────────────
    [LOGID_ECU_RAW_VTA_TYPE_U16]    = META_DISP(LOGID_ECU_RAW_VTA_NAME, LOGID_ECU_RAW_VTA_UNITS),
    [LOGID_ECU_RAW_MAP_TYPE_U8]     = META_DISP(LOGID_ECU_RAW_MAP_NAME, LOGID_ECU_RAW_MAP_UNITS),
    [LOGID_ECU_RAW_AAP_TYPE_U8]     = META_DISP(LOGID_ECU_RAW_AAP_NAME, LOGID_ECU_RAW_AAP_UNITS),
    [LOGID_ECU_RAW_THW_TYPE_U8]     = META_DISP(LOGID_ECU_RAW_THW_NAME, LOGID_ECU_RAW_THW_UNITS),
    [LOGID_ECU_RAW_THA_TYPE_U8]     = META_DISP(LOGID_ECU_RAW_THA_NAME, LOGID_ECU_RAW_THA_UNITS),
    [LOGID_ECU_RAW_VM_TYPE_U8]      = META_DISP(LOGID_ECU_RAW_VM_NAME,  LOGID_ECU_RAW_VM_UNITS),
    [LOGID_ECU_PORTG_DB_TYPE_U8]    = META_HIDE(LOGID_ECU_PORTG_DB_NAME),

    // ── ECU — error counts ────────────────────────────────────────────────
    [LOGID_ECU_CAM_ERR_TYPE_U8]     = META_DISP(LOGID_ECU_CAM_ERR_NAME,  LOGID_ECU_CAM_ERR_UNITS),
    [LOGID_ECU_NOSPARK_TYPE_U8]     = META_DISP(LOGID_ECU_NOSPARK_NAME,  LOGID_ECU_NOSPARK_UNITS),

    // ── WP — GPS time (not suitable for live display) ─────────────────────
    [LOGID_WP_CSECS_TYPE_U8]        = META_HIDE(LOGID_WP_CSECS_NAME),
    [LOGID_WP_SECS_TYPE_U8]         = META_HIDE(LOGID_WP_SECS_NAME),
    [LOGID_WP_MINS_TYPE_U8]         = META_HIDE(LOGID_WP_MINS_NAME),
    [LOGID_WP_HOURS_TYPE_U8]        = META_HIDE(LOGID_WP_HOURS_NAME),

    // ── WP — GPS fix / velocity ───────────────────────────────────────────
    [LOGID_WP_FIXTYPE_TYPE_U8]      = META_DISP(LOGID_WP_FIXTYPE_NAME,   LOGID_WP_FIXTYPE_UNITS),
    [LOGID_WP_GPS_VELO_TYPE_U16]    = META_HIDE(LOGID_WP_GPS_VELO_NAME),
};
