#if !defined ULTRAMOD_LOG_H
#define ULTRAMOD_LOG_H

// -----------------------------------------------------------------
// Define all the magic addresses used to send logging information from
// the ECU to the RP2040 fake EPROM.
//
// This file is included by .S and .c files, so it can only contain #defines!
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
//
// The log locations are typed:
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

#define   LOG_BASE                              (0x8000)

// We will not assign logging address 0x8000 to mean anything.
// For space reasons, we only store the low 8 bits of the address in the log that gets written to the filesystem.
// By not using log address 0x8000, it means that a run of zero bytes can be ignored in the logfile, if that should be useful.

// The LOG_VERSION refers to the version of this logfile.
// The version info is a little more complex than a straight 16-bit number
// I think it will get treated as major.minor in an 8.8 format. An increment
// of the major number indicates a backwards-incompatible change.
#define   LOG_LOG_VERSION_U16                   ((LOG_BASE) + 0x0001)
#define   LOG_LOG_VERSION_ID_1V0                (0x0100)

#define   LOG_FW_VERSION_S

// Except for RESET, all of these events represent bad things going on inside the CPU.
// They mainly represent situations where interrupts handlers got invoked
// which were never expected to be invoked.
#define   LOG_CPU_EVENT_U8                      ((LOG_BASE) + 0x0010)
#define     LOG_CPU_EVENT_ID_RTI                (0x0)
#define     LOG_CPU_EVENT_ID_IRQ                (0x1)
#define     LOG_CPU_EVENT_ID_XIRQ               (0x2)
#define     LOG_CPU_EVENT_ID_SWI                (0x3)
#define     LOG_CPU_EVENT_ID_IOP                (0x4)
#define     LOG_CPU_EVENT_ID_COP                (0x5)
#define     LOG_CPU_EVENT_ID_CMF                (0x6)
#define     LOG_CPU_EVENT_ID_RESET              (0x7)
#define     LOG_CPU_EVENT_ID_OC5F               (0x8)
#define     LOG_CPU_EVENT_ID_OC4F               (0x9)
#define     LOG_CPU_EVENT_ID_OC3F               (0xA)
#define     LOG_CPU_EVENT_ID_IC3                (0xB)

#define     LOG_L4000_EVENT_U8                  ((LOG_BASE) + 0x0011)

#define   LOG_TS_FRT_INJ_ON_U16                 ((LOG_BASE) + 0x0020)
#define   LOG_TS_FRT_INJ_OFF_U16                ((LOG_BASE) + 0x0022)
#define   LOG_TS_REAR_INJ_ON_U16                ((LOG_BASE) + 0x0024)
#define   LOG_TS_REAR_INJ_OFF_U16               ((LOG_BASE) + 0x0026)

#define   LOG_TS_FRT_COIL_ON_U16                ((LOG_BASE) + 0x0030)
#define   LOG_TS_FRT_COIL_OFF_U16               ((LOG_BASE) + 0x0032)
#define   LOG_TS_REAR_COIL_ON_U16               ((LOG_BASE) + 0x0034)
#define   LOG_TS_REAR_COIL_OFF_U16              ((LOG_BASE) + 0x0036)

#define   LOG_TS_FRT_COIL_MAN_ON_U16            ((LOG_BASE) + 0x0040)
#define   LOG_TS_FRT_COIL_MAN_OFF_U16           ((LOG_BASE) + 0x0042)
#define   LOG_TS_REAR_COIL_MAN_ON_U16           ((LOG_BASE) + 0x0044)
#define   LOG_TS_REAR_COIL_MAN_OFF_U16          ((LOG_BASE) + 0x0046)

#define   LOG_TS_FRT_IGN_DLY_0P8                ((LOG_BASE) + 0x0050)           // 0.8 fraction of 90 degrees (the value of L00DC during CR3)
#define   LOG_TS_REAR_IGN_DLY_0P8               ((LOG_BASE) + 0x0052)           // 0.8 fraction of 90 degrees (the value of L00DF during CR8)

#define   LOG_5MILLISEC_EVENT_V                 ((LOG_BASE) + 0x0060)           // indicates every time the 5 msec routine runs
#define   LOG_CRANK_P6_MAX_V                    ((LOG_BASE) + 0x0061)           // this indicates that the rotational period of the crank was too slow to track
#define   LOG_FUEL_PUMP_B                       ((LOG_BASE) + 0x0062)           // boolean represents the state of the fuel pump drive: false means pump off, true means pump on

#define   LOG_ECU_ERROR_L000C_U8                ((LOG_BASE) + 0x0070)           // the 8 error bits stored in L000C
#define   LOG_ECU_ERROR_L000D_U8                ((LOG_BASE) + 0x0071)           // the 8 error bits stored in L000D
#define   LOG_ECU_ERROR_L000E_U8                ((LOG_BASE) + 0x0072)           // the 8 error bits stored in L000E
#define   LOG_ECU_ERROR_L000F_U8                ((LOG_BASE) + 0x0073)           // the 8 error bits stored in L000F

#define   LOG_RAW_VTA_U16                       ((LOG_BASE) + 0x0080)           // Throttle angle
#define   LOG_RAW_MAP_U8                        ((LOG_BASE) + 0x0082)           // Manifold air pressure
#define   LOG_RAW_AAP_U8                        ((LOG_BASE) + 0x0083)           // Ambient air pressure
#define   LOG_RAW_THW_U8                        ((LOG_BASE) + 0x0084)           // Coolant Temp
#define   LOG_RAW_THA_U8                        ((LOG_BASE) + 0x0085)           // Air Temp
#define   LOG_RAW_VM_U8                         ((LOG_BASE) + 0x0086)           // Voltage Monitor
#define   LOG_PORTG_DB_U8                       ((LOG_BASE) + 0x0087)           // the state of PORTG (debounced)

#define   LOG_TS_CRANKREF_START_U16             ((LOG_BASE) + 0x0090)           // Timestamp of the start of the most recent crankshaft sub-rotation (6 crankrefs per full crank rotation)
#define   LOG_CRANKREF_ID_U8                    ((LOG_BASE) + 0x0092)           // The ID of the specific crankshaft subroutation (0..11): 2 full rotations == all 4 strokes of the 4-stroke engine

#define   LOG_LAST_ADDR                         ((LOG_BASE) + 0x00FF)

#endif