#if !defined ECU_LOG_H
#define ECU_LOG_H

#include "log_base.h"

// -----------------------------------------------------------------
// Define the log IDs the ECU will be generating.
//
// This file is included by .S and .c files and will be processed by Python scripts,
// so it must only contain #defines!
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
// As of ultraMod4, the fake EPROM can capture log writes to any address in its
// 32K byte address space that are not being used as a RAM window.
// That said, to save logfile space, only the low-order 8 address bits are captured
// when the ECU writes data to the magic logging address block.
// Most log items are associated with 8 data bits, so a typical log entry will be 16 bits:
//   - the low order 8 bits of the specific log address that was written
//   - the 8 bits of data written to that log address
//
// The data associated with each log address is typed:
//  _I16  int16
//  _U16  uint16
//  _I8   int8
//  _U8   uint8
//  _B    boolean (0 means false, and non-zero means true)
//  xPy   fixed point x digits in front of an assumed decimal point with y digits after it
//  0p8     would represent an 8-bit 0.8 fixed point value
//  8p8     would represent a 16-bit 8.8 fixed point value
//  _V    void - the information is the fact that this address got logged and the data is immaterial
//  _S    is it possible to do a 'string' write? Like collect all stringdata until you see a NULL byte

// We will not assign logging address 0x8000 to mean anything.
// For space reasons, we only store the low 8 bits of the address in the log that gets written to the filesystem.
// By not using log address 0x8000, it means that a run of zero bytes can be ignored in the logfile, if that should be useful.

// The symbol LW defines the start address of the 256 byte Log Window.
// It must be on a 256-byte boundary.
#define LW      (0x8000)

// To log a specific bit of data, the code needs to use LOGID_ECU_* as an offset into the log window:
//      STAA    LW+LOGID_ECU_CPU_EVENT_U8

// The LOG_VER refers to the version of this ECU-specific header file.
#define     LOGID_ECU_LOG_VER_V0                    (0x00)

// Except for RESET, all of these events represent bad things going on inside the CPU.
// They mainly represent situations where interrupts handlers got invoked
// which were never expected to be invoked.
#define   LOGID_ECU_CPU_EVENT_U8                    ((LOGID_ECU_BASE) + 0x00)
#define     LOGID_ECU_CPU_EVENT_ID_RTI              (0x0)
#define     LOGID_ECU_CPU_EVENT_ID_IRQ              (0x1)
#define     LOGID_ECU_CPU_EVENT_ID_XIRQ             (0x2)
#define     LOGID_ECU_CPU_EVENT_ID_SWI              (0x3)
#define     LOGID_ECU_CPU_EVENT_ID_IOP              (0x4)
#define     LOGID_ECU_CPU_EVENT_ID_COP              (0x5)
#define     LOGID_ECU_CPU_EVENT_ID_CMF              (0x6)
#define     LOGID_ECU_CPU_EVENT_ID_RESET            (0x7)
#define     LOGID_ECU_CPU_EVENT_ID_OC5F             (0x8)
#define     LOGID_ECU_CPU_EVENT_ID_OC4F             (0x9)
#define     LOGID_ECU_CPU_EVENT_ID_OC3F             (0xA)
#define     LOGID_ECU_CPU_EVENT_ID_IC3              (0xB)

#define   LOGID_ECU_L4000_EVENT_U8                  ((LOGID_ECU_BASE) + 0x08)

#define   LOGID_ECU_F_INJ_ON_TS_U16                 ((LOGID_ECU_BASE) + 0x10)       // timestamp of injector on time in ticks
#define   LOGID_ECU_F_INJ_DUR_U16                   ((LOGID_ECU_BASE) + 0x12)       // duration of injector pulse in ticks
#define   LOGID_ECU_R_INJ_ON_TS_U16                 ((LOGID_ECU_BASE) + 0x14)       // timestamp of injector on time in ticks
#define   LOGID_ECU_R_INJ_DUR_U16                   ((LOGID_ECU_BASE) + 0x16)       // duration of injector pulse in ticks

#define   LOGID_ECU_TS_FRT_COIL_ON_U16              ((LOGID_ECU_BASE) + 0x20)
#define   LOGID_ECU_TS_FRT_COIL_OFF_U16             ((LOGID_ECU_BASE) + 0x22)
#define   LOGID_ECU_TS_REAR_COIL_ON_U16             ((LOGID_ECU_BASE) + 0x24)
#define   LOGID_ECU_TS_REAR_COIL_OFF_U16            ((LOGID_ECU_BASE) + 0x26)

#define   LOGID_ECU_TS_FRT_COIL_MAN_ON_U16          ((LOGID_ECU_BASE) + 0x28)
#define   LOGID_ECU_TS_FRT_COIL_MAN_OFF_U16         ((LOGID_ECU_BASE) + 0x2A)
#define   LOGID_ECU_TS_REAR_COIL_MAN_ON_U16         ((LOGID_ECU_BASE) + 0x2C)
#define   LOGID_ECU_TS_REAR_COIL_MAN_OFF_U16        ((LOGID_ECU_BASE) + 0x2E)

#define   LOGID_ECU_TS_FRT_IGN_DLY_0P8              ((LOGID_ECU_BASE) + 0x30)         // 0.8 fraction of 90 degrees (the value of L00DC during CR3)
#define   LOGID_ECU_TS_REAR_IGN_DLY_0P8             ((LOGID_ECU_BASE) + 0x32)         // 0.8 fraction of 90 degrees (the value of L00DF during CR8)

#define   LOGID_ECU_5MILLISEC_EVENT_V               ((LOGID_ECU_BASE) + 0x38)         // indicates every time the 5 msec routine runs
#define   LOGID_ECU_CRANK_P6_MAX_V                  ((LOGID_ECU_BASE) + 0x39)         // this indicates that the rotational period of the crank was too slow to track
#define   LOGID_ECU_FUEL_PUMP_B                     ((LOGID_ECU_BASE) + 0x3A)         // boolean represents the state of the fuel pump drive: false means pump off, true means pump on

#define   LOGID_ECU_ECU_ERROR_L000C_U8              ((LOGID_ECU_BASE) + 0x40)         // the 8 error bits stored in L000C
#define   LOGID_ECU_ECU_ERROR_L000D_U8              ((LOGID_ECU_BASE) + 0x41)         // the 8 error bits stored in L000D
#define   LOGID_ECU_ECU_ERROR_L000E_U8              ((LOGID_ECU_BASE) + 0x42)         // the 8 error bits stored in L000E
#define   LOGID_ECU_ECU_ERROR_L000F_U8              ((LOGID_ECU_BASE) + 0x43)         // the 8 error bits stored in L000F

#define   LOGID_ECU_RAW_VTA_U16                     ((LOGID_ECU_BASE) + 0x50)         // Throttle angle
#define   LOGID_ECU_RAW_MAP_U8                      ((LOGID_ECU_BASE) + 0x52)         // Manifold air pressure
#define   LOGID_ECU_RAW_AAP_U8                      ((LOGID_ECU_BASE) + 0x53)         // Ambient air pressure
#define   LOGID_ECU_RAW_THW_U8                      ((LOGID_ECU_BASE) + 0x54)         // Coolant Temp
#define   LOGID_ECU_RAW_THA_U8                      ((LOGID_ECU_BASE) + 0x55)         // Air Temp
#define   LOGID_ECU_RAW_VM_U8                       ((LOGID_ECU_BASE) + 0x56)         // Voltage Monitor
#define   LOGID_ECU_PORTG_DB_U8                     ((LOGID_ECU_BASE) + 0x57)         // the state of PORTG (debounced)

#define   LOGID_ECU_TS_CRANKREF_START_U16           ((LOGID_ECU_BASE) + 0x60)         // Timestamp of the start of the most recent crankshaft sub-rotation (6 crankrefs per full crank rotation)
#define   LOGID_ECU_CRANKREF_ID_U8                  ((LOGID_ECU_BASE) + 0x62)         // The ID of the specific crankshaft subroutation (0..11): 2 full rotations == all 4 strokes of the 4-stroke engine

#define   LOGID_ECU_CAMSHAFT_U16                    ((LOGID_ECU_BASE) + 0x63)         // Timestamp of most recent camshaft sensor falling-edge event
#define   LOGID_ECU_CAM_ERR_U8                      ((LOGID_ECU_BASE) + 0x65)         // Error while processing CAM ISR

#endif