#if !defined ECU_LOG_H
#define ECU_LOG_H

#include "log_base.h"

// -----------------------------------------------------------------
// Define the log IDs the ECU will be generating.
//
// As of ultraMod4, the logging hardware is capable of capturing every single
// write cycle showing up at the EPROM, even if writes occur on adjacent bus
// cycles as occurs with STD instructions that write 16 bits of data
// as two successive writes. For example, a STD to address X would write:
//    addr X+0: contents of A (high byte)
//    addr X+1: contents of B (low byte)
//
// To write a single 8-bit value, use STA or STB to the proper log address.
//

// The LOG_VER refers to the version of this ECU-specific header file.
#define     LOGID_GEN_ECU_LOG_VER_VAL_V0            (0x00)      // Value constant for LOGID_GEN_ECU_LOG_VER_U8

// Except for RESET, all of these events represent bad things going on inside the CPU.
// They mainly represent situations where interrupts handlers got invoked
// which were never expected to be invoked.
#define   LOGID_ECU_CPU_EVENT_TYPE_U8               ((LOGID_ECU_BASE) + 0x00)
#define   LOGID_ECU_CPU_EVENT_DLEN                  1
// Value constants for LOGID_ECU_CPU_EVENT_TYPE_U8:
#define     LOGID_ECU_CPU_EVENT_VAL_RTI             (0x0)
#define     LOGID_ECU_CPU_EVENT_VAL_IRQ             (0x1)
#define     LOGID_ECU_CPU_EVENT_VAL_XIRQ            (0x2)
#define     LOGID_ECU_CPU_EVENT_VAL_SWI             (0x3)
#define     LOGID_ECU_CPU_EVENT_VAL_IOP             (0x4)
#define     LOGID_ECU_CPU_EVENT_VAL_COP             (0x5)
#define     LOGID_ECU_CPU_EVENT_VAL_CMF             (0x6)
#define     LOGID_ECU_CPU_EVENT_VAL_RESET           (0x7)
#define     LOGID_ECU_CPU_EVENT_VAL_OC5F            (0x8)
#define     LOGID_ECU_CPU_EVENT_VAL_OC4F            (0x9)
#define     LOGID_ECU_CPU_EVENT_VAL_OC3F            (0xA)
#define     LOGID_ECU_CPU_EVENT_VAL_IC3             (0xB)

#define   LOGID_ECU_T1_OFLO_TYPE_TS                 ((LOGID_ECU_BASE) + 0x02)       // Emitted just after time T1 overflows (every 131072 uSec, or ~7.6Hz )
#define   LOGID_ECU_T1_OFLO_DLEN                    2

#define   LOGID_ECU_L4000_EVENT_TYPE_U8             ((LOGID_ECU_BASE) + 0x08)
#define   LOGID_ECU_L4000_EVENT_DLEN                1

#define   LOGID_ECU_T1_HOFLO_TYPE_TS                ((LOGID_ECU_BASE) + 0x0E)       // Marks when the ECU has observed a 'half overflow',
#define   LOGID_ECU_T1_HOFLO_TYPE_DLEN              2                               // meaning the timer has advanced into the upper 15 bits of the timer count


// Injector events, front and rear
#define   LOGID_ECU_F_INJ_ON_TYPE_PTS               ((LOGID_ECU_BASE) + 0x10)       // timestamp of injector on time in ticks
#define   LOGID_ECU_F_INJ_ON_DLEN                   2

#define   LOGID_ECU_F_INJ_DUR_TYPE_U16              ((LOGID_ECU_BASE) + 0x12)       // duration of injector pulse in ticks
#define   LOGID_ECU_F_INJ_DUR_DLEN                  2

#define   LOGID_ECU_R_INJ_ON_TYPE_PTS               ((LOGID_ECU_BASE) + 0x14)       // timestamp of injector on time in ticks
#define   LOGID_ECU_R_INJ_ON_DLEN                   2

#define   LOGID_ECU_R_INJ_DUR_TYPE_U16              ((LOGID_ECU_BASE) + 0x16)       // duration of injector pulse in ticks
#define   LOGID_ECU_R_INJ_DUR_DLEN                  2

// Ignition coil events, front and rear
#define   LOGID_ECU_F_COIL_ON_TYPE_PTS              ((LOGID_ECU_BASE) + 0x20)
#define   LOGID_ECU_F_COIL_ON_DLEN                  2

#define   LOGID_ECU_F_COIL_OFF_TYPE_PTS             ((LOGID_ECU_BASE) + 0x22)
#define   LOGID_ECU_F_COIL_OFF_DLEN                 2

#define   LOGID_ECU_R_COIL_ON_TYPE_PTS              ((LOGID_ECU_BASE) + 0x24)
#define   LOGID_ECU_R_COIL_ON_DLEN                  2

#define   LOGID_ECU_R_COIL_OFF_TYPE_PTS             ((LOGID_ECU_BASE) + 0x26)
#define   LOGID_ECU_R_COIL_OFF_DLEN                 2

#define   LOGID_ECU_F_COIL_MAN_ON_TYPE_PTS          ((LOGID_ECU_BASE) + 0x28)
#define   LOGID_ECU_F_COIL_MAN_ON_DLEN              2

#define   LOGID_ECU_F_COIL_MAN_OFF_TYPE_PTS         ((LOGID_ECU_BASE) + 0x2A)
#define   LOGID_ECU_F_COIL_MAN_OFF_DLEN             2

#define   LOGID_ECU_R_COIL_MAN_ON_TYPE_PTS          ((LOGID_ECU_BASE) + 0x2C)
#define   LOGID_ECU_R_COIL_MAN_ON_DLEN              2

#define   LOGID_ECU_R_COIL_MAN_OFF_TYPE_PTS         ((LOGID_ECU_BASE) + 0x2E)
#define   LOGID_ECU_R_COIL_MAN_OFF_DLEN             2

#define   LOGID_ECU_F_IGN_DLY_TYPE_0P8              ((LOGID_ECU_BASE) + 0x30)           // 0.8 fraction of 90 degrees (the value of L00DC during CR3)
#define   LOGID_ECU_F_IGN_DLY_DLEN                  1

#define   LOGID_ECU_R_IGN_DLY_TYPE_0P8              ((LOGID_ECU_BASE) + 0x32)           // 0.8 fraction of 90 degrees (the value of L00DF during CR8)
#define   LOGID_ECU_R_IGN_DLY_DLEN                  1

// Here are various other ungrouped events:
#define   LOGID_ECU_5MILLISEC_EVENT_TYPE_V          ((LOGID_ECU_BASE) + 0x38)           // indicates every time the 5 msec routine runs
#define   LOGID_ECU_5MILLISEC_EVENT_DLEN            1                                   // ignore this garbage byte

#define   LOGID_ECU_CRANK_P6_MAX_TYPE_V             ((LOGID_ECU_BASE) + 0x39)           // this indicates that the rotational period of the crank was too slow to track
#define   LOGID_ECU_CRANK_P6_MAX_DLEN               1                                   // ignore this garbage byte

#define   LOGID_ECU_FUEL_PUMP_TYPE_B                ((LOGID_ECU_BASE) + 0x3A)           // boolean represents the state of the fuel pump drive: false means pump off, true means pump on
#define   LOGID_ECU_FUEL_PUMP_DLEN                  1

#define   LOGID_ECU_ECU_ERROR_L000C_TYPE_U8         ((LOGID_ECU_BASE) + 0x40)           // the 8 error bits stored in L000C
#define   LOGID_ECU_ECU_ERROR_L000C_DLEN            1

#define   LOGID_ECU_ECU_ERROR_L000D_TYPE_U8         ((LOGID_ECU_BASE) + 0x41)           // the 8 error bits stored in L000D
#define   LOGID_ECU_ECU_ERROR_L000D_DLEN            1

#define   LOGID_ECU_ECU_ERROR_L000E_TYPE_U8         ((LOGID_ECU_BASE) + 0x42)           // the 8 error bits stored in L000E
#define   LOGID_ECU_ECU_ERROR_L000E_DLEN            1

#define   LOGID_ECU_ECU_ERROR_L000F_TYPE_U8         ((LOGID_ECU_BASE) + 0x43)           // the 8 error bits stored in L000F
#define   LOGID_ECU_ECU_ERROR_L000F_DLEN            1

#define   LOGID_ECU_TP_CO1_RAW_TYPE_U8              ((LOGID_ECU_BASE) + 0x44)           // Trim Pot CO1 raw ADC value
#define   LOGID_ECU_TP_CO1_RAW_DLEN                 1

#define   LOGID_ECU_TP_CO2_RAW_TYPE_U8              ((LOGID_ECU_BASE) + 0x45)           // Trim Pot CO2 raw ADC value
#define   LOGID_ECU_TP_CO2_RAW_DLEN                 1

#define   LOGID_ECU_TP_CO1_DB_TYPE_U8               ((LOGID_ECU_BASE) + 0x46)           // Trim Pot CO1 ADC value after deadband processing
#define   LOGID_ECU_TP_CO1_DB_DLEN                  1

#define   LOGID_ECU_TP_CO2_DB_TYPE_U8               ((LOGID_ECU_BASE) + 0x47)           // Trim Pot CO2 ADC value after deadband processing
#define   LOGID_ECU_TP_CO2_DB_DLEN                  1

#define   LOGID_ECU_TP_RPM_FACTOR_TYPE_U8           ((LOGID_ECU_BASE) + 0x48)           // RPM factor for trimp pot calculations
#define   LOGID_ECU_TP_RPM_FACTOR_DLEN              1

#define   LOGID_ECU_TP_CO1_ADJ_FACTOR_TYPE_U8       ((LOGID_ECU_BASE) + 0x49)           // Fuel trim adjustment factor from trim pot CO1
#define   LOGID_ECU_TP_CO1_ADJ_FACTOR_DLEN          1

#define   LOGID_ECU_TP_CO2_ADJ_FACTOR_TYPE_U8       ((LOGID_ECU_BASE) + 0x4A)           // Fuel trim adjustment factor from trim pot CO2
#define   LOGID_ECU_TP_CO2_ADJ_FACTOR_DLEN          1


#define   LOGID_ECU_RAW_VTA_TYPE_U16                ((LOGID_ECU_BASE) + 0x50)           // Throttle angle
#define   LOGID_ECU_RAW_VTA_DLEN                    2

#define   LOGID_ECU_RAW_MAP_TYPE_U8                 ((LOGID_ECU_BASE) + 0x52)           // Manifold air pressure
#define   LOGID_ECU_RAW_MAP_DLEN                    1

#define   LOGID_ECU_RAW_AAP_TYPE_U8                 ((LOGID_ECU_BASE) + 0x53)           // Ambient air pressure
#define   LOGID_ECU_RAW_AAP_DLEN                    1

#define   LOGID_ECU_RAW_THW_TYPE_U8                 ((LOGID_ECU_BASE) + 0x54)           // Coolant Temp
#define   LOGID_ECU_RAW_THW_DLEN                    1

#define   LOGID_ECU_RAW_THA_TYPE_U8                 ((LOGID_ECU_BASE) + 0x55)           // Air Temp
#define   LOGID_ECU_RAW_THA_DLEN                    1

#define   LOGID_ECU_RAW_VM_TYPE_U8                  ((LOGID_ECU_BASE) + 0x56)           // Voltage Monitor
#define   LOGID_ECU_RAW_VM_DLEN                     1

#define   LOGID_ECU_PORTG_DB_TYPE_U8                ((LOGID_ECU_BASE) + 0x57)           // the state of PORTG (debounced)
#define   LOGID_ECU_PORTG_DB_DLEN                   1

#define   LOGID_ECU_CRANKREF_START_TYPE_TS          ((LOGID_ECU_BASE) + 0x60)           // Timestamp of the start of the most recent crankshaft sub-rotation (6 crankrefs per full crank rotation)
#define   LOGID_ECU_CRANKREF_START_DLEN             2

#define   LOGID_ECU_CRANKREF_ID_TYPE_U8             ((LOGID_ECU_BASE) + 0x62)           // The ID of the specific crankshaft subroutation (0..11): 2 full rotations == all 4 strokes of the 4-stroke engine
#define   LOGID_ECU_CRANKREF_ID_DLEN                1

#define   LOGID_ECU_CAM_ERR_TYPE_U8                 ((LOGID_ECU_BASE) + 0x63)           // Error while processing CAM ISR
#define   LOGID_ECU_CAM_ERR_DLEN                    1

#define   LOGID_ECU_CAMSHAFT_TYPE_TS                ((LOGID_ECU_BASE) + 0x64)           // Timestamp of most recent camshaft sensor falling-edge event
#define   LOGID_ECU_CAMSHAFT_DLEN                   2

#define   LOGID_ECU_SPRK_X1_TYPE_PTS                ((LOGID_ECU_BASE) + 0x70)           // The time when front OR rear coil #1 fired
#define   LOGID_ECU_SPRK_X1_DLEN                    2                                   // By design, this is reported during the CR period AFTER it occurred

#define   LOGID_ECU_SPRK_X2_TYPE_PTS                ((LOGID_ECU_BASE) + 0x72)           // The time when front OR rear coil #2 fired
#define   LOGID_ECU_SPRK_X2_DLEN                    2                                   // By design, this is reported during the CR period AFTER it occurred

#define   LOGID_ECU_NOSPARK_TYPE_U8                 ((LOGID_ECU_BASE) + 0x74)           // U8 value indicates the coil that did not spark: 0x11, 0x12, 0x21, 0x22
#define   LOGID_ECU_NOSPARK_DLEN                    1

#endif