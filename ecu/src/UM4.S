;-----------------------------------------------------------------
; This is the HC11 ECU firmware to be used in conjunction with an UltraMod4 board.
;
; It is based off the original Aprilia 549USA firmware, which itself
; was based off the Aprilia RP58 firmware.
;
; The RP58 firmware contained a set of restricted and derestricted maps stored
; in different areas inside the EPROM. By default, the restricted maps were used.
; The derestricted maps were enabled by cutting the MS1 wire going into the ECU.
;
; The 549USA contained a copy of the restricted maps in both EPROM areas so cutting
; the MS1 wire effectively didn't do anything.


        #include "ECU_log.h"

; The symbol LW defines the start address of the 256 byte Log Window in the HC11 address space.
; It must be on a 256-byte boundary.
#define LW      (0x8000)

; To log a specific bit of data, the code needs to use LOGID_ECU_* as an offset into
; the log window. For example:
;      staA    LW+LOGID_ECU_CPU_EVENT_TYPE_U8


; Define build options here

; If defined, use explicit IC3 and IC4 SPARK ISR routines to report spark events instead of checking during crankshaft reporting
#define SPARK_ISR       1
#define VTA_INCLUDES_TS 1

; Set RELOCATED_STACK to '1' to relocate the stack inside the EPROM address space. Only available if
; this firmware is running on a umod4 PCB. If this firmware is to be burned into
; an EPROM, this must be '0'
#define RELOCATED_STACK 0


#if defined RELOCATED_STACK && (RELOCATED_STACK != 0)
; The ram window inside the EPROM goes from FF00..FFBB (FFBC..FFFF is the vector table)
; Our new stack area in the RAM window will be 192 bytes long, way larger than the original
; stack that was about 128 bytes.
; Note that the stack is in a RAM window surrounded by ROM. If the stack ever were to go outside
; of its RAM window, the system would crash instantly. That's a much better situation
; than having the stack silently clobber some other RAM location.

; For the initial testing, the start of the RAM window and its length are hardcoded into the EP build.
; ToDo: It would be better to bake the info into the EPROM image itself somehow so that when the EP
; loaded a UM4 image, it would be able to set up the RAM window as per that image's specific requirements.
fakeRamStart = 0xFF00
; Stack is stored in the new EPROM RAM window
STACK_TOP    = 0xFFBB

#else
; Stack is stored in the internal RAM, like normal
STACK_TOP    = 0x01FF

#endif

;-----------------------------------------------------------------
; EPROM Memory Map
;
;    $8000..$9BD8 ( 7129 bytes) Map data
;    $9BD9..$9FDF ( 1031 bytes) unused space, set to $3F
;    $9FE0..$9FFA (   27 bytes) diag code table
;    $9FFB..$C002 ( 8200 bytes) is unused, set to $3F
;    $C003..$FFFF (16381 bytes) is code, although the last 1800-ish bytes are not currently used.
;                 -------------
;                  32768 bytes total
;
; The UM4 firmware will make sure to not do anything that moves the tables from
; their stock locations. This ensures that external tools or DXF map definitions
; will always find the maps where they expect to find them. When swapping maps between
; different [compatible] codebases, the map area can be treated as an opaque blob
; corresponding to the first 7129 bytes of the EPROM address space.
;
; In contrast, the code area can grow, shrink, and move around without affecting anything,
; including external tools.
; The code area traditionally starts at $C003, but it could start anywhere after the end
; of the map data, as long as the diag code table does not get moved. Actually, it might be
; possible that the diag code table could be moved if that helped things.
; Ultimately, there is space for over 25K of code if it were ever needed.

;-----------------------------------------------------------------
;To Do:

; - Figure out what variable sets the Ignition delay.  Work backwards
;   to figure out how it is generated.

; - Figure out what variable sets the fuel pulse width.  Work
;   backwards to figure out how it is generated.

; - Figure out the rest of the RPM-related variables.

; - Figure out what AN2 is connected to.  My schematics do not show it,
;   so how did I figure out that it is AAP?

;-----------------------------------------------------------------
;Want to know:


; -----------------------------------------------------------------
; CRID and Ignition Timing
;
; Facts: the Rotax V990 Shop Manual p 2-6 (Aprilia PN 8140587) states:
;    1. When the bike is being started, ignition advance is 5 degrees BTDC
;    2. At 2800 RPM, ignition advance is 21.8 degrees, +-2 degrees
;
; Measurements:
;    1. Diameter of rotor at top of protrusions: 150.7mm
;    2. width of protrusions at top face: 9.15mm
;
; Calculations:
;    1. Circumference at top of protrusions: Pi*150.7mm = 473.44mm
;    2. Width of protrusion in degrees: (9.15mm * 360deg)/473.44mm = 6.96 degrees
;       I would bet that the protrusions are designed to be 7 degrees wide.
;
; Putting it together:
;    If the protrusions are 7 degrees wide, and the trailing edge of CR5 is fixed at 5 degrees BTDC,
;    the leading edge of CR5 must be 12 degrees BTDC.

; The relationship of TDC to CR events:
; CR0
; CR1
; CR2
; CR3
; CR4    72 BTDC of front cyl
; CR5    12 BTDC of front cyl
; CR6
; CR7
; CR8
; CR9    72 BTDC of rear cyl
; CR10   12 BTDC of rear cyl
; CR11

;-----------------------------------------------------------------


; Originally disassembled using:
; DHC11 - 68HC11 Disassembler v1.1 (c) Copyright 2000 Tech Edge Pty. Ltd.

;               http://www.techedge.com.au/utils/dhc11.htm


; Disassembly of 549us.bin - from 0x8000 to 0xFFFF

; A reminder:
;       BHI, BHS, BLO, BLS are for unsigned comparisons (BHS=BCC, BLO=BCS)
;       BGT, BGE, BLT, BLE are for   signed comparisons
;
; Interestingly, signed comparisons are not used anywhere in the original codebase.

        .text
        .org     0x0000                 ; the linker will take care of relocating this code to $8000



;*****************************************************************
;Global RAM Usage

; Page 0 RAM
; The following locations on page0 appear to be completely unused.
;       L0009
;       L0013
;       L0014
;       L004B
;       L004C
; there might be more after L004C

;-----------------------------------------------------------------
;xxxxxxx1               This bit tracks the PG1 input via a threshold count
;xxxxxx1x               1 is set every time the engine speed is observed to be >= 800 RPM, cleared on RESEST and after errors
;xxxxx1xx               1 disables 'normal' ignition OC1F, OC2F processing.  tied to B7: init'd to 1 at RESET
;xxxx1xxx               1 means we just had a falling edge event on IC2 (crankshaft sensor)
;xxx1xxxx               1 means we have a fresh AAP (ambient Air Press) conversion.
;xx1xxxxx               1 means we have a fresh AN2 conversion.  This bit never gets cleared, but it does get tested.
;x1xxxxxx               ??tied to B2: init'd to 1 at RESET
;1xxxxxxx firstSync     1=crank/cam have sync'd, but less than 1/2 a crank rotation has occurred since then
L0000   =       0x0000

;-----------------------------------------------------------------
;All of the bits in this reg are used for something.
;xxxxxxx1
;xxxxxx1x
;xxxxx1xx
;xxxx1xxx
;xxx1xxxx
;xx1xxxxx
;x1xxxxxx               1 means engine is running below 215 RPM, cleared when engine spins over 750 RPM
;1xxxxxxx
L0001   =       0x0001


;-----------------------------------------------------------------
;xxxxxxx1               set to 1 when we see the start of CR8
;xxxxxx1x               set to 1 when we see the start of CR3
;xxxxx1xx               set to 1 every time see a crankshaft reference pulse: schedules crank period calculations
;xxxx1xxx               ???: set to 1 every 80 mSec \
;xxx1xxxx               ???: set to 1 every 40 mSec |-- only one of these three will be set on a given main-loop iter
;xx1xxxxx               ???: set to 1 every 40 mSec /
;x1xxxxxx               ???: set to 1 every 5 mSec
;1xxxxxxx               <not used>
L0002   =       0x0002


;-----------------------------------------------------------------
;xxxxxxx1 cam           1=cam event observed. Set by cam event ISR, cleared by crankshaft event ISR CR4.
;xxxxxx1x               <not used>
;xxxxx1xx               set to 1 every time we see a falling edge event on the crank sensor
;xxxx1xxx               1=cam event has occurred since last crank event.
;xxx1xxxx crankSync_bad ??conflict! 1=don't know where crank is.  Setting this stops engine from running.
;xx1xxxxx               1=set as part of THW conversion, cleared on disasters, but never gets tested.
;x1xxxxxx               <not used>
;1xxxxxxx               <not used>
L0003   =       0x0003

;-----------------------------------------------------------------
;Init'd to 0x00 by mem clear code.

;Seems to contain error flags.

;xxxxxxx1 DON_FILT      "DON Filtered": 1=we are really, really sure that the bike fell over
;xxxxxx1x sidestand     0==sidestand OK?, 1==sidestand is down? when it shouldn't be
;xxxxx1xx               1 means the most recent 1/6 crank period indicates < 575 RPM, cleared if RPM is ever > 1000
;                       also cleared if RPM is between [576..999] for 1.0 seconds.
;xxxx1xxx               <not used>
;xxx1xxxx THW           1=set as part of THW conversion, never tested or cleared
;xx1xxxxx               1 means bike appears to have fallen over, check again in 0.5 sec
;x1xxxxxx               ??? used by WL processing
;1xxxxxxx               <not used>
L0004   =       0x0004

;-----------------------------------------------------------------
;xxxxxxx1               ?
;xxxxxx1x               ?
;xxxxx1xx               ?
;xxxx1xxx TEST_FILT     "TEST Filtered": 1=we are sure that the TEST (diag) connector is grounded
;xxx1xxxx               1 means the next TOC1 compare event will start driving the front cyl ignition coil
;xx1xxxxx               1 means VTA sensor is OK as per L000C bit 3, 0 means not.
;x1xxxxxx               ?
;1xxxxxxx DON_RAW       1 means DON sensor says we are tipped over on this particular loop iteration
L0005   =       0x0005


;-----------------------------------------------------------------
;xxxxxxx1               ?
;xxxxxx1x               ?
;xxxxx1xx               1 == next TOC2 timeout will start the REAR coil charging
;xxxx1xxx               ?
;xxx1xxxx               ?
;xx1xxxxx               set when current RPM goes >= 10000 RPM, cleared when RPM drops below 9800.
;x1xxxxxx               ?
;1xxxxxxx               at start of mofo loop: 0 if (8.8RPM50 < 10500 RPM), else 1
L0006   =       0x0006

;-----------------------------------------------------------------
;xxxxxxx1
;xxxxxx1x
;xxxxx1xx
;xxxx1xxx
;xxx1xxxx
;xx1xxxxx
;x1xxxxxx  IGN_DIS      Ignition coils are disabled
;1xxxxxxx  INJ_DIS      Fuel injectors are disabled
L0007   =       0x0007


;-----------------------------------------------------------------
;xxxxxxx1               gets set to '1' the first time a low voltage is detected on VM
;xxxxxx1x               gets set to '1' the second time a low voltage is detected on VM
;xxxxx1xx
;xxxx1xxx
;xxx1xxxx               <unused>
;xx1xxxxx               <unused>
;x1xxxxxx
;1xxxxxxx
L0008   =       0x0008

;-----------------------------------------------------------------
;L0009 is not referenced

;-----------------------------------------------------------------
;xxxxxxx1
;[B7..B1] are not used.
L000A   =       0x000A

;-----------------------------------------------------------------
;xxxxxxx1               rear  hysteris flag: 1 when engine speed goes < 200 RPM, 0 when engine speed goes > 540 RPM
;xxxxxx1x               front hysteris flag: 1 when engine speed goes < 200 RPM, 0 when engine speed goes > 540 RPM
;xxxxx1xx               1 means engine is turning < 200 RPM && there is a request to fire rear on cr10 (rising)
;xxxx1xxx               1 means engine is turning < 200 RPM && there is a request to fire front on cr5 (rising)
;xxx1xxxx MAP_ACTIVE    1 means a MAP conversion is in progress, 0 means no MAP conversion in progress
;xx1xxxxx MAP_ERROR     1 means that the MAP conversion failed, 0 means it was OK
;x1xxxxxx               ?
;1xxxxxxx               ?
L000B   =       0x000B

;-----------------------------------------------------------------
;I have a feeling that these bits are set if the associated
;sensor is detectably "broken" in some fashion:
;xxxxxxx1 AAP           ambient air pressure
;xxxxxx1x THA           air temp
;xxxxx1xx THW           set if the THW water temp thermistor is deemed to be bad.
;xxxx1xxx VTA           set if the throttle angle sensor was bad 64 times in a row
;xxx1xxxx AN2           MAP sensor ?average?
;xx1xxxxx MAP           MAP sensor ?peak-detect?
;x1xxxxxx CRANK         crank sensor is broken
;1xxxxxxx CAM           cam sensor is broken
L000C   =       0x000C

;-----------------------------------------------------------------
;xxxxxxx1 DON_BAD       1 means DON sensor is disconnected (or open)
;xxxxxx1x               1 means REAR  ign coil IG22 is bad
;xxxxx1xx               1 means REAR  ign coil IG21 is bad
;xxxx1xxx               1 means FRONT ign coil IG12 is bad
;xxx1xxxx               1 means FRONT ign coil IG11 is bad
;xx1xxxxx               <not used>
;x1xxxxxx               <not used>
;1xxxxxxx               <not used>
L000D   =       0x000D

;-----------------------------------------------------------------
;Location L000E is a copy of L000C except that bits in L000C can
;be cleared if the sensor 'gets better'.  Any bits set in L000E
;are 'sticky' and remain set until the processor is reset.  Well,
;sticky except for bit 4 which has something to do with the
;AN2 conversion.
L000E   =       0x000E


;-----------------------------------------------------------------
;Location L000F is a copy of L000D except that bits in L000D can
;be cleared if the sensor 'gets better'.  Any bits set in L000F
;are 'sticky' and remain set until the processor is reset.
L000F   =       0x000F

;-----------------------------------------------------------------
;The "suspect" bits are set if the A/D conversion routine detects
;that a sensor conversion is outside of reasonable limits.  The
;general usage seems to be that if a sensor is "suspect" for
;enough conversions in a row, it then gets declared as being
;"broken", which would set the bit on L000C.
;xxxxxxx1
;xxxxxx1x THW_suspect   set if the THW (Water temp Thermistor) sensor reading is suspect
;xxxxx1xx
;xxxx1xxx THA_suspect   set if the THA (Air temp Thermistor) sensor reading is suspect
;xxx1xxxx AAP_suspect   set if the AAP (Ambient Air Pres.) sensor reading is suspect
;xx1xxxxx VTA_suspect   set if the VTA (throttle angle) sensor reading is suspect
;x1xxxxxx               <not used>
;1xxxxxxx
L0010   =       0x0010

;-----------------------------------------------------------------
;xxxxxxx1               <not used>
;xxxxxx1x               <not used>
;xxxxx1xx               <not used>
;xxxx1xxx               <not used>
;xxx1xxxx               set to '1' if the cam sensor is broken, will be zeroed if we ever see a cam event
;xx1xxxxx
;x1xxxxxx               set to '1' if the crank sensor is broken (30 cam events without a single crank event)
;1xxxxxxx
L0011   =       0x0011

;-----------------------------------------------------------------
;Debug execution visibility flags.  Probably for emulator use to
;see what section of the main loop was running when the world
;went to hell.
;xxxxxxx1              ;set to 1 on entry to LE3BA; cleared on exit
;xxxxxx1x              ;set to 1 on entry to LE26C; cleared on exit
;xxxxx1xx              ;set to 1 on entry to LDF4D; cleared on exit
;xxxx1xxx              ;set to 1 on entry to LD82E; cleared on exit
;xxx1xxxx              ;set to 1 while crankshaft period calculation is in progress.  0 means calc is complete.
;xx1xxxxx              ;set to 1 on entry to LD60B, cleared on exit
;x1xxxxxx              ;<not used>
;1xxxxxxx              ;toggles every time through the main loop.  Unused otherwise.
L0012   =       0x0012

;L0013, L0014 appear to be unused

;xxxxxxx1              ;?
;xxxxxx1x              ;0 means we are processing cylinder 1, 1 means cylinder 2
;xxxxx1xx              ; <not used>
;xxxx1xxx              ; <not used>
;xxx1xxxx              ; <not used>
;xx1xxxxx              ; <not used>
;x1xxxxxx              ; <not used>
;1xxxxxxx              ; <not used>
L0015   =       0x0015

L0016   =       0x0016                  ;Debug usage: values stored here describe what A/D conversion was just performed.
;L0017 is tied to L0016 via STD

L0018   =       0x0018
L0019   =       0x0019                  ;free-running counter that defines what conversion will be performed next
L001A   =       0x001A
L001B_tmp  =    0x001B
;L001C is tied to L001B via STD

L001D   =       0x001D                  ;holds the raw 8-bit MAP (AN3) conversion taken via the general A/D conversion
                                        ;BUT: AN3 is NOT converted during the 5 mSec loop.  I think it only gets
                                        ;converted once after reset...

L001E   =       0x001E                  ;holds the raw 8-bit MAP (AN3) conversion taken during the crankshaft ISR
L001F   =       0x001F
L0020   =       0x0020
L0021   =       0x0021
L0022   =       0x0022                  ;holds the raw 10-bit left-aligned VTA (throttle angle) reading
L0024   =       0x0024                  ;holds the raw 10-bit right-aligned VTA (throttle angle) reading

L0026   =       0x0026                  ;holds the low 8 bits of the initial post-reset VTA reading.  Never used.
L0027   =       0x0027                  ;holds the raw 8-bit AAP (Ambient Air Pressure) A/D.
L0028   =       0x0028                  ;holds the raw 8-bit THW A/D.  Never used except to store it here.
L0029   =       0x0029                  ;holds the raw 8-bit THA A/D.  Never used except to store it here.
L002A   =       0x002A                  ;holds the raw 8-bit VM A/D.
L002B   =       0x002B                  ;holds the raw 8-bit CO1 A/D.  Never used except to store it here.
L002C   =       0x002C                  ;holds the raw 8-bit CO2 A/D.  Never used except to store it here.
L002D   =       0x002D                  ;holds the raw 8-bit DON "tip over" sensor reading
L002E   =       0x002E                  ;holds the raw 8-bit AT channel A/D reading (not used)
L002F   =       0x002F                  ;holds the raw 8-bit REV channel A/D reading (not used)

L0030   =       0x0030                  ;holds the converted, interpolated 8-bit THW (water temp) reading
L0031   =       0x0031                  ;holds the converted, interpolated 8-bit THA (air temp) reading
L0032   =       0x0032                  ;holds a post-reset copy of the initial THA reading from L0031.  Never used.
L0033   =       0x0033                  ;holds the 8-bit AAP (Ambient Air Pressure) reading.  No filtering vs. raw A/D.
L0034   =       0x0034
L0035   =       0x0035
L0036   =       0x0036
L0038   =       0x0038
L0039   =       0x0039
L003A   =       0x003A
L003C   =       0x003C
L003E   =       0x003E                  ;used in AAP (Ambient Air Pressure) calculations
L0040   =       0x0040                  ;used in AAP (Ambient Air Pressure) calculations
L0041   =       0x0041
L0042   =       0x0042                  ;holds the filtered VTA (Throttle Angle) A/D reading.
L0044   =       0x0044                  ;holds the left-aligned VTA (not sure how much filtering happens)
L0046   =       0x0046                  ; some sort of 16-bit value in 46/47
L0048   =       0x0048

L0049_PORTG_debounced = 0x0049
L004A_PORTG_prev      = 0x004A

; L004D/4E is calculated by subr LC834.  It is defined to be an
; 8.8RPM50 value.  This value corresponds to the current engine RPM,
; in units of 50 RPM increments.  Not biased, so 0.0 = 0RPM, 100.00
; (decimal) would be 5000.0 RPM.  Max representable RPM would be
; 0xFF.FF, but that is used to indicate an overspeed condition.
L004D =         0x004D

; L004F/50 is calculated by subr LC834.  It is defined to be an
; 8.8RPM400 value.  This value corresponds to the current engine RPM,
; in units of 400 RPM increments, biased downwards by 800 RPM
; (i.e. 0.0 == 800 RPM).  It is clipped so that it can represent the
; range [800..12000] RPM.  For example, 10.00 (decimal) would be
; 4800.0 RPM.
L004F   =       0x004F

;-----------------------------------------------------------------
;L0051,52,53 count ?? events.  The counters are incremented at
;the same time inside a 5 mSec event subroutine, however, there
;are conditions where they might not be incremented.

;Every crank event: depending on the crankRef, a count is
;transferred from 51..53 to 54, then the transferred counter is
;cleared.

;The counters are init'd to FF on reset, and init'd to 14 on disaster.

;The values are clamped to 192. under 'normal' operating conditions.

;During the CR ISR:
;  - The 51..53 counters are cleared if the cam sensor is detected to be broken.
;  - The 51..53 counters are cleared if the 'slow RPM' flag (L0004: #0b00000100) is set

;During the 5msec routine, if L0053 gets >= 12, we kill the ignition.
;During the 5msec routine, if L0053 gets >= 103, we signal a disaster and forget everything we know about the engine params.

L0051   =       0x0051                  ;gets cleared every CR1,CR4,CR7,CR10
L0052   =       0x0052                  ;gets cleared every CR2,CR5,CR8,CR11
L0053   =       0x0053                  ;gets cleared every CR3,CR6,CR9,CR0
L0054   =       0x0054                  ;holds the most recent value of L0051,L0052,L0053 depending on which crankRef we just saw
;-----------------------------------------------------------------

L0055   =       0x0055                  ;>=0x80 means we don't know where the crank is.  0 through 11 means we do.

L0056   =       0x0056                  ;some kind of temp used during the crankshaft ISR
L0057   =       0x0057
L0058   =       0x0058
L0059   =       0x0059                  ;holds the time when the start of the current crank reference event occurred.
L005B   =       0x005B

L005D   =       0x005D                  ;holds the basic injection duration for the FRONT cyl in 2 uSec ticks
L005F   =       0x005F                  ;holds the basic injection duration for the REAR  cyl in 2 uSec ticks

L0061   =       0x0061
L0063   =       0x0063
L0065   =       0x0065
L0067   =       0x0067
L0069   =       0x0069
L006B   =       0x006B
L006D   =       0x006D
L006F   =       0x006F
L0070   =       0x0070
L0072   =       0x0072
L0074   =       0x0074
L0076   =       0x0076
L0078   =       0x0078
L0079   =       0x0079
L007B   =       0x007B
L007D   =       0x007D
L007F   =       0x007F
L0081   =       0x0081
L0083   =       0x0083
L0085   =       0x0085
L0087   =       0x0087
L0089   =       0x0089
L008B   =       0x008B
L008C   =       0x008C
L008D   =       0x008D
L008E   =       0x008E
L008F   =       0x008F
L0090   =       0x0090                  ;calc'd every 80 mSec based on THW
L0091   =       0x0091                  ;calc'd every 80 mSec based on THW
L0092   =       0x0092
L0094   =       0x0094
L0096   =       0x0096
L0097   =       0x0097
L0098   =       0x0098
L0099   =       0x0099
L009A   =       0x009A
L009B   =       0x009B
L009C   =       0x009C
L009D   =       0x009D
L009F   =       0x009F
L00A0   =       0x00A0                  ;Holds result of MAP/N   cyl 1 Fuel table processing (restricted or derestricted)
L00A1   =       0x00A1
L00A2   =       0x00A2                  ;Holds result of Alpha/N cyl 1 Fuel table processing (restricted or derestricted)
L00A3   =       0x00A3
L00A4   =       0x00A4                  ;Holds result of MAP/N   cyl 2 Fuel table processing (restricted or derestricted)
L00A5   =       0x00A5
L00A6   =       0x00A6                  ;Holds result of Alpha/N cyl 2 Fuel table processing (restricted or derestricted)
L00A7   =       0x00A7
L00A8   =       0x00A8
L00A9   =       0x00A9
L00AB   =       0x00AB
L00AD   =       0x00AD
L00AF   =       0x00AF
L00B0   =       0x00B0
L00B2   =       0x00B2
L00B4   =       0x00B4                  ;does get read once to write L00B5

L00B5   =       0x00B5                  ;debug? written but never read.
L00B6   =       0x00B6                  ;debug? written but never read.
L00B8   =       0x00B8                  ;debug? written but never read.
L00B9   =       0x00B9                  ;debug? written but never read.
L00BA   =       0x00BA                  ;debug? written but never read.
L00BC   =       0x00BC                  ;debug? written but never read.
L00BD   =       0x00BD                  ;debug? written but never read.
L00BE   =       0x00BE                  ;debug? written but never read.
L00BF   =       0x00BF                  ;debug? written but never read.  based on THW.
L00C0   =       0x00C0                  ;debug? written but never read.  based on THW.

L00C1   =       0x00C1
L00C2   =       0x00C2
L00C3   =       0x00C3
L00C4   =       0x00C4

L00C5   =       0x00C5                  ;CO1 (right) pot reading after A/D massaging to implement a mid-point deadband
L00C6   =       0x00C6                  ;CO2 (left)  pot reading after A/D massaging to implement a mid-point deadband

L00C7   =       0x00C7

L00C8   =       0x00C8                  ;a 8-bit value based on water temp that is used to stretch the amount of time
                                        ;to squirt the front and rear injectors

L00C9   =       0x00C9                  ;16-bit variable

L00CB   =       0x00CB
L00CC   =       0x00CC
L00CD   =       0x00CD                  ;calc'd every 80 mSec based on THW.  Differs if restricted/derestricted.

L00CE   =       0x00CE                  ;Holds 1.5 times the period (in 2 uSec ticks) between the most recent two CR events
                                        ;This would correspond to (60*1.5) or 90 degrees of rotation

L00D0   =       0x00D0                  ;1Holds the time (in 2 uSec ticks) between the most recent two crankshaft
                                        ;reference pulses. This means that it holds the (crank period/6).

L00D2   =       0x00D2                  ;the 1/6 crank period for crankRefs cr1, cr4, cr7, cr10
L00D4   =       0x00D4                  ;the 1/6 crank period for crankRefs cr2, cr5, cr8, cr11
L00D6   =       0x00D6                  ;the 1/6 crank period for crankRefs cr0, cr3, cr6, cr9

L00D8   =       0x00D8                  ;the 1/2 crank period over the last three 1/6 periods...
                                        ;...0xFFFF means max period (min RPM), calc'd from adding the (L00D2+L00D4+L00D6)

L00DA   =       0x00DA                  ;the full crank period for the current RPM (calc'd from doubling L00D8)

L00DC   =       0x00DC                  ;This is a 0.8 fraction that defines the spark advance for the Front cyl.
                                        ;It gets multiplied against the time it took to took to spin the last 90 degrees of rotation.
                                        ;The actual advance is based on current CR time + timeFor90degrees - (timeFor90Degrees * L00DC)
                                        ;There seem to be two defaults for this value used at different times: 0x55, and 0xD5.
                                        ;0x55 == 0.332, or 29.9 degrees, (90-30) or 60 degrees from start of CR (72 BTDC), or 12 BTDC
                                        ;0xD5 == 0.832, or 74.9 degrees, (90-75) or 15 degrees from start of CR (72 BTDC), or 57 BTDC
                                        ; That seems like a wide range, so maybe I have something left to figure out.

L00DD   =       0x00DD                  ;Same as L00DE.  It represents L00DE adjusted by an amount that is always 0.
L00DE   =       0x00DE                  ;Holds result of Alpha/N cyl 1 Ignition table processing (restricted or derestricted)

L00DF   =       0x00DF                  ;Same as L00DC, but rear cyl
L00E0   =       0x00E0                  ;Same as L00E1.  It represents L00E1 adjusted by an amount that is always 0.
L00E1   =       0x00E1                  ;Holds result of Alpha/N cyl 2 Ignition table processing (restricted or derestricted)
L00E2   =       0x00E2
L00E3   =       0x00E3
L00E4   =       0x00E4                  ;calc'd every 80 mSec based on THW
L00E5   =       0x00E5                  ;holds an interpolated value that always ends up as 0 due to table contents.
L00E6   =       0x00E6                  ;calc'd every 80 mSec based on THW
L00E7   =       0x00E7

;In rough terms, L00E8 is based strictly on RPM:
;  At 800RPM:0x00, 1200:0x18, ... , over 6000RPM, values gradually increase from about 0x80 to 0x8C.
L00E8   =       0x00E8

; These next two variables hold the ignition advance (actually, the ignition delay) for each cylinder.
; They represent time delays.  They are based on the degree delays of L00DC, L00DD and the current engine rotation speed.
L00E9   =       0x00E9                  ;Used by frt  cyl ignition timing.  The value is calculated during CR3.
L00EB   =       0x00EB                  ;Used by rear cyl ignition timing.  The value is calculated during CR9.

L00ED   =       0x00ED                  ;methinks this is the dwell period, or at least something used to calc the real dwell period.
L00EF   =       0x00EF                  ;used as a temp during dwell calculations
L00F1   =       0x00F1                  ;A temp of some sort: only used twice within 7 instrs of each other
L00F3   =       0x00F3
L00F5   =       0x00F5


L00F7   =       0x00F7                  ;during CR0, if FRT coil is not already charging, this will contain dwell delay
                                        ;that will be used during CR1 to define when to start charging the coil

L00F9   =       0x00F9
L00FB   =       0x00FB

L00FD   =       0x00FD                  ;during CR5, if REAR coil is not already charging, this will contain dwell delay
                                        ;that will be used during CR6 to define when to start charging the coil

; These next two values are calculated during the CR3/CR8 events, and used during the CR4/CR9 events.
L00FF   =       0x00FF                  ;FRT  cyl: the exact TCNT time during CR4 we want an OC1 match (fire FRT  spark) event
L0101   =       0x0101                  ;REAR cyl: the exact TCNT time during CR9 we want an OC2 match (fire REAR spark) event

L0103   =       0x0103                  ;holds the future TCNT value used to schedule 1.5 mSec main loop events (666.7 Hz)
L0105   =       0x0105                  ;holds the future TCNT value used to schedule 5.0 mSec main loop events (200 Hz)
L0107   =       0x0107                  ;free-running counter of 5.0 mSec events: used to periodically schedule processes
L0108   =       0x0108                  ;NEVER USED: probably schedules periodic main loop processes in some other product

L010A   =       0x010A
L010B   =       0x010B                  ;set to 0 every crank event that bike is upright.
L010C   =       0x010C                  ;???, but init'd to 0, inc&peg to 0xFF

L010D   =       0x010D                  ;used to count a 1.0 second timeout clearing the 'slow RPM' flag

L010E   =       0x010E                  ;used to generate a 0.5 second timeout from the last crank rotation event

L010F   =       0x010F
L0110   =       0x0110
L0111   =       0x0111

;PG1 debounce counters:
L0114   =       0x0114                  ;cleared when PG1==1, incremented if PG1=0
L0115   =       0x0115                  ;cleared when PG1==0, incremented if PG1=1

;Sidestand position debounce counters:
L0116   =       0x0116                  ;counter used to tell if the sidestand is up or down
L0117   =       0x0117                  ;counter used to tell if the sidestand is up or down

;DON sensor processing:
L0118   =       0x0118                  ;used to count 0.5 second to verify that the bike still seems to be on its side.
L0119   =       0x0119                  ;DON bad sensor count: incremented every time sensor reads out of range

;VTA (Throttle angle processing):
L011A   =       0x011A                  ;used as the rolling index to select one of the next 4 16-bit words
L011B   =       0x011B                  ;These next four locations save a rolling history of location L0042 (filtered VTA).
L011D   =       0x011D
L011F   =       0x011F
L0121   =       0x0121

L0123   =       0x0123
L0124   =       0x0124
L0125   =       0x0125                  ; gets pegged to 0xFF
L0126   =       0x0126                  ; gets pegged to 0xFF
L0127   =       0x0127
L0128   =       0x0128
L0129   =       0x0129
L012A   =       0x012A
L012B   =       0x012B
L012C   =       0x012C
L012D   =       0x012D
L012F   =       0x012F
L0130   =       0x0130
L0131   =       0x0131
L0132   =       0x0132
L0133   =       0x0133                  ;counts "suspect" (out-of-range) VTA conversion events
L0134   =       0x0134                  ;counts "suspect" (out-of-range) THW conversion events
L0135   =       0x0135                  ;counts "suspect" (out-of-range) THA conversion events
L0136   =       0x0136                  ;counts "suspect" (out-of-range) AAP conversion events
L0137   =       0x0137
L0138   =       0x0138                  ;counts "good" (in-range) VTA conversion events
L0139   =       0x0139                  ;counts "good" (in-range) THW conversion events
L013A   =       0x013A                  ;counts "good" (in-range) THA conversion events
L013B   =       0x013B                  ;counts "good" (in-range) AAP conversion events
L013C   =       0x013C
L013D   =       0x013D
L013E   =       0x013E
L013F   =       0x013F
L0140   =       0x0140
L0141   =       0x0141
L0142   =       0x0142
L0143   =       0x0143
L0144   =       0x0144

L0145   =       0x0145                  ;1 second timeout counter used to "filter" diag TEST switch input

L0146   =       0x0146

L0147   =       0x0147                  ;counts error events on FRONT ign coil IG11
L0148   =       0x0148                  ;counts error events on FRONT ign coil IG12
L0149   =       0x0149                  ;counts error events on REAR  ign coil IG21
L014A   =       0x014A                  ;counts error events on REAR  ign coil IG22

;These next three have something to do with transmitting data.
;The fault code for sure, and maybe something else???
L014B   =       0x014B                  ;contains the fault code to be transmitted
L014C   =       0x014C
L014D   =       0x014D

L014E   =       0x014E
L014F   =       0x014F                  ;incs every time 1/6 period is cal'd (max 0xFF). cleared under certain conditions.

L0150   =       0x0150                  ;I think this variable tracks the CRID as 0 through 11 under conditions
                                        ;where we had a missing CAM event.

L0151   =       0x0151                  ;Counts the number of crank events we have seen since the last cam event.
                                        ;Crank ISR zeroes it if it sees that a cam event has occurred since the last crank event.

L0152   =       0x0152                  ;zeroed by the crank event ISR on every falling (leading) edge of a crank sensor event
                                        ;It is used by the camshaft ISR to detect a malfunctioning crankshaft sensor.

;At one point (at least) this contain VTA massaged into an 8.8 value suitable for indexing an 18-entry interpolation table.
L0153   =       0x0153                  ;This is an 8.8 value used to interpolate along a column in a table
L0154   =       0x0154
L0155   =       0x0155
L0156   =       0x0156
L0157   =       0x0157
L0158   =       0x0158
L0159   =       0x0159
L015A   =       0x015A
L015C   =       0x015C
L015E   =       0x015E                  ;temp usage
L015F   =       0x015F                  ;holds a copy of the last MAP 8-bit converison value
L0160   =       0x0160                  ;holds the period of the last 1/6 crank rotation (in 2 uSec ticks)
L0162   =       0x0162                  ;used by fuel injection timing code

L0166   =       0x0166                  ;time (in 2 uSec ticks) that the FRONT fuel injector should be enabled
L0168   =       0x0168                  ;time (in 2 uSec ticks) that the REAR  fuel injector should be enabled

L016A   =       0x016A                  ;used by fuel injection timing code
L016C   =       0x016C                  ;used by fuel injection timing code

L016E   =       0x016E                  ;written once with something, never used otherwise
L0170   =       0x0170
L0171   =       0x0171
L0172   =       0x0172
L0173   =       0x0173
L0174   =       0x0174
L0175   =       0x0175
L0177   =       0x0177
L0179   =       0x0179                  ;??? (written, but never used)
L017B   =       0x017B                  ;holds the calculated checksum (written, but never used)

;*****************************************************************
; Original stack gets to use from 0x1ff down to 0x17D  (roughly 128 bytes).
; The stack does not use all of this space, so we can use some space way down low.
; To do: figure out how much stack is used by the firmware.

; These next variables are new, and extend into the original stack space.
; They are used by the ECU data logging mechanisms to detect changes that should get reported.
PORTG_DB_U8_prev              = 0x017D
RAW_THA_U8_prev               = 0x017E
ECU_ERROR_L000C_U8_prev       = 0x017F
ECU_ERROR_L000D_U8_prev       = 0x0180
RAW_AAP_U8_prev               = 0x0181
FUEL_PUMP_B_prev              = 0x0182
RAW_VTA_prev                  = 0x0183          ; 16-bits: 0x0183 & 0x0184
half_overflow                 = 0x0185
#if defined SPARK_ISR
FAKE_TFLG1                    = 0x0186          ; a fake copy of TFLG1 for use by crankshaft spark detection processing
#endif

;*****************************************************************
;68HC11G IO Register Map

PORTA   =       0x1000
DDRA    =       0x1001

PORTG   =       0x1002
DDRG    =       0x1003

PORTD   =       0x1008
DDRD    =       0x1009

OC1M    =       0x100C
OC1D    =       0x100D

;From the HC11G5 doc:
;   - TCNT1 is associated with OC1-OC4, OC5/IC4 and OC6/IC5.
TCNT    =       0x100E
TIC1    =       0x1010                          ; will be used to capture CAM events
TIC2    =       0x1012                          ; will be used to capture CRANK events
TIC3    =       0x1014                          ; captures when coil X1 fired
TOC1    =       0x1016                          ; drives front coils
TOC2    =       0x1018                          ; drives rear coils
TOC3    =       0x101A                          ; drives front injector
TOC4    =       0x101C                          ; drives rear injector
TO5I4   =       0x101E                          ; capture when coil x2 fired (was TOC5)
TCTL1   =       0x1020
TCTL2   =       0x1021
TMSK1   =       0x1022
TFLG1   =       0x1023
TMSK2   =       0x1024
TFLG2   =       0x1025

PACTL   =       0x1026

BAUD    =       0x102B
SCCR1   =       0x102C
SCCR2   =       0x102D
SCSR    =       0x102E
SCDR    =       0x102F

ADCTL   =       0x1030
PORTJ   =       0x1031
DDRJ    =       0x1032
PORTH   =       0x1033
DDRH    =       0x1034

;1035   = RESERVED
;1036   = RESERVED
;1037   = RESERVED

OPT2    =       0x1038
OPTION  =       0x1039
CONFIG  =       0x103F

;1040/1 ADR1
;1042/3 ADR2
;1044/5 ADR3
;1046/7 ADR4
ADR5   =       0x1048
;104A/B ADR6
;104C/D ADR7
;104E/F ADR8

TCTL3   =       0x1050
TCTL4   =       0x1051

;From the HC11G5 doc:
;  - TCNT2 is associated with IC1..IC3 and OC7/IC6.
;  - Can be cleared by writing it (but this program never writes it!)
;  - Counters are cleared to 0x0000 during reset.
TCNT2   =       0x1052
TO6I5   =       0x1054
TO7I6   =       0x1056
TPRE    =       0x1058
PWEN    =       0x1063
EVCLK   =       0x1070
EVCTL   =       0x1071
EVMSK   =       0x1072
ECMP1A  =       0x1076
ECMP2A  =       0x1077
ECMP1B  =       0x1078

;*****************************************************************
;PORTA:

;IC3/IC4 watch the collectors of the output transistor driving
;the ignition coils.  Due to the high reverse EMF generated when
;the ign coil is fired, the circuitry will generate a rising edge
;pulse.  A front is paired with a rear on each input.  By
;checking which crankRef we are in, we know if the pulse is due
;to a FRT or REAR event.  By pairing a FRT with a REAR, they only
;need two Input Compare circuits to watch all 4 coil inputs.

;PA7/OC1:  IG11,IG12 (output) drives the front cyl ignition coils ('0' energizes ignition coil to charge it, 0-->1 causes spark to fire)
;PA6/OC2:  IG21,IG22 (output) drives the rear  cyl ignition coils
;PA5/OC3:  #11       (output) drives the front cyl fuel injector  ('1' energizes injector causing fuel to spray. '0' turns spray off)
;PA4/OC4:  #21       (output) drives the rear  cyl fuel injector
;PA3/IC4:            (input)  watches rising edge of ignition event pulse on IG12-FRT and IG22-REAR
;PA2/IC1:            (input)  camshaft sensor   (1 pulse  per revolution)
;PA1/IC2:            (input)  crankshaft sensor (6 pulses per revolution)
;PA0/IC3:            (input)  watches rising edge of ignition event pulse on IG11-FRT and IG21-REAR

;*****************************************************************
;PORTD usage: DDRD is set to 0xFF (all outputs)
;The mux code overwrites PD2..PD7 with enthusiastic abandon
;PD7                 (output) <not bonded out>
;PD6                 (output) <not bonded out>
;PD5                 (output) <not used>
;PD4                 (output) <not used>
;PD3                 (output) '1': grounds the non-inverting inputs on op-amp IC711:P6 and IC711:P10
;PD2                 (output) <not used>
;PD1       MUXB      (output) Analog MUX selector B
;PD0       MUXA      (output) Analog MUX selector A


;*****************************************************************
;PORTE (Analog inputs)
;PE0/AN8   MUX       Y output
;PE1/AN7   MUX       X output
;PE2/AN6   THA       Air Temp Thermistor
;PE3/AN5   VM        Voltage Monitor: monitors Vbatt applied to injectors and ign. coils.
;PE4/AN4   OX        <not used>
;PE5/AN3   PM        MAP (Manifold Atmospheric Pressure) run through a peak-detect circuit
;PE6/AN2   ---       <not used>, MAP signal run through a simple RC filter
;PE7/AN1   VTA       Throttle Angle

;       MUX Inputs, selected by PD0/1.  Mux output X feeds into AN7, Y into AN8

;       X0         CO0   CO Pot 0 (VR821: pot on right)
;       X1         THW   Water Temp Thermistor
;       X2         AT    <unused>
;       X3         DON   Fall Sensor

;       Y0         CO1   CO Pot 1 (VR831: pot on right)
;       Y1         REV   <unused>
;       Y2         AAP   Ambient Air Pressure
;       Y3         REV   <unused>


;*****************************************************************
;PORTG:
;The code always assigns 0xC0 to DDRG, so we have:
;PG7 -     --        (output) <not used>
;PG6 -     --        (output) <not used>
;PG5 -     MS2       (input)  Map Select #2
;PG4 -     T         (input)  Diagnostic Test connector
;PG3 -     MS1       (input)  Map Select #1
;PG2 -     SS        (input)  Sidestand switch
;PG1 -     ??        (input)  A weird-ass signal that appears to be a free-running toggle signal: is this the watchdog signal for the voltage regulator?
;PG0 -     CLT       (input)  Clutch switch

;The PORTG inputs are debounced by software.  A change is not
;considered valid unless it is observed two times in succession.
;This also means that PORTG is never accessed directly.  All
;firmware tests the debounced version ONLY.

;The Map Select bits are always tested as a pair.  The default
;wiring harness grounds both MS1 and MS2.  Since MS1 and MS2 are
;buffered to the CPU through an inverting buffer (IC601), the
;default (restricted) value observed by the CPU would be
;%00101000 (0x28).  If the "derestriction" wire (MS1) is cut, the
;MS1 input will be pulled high at the input to the inverting
;buffer, so the derestricted value will be %00100000 (0x20).

;In theory, there could be 4 maps:
map0    = 0x28           ;stock wiring harness: restricted
map1    = 0x20           ;derestriction wire cut: derestricted
map2    = 0x08
map3    = 0x00

;However, this code only implements two of them:
restricted   = map0
derestricted = map1



;*****************************************************************
;PORTH usage:  DDRH is set to 0x6f

;PH0       --        (output) <unused>
;PH1       ROM_OK    (output) the selfTest routine drives this to '1' if EPROM cksum is OK
;PH2       WL        (output) drives data stream to instrument panel? or Warning Light (the "EFI" indicator on dash)?
;PH3       RAM_OK    (output) the selfTest routine drives this to '1' if the RAM test is OK
;PH4       ??        (input)  has a pullup & testpoint, used in selftest code
;PH5       HEAT      (output) Unused by this software, but PCB does seem to drive something
;PH6       FP        (output) drives the fuel pump relay (0=relay energized)
;PH7       SELFTEST  (input)  has pullup & testpoint: 0=perform a SELF TEST and wedge, 1=normal operation

;*****************************************************************
;PORTJ usage:

;PJ0       TAC       (output) tachometer drive
;PJ1       VSV       (ouput)  who knows?  it has a driver, but the bike does not connect to the harness
;PJ2       --        (output) <unused>
;PJ3       ALIVE     (output) The self-test toggles this line as long as PH4 is '1'.
;                             It should toggle at roughly 200 Hz since it is in the 5 mSec main loop routine.



;*****************************************************************
;Are these mask ROM locations?  L4000 is referenced once, and
;then calls L5000 or not.
L4000   =       0x4000
L5000   =       0x5000

;*****************************************************************
;Start of EPROM code.


        .byte   0x3F, 0x3F, 0x3F, 0x3F
        .byte   0x3F, 0x3F, 0x3F, 0x3F
        .byte   0xB9, 0x72, 0x3F, 0x3F
        .byte   0x3F, 0x3F, 0xB9, 0x72
        .byte   0x20, 0x00, 0x3F, 0xFF
        .byte   0xC0, 0x00, 0xFF, 0xFF
        .byte   0x70, 0x00, 0x5A, 0xA5
        .byte   0x3F, 0x3F, 0x3F, 0x3F

L8020:  .byte   0xC0

L8021:  .byte   0x80

L8022:  .byte   0x07
L8023:  .byte   0x84
L8024:  .word   0x0078
L8026:  .word   0x0090
L8028:  .byte   0x04
L8029:  .byte   0x0B
L802A:  .byte   0x00, 0xFF, 0x20
L802D:  .byte   0x0C

L802E:  .byte   0x04
L802F:  .byte   0x06

L8030:  .byte   0x5B                    ;the MSbyte of a 1/6 crank period: 0x5B == 0x5B00 ->  215 RPM
L8031:  .byte   0x1A                    ;the MSbyte of a 1/6 crank period: 0x1A == 0x1A00 ->  750 RPM

L8032:  .byte   0x06
L8033:  .byte   0x05
L8034:  .byte   0x38
L8035:  .byte   0x3C

L8036:  .byte   0x06                    ;6 crank ref pulses means 360 degrees
L8037:  .byte   0x05                    ;5 crank ref pulses means 310 degrees
L8038:  .byte   0x38
L8039:  .byte   0x3C

L803A:  .byte   0x10, 0xE0, 0x08
        .byte   0xF9, 0xCA, 0x33, 0x16, 0x0B, 0x09, 0x07, 0x06

L8045:  .byte   0x05, 0x14, 0x40
        .byte   0x37, 0x4E, 0x63, 0x74, 0x7E, 0x80

L804E:  .byte   0x20, 0xA0, 0x08
        .byte   0x80, 0x80, 0x00, 0x00, 0x00, 0x00

L8057:  .byte   0x70, 0x70, 0x10
        .byte   0x46, 0x54, 0x61, 0x70, 0x7D, 0x8A, 0x98, 0xA5

L8062:  .word   0x0064
L8064:  .word   0x61A8
L8066:  .byte   0x80
L8067:  .byte   0x00
L8068:  .byte   0x00
L8069:  .byte   0x80

;*****************************************************************
;These next two blocks are identical:

;This is the restricted version:
L806A:  .byte   0x2A
L806B:  .byte   0x02
L806C:  .byte   0x84
L806D:  .byte   0x0C
        .byte   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        .byte   0x10, 0x0B, 0x07, 0x04, 0x00, 0x00, 0x02, 0x02
        .byte   0x1F, 0x18, 0x13, 0x0D, 0x00, 0x00, 0x05, 0x05

;This is the derestricted version:
L8086:  .byte   0x2A
L8087:  .byte   0x02
        .byte   0x84
L8089:  .byte   0x0C
        .byte   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        .byte   0x10, 0x0B, 0x07, 0x04, 0x00, 0x00, 0x02, 0x02
        .byte   0x1F, 0x18, 0x13, 0x0D, 0x00, 0x00, 0x05, 0x05

;*****************************************************************
;For the 549US EPROM, all of the restricted/derestricted tables
;are identical.

; Current theory:

; The table is designed to work with the L004F 8.8 representation of
; 400 RPM increments, biased by 800 RPM.  The first row represents 800
; RPM, the second, 1200 RPM, etc.  With 29 rows, this would be 800 RPM
; through 800 + ((29-1) * 400) or 12000 RPM.  RPMs can be interpolated
; between rows, just like we would also interpolate between the the
; values within the rows.

;Fuel map: 16x29 FR: restricted MAP/N cyl 1
L80A2:  .byte   0x12, 0x12, 0x12, 0x11, 0x13, 0x16, 0x1E, 0x24, 0x2B, 0x32, 0x40, 0x41, 0x57, 0x6F, 0x7F, 0x86
        .byte   0x11, 0x11, 0x0E, 0x0F, 0x0F, 0x14, 0x1E, 0x23, 0x28, 0x2D, 0x3A, 0x3B, 0x58, 0x6F, 0x7F, 0x83
        .byte   0x0F, 0x0F, 0x0D, 0x0D, 0x0C, 0x11, 0x19, 0x22, 0x27, 0x30, 0x33, 0x33, 0x56, 0x6E, 0x7D, 0x80
        .byte   0x0E, 0x0E, 0x0C, 0x0B, 0x0C, 0x14, 0x1C, 0x21, 0x25, 0x2D, 0x35, 0x38, 0x57, 0x6B, 0x7B, 0x7D
        .byte   0x0E, 0x0E, 0x0E, 0x10, 0x13, 0x18, 0x1C, 0x25, 0x2B, 0x32, 0x38, 0x3E, 0x5A, 0x6D, 0x7B, 0x7D
        .byte   0x10, 0x12, 0x13, 0x13, 0x12, 0x16, 0x1C, 0x24, 0x2A, 0x2E, 0x35, 0x37, 0x6E, 0x7B, 0x7F, 0x7F
        .byte   0x18, 0x1A, 0x1C, 0x16, 0x10, 0x11, 0x17, 0x1E, 0x22, 0x28, 0x2E, 0x2C, 0x5C, 0x74, 0x71, 0x7D
        .byte   0x18, 0x20, 0x1F, 0x17, 0x11, 0x14, 0x1A, 0x22, 0x26, 0x30, 0x32, 0x3A, 0x60, 0x72, 0x76, 0x7A
        .byte   0x15, 0x1F, 0x1C, 0x15, 0x14, 0x15, 0x1E, 0x27, 0x2C, 0x36, 0x39, 0x3E, 0x66, 0x74, 0x78, 0x7B
        .byte   0x12, 0x1C, 0x1B, 0x12, 0x13, 0x17, 0x21, 0x28, 0x2E, 0x34, 0x39, 0x37, 0x69, 0x73, 0x78, 0x7B
        .byte   0x11, 0x1B, 0x1B, 0x14, 0x15, 0x1D, 0x25, 0x2A, 0x30, 0x33, 0x37, 0x38, 0x6B, 0x73, 0x78, 0x7B
        .byte   0x11, 0x1A, 0x1B, 0x11, 0x18, 0x1D, 0x26, 0x2B, 0x2E, 0x2F, 0x31, 0x32, 0x68, 0x78, 0x7C, 0x7D
        .byte   0x13, 0x1A, 0x1B, 0x15, 0x15, 0x1D, 0x23, 0x28, 0x2B, 0x2C, 0x30, 0x33, 0x6B, 0x7C, 0x80, 0x81
        .byte   0x13, 0x18, 0x1A, 0x15, 0x15, 0x1C, 0x21, 0x27, 0x28, 0x2D, 0x2F, 0x36, 0x70, 0x84, 0x87, 0x88
        .byte   0x12, 0x16, 0x19, 0x16, 0x15, 0x1E, 0x21, 0x28, 0x2B, 0x2E, 0x31, 0x3C, 0x73, 0x87, 0x88, 0x8A
        .byte   0x11, 0x15, 0x19, 0x16, 0x18, 0x1D, 0x22, 0x29, 0x2F, 0x32, 0x36, 0x3C, 0x7E, 0x8A, 0x8B, 0x8C
        .byte   0x11, 0x13, 0x17, 0x12, 0x17, 0x1D, 0x25, 0x2C, 0x2F, 0x34, 0x3B, 0x41, 0x82, 0x90, 0x93, 0x95
        .byte   0x11, 0x14, 0x16, 0x15, 0x18, 0x1F, 0x25, 0x2B, 0x30, 0x35, 0x3C, 0x45, 0x86, 0x91, 0x97, 0x9C
        .byte   0x10, 0x14, 0x16, 0x15, 0x17, 0x1D, 0x22, 0x2A, 0x32, 0x37, 0x3D, 0x48, 0x86, 0x91, 0x9C, 0x9C
        .byte   0x0C, 0x11, 0x15, 0x16, 0x17, 0x1E, 0x25, 0x2A, 0x33, 0x39, 0x3D, 0x49, 0x83, 0x90, 0x9C, 0x9C
        .byte   0x0B, 0x0F, 0x14, 0x1A, 0x1B, 0x1F, 0x25, 0x2C, 0x33, 0x3A, 0x3E, 0x48, 0x85, 0x8E, 0x9C, 0x9C
        .byte   0x0B, 0x0E, 0x13, 0x1C, 0x1E, 0x1F, 0x25, 0x2D, 0x33, 0x39, 0x3E, 0x4B, 0x85, 0x8A, 0x9C, 0x9C
        .byte   0x08, 0x0B, 0x12, 0x1E, 0x21, 0x24, 0x24, 0x2D, 0x36, 0x38, 0x3D, 0x4E, 0x87, 0x89, 0x9A, 0x9C
        .byte   0x07, 0x0B, 0x12, 0x20, 0x24, 0x24, 0x2A, 0x2D, 0x34, 0x38, 0x43, 0x4F, 0x8A, 0x8A, 0x97, 0x9C
        .byte   0x07, 0x0C, 0x12, 0x20, 0x25, 0x29, 0x2E, 0x34, 0x35, 0x37, 0x43, 0x4D, 0x8C, 0x8D, 0x95, 0x9C
        .byte   0x07, 0x0C, 0x12, 0x20, 0x25, 0x29, 0x2F, 0x38, 0x3D, 0x41, 0x43, 0x5B, 0x94, 0x97, 0x94, 0x9C
        .byte   0x07, 0x0C, 0x13, 0x20, 0x25, 0x29, 0x2F, 0x38, 0x3D, 0x41, 0x43, 0x5B, 0x9C, 0x9C, 0x9C, 0x9C
        .byte   0x06, 0x0C, 0x13, 0x20, 0x25, 0x29, 0x2F, 0x38, 0x3D, 0x41, 0x43, 0x5B, 0x9C, 0x9C, 0x9C, 0x9C
        .byte   0x06, 0x0B, 0x13, 0x20, 0x25, 0x29, 0x2F, 0x38, 0x3D, 0x41, 0x43, 0x5B, 0x9C, 0x9C, 0x9C, 0x9C

;Fuel Map: 16x29 FR: restricted MAP/N cyl 2
L8272:  .byte   0x11, 0x10, 0x0F, 0x0E, 0x0E, 0x0E, 0x12, 0x13, 0x1C, 0x32, 0x42, 0x45, 0x62, 0x77, 0x7F, 0x86
        .byte   0x10, 0x10, 0x0E, 0x0D, 0x0C, 0x0E, 0x11, 0x16, 0x1C, 0x30, 0x41, 0x43, 0x65, 0x77, 0x7D, 0x87
        .byte   0x10, 0x0F, 0x0E, 0x0C, 0x0A, 0x0F, 0x13, 0x1A, 0x22, 0x2A, 0x32, 0x3C, 0x65, 0x76, 0x7D, 0x86
        .byte   0x0F, 0x0F, 0x0E, 0x10, 0x0F, 0x0E, 0x16, 0x1C, 0x26, 0x2E, 0x33, 0x3B, 0x65, 0x76, 0x7C, 0x85
        .byte   0x10, 0x0F, 0x10, 0x15, 0x0E, 0x12, 0x1C, 0x24, 0x2C, 0x33, 0x37, 0x43, 0x68, 0x75, 0x7C, 0x84
        .byte   0x13, 0x14, 0x15, 0x15, 0x0D, 0x16, 0x1F, 0x27, 0x2E, 0x35, 0x39, 0x45, 0x68, 0x74, 0x7B, 0x80
        .byte   0x14, 0x17, 0x16, 0x0E, 0x0E, 0x11, 0x1A, 0x21, 0x2A, 0x2D, 0x39, 0x3D, 0x68, 0x6F, 0x74, 0x79
        .byte   0x13, 0x17, 0x18, 0x15, 0x10, 0x13, 0x1D, 0x25, 0x2F, 0x39, 0x3E, 0x4E, 0x59, 0x69, 0x6F, 0x73
        .byte   0x13, 0x16, 0x1A, 0x1A, 0x0E, 0x17, 0x23, 0x30, 0x3A, 0x3E, 0x45, 0x4B, 0x55, 0x68, 0x66, 0x6C
        .byte   0x10, 0x13, 0x1A, 0x19, 0x14, 0x1B, 0x27, 0x32, 0x3B, 0x40, 0x43, 0x4D, 0x61, 0x6D, 0x6F, 0x74
        .byte   0x0E, 0x12, 0x19, 0x18, 0x16, 0x20, 0x2C, 0x36, 0x3C, 0x3F, 0x44, 0x4D, 0x66, 0x75, 0x77, 0x7A
        .byte   0x0D, 0x11, 0x18, 0x12, 0x19, 0x25, 0x2D, 0x34, 0x38, 0x3C, 0x3E, 0x49, 0x65, 0x77, 0x79, 0x7A
        .byte   0x0C, 0x11, 0x15, 0x11, 0x18, 0x1F, 0x28, 0x30, 0x35, 0x38, 0x3F, 0x49, 0x66, 0x7D, 0x7F, 0x7F
        .byte   0x0C, 0x11, 0x17, 0x11, 0x16, 0x1D, 0x25, 0x29, 0x2F, 0x33, 0x3A, 0x45, 0x68, 0x83, 0x85, 0x85
        .byte   0x0C, 0x10, 0x16, 0x12, 0x1A, 0x1E, 0x25, 0x2E, 0x33, 0x37, 0x3E, 0x44, 0x68, 0x84, 0x89, 0x89
        .byte   0x0C, 0x0E, 0x11, 0x13, 0x1A, 0x1F, 0x26, 0x2F, 0x33, 0x39, 0x3F, 0x45, 0x70, 0x8E, 0x8F, 0x90
        .byte   0x0D, 0x0E, 0x0E, 0x13, 0x1B, 0x21, 0x28, 0x32, 0x39, 0x3F, 0x47, 0x4A, 0x7B, 0x94, 0x96, 0x95
        .byte   0x0C, 0x0E, 0x0E, 0x13, 0x1E, 0x22, 0x2C, 0x36, 0x3D, 0x42, 0x47, 0x4D, 0x81, 0x9C, 0x9C, 0x9C
        .byte   0x0C, 0x0F, 0x0F, 0x13, 0x1E, 0x25, 0x2E, 0x37, 0x3D, 0x42, 0x47, 0x4D, 0x82, 0x9C, 0x9C, 0x9C
        .byte   0x09, 0x0D, 0x10, 0x16, 0x1E, 0x24, 0x2E, 0x37, 0x3D, 0x42, 0x48, 0x4D, 0x82, 0x9C, 0x9C, 0x9C
        .byte   0x05, 0x0A, 0x11, 0x17, 0x1F, 0x24, 0x2D, 0x37, 0x3D, 0x41, 0x47, 0x4E, 0x84, 0x9C, 0x9C, 0x9C
        .byte   0x05, 0x09, 0x12, 0x17, 0x21, 0x26, 0x2D, 0x36, 0x3D, 0x41, 0x46, 0x4F, 0x84, 0x9C, 0x9C, 0x9C
        .byte   0x04, 0x09, 0x13, 0x17, 0x26, 0x29, 0x31, 0x39, 0x3D, 0x42, 0x47, 0x50, 0x83, 0x9C, 0x9C, 0x9C
        .byte   0x06, 0x0B, 0x13, 0x18, 0x27, 0x2C, 0x31, 0x39, 0x3D, 0x42, 0x47, 0x51, 0x84, 0x9C, 0x9C, 0x9C
        .byte   0x06, 0x0B, 0x13, 0x18, 0x27, 0x2C, 0x31, 0x3A, 0x3D, 0x44, 0x47, 0x53, 0x86, 0x9C, 0x9C, 0x9C
        .byte   0x05, 0x0B, 0x13, 0x18, 0x27, 0x2C, 0x31, 0x39, 0x3D, 0x44, 0x47, 0x58, 0x88, 0x9C, 0x9C, 0x9C
        .byte   0x05, 0x0B, 0x13, 0x18, 0x27, 0x2C, 0x31, 0x39, 0x3D, 0x44, 0x47, 0x5D, 0x8D, 0x9C, 0x9C, 0x9C
        .byte   0x05, 0x0B, 0x13, 0x18, 0x27, 0x2C, 0x31, 0x39, 0x3D, 0x44, 0x47, 0x5D, 0x8D, 0x9C, 0x9C, 0x9C
        .byte   0x05, 0x0B, 0x13, 0x18, 0x27, 0x2C, 0x31, 0x39, 0x3D, 0x44, 0x47, 0x5D, 0x8D, 0x9C, 0x9C, 0x9C

;ALPHA/N means ThrottleAngle/RPM
;Fuel Map: 18x29 Restricted ALPHA/N cyl 1
L8442:  .byte   0x27, 0x29, 0x30, 0x2E, 0x32, 0x37, 0x3C, 0x40, 0x44, 0x47, 0x49, 0x4D, 0x4F, 0x51, 0x55, 0x58, 0x5B, 0x5E
        .byte   0x1E, 0x27, 0x31, 0x2F, 0x33, 0x38, 0x3B, 0x3E, 0x43, 0x48, 0x4B, 0x4F, 0x51, 0x54, 0x58, 0x5A, 0x5D, 0x60
        .byte   0x16, 0x19, 0x25, 0x28, 0x30, 0x38, 0x3F, 0x45, 0x4B, 0x4F, 0x52, 0x54, 0x55, 0x59, 0x5E, 0x62, 0x65, 0x69
        .byte   0x14, 0x18, 0x1A, 0x22, 0x28, 0x32, 0x3A, 0x46, 0x54, 0x5C, 0x61, 0x65, 0x69, 0x6C, 0x6D, 0x6F, 0x72, 0x76
        .byte   0x11, 0x14, 0x1D, 0x20, 0x26, 0x36, 0x41, 0x49, 0x58, 0x64, 0x74, 0x7C, 0x7F, 0x80, 0x80, 0x81, 0x82, 0x80
        .byte   0x0F, 0x0E, 0x12, 0x1B, 0x1E, 0x2E, 0x33, 0x39, 0x42, 0x48, 0x50, 0x59, 0x63, 0x67, 0x6A, 0x6F, 0x73, 0x75
        .byte   0x1E, 0x1E, 0x0F, 0x12, 0x1A, 0x26, 0x32, 0x3A, 0x3F, 0x45, 0x50, 0x60, 0x6E, 0x75, 0x7C, 0x7E, 0x80, 0x82
        .byte   0x1D, 0x20, 0x10, 0x11, 0x19, 0x26, 0x33, 0x3D, 0x45, 0x4F, 0x5C, 0x6D, 0x80, 0x87, 0x8A, 0x8C, 0x8C, 0x8E
        .byte   0x1B, 0x1F, 0x10, 0x12, 0x12, 0x23, 0x32, 0x3B, 0x41, 0x4C, 0x58, 0x68, 0x7C, 0x84, 0x8C, 0x92, 0x93, 0x94
        .byte   0x17, 0x19, 0x17, 0x0E, 0x13, 0x22, 0x2E, 0x38, 0x40, 0x49, 0x53, 0x62, 0x74, 0x7D, 0x83, 0x85, 0x88, 0x89
        .byte   0x14, 0x17, 0x1A, 0x10, 0x0C, 0x1E, 0x2B, 0x35, 0x3E, 0x45, 0x4E, 0x5A, 0x6A, 0x71, 0x77, 0x79, 0x7C, 0x82
        .byte   0x15, 0x15, 0x1D, 0x13, 0x0F, 0x1C, 0x2A, 0x31, 0x38, 0x3E, 0x46, 0x50, 0x5E, 0x6A, 0x72, 0x73, 0x73, 0x75
        .byte   0x15, 0x15, 0x17, 0x14, 0x0B, 0x18, 0x26, 0x2D, 0x34, 0x38, 0x3F, 0x48, 0x59, 0x66, 0x75, 0x77, 0x79, 0x76
        .byte   0x12, 0x14, 0x16, 0x15, 0x0F, 0x16, 0x23, 0x2C, 0x34, 0x37, 0x3F, 0x48, 0x58, 0x68, 0x72, 0x76, 0x79, 0x7B
        .byte   0x12, 0x16, 0x17, 0x16, 0x16, 0x12, 0x22, 0x2B, 0x34, 0x39, 0x40, 0x48, 0x58, 0x69, 0x76, 0x7C, 0x7F, 0x7E
        .byte   0x0E, 0x10, 0x14, 0x17, 0x19, 0x12, 0x22, 0x2B, 0x36, 0x3B, 0x42, 0x49, 0x57, 0x67, 0x76, 0x81, 0x8B, 0x8F
        .byte   0x0E, 0x0E, 0x14, 0x16, 0x1C, 0x10, 0x22, 0x29, 0x36, 0x3A, 0x41, 0x4A, 0x56, 0x68, 0x79, 0x84, 0x92, 0x95
        .byte   0x13, 0x12, 0x13, 0x13, 0x1B, 0x11, 0x20, 0x27, 0x35, 0x39, 0x41, 0x4A, 0x56, 0x68, 0x7A, 0x85, 0x90, 0x92
        .byte   0x10, 0x10, 0x10, 0x10, 0x16, 0x15, 0x1D, 0x25, 0x33, 0x38, 0x42, 0x4B, 0x59, 0x6B, 0x7D, 0x87, 0x93, 0x96
        .byte   0x10, 0x13, 0x13, 0x13, 0x15, 0x16, 0x18, 0x22, 0x31, 0x38, 0x40, 0x49, 0x59, 0x6C, 0x81, 0x8C, 0x9A, 0x9F
        .byte   0x10, 0x10, 0x10, 0x10, 0x18, 0x1B, 0x19, 0x23, 0x2F, 0x36, 0x3F, 0x49, 0x5A, 0x6C, 0x83, 0x8F, 0x9A, 0xA4
        .byte   0x10, 0x10, 0x10, 0x10, 0x16, 0x19, 0x19, 0x22, 0x2E, 0x37, 0x3F, 0x49, 0x59, 0x6B, 0x82, 0x8E, 0x99, 0xA0
        .byte   0x10, 0x10, 0x10, 0x10, 0x14, 0x17, 0x19, 0x20, 0x2E, 0x37, 0x3E, 0x48, 0x59, 0x6A, 0x7F, 0x8A, 0x95, 0x9C
        .byte   0x0F, 0x10, 0x13, 0x13, 0x15, 0x18, 0x19, 0x1F, 0x2A, 0x36, 0x3D, 0x46, 0x58, 0x69, 0x79, 0x87, 0x95, 0x9A
        .byte   0x0F, 0x10, 0x13, 0x14, 0x15, 0x18, 0x19, 0x22, 0x2E, 0x35, 0x3E, 0x4B, 0x55, 0x68, 0x78, 0x83, 0x91, 0x99
        .byte   0x10, 0x10, 0x13, 0x15, 0x17, 0x18, 0x1A, 0x22, 0x2B, 0x34, 0x40, 0x4E, 0x5C, 0x68, 0x76, 0x7E, 0x8B, 0x93
        .byte   0x10, 0x10, 0x13, 0x15, 0x17, 0x18, 0x1A, 0x22, 0x2B, 0x34, 0x40, 0x4E, 0x5C, 0x68, 0x71, 0x7E, 0x89, 0x93
        .byte   0x10, 0x10, 0x13, 0x15, 0x17, 0x18, 0x1A, 0x22, 0x2B, 0x34, 0x40, 0x4E, 0x5C, 0x68, 0x71, 0x7E, 0x89, 0x93
        .byte   0x10, 0x10, 0x13, 0x17, 0x17, 0x18, 0x1A, 0x22, 0x2B, 0x34, 0x40, 0x4E, 0x5C, 0x68, 0x72, 0x7E, 0x89, 0x92

;Fuel Map: 18x29 restricted ALPHA/N cyl 2
L864C:  .byte   0x25, 0x25, 0x29, 0x2C, 0x32, 0x3A, 0x41, 0x45, 0x49, 0x4E, 0x50, 0x51, 0x54, 0x57, 0x5B, 0x5D, 0x60, 0x63
        .byte   0x17, 0x28, 0x2B, 0x2F, 0x32, 0x3A, 0x41, 0x46, 0x4A, 0x4E, 0x50, 0x51, 0x54, 0x59, 0x5D, 0x5F, 0x62, 0x66
        .byte   0x0F, 0x0F, 0x23, 0x21, 0x31, 0x3A, 0x42, 0x49, 0x4D, 0x4F, 0x51, 0x53, 0x56, 0x5C, 0x60, 0x62, 0x64, 0x67
        .byte   0x0C, 0x08, 0x16, 0x1D, 0x22, 0x35, 0x3E, 0x48, 0x54, 0x5B, 0x5D, 0x5E, 0x5E, 0x5F, 0x63, 0x68, 0x6A, 0x6B
        .byte   0x11, 0x0B, 0x15, 0x1A, 0x21, 0x32, 0x3C, 0x44, 0x56, 0x61, 0x6A, 0x6F, 0x70, 0x72, 0x77, 0x7A, 0x7A, 0x78
        .byte   0x10, 0x0F, 0x10, 0x18, 0x1B, 0x2B, 0x35, 0x3F, 0x4A, 0x56, 0x65, 0x6D, 0x6D, 0x6F, 0x70, 0x73, 0x78, 0x7A
        .byte   0x10, 0x0F, 0x11, 0x0F, 0x16, 0x26, 0x31, 0x36, 0x3E, 0x4B, 0x58, 0x63, 0x6E, 0x75, 0x7B, 0x7E, 0x80, 0x7E
        .byte   0x11, 0x11, 0x0E, 0x10, 0x13, 0x25, 0x36, 0x42, 0x4B, 0x58, 0x63, 0x75, 0x8D, 0x98, 0x9C, 0x9F, 0xA1, 0xA1
        .byte   0x12, 0x11, 0x11, 0x14, 0x0F, 0x21, 0x36, 0x44, 0x50, 0x5E, 0x68, 0x7C, 0x8E, 0x96, 0x9A, 0x9F, 0xA1, 0xA0
        .byte   0x0F, 0x10, 0x12, 0x10, 0x11, 0x1E, 0x30, 0x42, 0x4B, 0x57, 0x63, 0x71, 0x84, 0x8E, 0x93, 0x97, 0x96, 0x95
        .byte   0x0D, 0x10, 0x16, 0x13, 0x10, 0x1A, 0x2D, 0x3B, 0x46, 0x4F, 0x58, 0x66, 0x79, 0x81, 0x81, 0x83, 0x85, 0x85
        .byte   0x0C, 0x0F, 0x17, 0x15, 0x0D, 0x1A, 0x2B, 0x37, 0x40, 0x47, 0x50, 0x5D, 0x6D, 0x78, 0x7D, 0x7F, 0x80, 0x82
        .byte   0x0C, 0x0D, 0x14, 0x13, 0x0B, 0x16, 0x27, 0x30, 0x3E, 0x44, 0x4A, 0x54, 0x64, 0x71, 0x7B, 0x7E, 0x7E, 0x7E
        .byte   0x0C, 0x0E, 0x13, 0x10, 0x0F, 0x13, 0x23, 0x2C, 0x38, 0x3F, 0x47, 0x4F, 0x62, 0x70, 0x7A, 0x7B, 0x7A, 0x78
        .byte   0x0B, 0x0C, 0x13, 0x12, 0x12, 0x12, 0x21, 0x2A, 0x37, 0x3C, 0x44, 0x4D, 0x60, 0x71, 0x80, 0x83, 0x82, 0x80
        .byte   0x09, 0x0C, 0x10, 0x12, 0x12, 0x12, 0x1F, 0x28, 0x38, 0x3D, 0x44, 0x4F, 0x5F, 0x71, 0x81, 0x86, 0x92, 0x92
        .byte   0x0B, 0x0E, 0x10, 0x10, 0x10, 0x11, 0x1E, 0x26, 0x37, 0x3D, 0x45, 0x52, 0x5E, 0x70, 0x81, 0x87, 0x92, 0x96
        .byte   0x0F, 0x0F, 0x0F, 0x0F, 0x10, 0x10, 0x1D, 0x24, 0x35, 0x3C, 0x44, 0x50, 0x5E, 0x6F, 0x80, 0x87, 0x8E, 0x8F
        .byte   0x0F, 0x0F, 0x0F, 0x0F, 0x10, 0x11, 0x1C, 0x23, 0x34, 0x3B, 0x44, 0x4E, 0x5C, 0x6E, 0x80, 0x87, 0x90, 0x91
        .byte   0x0E, 0x0F, 0x0F, 0x0F, 0x11, 0x12, 0x18, 0x20, 0x33, 0x39, 0x41, 0x4B, 0x59, 0x6C, 0x80, 0x89, 0x99, 0x9D
        .byte   0x0E, 0x0F, 0x0F, 0x0F, 0x11, 0x16, 0x1A, 0x21, 0x31, 0x39, 0x40, 0x4A, 0x57, 0x69, 0x7F, 0x89, 0x9A, 0xA0
        .byte   0x0E, 0x0E, 0x0E, 0x0E, 0x12, 0x16, 0x1B, 0x21, 0x30, 0x39, 0x41, 0x49, 0x58, 0x67, 0x7C, 0x87, 0x96, 0x9A
        .byte   0x0E, 0x0E, 0x0E, 0x0E, 0x10, 0x14, 0x1B, 0x20, 0x30, 0x3A, 0x41, 0x4A, 0x59, 0x68, 0x7B, 0x83, 0x91, 0x93
        .byte   0x0E, 0x0E, 0x0E, 0x0F, 0x10, 0x15, 0x19, 0x1F, 0x2F, 0x3B, 0x41, 0x4A, 0x59, 0x6A, 0x79, 0x83, 0x90, 0x92
        .byte   0x0E, 0x0E, 0x0F, 0x10, 0x14, 0x16, 0x19, 0x21, 0x31, 0x3C, 0x45, 0x51, 0x5A, 0x68, 0x7B, 0x88, 0x92, 0x93
        .byte   0x0E, 0x0F, 0x10, 0x10, 0x15, 0x16, 0x19, 0x21, 0x2B, 0x34, 0x40, 0x4B, 0x54, 0x66, 0x73, 0x81, 0x8B, 0x92
        .byte   0x0E, 0x0F, 0x10, 0x10, 0x15, 0x16, 0x1B, 0x21, 0x2C, 0x34, 0x40, 0x4B, 0x54, 0x65, 0x73, 0x80, 0x8B, 0x92
        .byte   0x0E, 0x0F, 0x10, 0x10, 0x15, 0x16, 0x1B, 0x21, 0x2B, 0x35, 0x40, 0x4B, 0x53, 0x66, 0x73, 0x80, 0x8B, 0x92
        .byte   0x0E, 0x0F, 0x10, 0x10, 0x15, 0x16, 0x1B, 0x21, 0x2C, 0x35, 0x40, 0x4A, 0x52, 0x65, 0x73, 0x80, 0x8B, 0x90

;Fuel Map: 16x29 derestricted MAP/N cyl 1
L8856:  .byte   0x12, 0x12, 0x12, 0x11, 0x13, 0x16, 0x1E, 0x24, 0x2B, 0x32, 0x40, 0x41, 0x57, 0x6F, 0x7F, 0x86
        .byte   0x11, 0x11, 0x0E, 0x0F, 0x0F, 0x14, 0x1E, 0x23, 0x28, 0x2D, 0x3A, 0x3B, 0x58, 0x6F, 0x7F, 0x83
        .byte   0x0F, 0x0F, 0x0D, 0x0D, 0x0C, 0x11, 0x19, 0x22, 0x27, 0x30, 0x33, 0x33, 0x56, 0x6E, 0x7D, 0x80
        .byte   0x0E, 0x0E, 0x0C, 0x0B, 0x0C, 0x14, 0x1C, 0x21, 0x25, 0x2D, 0x35, 0x38, 0x57, 0x6B, 0x7B, 0x7D
        .byte   0x0E, 0x0E, 0x0E, 0x10, 0x13, 0x18, 0x1C, 0x25, 0x2B, 0x32, 0x38, 0x3E, 0x5A, 0x6D, 0x7B, 0x7D
        .byte   0x10, 0x12, 0x13, 0x13, 0x12, 0x16, 0x1C, 0x24, 0x2A, 0x2E, 0x35, 0x37, 0x6E, 0x7B, 0x7F, 0x7F
        .byte   0x18, 0x1A, 0x1C, 0x16, 0x10, 0x11, 0x17, 0x1E, 0x22, 0x28, 0x2E, 0x2C, 0x5C, 0x74, 0x71, 0x7D
        .byte   0x18, 0x20, 0x1F, 0x17, 0x11, 0x14, 0x1A, 0x22, 0x26, 0x30, 0x32, 0x3A, 0x60, 0x72, 0x76, 0x7A
        .byte   0x15, 0x1F, 0x1C, 0x15, 0x14, 0x15, 0x1E, 0x27, 0x2C, 0x36, 0x39, 0x3E, 0x66, 0x74, 0x78, 0x7B
        .byte   0x12, 0x1C, 0x1B, 0x12, 0x13, 0x17, 0x21, 0x28, 0x2E, 0x34, 0x39, 0x37, 0x69, 0x73, 0x78, 0x7B
        .byte   0x11, 0x1B, 0x1B, 0x14, 0x15, 0x1D, 0x25, 0x2A, 0x30, 0x33, 0x37, 0x38, 0x6B, 0x73, 0x78, 0x7B
        .byte   0x11, 0x1A, 0x1B, 0x11, 0x18, 0x1D, 0x26, 0x2B, 0x2E, 0x2F, 0x31, 0x32, 0x68, 0x78, 0x7C, 0x7D
        .byte   0x13, 0x1A, 0x1B, 0x15, 0x15, 0x1D, 0x23, 0x28, 0x2B, 0x2C, 0x30, 0x33, 0x6B, 0x7C, 0x80, 0x81
        .byte   0x13, 0x18, 0x1A, 0x15, 0x15, 0x1C, 0x21, 0x27, 0x28, 0x2D, 0x2F, 0x36, 0x70, 0x84, 0x87, 0x88
        .byte   0x12, 0x16, 0x19, 0x16, 0x15, 0x1E, 0x21, 0x28, 0x2B, 0x2E, 0x31, 0x3C, 0x73, 0x87, 0x88, 0x8A
        .byte   0x11, 0x15, 0x19, 0x16, 0x18, 0x1D, 0x22, 0x29, 0x2F, 0x32, 0x36, 0x3C, 0x7E, 0x8A, 0x8B, 0x8C
        .byte   0x11, 0x13, 0x17, 0x12, 0x17, 0x1D, 0x25, 0x2C, 0x2F, 0x34, 0x3B, 0x41, 0x82, 0x90, 0x93, 0x95
        .byte   0x11, 0x14, 0x16, 0x15, 0x18, 0x1F, 0x25, 0x2B, 0x30, 0x35, 0x3C, 0x45, 0x86, 0x91, 0x97, 0x9C
        .byte   0x10, 0x14, 0x16, 0x15, 0x17, 0x1D, 0x22, 0x2A, 0x32, 0x37, 0x3D, 0x48, 0x86, 0x91, 0x9C, 0x9C
        .byte   0x0C, 0x11, 0x15, 0x16, 0x17, 0x1E, 0x25, 0x2A, 0x33, 0x39, 0x3D, 0x49, 0x83, 0x90, 0x9C, 0x9C
        .byte   0x0B, 0x0F, 0x14, 0x1A, 0x1B, 0x1F, 0x25, 0x2C, 0x33, 0x3A, 0x3E, 0x48, 0x85, 0x8E, 0x9C, 0x9C
        .byte   0x0B, 0x0E, 0x13, 0x1C, 0x1E, 0x1F, 0x25, 0x2D, 0x33, 0x39, 0x3E, 0x4B, 0x85, 0x8A, 0x9C, 0x9C
        .byte   0x08, 0x0B, 0x12, 0x1E, 0x21, 0x24, 0x24, 0x2D, 0x36, 0x38, 0x3D, 0x4E, 0x87, 0x89, 0x9A, 0x9C
        .byte   0x07, 0x0B, 0x12, 0x20, 0x24, 0x24, 0x2A, 0x2D, 0x34, 0x38, 0x43, 0x4F, 0x8A, 0x8A, 0x97, 0x9C
        .byte   0x07, 0x0C, 0x12, 0x20, 0x25, 0x29, 0x2E, 0x34, 0x35, 0x37, 0x43, 0x4D, 0x8C, 0x8D, 0x95, 0x9C
        .byte   0x07, 0x0C, 0x12, 0x20, 0x25, 0x29, 0x2F, 0x38, 0x3D, 0x41, 0x43, 0x5B, 0x94, 0x97, 0x94, 0x9C
        .byte   0x07, 0x0C, 0x13, 0x20, 0x25, 0x29, 0x2F, 0x38, 0x3D, 0x41, 0x43, 0x5B, 0x9C, 0x9C, 0x9C, 0x9C
        .byte   0x06, 0x0C, 0x13, 0x20, 0x25, 0x29, 0x2F, 0x38, 0x3D, 0x41, 0x43, 0x5B, 0x9C, 0x9C, 0x9C, 0x9C
        .byte   0x06, 0x0B, 0x13, 0x20, 0x25, 0x29, 0x2F, 0x38, 0x3D, 0x41, 0x43, 0x5B, 0x9C, 0x9C, 0x9C, 0x9C

;Fuel Map: 16x29 derestricted MAP/N cyl 2
L8A26:  .byte   0x11, 0x10, 0x0F, 0x0E, 0x0E, 0x0E, 0x12, 0x13, 0x1C, 0x32, 0x42, 0x45, 0x62, 0x77, 0x7F, 0x86
        .byte   0x10, 0x10, 0x0E, 0x0D, 0x0C, 0x0E, 0x11, 0x16, 0x1C, 0x30, 0x41, 0x43, 0x65, 0x77, 0x7D, 0x87
        .byte   0x10, 0x0F, 0x0E, 0x0C, 0x0A, 0x0F, 0x13, 0x1A, 0x22, 0x2A, 0x32, 0x3C, 0x65, 0x76, 0x7D, 0x86
        .byte   0x0F, 0x0F, 0x0E, 0x10, 0x0F, 0x0E, 0x16, 0x1C, 0x26, 0x2E, 0x33, 0x3B, 0x65, 0x76, 0x7C, 0x85
        .byte   0x10, 0x0F, 0x10, 0x15, 0x0E, 0x12, 0x1C, 0x24, 0x2C, 0x33, 0x37, 0x43, 0x68, 0x75, 0x7C, 0x84
        .byte   0x13, 0x14, 0x15, 0x15, 0x0D, 0x16, 0x1F, 0x27, 0x2E, 0x35, 0x39, 0x45, 0x68, 0x74, 0x7B, 0x80
        .byte   0x14, 0x17, 0x16, 0x0E, 0x0E, 0x11, 0x1A, 0x21, 0x2A, 0x2D, 0x39, 0x3D, 0x68, 0x6F, 0x74, 0x79
        .byte   0x13, 0x17, 0x18, 0x15, 0x10, 0x13, 0x1D, 0x25, 0x2F, 0x39, 0x3E, 0x4E, 0x59, 0x69, 0x6F, 0x73
        .byte   0x13, 0x16, 0x1A, 0x1A, 0x0E, 0x17, 0x23, 0x30, 0x3A, 0x3E, 0x45, 0x4B, 0x55, 0x68, 0x66, 0x6C
        .byte   0x10, 0x13, 0x1A, 0x19, 0x14, 0x1B, 0x27, 0x32, 0x3B, 0x40, 0x43, 0x4D, 0x61, 0x6D, 0x6F, 0x74
        .byte   0x0E, 0x12, 0x19, 0x18, 0x16, 0x20, 0x2C, 0x36, 0x3C, 0x3F, 0x44, 0x4D, 0x66, 0x75, 0x77, 0x7A
        .byte   0x0D, 0x11, 0x18, 0x12, 0x19, 0x25, 0x2D, 0x34, 0x38, 0x3C, 0x3E, 0x49, 0x65, 0x77, 0x79, 0x7A
        .byte   0x0C, 0x11, 0x15, 0x11, 0x18, 0x1F, 0x28, 0x30, 0x35, 0x38, 0x3F, 0x49, 0x66, 0x7D, 0x7F, 0x7F
        .byte   0x0C, 0x11, 0x17, 0x11, 0x16, 0x1D, 0x25, 0x29, 0x2F, 0x33, 0x3A, 0x45, 0x68, 0x83, 0x85, 0x85
        .byte   0x0C, 0x10, 0x16, 0x12, 0x1A, 0x1E, 0x25, 0x2E, 0x33, 0x37, 0x3E, 0x44, 0x68, 0x84, 0x89, 0x89
        .byte   0x0C, 0x0E, 0x11, 0x13, 0x1A, 0x1F, 0x26, 0x2F, 0x33, 0x39, 0x3F, 0x45, 0x70, 0x8E, 0x8F, 0x90
        .byte   0x0D, 0x0E, 0x0E, 0x13, 0x1B, 0x21, 0x28, 0x32, 0x39, 0x3F, 0x47, 0x4A, 0x7B, 0x94, 0x96, 0x95
        .byte   0x0C, 0x0E, 0x0E, 0x13, 0x1E, 0x22, 0x2C, 0x36, 0x3D, 0x42, 0x47, 0x4D, 0x81, 0x9C, 0x9C, 0x9C
        .byte   0x0C, 0x0F, 0x0F, 0x13, 0x1E, 0x25, 0x2E, 0x37, 0x3D, 0x42, 0x47, 0x4D, 0x82, 0x9C, 0x9C, 0x9C
        .byte   0x09, 0x0D, 0x10, 0x16, 0x1E, 0x24, 0x2E, 0x37, 0x3D, 0x42, 0x48, 0x4D, 0x82, 0x9C, 0x9C, 0x9C
        .byte   0x05, 0x0A, 0x11, 0x17, 0x1F, 0x24, 0x2D, 0x37, 0x3D, 0x41, 0x47, 0x4E, 0x84, 0x9C, 0x9C, 0x9C
        .byte   0x05, 0x09, 0x12, 0x17, 0x21, 0x26, 0x2D, 0x36, 0x3D, 0x41, 0x46, 0x4F, 0x84, 0x9C, 0x9C, 0x9C
        .byte   0x04, 0x09, 0x13, 0x17, 0x26, 0x29, 0x31, 0x39, 0x3D, 0x42, 0x47, 0x50, 0x83, 0x9C, 0x9C, 0x9C
        .byte   0x06, 0x0B, 0x13, 0x18, 0x27, 0x2C, 0x31, 0x39, 0x3D, 0x42, 0x47, 0x51, 0x84, 0x9C, 0x9C, 0x9C
        .byte   0x06, 0x0B, 0x13, 0x18, 0x27, 0x2C, 0x31, 0x3A, 0x3D, 0x44, 0x47, 0x53, 0x86, 0x9C, 0x9C, 0x9C
        .byte   0x05, 0x0B, 0x13, 0x18, 0x27, 0x2C, 0x31, 0x39, 0x3D, 0x44, 0x47, 0x58, 0x88, 0x9C, 0x9C, 0x9C
        .byte   0x05, 0x0B, 0x13, 0x18, 0x27, 0x2C, 0x31, 0x39, 0x3D, 0x44, 0x47, 0x5D, 0x8D, 0x9C, 0x9C, 0x9C
        .byte   0x05, 0x0B, 0x13, 0x18, 0x27, 0x2C, 0x31, 0x39, 0x3D, 0x44, 0x47, 0x5D, 0x8D, 0x9C, 0x9C, 0x9C
        .byte   0x05, 0x0B, 0x13, 0x18, 0x27, 0x2C, 0x31, 0x39, 0x3D, 0x44, 0x47, 0x5D, 0x8D, 0x9C, 0x9C, 0x9C

;Fuel Map: 18x29 derestricted ALPHA/N cyl 1
L8BF6:  .byte   0x27, 0x29, 0x30, 0x2E, 0x32, 0x37, 0x3C, 0x40, 0x44, 0x47, 0x49, 0x4D, 0x4F, 0x51, 0x55, 0x58, 0x5B, 0x5E
        .byte   0x1E, 0x27, 0x31, 0x2F, 0x33, 0x38, 0x3B, 0x3E, 0x43, 0x48, 0x4B, 0x4F, 0x51, 0x54, 0x58, 0x5A, 0x5D, 0x60
        .byte   0x16, 0x19, 0x25, 0x28, 0x30, 0x38, 0x3F, 0x45, 0x4B, 0x4F, 0x52, 0x54, 0x55, 0x59, 0x5E, 0x62, 0x65, 0x69
        .byte   0x14, 0x18, 0x1A, 0x22, 0x28, 0x32, 0x3A, 0x46, 0x54, 0x5C, 0x61, 0x65, 0x69, 0x6C, 0x6D, 0x6F, 0x72, 0x76
        .byte   0x11, 0x14, 0x1D, 0x20, 0x26, 0x36, 0x41, 0x49, 0x58, 0x64, 0x74, 0x7C, 0x7F, 0x80, 0x80, 0x81, 0x82, 0x80
        .byte   0x0F, 0x0E, 0x12, 0x1B, 0x1E, 0x2E, 0x33, 0x39, 0x42, 0x48, 0x50, 0x59, 0x63, 0x67, 0x6A, 0x6F, 0x73, 0x75
        .byte   0x1E, 0x1E, 0x0F, 0x12, 0x1A, 0x26, 0x32, 0x3A, 0x3F, 0x45, 0x50, 0x60, 0x6E, 0x75, 0x7C, 0x7E, 0x80, 0x82
        .byte   0x1D, 0x20, 0x10, 0x11, 0x19, 0x26, 0x33, 0x3D, 0x45, 0x4F, 0x5C, 0x6D, 0x80, 0x87, 0x8A, 0x8C, 0x8C, 0x8E
        .byte   0x1B, 0x1F, 0x10, 0x12, 0x12, 0x23, 0x32, 0x3B, 0x41, 0x4C, 0x58, 0x68, 0x7C, 0x84, 0x8C, 0x92, 0x93, 0x94
        .byte   0x17, 0x19, 0x17, 0x0E, 0x13, 0x22, 0x2E, 0x38, 0x40, 0x49, 0x53, 0x62, 0x74, 0x7D, 0x83, 0x85, 0x88, 0x89
        .byte   0x14, 0x17, 0x1A, 0x10, 0x0C, 0x1E, 0x2B, 0x35, 0x3E, 0x45, 0x4E, 0x5A, 0x6A, 0x71, 0x77, 0x79, 0x7C, 0x82
        .byte   0x15, 0x15, 0x1D, 0x13, 0x0F, 0x1C, 0x2A, 0x31, 0x38, 0x3E, 0x46, 0x50, 0x5E, 0x6A, 0x72, 0x73, 0x73, 0x75
        .byte   0x15, 0x15, 0x17, 0x14, 0x0B, 0x18, 0x26, 0x2D, 0x34, 0x38, 0x3F, 0x48, 0x59, 0x66, 0x75, 0x77, 0x79, 0x76
        .byte   0x12, 0x14, 0x16, 0x15, 0x0F, 0x16, 0x23, 0x2C, 0x34, 0x37, 0x3F, 0x48, 0x58, 0x68, 0x72, 0x76, 0x79, 0x7B
        .byte   0x12, 0x16, 0x17, 0x16, 0x16, 0x12, 0x22, 0x2B, 0x34, 0x39, 0x40, 0x48, 0x58, 0x69, 0x76, 0x7C, 0x7F, 0x7E
        .byte   0x0E, 0x10, 0x14, 0x17, 0x19, 0x12, 0x22, 0x2B, 0x36, 0x3B, 0x42, 0x49, 0x57, 0x67, 0x76, 0x81, 0x8B, 0x8F
        .byte   0x0E, 0x0E, 0x14, 0x16, 0x1C, 0x10, 0x22, 0x29, 0x36, 0x3A, 0x41, 0x4A, 0x56, 0x68, 0x79, 0x84, 0x92, 0x95
        .byte   0x13, 0x12, 0x13, 0x13, 0x1B, 0x11, 0x20, 0x27, 0x35, 0x39, 0x41, 0x4A, 0x56, 0x68, 0x7A, 0x85, 0x90, 0x92
        .byte   0x10, 0x10, 0x10, 0x10, 0x16, 0x15, 0x1D, 0x25, 0x33, 0x38, 0x42, 0x4B, 0x59, 0x6B, 0x7D, 0x87, 0x93, 0x96
        .byte   0x10, 0x13, 0x13, 0x13, 0x15, 0x16, 0x18, 0x22, 0x31, 0x38, 0x40, 0x49, 0x59, 0x6C, 0x81, 0x8C, 0x9A, 0x9F
        .byte   0x10, 0x10, 0x10, 0x10, 0x18, 0x1B, 0x19, 0x23, 0x2F, 0x36, 0x3F, 0x49, 0x5A, 0x6C, 0x83, 0x8F, 0x9A, 0xA4
        .byte   0x10, 0x10, 0x10, 0x10, 0x16, 0x19, 0x19, 0x22, 0x2E, 0x37, 0x3F, 0x49, 0x59, 0x6B, 0x82, 0x8E, 0x99, 0xA0
        .byte   0x10, 0x10, 0x10, 0x10, 0x14, 0x17, 0x19, 0x20, 0x2E, 0x37, 0x3E, 0x48, 0x59, 0x6A, 0x7F, 0x8A, 0x95, 0x9C
        .byte   0x0F, 0x10, 0x13, 0x13, 0x15, 0x18, 0x19, 0x1F, 0x2A, 0x36, 0x3D, 0x46, 0x58, 0x69, 0x79, 0x87, 0x95, 0x9A
        .byte   0x0F, 0x10, 0x13, 0x14, 0x15, 0x18, 0x19, 0x22, 0x2E, 0x35, 0x3E, 0x4B, 0x55, 0x68, 0x78, 0x83, 0x91, 0x99
        .byte   0x10, 0x10, 0x13, 0x15, 0x17, 0x18, 0x1A, 0x22, 0x2B, 0x34, 0x40, 0x4E, 0x5C, 0x68, 0x76, 0x7E, 0x8B, 0x93
        .byte   0x10, 0x10, 0x13, 0x15, 0x17, 0x18, 0x1A, 0x22, 0x2B, 0x34, 0x40, 0x4E, 0x5C, 0x68, 0x71, 0x7E, 0x89, 0x93
        .byte   0x10, 0x10, 0x13, 0x15, 0x17, 0x18, 0x1A, 0x22, 0x2B, 0x34, 0x40, 0x4E, 0x5C, 0x68, 0x71, 0x7E, 0x89, 0x93
        .byte   0x10, 0x10, 0x13, 0x17, 0x17, 0x18, 0x1A, 0x22, 0x2B, 0x34, 0x40, 0x4E, 0x5C, 0x68, 0x72, 0x7E, 0x89, 0x92

;Fuel Map: 18x29 derestricted ALPHA/N cyl 2
L8E00:  .byte   0x25, 0x25, 0x29, 0x2C, 0x32, 0x3A, 0x41, 0x45, 0x49, 0x4E, 0x50, 0x51, 0x54, 0x57, 0x5B, 0x5D, 0x60, 0x63
        .byte   0x17, 0x28, 0x2B, 0x2F, 0x32, 0x3A, 0x41, 0x46, 0x4A, 0x4E, 0x50, 0x51, 0x54, 0x59, 0x5D, 0x5F, 0x62, 0x66
        .byte   0x0F, 0x0F, 0x23, 0x21, 0x31, 0x3A, 0x42, 0x49, 0x4D, 0x4F, 0x51, 0x53, 0x56, 0x5C, 0x60, 0x62, 0x64, 0x67
        .byte   0x0C, 0x08, 0x16, 0x1D, 0x22, 0x35, 0x3E, 0x48, 0x54, 0x5B, 0x5D, 0x5E, 0x5E, 0x5F, 0x63, 0x68, 0x6A, 0x6B
        .byte   0x11, 0x0B, 0x15, 0x1A, 0x21, 0x32, 0x3C, 0x44, 0x56, 0x61, 0x6A, 0x6F, 0x70, 0x72, 0x77, 0x7A, 0x7A, 0x78
        .byte   0x10, 0x0F, 0x10, 0x18, 0x1B, 0x2B, 0x35, 0x3F, 0x4A, 0x56, 0x65, 0x6D, 0x6D, 0x6F, 0x70, 0x73, 0x78, 0x7A
        .byte   0x10, 0x0F, 0x11, 0x0F, 0x16, 0x26, 0x31, 0x36, 0x3E, 0x4B, 0x58, 0x63, 0x6E, 0x75, 0x7B, 0x7E, 0x80, 0x7E
        .byte   0x11, 0x11, 0x0E, 0x10, 0x13, 0x25, 0x36, 0x42, 0x4B, 0x58, 0x63, 0x75, 0x8D, 0x98, 0x9C, 0x9F, 0xA1, 0xA1
        .byte   0x12, 0x11, 0x11, 0x14, 0x0F, 0x21, 0x36, 0x44, 0x50, 0x5E, 0x68, 0x7C, 0x8E, 0x96, 0x9A, 0x9F, 0xA1, 0xA0
        .byte   0x0F, 0x10, 0x12, 0x10, 0x11, 0x1E, 0x30, 0x42, 0x4B, 0x57, 0x63, 0x71, 0x84, 0x8E, 0x93, 0x97, 0x96, 0x95
        .byte   0x0D, 0x10, 0x16, 0x13, 0x10, 0x1A, 0x2D, 0x3B, 0x46, 0x4F, 0x58, 0x66, 0x79, 0x81, 0x81, 0x83, 0x85, 0x85
        .byte   0x0C, 0x0F, 0x17, 0x15, 0x0D, 0x1A, 0x2B, 0x37, 0x40, 0x47, 0x50, 0x5D, 0x6D, 0x78, 0x7D, 0x7F, 0x80, 0x82
        .byte   0x0C, 0x0D, 0x14, 0x13, 0x0B, 0x16, 0x27, 0x30, 0x3E, 0x44, 0x4A, 0x54, 0x64, 0x71, 0x7B, 0x7E, 0x7E, 0x7E
        .byte   0x0C, 0x0E, 0x13, 0x10, 0x0F, 0x13, 0x23, 0x2C, 0x38, 0x3F, 0x47, 0x4F, 0x62, 0x70, 0x7A, 0x7B, 0x7A, 0x78
        .byte   0x0B, 0x0C, 0x13, 0x12, 0x12, 0x12, 0x21, 0x2A, 0x37, 0x3C, 0x44, 0x4D, 0x60, 0x71, 0x80, 0x83, 0x82, 0x80
        .byte   0x09, 0x0C, 0x10, 0x12, 0x12, 0x12, 0x1F, 0x28, 0x38, 0x3D, 0x44, 0x4F, 0x5F, 0x71, 0x81, 0x86, 0x92, 0x92
        .byte   0x0B, 0x0E, 0x10, 0x10, 0x10, 0x11, 0x1E, 0x26, 0x37, 0x3D, 0x45, 0x52, 0x5E, 0x70, 0x81, 0x87, 0x92, 0x96
        .byte   0x0F, 0x0F, 0x0F, 0x0F, 0x10, 0x10, 0x1D, 0x24, 0x35, 0x3C, 0x44, 0x50, 0x5E, 0x6F, 0x80, 0x87, 0x8E, 0x8F
        .byte   0x0F, 0x0F, 0x0F, 0x0F, 0x10, 0x11, 0x1C, 0x23, 0x34, 0x3B, 0x44, 0x4E, 0x5C, 0x6E, 0x80, 0x87, 0x90, 0x91
        .byte   0x0E, 0x0F, 0x0F, 0x0F, 0x11, 0x12, 0x18, 0x20, 0x33, 0x39, 0x41, 0x4B, 0x59, 0x6C, 0x80, 0x89, 0x99, 0x9D
        .byte   0x0E, 0x0F, 0x0F, 0x0F, 0x11, 0x16, 0x1A, 0x21, 0x31, 0x39, 0x40, 0x4A, 0x57, 0x69, 0x7F, 0x89, 0x9A, 0xA0
        .byte   0x0E, 0x0E, 0x0E, 0x0E, 0x12, 0x16, 0x1B, 0x21, 0x30, 0x39, 0x41, 0x49, 0x58, 0x67, 0x7C, 0x87, 0x96, 0x9A
        .byte   0x0E, 0x0E, 0x0E, 0x0E, 0x10, 0x14, 0x1B, 0x20, 0x30, 0x3A, 0x41, 0x4A, 0x59, 0x68, 0x7B, 0x83, 0x91, 0x93
        .byte   0x0E, 0x0E, 0x0E, 0x0F, 0x10, 0x15, 0x19, 0x1F, 0x2F, 0x3B, 0x41, 0x4A, 0x59, 0x6A, 0x79, 0x83, 0x90, 0x92
        .byte   0x0E, 0x0E, 0x0F, 0x10, 0x14, 0x16, 0x19, 0x21, 0x31, 0x3C, 0x45, 0x51, 0x5A, 0x68, 0x7B, 0x88, 0x92, 0x93
        .byte   0x0E, 0x0F, 0x10, 0x10, 0x15, 0x16, 0x19, 0x21, 0x2B, 0x34, 0x40, 0x4B, 0x54, 0x66, 0x73, 0x81, 0x8B, 0x92
        .byte   0x0E, 0x0F, 0x10, 0x10, 0x15, 0x16, 0x1B, 0x21, 0x2C, 0x34, 0x40, 0x4B, 0x54, 0x65, 0x73, 0x80, 0x8B, 0x92
        .byte   0x0E, 0x0F, 0x10, 0x10, 0x15, 0x16, 0x1B, 0x21, 0x2B, 0x35, 0x40, 0x4B, 0x53, 0x66, 0x73, 0x80, 0x8B, 0x92
        .byte   0x0E, 0x0F, 0x10, 0x10, 0x15, 0x16, 0x1B, 0x21, 0x2C, 0x35, 0x40, 0x4A, 0x52, 0x65, 0x73, 0x80, 0x8B, 0x90

L900A:  .byte   0x00
L900B:  .byte   0x70
L900C:  .byte   0x90
L900D:  .byte   0x7D
L900E:  .byte   0x7D
L900F:  .byte   0x18

L9010:  .byte   0x76

L9011:  .byte   0x10, 0xC0, 0x08
        .byte   0xAC, 0x8F, 0x71, 0x67, 0x55, 0x40, 0x2D, 0x70

        .byte   0x70, 0x10, 0x10, 0x1E
        .byte   0x31, 0x4C, 0x69, 0x87
        .byte   0x9F, 0xB8, 0x70, 0x70
        .byte   0x10, 0x43, 0x4F, 0x5A
        .byte   0x63, 0x70, 0x7C, 0x88
        .byte   0x95

;Table used to convert THW water temp into an ?injection stretcher?
L9031:  .byte   0x10, 0xE0, 0x08
        .byte   0xA7, 0x6C, 0x43, 0x23, 0x13, 0x0A, 0x04, 0x04

;Table used to convert THW (water temp) into ?
L903C:  .byte   0x10, 0xE0, 0x08
        .byte   0x34, 0x24, 0x1C, 0x15, 0x0F, 0x09, 0x05, 0x03

L9047:  .byte   0x7D

L9048:  .byte   0x54

;A Table used to convert THW (water temp) into ?
L9049:  .byte   0x10, 0xE0, 0x08
        .byte   0x43, 0x38, 0x2C, 0x1A, 0x10, 0x09, 0x00, 0x00

;A Table used to convert THW (water temp) into ?
L9054:  .byte   0x10, 0xE0, 0x08
        .byte   0x5A, 0x48, 0x36, 0x20, 0x10, 0x08, 0x00, 0x00

; One of these next two constants will be chosen depending if the clutch is engaged or not
; Give that they have the same value, it looks like dead code.
L905F:  .byte   0x3C
L9060:  .byte   0x3C

L9061:  .byte   0x22
L9062:  .byte   0x88
L9063:  .byte   0x04
L9064:  .byte   0x1A
L9065:  .byte   0x02
L9066:  .byte   0xC8
L9067:  .byte   0x01
L9068:  .byte   0x0A
L9069:  .byte   0x06

L906A:  .byte   0x10, 0xE0, 0x08
        .byte   0x35, 0x2B, 0x24, 0x20, 0x1D, 0x1C, 0x1B, 0x1B

L9075:  .byte   0x10, 0xE0, 0x08
        .byte   0xE8, 0x8F, 0x65, 0x49, 0x36, 0x36, 0x36, 0x36

        .byte   0x57, 0x4A, 0x42, 0x3D, 0x3A, 0x3A, 0x38, 0x34, 0x2C, 0x1E
        .byte   0x4D, 0x45, 0x40, 0x3C, 0x3A, 0x3A, 0x3B, 0x43, 0x5A, 0x78

; One of these next two constants will be chosen depending if the clutch is engaged or not
; Give that they have the same value, it looks like dead code.
L9094:  .byte   0x3C
L9095:  .byte   0x3C

L9096:  .byte   0x22
L9097:  .byte   0x88
L9098:  .byte   0x04
L9099:  .byte   0x1A
L909A:  .byte   0x02
L909B:  .byte   0xC8
L909C:  .byte   0x01
L909D:  .byte   0x0A
L909E:  .byte   0x06

L909F:  .byte   0x10, 0xE0, 0x08
        .byte   0x35, 0x2B, 0x24, 0x20, 0x1D, 0x1C, 0x1B, 0x1B

L90AA:  .byte   0x10, 0xE0, 0x08
        .byte   0xE8, 0x8F, 0x65, 0x49, 0x36, 0x36, 0x36, 0x36

        .byte   0x57, 0x4A, 0x42, 0x3D, 0x3A, 0x3A, 0x38, 0x34, 0x2C
        .byte   0x1E, 0x4D, 0x74, 0x40
        .byte   0x3C, 0x3A, 0x3A, 0x3B
        .byte   0x43, 0x5A, 0x78, 0x70
        .byte   0x70, 0x70, 0x01, 0x01
        .byte   0x01
L90CF:  .byte   0x80

L90D0:  .byte   0x18

;Table used to convert a raw VM A/D into something else.
L90D1:  .byte   0x53, 0x80, 0x08
        .byte   0xD2, 0x70, 0x48, 0x30, 0x21

L90D9:  .byte   0xBE
L90DA:  .byte   0x66
L90DB:  .byte   0x86
L90DC:  .word   0x0100
L90DE:  .byte   0x86
L90DF:  .byte   0x08
L90E0:  .byte   0x40
L90E1:  .byte   0x04
L90E2:  .byte   0x05
L90E3:  .byte   0x40

L90E4:  .byte   0x80, 0x30, 0x20
        .byte   0x07, 0x08, 0x0A, 0x0B, 0x0D, 0x1D, 0x45

L90EE:  .byte   0x20, 0x80, 0x08
        .byte   0x0F, 0x13, 0x16, 0x18, 0x1A

L90F6:  .byte   0x10, 0xE0, 0x08
        .byte   0x2F, 0x27, 0x20, 0x1C, 0x1A, 0x15, 0x11, 0x0D

L9101:  .byte   0x10, 0xC0, 0x08
        .byte   0xF1, 0xC1, 0x8C, 0x68, 0x4A, 0x30, 0x19

L910B:  .byte   0x70, 0x70, 0x10
        .byte   0x2E, 0x40, 0x50, 0x63, 0x78, 0x8E, 0xA0, 0xB0

L9116:  .byte   0xBE
L9117:  .byte   0x66
L9118:  .byte   0x86
L9119:  .word   0x0100
L911B:  .byte   0x86
L911C:  .byte   0x08
L911D:  .byte   0x40
L911E:  .byte   0x04
L911F:  .byte   0x05

L9120:  .byte   0x40

L9121:  .byte   0x80, 0x30, 0x20
        .byte   0x07, 0x08, 0x0A, 0x0B, 0x0D, 0x1D, 0x45

L912B:  .byte   0x20, 0x80, 0x08
        .byte   0x0F, 0x13, 0x16, 0x18, 0x1A

L9133:  .byte   0x10, 0xE0, 0x08
        .byte   0x2F, 0x27, 0x20, 0x1C, 0x1A, 0x15, 0x11, 0x0D

L913E:  .byte   0x10, 0xC0, 0x08
        .byte   0xF1, 0xC1, 0x8C, 0x68, 0x4A, 0x30, 0x19

L9148:  .byte   0x70, 0x70, 0x10
        .byte   0x2E, 0x40, 0x50, 0x63, 0x78, 0x8E, 0xA0, 0xB0

; This table is used by CO trimpot processing for both CO1 and CO2.
L9153:  .byte   0x10, 0x60, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80
; from Gabro's P08 with trimpots active:
;L9153: .byte   0x10, 0x60, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80
;From P08 with no trimpots
;L9153: .byte   0x10, 0x60, 0x10, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80

; This table is used by CO trimpot processing for both CO1 and CO2.
L915D:  .byte   0x90, 0x60, 0x10, 0x80, 0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0
;from Gabro's P08 with trimpots active:
;L915D: .byte   0x90, 0x60, 0x10, 0x80, 0x90, 0xA0, 0xB0, 0xC2, 0xDB, 0xFD
;From P08 with no trimpots:
;L915D: .byte   0x90, 0x60, 0x10, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80

L9167:  .byte   0xFF, 0xFF, 0xFF        ;0x18, 0x38, 0x20 in both normal P08 (no trimpots) and Gabro's P08 code with trimpots active
        .byte   0xFF, 0xFF, 0xFF, 0xFF
        .byte   0xE4, 0xB7
        .byte   0x6A, 0x00

L9172:  .byte   0xD2                    ;restricted
L9173:  .byte   0xD2                    ;restricted

L9174:  .byte   0x7D
L9175:  .byte   0xFF
L9176:  .byte   0x86
L9177:  .byte   0x0A
L9178:  .byte   0x3C
L9179:  .byte   0x42
L917A:  .byte   0x62
L917B:  .byte   0x66

L917C:  .byte   0x20, 0x90, 0x10
        .byte   0x86, 0x89, 0x8A, 0x8A, 0x8A, 0x8A, 0x8A, 0x8A

        .byte   0x8A, 0x8A
L9189:  .byte   0x04
L918A:  .byte   0x04
L918B:  .byte   0x05
L918C:  .byte   0x63
L918D:  .byte   0x28
L918E:  .byte   0xC4
L918F:  .byte   0xC8
L9190:  .byte   0x82
L9191:  .byte   0x78
L9192:  .byte   0xBA, 0x80, 0x70, 0x10
        .byte   0x7B, 0x7B, 0x7B, 0x7B
        .byte   0x7B, 0x7B, 0x7B, 0x7B
        .byte   0x00, 0x1C, 0x40, 0x80
        .byte   0x80, 0x80, 0x80, 0x80
        .byte   0x80, 0x80, 0x80

L91A9:  .byte   0x1B                    ;the MSbyte of a 1/6 crank period: 0x1B == 0x1B00 ->  725 RPM
L91AA:  .byte   0x12                    ;the MSbyte of a 1/6 crank period: 0x12 == 0x1200 -> 1085 RPM
L91AB:  .byte   0x05                    ;the MSbyte of a 1/6 crank period: 0x05 == 0x0500 -> 3906 RPM
L91AC:  .byte   0x61                    ;the MSbyte of a 1/6 crank period: 0x61 == 0x6100 ->  200 RPM
L91AD:  .byte   0x24                    ;the MSbyte of a 1/6 crank period: 0x24 == 0x2400 ->  540 RPM

L91AE:  .byte   0x55
L91AF:  .byte   0xD5
L91B0:  .byte   0x19
L91B1:  .byte   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        .byte   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        .byte   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00

L91C9:  .byte   0x64
L91CA:  .byte   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        .byte   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        .byte   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00

L91E2:  .byte   0x80
L91E3:  .byte   0x00
L91E4:  .byte   0x00

;L91E5: Ignition map 18x29 Restricted ALPHA/N cyl 1
L91E5:  .byte   0x59, 0x59, 0x59, 0x61, 0x63, 0x66, 0x6B, 0x6D, 0x6E, 0x6E, 0x6A, 0x67, 0x63, 0x5F, 0x5A, 0x56, 0x54, 0x54
        .byte   0x5A, 0x5A, 0x5A, 0x62, 0x69, 0x6C, 0x6E, 0x70, 0x70, 0x71, 0x6D, 0x69, 0x65, 0x61, 0x5C, 0x58, 0x56, 0x56
        .byte   0x60, 0x68, 0x6B, 0x6C, 0x70, 0x70, 0x72, 0x72, 0x72, 0x72, 0x6E, 0x6B, 0x67, 0x63, 0x5E, 0x5A, 0x59, 0x59
        .byte   0x66, 0x73, 0x74, 0x74, 0x75, 0x73, 0x73, 0x75, 0x75, 0x75, 0x71, 0x6D, 0x69, 0x65, 0x61, 0x5D, 0x5C, 0x5C
        .byte   0x6B, 0x76, 0x77, 0x77, 0x77, 0x76, 0x75, 0x77, 0x77, 0x76, 0x73, 0x6F, 0x6C, 0x68, 0x64, 0x60, 0x5F, 0x5F
        .byte   0x73, 0x7A, 0x7B, 0x7B, 0x7A, 0x79, 0x78, 0x79, 0x79, 0x79, 0x76, 0x73, 0x70, 0x6C, 0x67, 0x64, 0x63, 0x63
        .byte   0x7E, 0x7F, 0x80, 0x7F, 0x7D, 0x7D, 0x7D, 0x7D, 0x7C, 0x7C, 0x79, 0x77, 0x74, 0x71, 0x69, 0x66, 0x65, 0x65
        .byte   0x87, 0x8A, 0x88, 0x84, 0x84, 0x82, 0x81, 0x82, 0x81, 0x80, 0x7D, 0x7B, 0x79, 0x75, 0x6D, 0x68, 0x66, 0x66
        .byte   0x91, 0x91, 0x90, 0x8E, 0x8B, 0x8A, 0x87, 0x87, 0x87, 0x87, 0x85, 0x83, 0x7F, 0x7B, 0x74, 0x6D, 0x67, 0x67
        .byte   0x9A, 0x99, 0x97, 0x97, 0x94, 0x94, 0x93, 0x92, 0x91, 0x8F, 0x8C, 0x8A, 0x84, 0x80, 0x7A, 0x73, 0x6D, 0x6C
        .byte   0xA4, 0xA2, 0xA0, 0x9F, 0x9F, 0xA1, 0x9F, 0x9B, 0x98, 0x94, 0x90, 0x8C, 0x87, 0x84, 0x81, 0x7A, 0x72, 0x72
        .byte   0xAF, 0xB0, 0xB1, 0xB2, 0xB2, 0xB2, 0xAF, 0xA8, 0xA1, 0x98, 0x93, 0x90, 0x8B, 0x88, 0x86, 0x81, 0x79, 0x78
        .byte   0xB8, 0xBA, 0xBB, 0xBD, 0xBF, 0xC0, 0xBD, 0xB4, 0xAA, 0x9F, 0x97, 0x92, 0x8E, 0x8C, 0x8C, 0x89, 0x84, 0x81
        .byte   0xC0, 0xC1, 0xC5, 0xC5, 0xC7, 0xC7, 0xC4, 0xBD, 0xB4, 0xA5, 0x9B, 0x95, 0x8E, 0x8E, 0x8E, 0x8E, 0x8C, 0x89
        .byte   0xC6, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC2, 0xBA, 0xAC, 0x9F, 0x96, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC4, 0xBD, 0xAF, 0xA2, 0x99, 0x91, 0x91, 0x91, 0x91, 0x91, 0x91
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC5, 0xBE, 0xB1, 0xA5, 0x9B, 0x92, 0x92, 0x92, 0x92, 0x92, 0x92
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x93, 0x93, 0x93, 0x93, 0x93, 0x93
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x95, 0x95, 0x95, 0x95, 0x95, 0x94
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x95, 0x95, 0x95, 0x95, 0x95, 0x95
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x95, 0x95, 0x95, 0x95, 0x95, 0x94
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x96, 0x95, 0x94, 0x94, 0x94, 0x93
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x96, 0x96, 0x93, 0x93, 0x93, 0x93
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x97, 0x95, 0x92, 0x92, 0x92, 0x92
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x96, 0x94, 0x91, 0x91, 0x91, 0x91
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x96, 0x94, 0x91, 0x91, 0x91, 0x91
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x96, 0x94, 0x91, 0x91, 0x91, 0x91
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x96, 0x94, 0x91, 0x91, 0x91, 0x91
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x96, 0x94, 0x91, 0x91, 0x91, 0x91

;L93EF: Ignition map 18x29 Restricted ALPHA/N cyl 2
L93EF:  .byte   0x59, 0x59, 0x59, 0x61, 0x63, 0x66, 0x6B, 0x6D, 0x6E, 0x6E, 0x6A, 0x67, 0x63, 0x5F, 0x5A, 0x56, 0x54, 0x54
        .byte   0x5A, 0x5A, 0x5A, 0x62, 0x69, 0x6C, 0x6E, 0x70, 0x70, 0x71, 0x6D, 0x69, 0x65, 0x61, 0x5C, 0x58, 0x56, 0x56
        .byte   0x60, 0x68, 0x6B, 0x6C, 0x70, 0x70, 0x72, 0x72, 0x72, 0x72, 0x6E, 0x6B, 0x67, 0x63, 0x5E, 0x5A, 0x59, 0x59
        .byte   0x66, 0x73, 0x74, 0x74, 0x75, 0x73, 0x73, 0x75, 0x75, 0x75, 0x71, 0x6D, 0x69, 0x65, 0x61, 0x5D, 0x5C, 0x5C
        .byte   0x6B, 0x76, 0x77, 0x77, 0x77, 0x76, 0x75, 0x77, 0x77, 0x76, 0x73, 0x6F, 0x6C, 0x68, 0x64, 0x60, 0x5F, 0x5F
        .byte   0x73, 0x7A, 0x7B, 0x7B, 0x7A, 0x79, 0x78, 0x79, 0x79, 0x79, 0x76, 0x73, 0x70, 0x6C, 0x67, 0x64, 0x63, 0x63
        .byte   0x7E, 0x7F, 0x80, 0x7F, 0x7D, 0x7D, 0x7D, 0x7D, 0x7C, 0x7C, 0x79, 0x77, 0x74, 0x71, 0x69, 0x66, 0x65, 0x65
        .byte   0x87, 0x8A, 0x88, 0x84, 0x84, 0x82, 0x81, 0x82, 0x81, 0x80, 0x7D, 0x7B, 0x79, 0x75, 0x6D, 0x68, 0x66, 0x66
        .byte   0x91, 0x91, 0x90, 0x8E, 0x8B, 0x8A, 0x87, 0x87, 0x87, 0x87, 0x85, 0x83, 0x7F, 0x7B, 0x74, 0x6D, 0x67, 0x67
        .byte   0x9A, 0x99, 0x97, 0x97, 0x94, 0x94, 0x93, 0x92, 0x91, 0x8F, 0x8C, 0x8A, 0x84, 0x80, 0x7A, 0x73, 0x6D, 0x6C
        .byte   0xA4, 0xA2, 0xA0, 0x9F, 0x9F, 0xA1, 0x9F, 0x9B, 0x98, 0x94, 0x90, 0x8C, 0x87, 0x84, 0x81, 0x7A, 0x72, 0x72
        .byte   0xAF, 0xB0, 0xB1, 0xB2, 0xB2, 0xB2, 0xAF, 0xA8, 0xA1, 0x98, 0x93, 0x90, 0x8B, 0x88, 0x86, 0x81, 0x79, 0x78
        .byte   0xB8, 0xBA, 0xBB, 0xBD, 0xBF, 0xC0, 0xBD, 0xB4, 0xAA, 0x9F, 0x97, 0x92, 0x8E, 0x8C, 0x8C, 0x89, 0x84, 0x81
        .byte   0xC0, 0xC1, 0xC5, 0xC5, 0xC7, 0xC7, 0xC4, 0xBD, 0xB4, 0xA5, 0x9B, 0x95, 0x8E, 0x8E, 0x8E, 0x8E, 0x8C, 0x89
        .byte   0xC6, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC2, 0xBA, 0xAC, 0x9F, 0x96, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC4, 0xBD, 0xAF, 0xA2, 0x99, 0x91, 0x91, 0x91, 0x91, 0x91, 0x91
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC5, 0xBE, 0xB1, 0xA5, 0x9B, 0x92, 0x92, 0x92, 0x92, 0x92, 0x92
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x93, 0x93, 0x93, 0x93, 0x93, 0x93
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x95, 0x95, 0x95, 0x95, 0x95, 0x94
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x95, 0x95, 0x95, 0x95, 0x95, 0x95
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x95, 0x95, 0x95, 0x95, 0x95, 0x94
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x96, 0x95, 0x94, 0x94, 0x94, 0x93
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x96, 0x96, 0x93, 0x93, 0x93, 0x93
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x97, 0x95, 0x92, 0x92, 0x92, 0x92
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x96, 0x94, 0x91, 0x91, 0x91, 0x91
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x96, 0x94, 0x91, 0x91, 0x91, 0x91
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x96, 0x94, 0x91, 0x91, 0x91, 0x91
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x96, 0x94, 0x91, 0x91, 0x91, 0x91
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x96, 0x94, 0x91, 0x91, 0x91, 0x91

;L95F9: Ignition map 18x29 Derestricted ALPHA/N cyl 1
L95F9:  .byte   0x59, 0x59, 0x59, 0x61, 0x63, 0x66, 0x6B, 0x6D, 0x6E, 0x6E, 0x6A, 0x67, 0x63, 0x5F, 0x5A, 0x56, 0x54, 0x54
        .byte   0x5A, 0x5A, 0x5A, 0x62, 0x69, 0x6C, 0x6E, 0x70, 0x70, 0x71, 0x6D, 0x69, 0x65, 0x61, 0x5C, 0x58, 0x56, 0x56
        .byte   0x60, 0x68, 0x6B, 0x6C, 0x70, 0x70, 0x72, 0x72, 0x72, 0x72, 0x6E, 0x6B, 0x67, 0x63, 0x5E, 0x5A, 0x59, 0x59
        .byte   0x66, 0x73, 0x74, 0x74, 0x75, 0x73, 0x73, 0x75, 0x75, 0x75, 0x71, 0x6D, 0x69, 0x65, 0x61, 0x5D, 0x5C, 0x5C
        .byte   0x6B, 0x76, 0x77, 0x77, 0x77, 0x76, 0x75, 0x77, 0x77, 0x76, 0x73, 0x6F, 0x6C, 0x68, 0x64, 0x60, 0x5F, 0x5F
        .byte   0x73, 0x7A, 0x7B, 0x7B, 0x7A, 0x79, 0x78, 0x79, 0x79, 0x79, 0x76, 0x73, 0x70, 0x6C, 0x67, 0x64, 0x63, 0x63
        .byte   0x7E, 0x7F, 0x80, 0x7F, 0x7D, 0x7D, 0x7D, 0x7D, 0x7C, 0x7C, 0x79, 0x77, 0x74, 0x71, 0x69, 0x66, 0x65, 0x65
        .byte   0x87, 0x8A, 0x88, 0x84, 0x84, 0x82, 0x81, 0x82, 0x81, 0x80, 0x7D, 0x7B, 0x79, 0x75, 0x6D, 0x68, 0x66, 0x66
        .byte   0x91, 0x91, 0x90, 0x8E, 0x8B, 0x8A, 0x87, 0x87, 0x87, 0x87, 0x85, 0x83, 0x7F, 0x7B, 0x74, 0x6D, 0x67, 0x67
        .byte   0x9A, 0x99, 0x97, 0x97, 0x94, 0x94, 0x93, 0x92, 0x91, 0x8F, 0x8C, 0x8A, 0x84, 0x80, 0x7A, 0x73, 0x6D, 0x6C
        .byte   0xA4, 0xA2, 0xA0, 0x9F, 0x9F, 0xA1, 0x9F, 0x9B, 0x98, 0x94, 0x90, 0x8C, 0x87, 0x84, 0x81, 0x7A, 0x72, 0x72
        .byte   0xAF, 0xB0, 0xB1, 0xB2, 0xB2, 0xB2, 0xAF, 0xA8, 0xA1, 0x98, 0x93, 0x90, 0x8B, 0x88, 0x86, 0x81, 0x79, 0x78
        .byte   0xB8, 0xBA, 0xBB, 0xBD, 0xBF, 0xC0, 0xBD, 0xB4, 0xAA, 0x9F, 0x97, 0x92, 0x8E, 0x8C, 0x8C, 0x89, 0x84, 0x81
        .byte   0xC0, 0xC1, 0xC5, 0xC5, 0xC7, 0xC7, 0xC4, 0xBD, 0xB4, 0xA5, 0x9B, 0x95, 0x8E, 0x8E, 0x8E, 0x8E, 0x8C, 0x89
        .byte   0xC6, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC2, 0xBA, 0xAC, 0x9F, 0x96, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC4, 0xBD, 0xAF, 0xA2, 0x99, 0x91, 0x91, 0x91, 0x91, 0x91, 0x91
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC5, 0xBE, 0xB1, 0xA5, 0x9B, 0x92, 0x92, 0x92, 0x92, 0x92, 0x92
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x93, 0x93, 0x93, 0x93, 0x93, 0x93
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x95, 0x95, 0x95, 0x95, 0x95, 0x94
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x95, 0x95, 0x95, 0x95, 0x95, 0x95
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x95, 0x95, 0x95, 0x95, 0x95, 0x94
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x96, 0x95, 0x94, 0x94, 0x94, 0x93
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x96, 0x96, 0x93, 0x93, 0x93, 0x93
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x97, 0x95, 0x92, 0x92, 0x92, 0x92
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x96, 0x94, 0x91, 0x91, 0x91, 0x91
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x96, 0x94, 0x91, 0x91, 0x91, 0x91
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x96, 0x94, 0x91, 0x91, 0x91, 0x91
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x96, 0x94, 0x91, 0x91, 0x91, 0x91
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x96, 0x94, 0x91, 0x91, 0x91, 0x91

;L9803: Ignition map 18x29 Derestricted ALPHA/N cyl 2
L9803:  .byte   0x59, 0x59, 0x59, 0x61, 0x63, 0x66, 0x6B, 0x6D, 0x6E, 0x6E, 0x6A, 0x67, 0x63, 0x5F, 0x5A, 0x56, 0x54, 0x54
        .byte   0x5A, 0x5A, 0x5A, 0x62, 0x69, 0x6C, 0x6E, 0x70, 0x70, 0x71, 0x6D, 0x69, 0x65, 0x61, 0x5C, 0x58, 0x56, 0x56
        .byte   0x60, 0x68, 0x6B, 0x6C, 0x70, 0x70, 0x72, 0x72, 0x72, 0x72, 0x6E, 0x6B, 0x67, 0x63, 0x5E, 0x5A, 0x59, 0x59
        .byte   0x66, 0x73, 0x74, 0x74, 0x75, 0x73, 0x73, 0x75, 0x75, 0x75, 0x71, 0x6D, 0x69, 0x65, 0x61, 0x5D, 0x5C, 0x5C
        .byte   0x6B, 0x76, 0x77, 0x77, 0x77, 0x76, 0x75, 0x77, 0x77, 0x76, 0x73, 0x6F, 0x6C, 0x68, 0x64, 0x60, 0x5F, 0x5F
        .byte   0x73, 0x7A, 0x7B, 0x7B, 0x7A, 0x79, 0x78, 0x79, 0x79, 0x79, 0x76, 0x73, 0x70, 0x6C, 0x67, 0x64, 0x63, 0x63
        .byte   0x7E, 0x7F, 0x80, 0x7F, 0x7D, 0x7D, 0x7D, 0x7D, 0x7C, 0x7C, 0x79, 0x77, 0x74, 0x71, 0x69, 0x66, 0x65, 0x65
        .byte   0x87, 0x8A, 0x88, 0x84, 0x84, 0x82, 0x81, 0x82, 0x81, 0x80, 0x7D, 0x7B, 0x79, 0x75, 0x6D, 0x68, 0x66, 0x66
        .byte   0x91, 0x91, 0x90, 0x8E, 0x8B, 0x8A, 0x87, 0x87, 0x87, 0x87, 0x85, 0x83, 0x7F, 0x7B, 0x74, 0x6D, 0x67, 0x67
        .byte   0x9A, 0x99, 0x97, 0x97, 0x94, 0x94, 0x93, 0x92, 0x91, 0x8F, 0x8C, 0x8A, 0x84, 0x80, 0x7A, 0x73, 0x6D, 0x6C
        .byte   0xA4, 0xA2, 0xA0, 0x9F, 0x9F, 0xA1, 0x9F, 0x9B, 0x98, 0x94, 0x90, 0x8C, 0x87, 0x84, 0x81, 0x7A, 0x72, 0x72
        .byte   0xAF, 0xB0, 0xB1, 0xB2, 0xB2, 0xB2, 0xAF, 0xA8, 0xA1, 0x98, 0x93, 0x90, 0x8B, 0x88, 0x86, 0x81, 0x79, 0x78
        .byte   0xB8, 0xBA, 0xBB, 0xBD, 0xBF, 0xC0, 0xBD, 0xB4, 0xAA, 0x9F, 0x97, 0x92, 0x8E, 0x8C, 0x8C, 0x89, 0x84, 0x81
        .byte   0xC0, 0xC1, 0xC5, 0xC5, 0xC7, 0xC7, 0xC4, 0xBD, 0xB4, 0xA5, 0x9B, 0x95, 0x8E, 0x8E, 0x8E, 0x8E, 0x8C, 0x89
        .byte   0xC6, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC2, 0xBA, 0xAC, 0x9F, 0x96, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC4, 0xBD, 0xAF, 0xA2, 0x99, 0x91, 0x91, 0x91, 0x91, 0x91, 0x91
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC5, 0xBE, 0xB1, 0xA5, 0x9B, 0x92, 0x92, 0x92, 0x92, 0x92, 0x92
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x93, 0x93, 0x93, 0x93, 0x93, 0x93
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x95, 0x95, 0x95, 0x95, 0x95, 0x94
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x95, 0x95, 0x95, 0x95, 0x95, 0x95
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x95, 0x95, 0x95, 0x95, 0x95, 0x94
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x96, 0x95, 0x94, 0x94, 0x94, 0x93
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x96, 0x96, 0x93, 0x93, 0x93, 0x93
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x97, 0x95, 0x92, 0x92, 0x92, 0x92
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x96, 0x94, 0x91, 0x91, 0x91, 0x91
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x96, 0x94, 0x91, 0x91, 0x91, 0x91
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x96, 0x94, 0x91, 0x91, 0x91, 0x91
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x96, 0x94, 0x91, 0x91, 0x91, 0x91
        .byte   0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xC7, 0xBF, 0xB3, 0xA7, 0x9D, 0x96, 0x94, 0x91, 0x91, 0x91, 0x91

L9A0D:  .byte   0x10, 0xE0, 0x08
        .byte   0xFF, 0xDA, 0xBB, 0xA1, 0x8A, 0x80, 0x80, 0x80

L9A18:  .byte   0x83
L9A19:  .byte   0x0A
L9A1A:  .byte   0x80

L9A1B:  .byte   0x10, 0xE0, 0x08
        .byte   0x53, 0x54, 0x5C, 0x61, 0x62, 0x72, 0x77, 0x7E

L9A26:  .byte   0x10
L9A27:  .byte   0x80
L9A28:  .byte   0x80
L9A29:  .byte   0x80
L9A2A:  .byte   0x83
L9A2B:  .byte   0x04
L9A2C:  .byte   0x04
L9A2D:  .byte   0x04
L9A2E:  .byte   0x19
L9A2F:  .byte   0x18

;Used to convert VM A/D readings to ???.  (0x80-0x53)*0x08 = 0x0168.
;The 0x01xx means that the table will be 2 entries long (and it is!).
;The low entry 0x53 represents 83. or (83/256) * 20V = 6.5V
;The high entry 0x80 represents 128, or 128/256 * 20V = 10V
L9A30:  .byte   0x53, 0x80, 0x08
        .byte   0x13, 0x0C

L9A35:  .byte   0x09, 0x06, 0x05

;A 15-entry interpolation table indexed by 800 RPM increments, starting at 800 RPM.
;?Could it be a dwell calculation table?
;               800  1600  2400  3200  4000  4800  5600  6400  7200  8000  8800  9600  10400  11200  12000
L9A38:  .byte   0x16,  0x16,  0x16,  0x16,  0x16,  0x13,  0x11,  0x10,  0x0F,  0x0E,  0x0D,  0x0C,   0x0C,   0x0B,   0x0A

L9A47:  .byte   0x0D
L9A48:  .byte   0x32
L9A49:  .byte   0x0D
L9A4A:  .byte   0x64
L9A4B:  .word   0x1000
L9A4D:  .byte   0x44
L9A4E:  .byte   0x19
L9A4F:  .byte   0x27
L9A50:  .byte   0x28

L9A51:  .byte   0x0F                    ;sidestand state must read consistently this many times to be recognized

L9A52:  .byte   0x08
L9A53:  .byte   0x02
L9A54:  .byte   0x19
L9A55:  .byte   0x1E

L9A56:  .byte   0x66                    ;102.

L9A57:  .byte   0x29
L9A58:  .byte   0x30
L9A59:  .byte   0xF8
L9A5A:  .byte   0xC8
L9A5B:  .byte   0x19
L9A5C:  .byte   0xBA
L9A5D:  .word   0x0340
L9A5F:  .byte   0xB0                    ;used by water temp converter as a default temperature if the sensor is broken
L9A60:  .byte   0x70                    ;used by air   temp converter as a default temperature if the sensor is broken
L9A61:  .byte   0xBA
L9A62:  .byte   0x80
L9A63:  .byte   0x84
L9A64:  .byte   0x0A

L9A65:  .word   0x2700                  ;1/6 period = 500 RPM
L9A67:  .word   0x1A00                  ;1/6 period = 751 RPM
L9A69:  .word   0x0783                  ;1/6 period = 2600 RPM
L9A6B:  .word   0x0682                  ;1/6 period = 3000 RPM

L9A6D:  .byte   0xD2                    ;derestricted
L9A6E:  .byte   0xD2                    ;derestricted

;-----------------------------------------------------------------
;These are crankshaft 1/2 periods for use in detecting redline
;situations.  Each timer tick count represents 2 uSec.

;We convert RPM to crank 1/2 periods as follows:

;period     (in seconds) = (1 / (RPM/60)) = (60/RPM)
;halfPeriod (in seconds) = 1/2 * (60/RPM) = (30/RPM)

;To convert seconds to timer ticks: 1 sec == 500,000 timer ticks,
;since each timer tick is 2 Sec.  To convert crankshaft RPM to a
;half period in timer ticks, we use:

;halfPeriod (in timer ticks) = 500,000 * (30/RPM)
;halfPeriod (in timer ticks) = (15,000,000/RPM)

; To go the other way: RPM = 15,000,000 / halfperiod (in timer ticks)

;Restricted:
L9A6F:  .word   0x0597                  ;Fuel is enabled  if 1/2 period is >= this limit: 0x0597, i.e. <= 10482 RPM
L9A71:  .word   0x0597                  ;Fuel is disabled if 1/2 period is < this limit: 0x0597, i.e. > 10482 RPM
                                        ;Fuel state is unchanged if 1/2 period is between L9A6F and L9A71
L9A73:  .word   0x059D                  ;Spark is enabled  if 1/2 period is >= this limit: 0x059D, i.e. <= 10438 RPM
L9A75:  .word   0x059D                  ;Spark is disabled if 1/2 period is < this limit: 0x059D, i.e. > 10438 RPM
                                        ;Spark state is unchanged 1/2 period is between L9A73 and L9A75

;Derestricted:
L9A77:  .word   0x0597                  ;Fuel is enabled  if 1/2 period is < this limit: 0x0597 == 10482 RPM
L9A79:  .word   0x0597                  ;Fuel is disabled if 1/2 period is < this limit: 0x0597 == 10482 RPM
                                        ;Fuel state is unchanged if 1/2 period is between L9A77 and L9A79
L9A7B:  .word   0x059D                  ;Spark is enabled  if 1/2 period is < this limit: 0x059D == 10438 RPM
L9A7D:  .word   0x059D                  ;Spark is disabled if 1/2 period is < this limit: 0x059D == 10438 RPM
                                        ;Spark state is unchanged 1/2 period is between L9A7B and L9A7D

;9A7F:
        .word   0x1090
        .word   0x1070, 0x7070
        .word   0x7070, 0x7070
        .word   0x7070, 0x7010
        .word   0x9010, 0x7070

;9A91:
        .fill   88, 1, 0x70

;9AE9:
        .byte   0x10, 0x90, 0x10, 0x01
        .byte   0x01, 0x01, 0x01, 0x01
        .byte   0x01, 0x01, 0x01, 0x01
        .byte   0x01, 0x10, 0x90, 0x10

;9AF9:
        .fill   90, 1, 0x01

; A 549USA fills this block starting at 9B53 with zeroes.
; In an RP58 build (and others), this area has a few locations containing non-zero values.
; It's not clear that the codebase references this data at all.
        .fill   64, 1, 0x00

; This area starting at 9B93 has differences between a 549USA and an RP58.
; It's not clear if the codebase references this data at all.
        .byte   0xFF, 0xCE, 0xA3, 0x8A
        .byte   0x8A, 0x97, 0xCB, 0xE4
        .byte   0xF4, 0xF2, 0xE6, 0xCC
        .byte   0x80, 0x42, 0x00, 0x00
        .byte   0x00, 0x05, 0x0E, 0x15
        .byte   0x17, 0x17, 0x17, 0x14
        .byte   0x11, 0x0C, 0x09, 0x06
        .byte   0x03, 0x02, 0x01, 0x00
        .byte   0x00, 0x00, 0xFF, 0xCE
        .byte   0xA3, 0x8A, 0x8A, 0x97
        .byte   0xCB, 0xE4, 0xF4, 0xF2
        .byte   0xE6, 0xCC, 0x80, 0x42
        .byte   0x00, 0x00, 0x00, 0x05
        .byte   0x0E, 0x15, 0x17, 0x17
        .byte   0x17, 0x14, 0x11, 0x0C
        .byte   0x09, 0x06, 0x03, 0x02
        .byte   0x01, 0x00, 0x00, 0x00
        .byte   0x80, 0x80, 0x3F, 0x3F

        .fill   1029, 1, 0x3F

;This table holds the fault codes that get displayed when there
;is a fault and the DIAG connector is connected.  The
;descriptions come from the Mille service manual of 2002-10.
;These codes are selected by 'sticky' error bits being set in
;locations L000E and L000F.  These locations are copies of the
;unsticky versions in L000C and L000D.
L9FE0:  .byte   0x11                    ;L000C:7 camshaft posn sensor
        .byte   0x12                    ;L000C:6 crankshaft posn sensor
        .byte   0x13                    ;L000C:5 MAP sensor (via AN3)
        .byte   0x14                    ;L000C:4 MAP sensor (via AN2): "excessive difference between the sensors"
        .byte   0x15                    ;L000C:3 VTA Throttle Valve pot
        .byte   0x21                    ;L000C:2 THW Coolant temp sensor
        .byte   0x22                    ;L000C:1 THA Air Temp sensor
        .byte   0x23                    ;L000C:0 AAP Ambient Air Pressure

        .byte   0x24                    ;L000D:7 not used by software, not listed in manual
        .byte   0x31                    ;L000D:6 not used by software, not listed in manual
        .byte   0x32                    ;L000D:5 not used by software, not listed in manual
        .byte   0x33                    ;L000D:4 Ign coil #1 (Front)
        .byte   0x34                    ;L000D:3 Ign coil #2 (Front)
        .byte   0x35                    ;L000D:2 Ign coil #1 (Rear)
        .byte   0x36                    ;L000D:1 Ign coil #2 (Rear)
        .byte   0x41                    ;L000D:0 Fall sensor

L9FF0:  .byte   0x0B

L9FF1:  .byte   0x0B, 0x03, 0x03, 0x04
        .byte   0x17

;This is a 5 element byte table containing who the hell knows what.
L9FF6:  .byte   0x48, 0x24, 0x1A, 0x0B, 0x0B

        .fill   8200-3, 1, 0x3F

; All Aprilia EPROMs have $3F bytes at $C000..$C002, and the code starts at $C003.
; To identify UM4 EPROMs, we will set $C000..$C002 to the characters "UM4".
LC000:  .byte   'U', 'M', '4'

;*****************************************************************
; Perhaps this code does something in a special development environment for the OEM,
; but it certainly is a NOP in the real ECU.
; The overall system memory map looks like this:
;   $0000..$01FF RAM
;   $1000..$10FF Processor registers
;   $8000..$FFFF EPROM
;   $C000..$FFFF Internal ROM, always disabled

LC003:  ldaA    L4000                   ; This appears to always reads as $00
         cmpA   #0x00
         beq    0f
         staA   LW+LOGID_ECU_L4000_EVENT_TYPE_U8 ; A non-zero value is unusual!!! Log it!
        cmpA    #0x19
        bne     0f                      ; always taken
        jsr     L5000
0:      rts

        .byte   0x02, 0xA0

LC010:  .word   0x0022                  ;used for injector timing: 68 uSec

LC012:  .word   0x0030                  ;used for injector timing: 96 uSec

LC014:  .word   0x00FF
LC016:  .word   0x014A

;This is an interpolation table for converting the air and water
;temps.  Remember that the interpolation routines use entries at
;3[X] and 4[X] as the interpolation endpoints, so the start
;reference address of this table must be adjusted downwards by
;3[X] when passing a pointer to the table to the interpolation
;routine.
LC018:  .byte   0xFF,0xFF,0xD3,0xB8,0xA6,0x98,0x8D,0x83
        .byte   0x7B,0x74,0x6D,0x67,0x61,0x5B,0x56,0x51
        .byte   0x4C,0x47,0x43,0x3E,0x39,0x34,0x30,0x2B
        .byte   0x25,0x20,0x1A,0x13,0x0C,0x03,0x00,0x00

        .byte   0x00

; A single row 2D table of length 0x1D (always indexes to exactly row 0).
; The column index is 8.8RPM400, so the first entry is 800 RPM.  The final entry would be (0x1d * 400)+800, or 12,400 RPM.
LC039:  .byte   0x00,0x18,0x41,0x53,0x61,0x68,0x70,0x71,0x78,0x7A,0x7E,0x7D,0x81,0x81,0x84,0x84,0x86,0x86,0x87,0x88,0x89,0x8A,0x8A,0x8B,0x8C,0x8C,0x8C,0x8C,0x8C

LC056:  .word   0x007D, 0x803F
        .word   0x3F3F

;*****************************************************************
; Initialize System IO.

; Called once immediately after reset, and then never again.
;
; The HC11 INIT register is never written by this code. If it were to be written,
; it needs to be written within 64 E-clks after RESET, at which point it becomes read-only.

LC05C:  ldaA    #0x90                   ;Disable interrupts, disable STOP mode
        tAP

        ldaA    #0x04                   ;NOCOP is true (watchdog disabled), ROMON is false (no ROM in memory map)
        staA    CONFIG

        ldaA    #0x11                   ;IRV=1: data from internal reads is driven to external data bus
                                        ;NHALT=1: HALT pin operation is disabled
        staA    OPT2                    ;OPT2 register

        ldaA    #0xB1                   ;A/D is powered
                                        ;A/D uses E clk
                                        ;IRQ is falling edge sensitive
                                        ;DLY when exiting STOP
                                        ;CLK MON disabled
                                        ;FCME=0
                                        ;watchdog scaling=%01
        staA    OPTION

        ldaA    #0x22                   ;counter2 uses internal clk and prescaler
                                        ;counter2 is prescaled by 4
                                        ;counter1 is prescaled by 4
        staA    TPRE                    ;E-clk is 2MHz with 8MHz crystal, so both timers always tick at 2 uSec per count

        ldD     #0xFFF0                 ;PORTA=0xFF, DDRA=0xF0 (PORTA[7..4]=output, PORTA[3..0]=input)
        stD     PORTA

        ldD     #0xF0FF                 ;PORTD=0xF0, DDRD=0xFF (all outputs)
        stD     PORTD

        ldD     #0xFFC0                 ;PORTG=0xFF, DDRG=0xC0
        stD     PORTG

        ldD     #0xFF6F                 ;PORTH=0xFF, DDRH=0x6F
        stD     PORTH

        ldD     #0xFFFF                 ;PORTJ=0xFF, DDRJ=0xFF
        stD     PORTJ

; Program OC1 so that it will drive PA7 to '1' on a match (stop charging the front ignition coils)
        ldaA    #0x80
        staA    OC1M
        ldaA    #0x80
        staA    OC1D

; Init TCTL1 so that:
; 11xxxxxx OC2 will drive PA6/OC2 to '1' on a match (stop charging rear ignition coils)
; xx11xxxx OC3 will drive PA5/OC3 to '1' on a match (disable driving front fuel injector)
; xxxx11xx OC4 will drive PA4/OC4 to '1' on a match (disable driving rear fuel injector)
; xxxxxx00 OC5 is disconnected from PA3 (GPIO PA3/OC5/IC4 will be used as IC4 #2 spark event capture)
        ldaA    #0xFC
        staA    TCTL1

;Program the edge directions that will cause the Input Capture events to trigger:
;IC1 - camshaft
;IC2 - crankshaft
;IC3 - watches ignition events on IG11-FRT and IG21-REAR
;IC4 - watches ignition events on IG12-FRT and IG22-REAR

        ldaA    #0x69                   ;Ignition=IC4=01xxxxxx=rising, camshaft=IC1=xx10xxxx=falling, crankshaft=IC2=xxxx10xx=falling, IC3=xxxxxx01=rising
        staA    TCTL2

        ldaA    #0x0F                   ;IC5/IC6 disabled, OC6/OC7 are set to 1 on timer compare match
        staA    TCTL3

        ldaA    #0x80                   ;TMSK2=0x80: Timer Overflow 1 enabled, everything else disabled
        staA    TMSK2

        ldaA    #0x00                   ;TCNT1 & TCNT2 are free running,
                                        ;OC6 enabled (implicitly disables IC5)
                                        ;OC7 enabled (implicitly disables IC6)
        staA    TCTL4                   ;only mention

        ldD     TCNT                    ;read TCNT to get current time 'now'
        addD    #0x0010                 ;calc 32 uSec from now
        stD     TOC1                    ;write the future value to TOC1
        stD     TOC2                    ;...OC2
        stD     TOC3                    ;...OC3
        stD     TOC4                    ;...OC4
        stD     TO5I4                   ;...OC5 (or IC4)
        stD     TO6I5                   ;...OC6

; Any OC mechanism set up above will trigger around now, driving all the OC GPIO pins to the initial state
; that was programmed, above.

; In the same fashion, arrange for TCNT2 to trigger any OCx events to initialize them:
        ldD     TCNT2                   ;read the current time
        addD    #0x000C                 ;calc a time 24 uSec in the future
        stD     TO7I6                   ;use the future time as the OC match time

        ldaA    #0x00
        staA    PWEN                    ;Disable all PWM (only mention)

        ldaA    #0x00
        staA    EVCLK                   ;Event Counter CLK source (only mention)

        ldaA    #0x00
        staA    EVCTL                   ;Event Counter Control reg (only mention)

        ldaA    #0x00
        staA    EVMSK                   ;Event Counter Mask (only mention)

        ldaA    #0xFF
        staA    ECMP1B                  ;only mention

        ldaA    #0x01                   ;BAUD=0x01
        staA    BAUD

        ldaA    #0x00                   ;8 bit data, 1 stop
        staA    SCCR1

        ldaA    #0x00                   ;serial port totally disabled, since we are using TX and RX pins for other purposes
        staA    SCCR2

        ldaA    SCSR                    ;read the serial status register..
        ldaB    SCDR                    ;...then read the serial data register to totally clear all serial status

        ldaA    PORTD
        andA    #0xF0                   ;%11110000 clear the MUX selector bits (and two others!)
        staA    PORTD

        rts

;*****************************************************************
;Reinitialize IO port direction programming in case something got corrupted.

;Called every time through the mainloop.

LC10C_initIO:
        sei                             ; Disable interrupts

; While the interrupts are off, it is OK for the EP to switch out all of the map tables.
; Notify the EP that the map switching window is OPEN

        clrA                            ;[2 2]
        staA    ECMP2A                  ;[4 6] Event Compare 2A (only mention)

        ldaA    #0x01                   ;[2 8]
        staA    ECMP1A                  ;[4 12] Event Compare 1A (only mention)

        ldaA    #0xF0                   ;[2 14] DDRA[7..4] are outputs, DDRA[3..0] are inputs
        staA    DDRA                    ;[4 18]

        ldaA    #0xFF                   ;[2 20] PORTD is all outputs
        staA    DDRD                    ;[4 24]

        ldaA    #0xC0                   ;[2 26] DDRG[7..6] are outputs, DDRG[5..0] are inputs
        staA    DDRG                    ;[4 30]

        ldaA    #0x6F                   ;[2 32] PH5,PH6 are inputs, rest are outputs
        staA    DDRH                    ;[4 36]

        ldaA    #0xFF                   ;[2 38] PORTJ is all outputs
        staA    DDRJ                    ;[4 42]

; Total time with interrupts off is 42 cycles, or 21 uSec.
        cli                             ; Enable interrupts
        rts


;*****************************************************************
; It all begins here, at the target of the reset vector.
reset:
        sei
        ldS     #STACK_TOP              ;init stack pointer

        jsr     LC05C                   ;init processor IO resources

        ldX     #0x01FF                 ;clear all of RAM (bug: doesn't clear location 0x0000!)
        ldaA    #0x00
0:      staA    0, X
        dex
        bne     0b

        ldY     #0x1234
        ldaA    #0x00                   ;scan=false, single channel conversion, select AN1
        staA    ADCTL
        nop
        nop
        nop
        nop
        nop
        nop
        nop

        ldaA    PORTJ
        eora    #0x08                   ;%00001000 toggle 'alive' signal
        staA    PORTJ

        ldaA    PORTD
        andA    #0xF0                   ;%11110000 select mux channel 0
        staA    PORTD

        ldaA    #0x44                   ;%01000100 0x04 disables ignition processing.  0x40 does ?
        staA    *L0000

        ldaA    #0xD0                   ;%11010000
        staA    *L0001

        ldaA    #0x80
        staA    *L0010

        ldaA    #0xCF                   ;%11001111
        staA    *L000B

        ldaA    #0x00
        staA    TCTL3

        ldaA    #0x7F                   ;init the A/D conversion selector.
        staA    *L0019

        ldaA    #0x7B                   ;init the 5.0 mSec free-running counter to 0b01111011
        staA    L0107

        ldD     #0xFFFF
        staA    *L0054
        staA    *L0051
        staA    *L0052
        staA    *L0053
        staA    L0143
        staA    L0137
        staA    L013B
        staA    L0138
        staA    L0139
        staA    L013A
        staA    L0118
        staA    L012C
        staA    L0116
        staA    L0117
        staA    L0114
        staA    L0115
        staA    L0111
        staA    L013C
        staA    L0146
        staA    L010F
        staA    *L00AF
        staA    *L00C2
        stD     *L00D2
        stD     *L00D4
        stD     *L00D6
        stD     *L00D0
        stD     *L00CE
        stD     *L00DA
        stD     *L00D8

        ldD     #0x0213
        stD     *L0079

        ldaA    #0x01
        staA    L012D

        ldaA    L9A64                   ;contains the constant 10.
        staA    L013D                   ;has something to do with the EFI light.  Maybe the EFI light is ON to
                                        ;start with and goes out when everything is OK?

        ldaA    #0x80
        staA    *L006F
        staA    *L0078
        staA    *L008D
        staA    *L0094
        staA    *L008E
        staA    *L008B
        staA    *L008C
        staA    *L009B
        staA    *L0096
        staA    *L00C7
        staA    *L0055                  ;0x80 means we don't know where the crankshaft is
        staA    *L0041
        staA    L012F

        ldaA    PORTG                   ;Init the PORTG debounce by reading the current PORTG bits...
        tAB                             ;and saving them as being "debounced" (see the debouncer, below)
        stD     *L0049_PORTG_debounced

;Init the A/D readings by doing a conversion on AN1..AN6, then for each mux input on AN7 and AN8.
        jsr     LC533

        ldD     *L0024                  ;get the instantaneous VTA
        stD     *L0042                  ;save it as the filtered VTA
        stD     L011B                   ;init all elements in the rolling VTA history buffer with the same value
        stD     L011D
        stD     L011F
        stD     L0121

        cmpD    L8026                   ;0x0090
        bhs     2f                      ;br if D>=0x0090
        cmpD    L8024                   ;0x0078
        bhi     1f                      ;br if D in [0079..008F]

        bra     2f                      ;D must be <= 0x0078


1:      tstA
        beq     3f                      ;must always take this since we know D is in [0079..008F]

        ldaB    #0xFF                   ;will never be taken!
        bra     3f

;We get here if the initial VTA reading was >=0x0090 OR if it was < 0x78
2:      ldaB    L8023                   ;0x84 splits the difference between 0x78 and 0x90

;Save the 8 ls-bits of the initial VTA reading. Oddly enough,
;no one seems to use this value...
3:      staB    *L0026

        ldaA    *L0027                  ;the initial post-reset raw AAP (Ambient Air Pressure)
        staA    *L0033                  ;save it as the "filtered" AAP

        ldaA    *L0031                  ;get the initial post-reset THA (Air Temp) reading
        staA    *L0032                  ;save it here, but no one ever reads this location

;This next section is initializing a CRref value for some reason.
        ldaA    L8032                   ;constant: 0x06
        beq     4f                      ;0x06 is not 0x00, so branch is NEVER TAKEN
        cmpA    #0x0A
        bhs     4f                      ;br if 0x06 >= 0x0A (NEVER TAKEN)

        ldaA    #0x09                   ;always taken
        bra     5f


4:      ldaA    #0x05

5:      staA    L0171                   ;always init'ed to CR9
        staA    *L0058                  ;always init'ed to CR9

        ldD     #(2500)                 ;5000 uSec
        stD     *L005D                  ;the basic injection time duration (front injector)
        stD     *L005F                  ;the basic injection time duration (rear  injector)

        ldD     TCNT
        addD    #(1000)
        stD     L0105                   ;schedule the first of the L0105 5.0 mSec events starting 2.0 mSec from now

        addD    #(3500)
        stD     L0108                   ;NEVER USED: presumably, this schedules the first of the L0108 events

        ldD     TCNT
        addD    #(500)
        stD     L0103                   ;schedule the first of the 1.5 mSec L0103 events 1.0 mSec from now

        clr     L010B                   ;set to 0 every crank event when the bike is upright

        ldaA    PORTH
        andA    #0xBF                   ;%10111111 active low to enable fuel pump drive
        staA    PORTH
        ldaA    #1
        staA    LW+LOGID_ECU_FUEL_PUMP_TYPE_B
        staA    FUEL_PUMP_B_prev

        ldaA    #0x0F                   ;flush any outstanding IC events: IC1..IC4 crank, cam, spark events
        staA    TFLG1

#if !defined SPARK_ISR
        ldaA    #0x06                   ;disable IC1/IC4 ints (spark generation), enable IC1/IC2 interrupts (crank and cam sensors)
        staA    TMSK1
#else
        clr     FAKE_TFLG1              ;flush IC1/IC4 spark IC events in the fake copy of TFLG1
        ldaA    #0x0F                   ;enable IC1/IC4 ints (spark generation), enable IC1/IC2 ints (crank and cam sensors)
        staA    TMSK1
#endif

        ldaA    #0x08                   ;Timer O5I4 can be input capture or output compare, but not both at the same time.
        staA    PACTL                   ;Configure it to operate as Input Capture 4 where it will watch for spark-watcher events.
        cli

;*****************************************************************
;The start of the main processing loop.
LC27F:  ldS     #STACK_TOP              ;reset stack to top of RAM (just due to stack pointer paranoia)

        ldaA    *L0012
        eora    #0b10000000             ;debugging? no one ever uses *L0012:B7!
        staA    *L0012

        jsr     LC10C_initIO            ;set up all the iO ports (non-destructively)
        jsr     LC003                   ;calls into the mask ROM, if req'd

        jsr     LC3DD                   ;schedule what we will run this time through the main loop
                                        ;also performs an A/D conversion on the current channel

        brclr   *L0002 0b00000100 LC298 ;gets set every time see a crank reference pulse
        jmp     LC834                   ;calc the most recent crankshaft period and crank 1/2 period info

LC298:  jsr     LC2D9_overRev           ;test for over rev situations

        jsr     LC337                   ;manipulates VSV (not connected on the external wiring harness).
                                        ;This is the only call to this routine.

        jsr     LC003                   ;calls into the mask ROM, if req'd

        brclr   *L0001 #0b00010000 1f   ;????
        brclr   *L0001 #0b10000000 0f
        bra     LC2B6

0:      bset    *L0015 #0b00000001
        bra     2f

1:      bset    *L0015 0b00000010

2:      jmp     LC8AF                           ;this is the mo-fo big routine
                                                ;returns by jumping to LC2B6

LC2B6:  brclr   *L0002 #0b01000000 LC2BD        ;this bit gets set every 5.0 mSec
        jmp     LD82E                           ;returns by jumping to LC2BD

LC2BD:  brclr   *L0002 #0b00000011 LC2C4        ;branch if we have not seen neither both CR3 and CR8
        jmp     LD60B                           ;returns by jumping to LC2C4

LC2C4:  brclr   *L0002 #0b00100000 LC2CB        ;this bit gets set every 40 mSec
        jmp     LDF4D                           ;returns by jumping to LC2CB

LC2CB:  brclr   *L0002 #0b00010000 LC2D2        ;this bit gets set on a different 40 mSec timeslice
        jmp     LE26C                           ;returns by jumping to LC2D2

LC2D2:  brclr   *L0002 #0b00001000 LC27F        ;this bit gets set of a still different 80 mSec timeslice
        jmp     LE3BA

;the prev routine returns by jumping back to LC27F (start of main loop)


;*****************************************************************
;Test for the most recent crankshaft 1/2 period for overrev
;situations.

;The different maps have different spark and fuel cutoffs.  As
;RPMs rise, the spark cuts off first, then the fuel.

;There are a pair of limits for the spark control and a pair for
;the fuel control.  The pair of limits describe three ranges:

;1. If the crank period is >= the low speed fuel [spark] limit,
;everything is fine and the appropriate fuel [spark] control is
;explicitly enabled.

;2. If the crank period is < the high speed fuel [spark] limit,
;the engine is spinning too fast, and the corresponding fuel
;[spark] control is explicitly disabled.

;3. If the crank period is between the lower limit and the upper
;limit, no change is made to the state of the fuel [spark] control.

;Note that the 549USA firmware stores the same limit in both the
;lower and the upper limits.  This means that situation (3) never
;occurs: the fuel [spark] control will always be explicitly enabled
;or explicitly disabled.


LC2D9_overRev:
        ldaA    *L0049_PORTG_debounced
        andA    #0b00101000             ;check both map selectes: MS1, MS2
        cmpA    #derestricted           ;are we derestricted?
        bne     4f                      ;branch if this is NOT the case

;We are derestricted.  Check the fuel limit first:
        ldD     *L00D8                  ;get the most recent crank 1/2 period
        cmpD    L9A77                   ;compare it to stored low-rpm limit: 0x0597 (10482 rpm)
        bhs     0f                      ;br if current period is >= stored period (i.e. RPM is less than low RPM limit)
        cmpD    L9A79                   ;cmp to stored high-rpm limit: 0x0597 (10482 rpm)
        bhs     1f                      ;if >= high-rpm limit (i.e. RPM is less than high RPM limit)

;If we get here, the current period is < our high RPM limit.  For
;the 549US EPROM, this means the engine is turning faster than
;10482 RPM.

        bset    *L0007 #0b10000000      ;disable the fuel injectors
        bra     1f

;We get here if the current period is greater than the low-RPM limit (everything is OK)
0:      bclr    *L0007 #0b10000000      ;enable the fuel injectors


;Now do the same checks, but for the spark limit:
1:      cmpD    L9A7B                   ;549US: 0x059D (10438 RPM)
        bhs     2f                      ;if we are within limits
        cmpD    L9A7D                   ;549US: 0x059D (also!)
        bhs     3f                      ;if we are withing limits

;If we get here, the current period is < our high RPM limit.  For
;the 549US EPROM, this means the engine is turning faster than
;10438 RPM.

        bset    *L0007 #0b01000000      ;disable the ignition
        bra     3f

;We get here is the current period is within limits
2:      bclr    *L0007 #0b01000000      ;enable for the ignition
3:      rts


;Same, but for the restricted version:
4:      ldD     *L00D8
        cmpD    L9A6F
        bhs     5f
        cmpD    L9A71
        bhs     6f
        bset    *L0007 #0b10000000
        bra     6f

5:      bclr    *L0007 #0b10000000
6:      cmpD    L9A73
        bhs     7f
        cmpD    L9A75
        bhs     8f
        bset    *L0007 #0b01000000
        bra     8f

7:      bclr    *L0007 #0b01000000
8:      rts


;*****************************************************************
;This routine manipulates VSV.

;VSV has a driver circuit, but is not connected to anything on
;the motorbike harness.  Could it be useful with a special test
;harness?

;This routine sets location L0172 to one of 5 values:
;  0 - if current RPM [0..500]          always sets VSV
;  1 - if current RPM [500..750]        if oldrange==0, set VSV
;  2 - if current RPM [750..2600]
;  3 - if current RPM [2600..3000]
;  4 - if current RPM [3000..max]       always sets VSV

LC337:  ldX     *L00D0                  ;Get the current period for the most recent 1/6 crank rotation.
        cmpX    L9A6B                   ;1/6 period at 3000 RPM
        blo     6f                      ;if our RPM is > 3000 RPM

        cmpX    L9A65                   ;1/6 period at 500 RPM
        bhs     5f                      ;if our RPM is [0..500]

        cmpX    L9A67                   ;1/6 period at 750 RPM
        bhs     1f                      ;if our current RPM is [500..750]

        cmpX    L9A69                   ;1/6 period at 2600 RPM
        bhs     3f                      ;if our current RPM is [750..2600]

;our current RPM must be [2600..3000]
        ldaB    L0172
        cmpB    #0x04                   ;if we used to be range 4, we are now range 3, set VSV and exit
        bhs     0f

        cmpB    #0x03                   ;if we used to be range 3, nothing has changed
        beq     9f

        ldaB    #0x03                   ;otherwise we must have been 0..2 and are are range 3 now, clear VSV and exit
        bra     4f

0:      ldaB    #0x03
        bra     7f

;current RPM is [500..750] (range 1)
1:      ldaB    L0172
        cmpB    #0x00
        beq     2f                      ;if we used to be range 0, change to range 1, set VSV and exit
        cmpB    #0x01
        beq     9f                      ;if we used to be range 1, nothing has changed
        ldaB    #0x01
        bra     4f                      ;if we used to be > range 2..4, we are range 1 now, clear VSV and exit


2:      ldaB    #0x01
        bra     7f


;Current RPM is [750..2600] (range 2) always clears VSV
3:      ldaB    #0b00000010
4:      sei
        ldaA    PORTJ
        andA    #0b11111101             ;clear VSV driver bit
        bra     8f

;Current RPM is [0..500] (range 0)
5:      ldaB    #0x00
        bra     7f                      ;set VSV and exit

;Current RPM is [3000..max] (range 4)
6:      ldaB    #0x04                   ;always sets VSV

7:      sei
        ldaA    PORTJ
        oraA    #0b00000010             ;set VSV driver bit
8:      staA    PORTJ
        cli
        staB    L0172
9:      rts


;*****************************************************************
; Called once only during fuel map processing.
; No input params.  A and B are both destroyed during execution.

LC391:  ldaA    *L0030                  ; converted, interpolated THW (water temp)
        subA    #0x10
        bhs     0f
        ldD     #0x0000
        bra     2f

0:      cmpA    #0xE0
        blo     1f
        ldD     #0x0700
        bra     2f

1:      ldaB    #0x08
        mul
2:      stD     *L00A9

        ldaA    *L0044                  ; something to do with throttle angle
        subA    #0x20
        bhs     3f
        ldD     #0x0000
        bra     5f

3:      cmpA    #0x80
        blo     4f
        ldD     #0x0200
        bra     5f

4:      ldaB    #0x04
        mul
5:      stD     *L00AB

        ldaA    *L0034                  ; something to do with air pressure
        subA    #0x20
        bhs     6f
        ldD     #0x0000
        bra     8f

6:      cmpA    #0x60
        blo     7f
        ldD     #0x0300
        bra     8f

7:      ldaB    #0x08
        mul
8:      stD     *L00AD

        rts


;*****************************************************************
;This routine schedules what we want to run on this iteration of
;the mainloop.

LC3DD:  ldD     TCNT
        subD    L0105
        cmpD    #(2500)                 ;has 5000 uSec elapsed since the time scheduled in L0105?
        bhs     1f                      ;br if so

0:      jmp     6f                      ;else not


;We get here if our basic timeout on L0105 says that enough time
;has passed.  The next scheduled event is based on 5 mSec from
;the previously scheduled time.  This means that variations or
;delays in noticing the event timeout have no cumulative effect
;in when we schedule the next event.

1:      ldD     L0105                   ;bump our next target time by 5000 uSec
        addD    #(2500)
        stD     L0105

;scheduled this routine to run every 5 mSec:
        bset    *L0002 #0b01000000

;L0107 is an overflowing 8 bit count that gets incremented every 5.0 mSec
        inc     L0107

;        ldaB    L0107
;        staB    log_5msecEvent

        ldaB    #0x07
        andB    L0107
        cmpB    #0x02                   ;B will match this value 1/8 of the times we do this test
        bne     2f

;Schedule this routine to run every 40 mSec (once every 8 * 5 mSec):
        bset    *L0002 #0b00100000

2:      cmpB    #0x06                   ;B will match this value 1/8 of the times we do this test
        bne     3f

;Schedule this routine to run every 40 mSec (once every 8 * 5 mSec):
        bset    *L0002 #0b00010000

3:      ldaB    #0x0F
        andB    L0107
        cmpB    #0x08                   ;B will match this value 1/16 of the times we do this test
        bne     4f

;Schedule this routine to run every 80 mSec (once every 16 * 5 mSec):
        bset    *L0002 #0b00001000

;The rest of this code runs every 5 mSec.

;This looks like an RPM test: if L0053 makes it to 12, it means that (12 * 5msec) has elapsed since ??? (what clears it?)
4:      ldaA    *L0053
        cmpA    #0x0C
        blo     0b                      ;br to @6 (via @0) if L0053<12  FIX ME IS THIS @0 or @6???

;Possibly: the engine is going too slow for 'normal' processing.
;Nuke all outstanding ignition processing.
        sei
        ldaA    TMSK1
        andA    #0b01111111             ;disable OC1 (frt ign) interrupts
        staA    TMSK1

        ldaA    #0x80
        staA    TFLG1                   ;clear the IC1 interrupt request

        ldaA    TMSK1
        andA    #0b10111111             ;disable OC2 (rear ign) interrupts
        staA    TMSK1

        ldaA    #0x40                   ;clear the IC2 interrupt request
        staA    TFLG1

        bset    *L0000 #0b01000100
        cli

;;If we have gone a REALLY long time, treat this as a disaster
        ldaA    *L0053
        cmpA    #0x67
        bhs     5f                      ;br if L0053 >= 103 (disaster)

        bra     6f

;Disaster:
5:      sei
        ldaA    #0x00
        staA    TCTL3                   ;disable IC5/OC6, IC6/OC7

        ldaA    TMSK2
        andA    #0b11111001             ;ensure that IC5/OC6 & IC6/OC7 ints are inhibited
        staA    TMSK2
        cli

;disaster: we discard everything we know about engine params and reinit
        jsr     LC7D2                   ;

;-----------------------------------------------------------------
;We get here every time through the main loop regardless of the 5
;mSec processing (above).  This

6:      ldD     TCNT
        subD    L0103
        cmpD    #(750)                  ;1500 uSec
        bhs     7f                      ;br if elapsed time since our last 1.5 mSec event is >= 1.5 mSec

        rts                             ;else we are done

;The 1.5 mSec event has 'fired'.
7:      ldD     L0103
        addD    #(750)                  ;1500 uSec
        stD     L0103                   ;schedule the next 1.5 mSec event 1.5 mSec from the previous one

;...and then drop into the A/D conversion driver

;*****************************************************************
;The A/D Conversion Driver.
LC46F:  ldaA    ADCTL
        bitA    #0b10000000             ;is the A/D conversion complete?
        bne     1f                      ;if yes

        inc     *L001A                  ;inc a timeout counter for the conversion
        bpl     0f                      ;if counter<128, keep waiting

;Timeout counter overflow: Give up on the conversion.
        clr     *L001A                  ;reset the timeout counter
        andA    #0b00000111             ;get the current channel selector
        sei
        staA    ADCTL                   ;restart the conversion with the same channel selection
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        cli
0:      rts

;A/D Conversion is complete.
1:

; The OFLO ISR will have set 'half_overflow' non-zero to indicate we should check for TCNT being >0x8000
        ldaA    half_overflow           ; Are we allowed to check yet?
        beq     99f                     ; no

; Check and see if the MS bit of the timer has become set,
; indicating we have transitioned into the upper half of the timer count:
        sei                             ; begin critical section:
        ldX     TCNT                            ; Read the current time
        bpl     98f                             ; if B15 of current time is 0, its too soon

; Before logging TCNT, check to see if either IC1 (cam) or IC2 (crank) has become pending
; since entering this critical section:
        ldaA    TFLG1
        andA    #0x06
        bne     98f                             ; Yes: abort logging our marker and let the IC ISR run first
                                                ; Next time through, we will try again.

; All is clear: no pending IC ISR, so we are free to log our half-overflow event
        stX     LW+LOGID_ECU_T1_HOFLO_TYPE_TS
        clrA
        staA    half_overflow
98:     cli                             ; end critical section
99:

        clr     *L001A                  ;reset the conversion timeout counter for the next conversion

        ldaB    ADCTL                   ;which channel did we just convert?
        andB    #0b00000111
        lslB                            ;convert the channel # to a table index
        ldX     #LC513
        aBX                             ;index into the tbl at 0xC513
        ldD     ADR5                    ;single channel, no scan always puts result in ADR5
        ldX     0, X
        jsr     0, X                    ;process the converted value using the appropriate routine

;Figure out which channel to convert next.  In each case, the
;conversion is scan=false, single channel.

;The counter bit pattern triggers particular conversions with
;associated probabilities.  The patterns are checked in a
;particular order, which explains why one pattern will never
;trigger.

; xxxxxx0x throttle angle      128 of 256 (50.0%)
; xxxxxx10 VM Voltage Monitor   64 of 256 (25.0%)
; xxxxxx01 water temp            0 of 256 ( 0.0%) this pattern will never trigger: see below
; xxxxx011 air temp             32 of 256 (12.5%)
; xxxx0111 MUX_Y:2 AAP          16 of 256 ( 6.3%) ambient air pressure
; xxx01111 tipover               8 of 256 ( 3.2%)
; xx011111 MUX_X:AT              4 of 256 ( 1.6%) (AT is not used)
; x0111111 water temp            2 of 256 ( 0.8%)
; 01111111 MUX_Y:0 CO2L          1 of 256 ( 0.4%)
; 11111111 MUX_X:0 CO1R          1 of 256 ( 0.4%)

        inc     *L0019                  ;init'd at reset to 0x7f

        ldaA    *L0019
        ldaB    #0x00                   ;AN1 (Throttle Angle) if the
        andA    #0b00000011             ;2 LS bits of the counter are either 00 or 01
        beq     2f
        cmpA    #0x01
        beq     2f

        ldaB    #0x04                   ;AN5
        ldaA    *L0019                  ;reload the current count
        lsrA
        bhs     2f                      ;convert AN5 [VM (voltage monitor)] if counter B0==0

        sei
        ldaB    #0x01                   ;mux channel 1
        staB    PORTD
        cli
        ldaB    #0x07                   ;AN8
        lsrA
        bhs     2f                      ;convert AN8 [MUX_X:1 THW (Water Temp)] if counter B1==0

        ldaB    #0x05                   ;AN6
        lsrA
        bhs     2f                      ;select AN6 [THA] (air temp) if B2==0

        sei
        ldaB    #0x02                   ;select mux channel 2
        staB    PORTD
        cli
        ldaB    #0x07                   ;AN8
        lsrA
        bhs     2f                      ;convert AN8 [MUX_Y:2] (Ambient Air Pressure) if B3==0

        sei
        ldaB    #0x03                   ;select MUX input 3
        staB    PORTD
        cli
        ldaB    #0x06                   ;AN7
        lsrA
        bhs     2f                      ;select AN7 [MUX_X:DON] (fall sensor) if B4==0

        sei
        ldaB    #0x02                   ;select MUX input 2
        staB    PORTD
        cli
        ldaB    #0x06                   ;AN7
        lsrA
        bhs     2f                      ;select AN7 [MUX_X:AT] (not used) if B5==0

        sei
        ldaB    #0x01                   ;select MUX input 1
        staB    PORTD
        cli
        ldaB    #0x06                   ;AN7
        lsrA
        bhs     2f                      ;convert AN7 [MUX_X:THW] (water temp) if B6==0

        sei
        ldaB    #0x00                   ;select MUX input 0
        staB    PORTD
        cli
        ldaB    #0x07                   ;AN8
        lsrA
        bhs     2f                      ;select AN8 [MUX_Y:0] (CO2) if B7==0

        ldaB    #0x06                   ;default: select AN7 [MUX_X:0] (CO1) if B7==1

2:      sei
        staB    ADCTL                   ;trigger a conversion by writing the channel selector
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        cli
        rts

;*****************************************************************
jmpTbl_1:
LC513:  .word   tbl1__00                ;AN1: VTA    Throttle Angle
LC515:  .word   tbl1__01                ;AN2: ---    MAP: ?average? MAP.  This gets converted once at reset, and never again!
                                        ;Is this an error check: compare MAP versus AAP, which should be the same at power-up?
LC517:  .word   tbl1__02                ;AN3: PM     MAP: Manifold Air Pressure run through a peak-detect circuit
LC519:  .word   tbl1__03                ;AN4: OX     <not used>
LC51B:  .word   tbl1__04                ;AN5: VM     Voltage Monitor of 12V source that powers ign/injector coils
LC51D:  .word   tbl1__05                ;AN6: THA    Air Temp          F: water temp
LC51F:  .word   tbl1__06                ;AN7: MUX_X
LC521:  .word   tbl1__07                ;AN8: MUX_Y

;Conversion routines for the MUX-X inputs:
LC523:  .word   tbl1__08                ;C0:  CO1    CO Pot #1 (right)
LC525:  .word   tbl1__09                ;C1:  THW    Water temp        F: air temp
LC527:  .word   tbl1__10                ;C2:  AT     <not used>
LC529:  .word   tbl1__11                ;C3:  DON    Tipover sensor

;Conversion routines for the MUX-Y inputs:
LC52B:  .word   tbl1__12                ;C0:  CO2    CO Pot #2 (left)
LC52D:  .word   tbl1__13                ;C1:  REV    <not used>
LC52F:  .word   tbl1__14                ;C2:  ---    Ambient Air Pressure
LC531:  .word   tbl1__13                ;C3:  REV    <not used>


;*****************************************************************
;Init the memory-resident A/D readings by performing a conversion
;on each of the analog channels AN1..AN6.  Then, process each of
;the 4 channels for each of the MUX'd inputs AN7 and AN8.

;This routine is called once after RESET.

;Assumes that ADCTL has been written with 0 at some point before
;we get here.

; A complete conversion sequence takes 128 E-clks.

LC533:  clr     *L001A                  ;clear a timeout counter: a complete conversion takes 128 E-clks
0:      ldaA    ADCTL                   ;read ADCTL
        bitA    #0b10000000             ;test CCF (Conversion Complete Flag)
        bne     1f                      ;br if the conversion completed sucessfully

        inc     *L001A                  ;inc timeout counter
        bne     0b                      ;and try again...

        staA    ADCTL                   ;zero ADCTL: SCAN=off, MULT=off., chan=PE0
                                        ;writing ADCTL initiates a new conversion sequence
        nop                             ;2 E-clks per NOP
        nop
        nop
        nop
        nop
        nop
        nop
        bra     LC533                   ;total of about 17 E-clks allows A/D to do the sample phase

;Conversion is complete.
1:      ldaB    PORTJ
        eorB    #0b00001000
        staB    PORTJ

        ldaB    ADCTL                   ;which channel did we just convert?
        andB    #0b00000111
        lslB
        ldX     #LC513
        aBX                             ;index into the table based on the channel we just converted
        ldX     0, X
        ldD     ADR5                    ;read the A/D result
        jsr     0, X                    ;execute a routine based on which channel we just converted

        ldaB    ADCTL                   ;read the current conversion channel
        andB    #0b00000111             ;mask to AN1..AN8
        cmpB    #0x06
        bhs     4f                      ;convert AN1 through AN6 only

        incB                            ;else increment channel #
2:      staB    ADCTL                   ;writing the new channel number triggers a new converison
        nop
        nop
        nop
        nop
        nop
        nop
        nop                             ;delay during the sample phase?

        ldaA    #40                     ;delay 200 (40*5) E-clks
3:      decA                            ;2 E-clk
        bne     3b                      ;3 E-clk

        bra     LC533                   ;start over

;When we get here, we are done converting AN1..AN6.

;Now scan the MUX'd inputs for each of AN7 and AN8:
4:      ldaA    PORTD                   ;read the mux selector
        incA                            ;calc next channel selector addr
        andA    #0b00000011
        bne     5f                      ;if not exceeding channel 3

        incB                            ;else point at the next analog channel
        cmpB    #0x08
        bhs     6f                      ;until we have processed AN7 and AN8

5:      staA    PORTD                   ;select the next mux channel
        bra     2b

6:      ldD     *L0022                  ;get the left-aligned raw VTA reading
        stD     *L0044
        stD     *L0046

        ldaA    *L0027                  ;get the raw AAP (Atmospheric Air Pressure)
        staA    *L003E
        staA    *L0040

        rts

;-----------------------------------------------------------------
;AN7 - MUX_X converter
tbl1__06:
        ldaB    PORTD
        andB    #0b00000011             ;read the mux selector bits
        lslB                            ;generate index into jmp table
        ldX     #LC523
        aBX
        ldX     0, X
        jsr     0, X                    ;invoke the jmp table routine
        rts

;-----------------------------------------------------------------
;AN8 - MUX-Y converter
tbl1__07:
        ldaB    PORTD
        andB    #0b00000011             ;read the mux selector bits
        lslB                            ;generate index into jmp table
        ldX     #LC52B
        aBX
        ldX     0, X
        jsr     0, X                    ;invoke the jmp table routine
        rts

;-----------------------------------------------------------------
;AN1 - VTA: Throttle Angle A/D Handler
tbl1__00:
        stD     *L001B_tmp              ;save the current A/D reading

        cmpA    #0x0A                   ;check if the VTA sensor is stuck at either limit
        blo     1f                      ;br if A/D < 0x0A (we are lower than the lower limit)
        cmpA    #0xF6
        bhs     1f                      ;br if A/D >= 0xF6 (we are higher than the higher limit)

;VTA sensor is at some reasonable reading:
        brclr   *L0010 #0b00100000 0f   ;br if this reading was not the first non-suspect reading

;the sensor was "suspect", but now has a reasonable reading
        bclr    *L0010 #0b00100000      ;remove the "suspect" indicator
        bra     3f

;We get here if the VTA sensor is not suspect, and was not suspect the last time either.
0:      ldaA    L0138                   ;how many "good" VTA conversions have we done in a row?
        cmpA    #0x40
        blo     3f                      ;br if < 64.

;We have done more than 64 good conversions in a row:
        bclr    *L000C #0b00001000      ;mark the VTA sensor as being "good"

        ldD     *L001B_tmp              ;Restore the original A/D reading (remember: low 6 bits of HC11 A/D result are always 0)
        stD     *L0022                  ;L0022 gets the raw left-aligned VTA A/D reading
        lsrD                            ;Move the 10-bit A/D reading into the 10 LS bits
        lsrD
        lsrD
        lsrD
        lsrD
        lsrD
        stD     *L0024                  ;save the right-aligned, unfiltered version of VTA in L0024

#if !defined VTA_INCLUDES_TS
        cmpD    RAW_VTA_prev            ;only log good sensor readings that differ from the previous reading
        beq     3f
        stD     LW+LOGID_ECU_RAW_VTA_TYPE_U16
        stD     RAW_VTA_prev
        bra     3f
#else
; Combine every VTA reading with the upper 6 bits of TCNT so that the log decoder
; can see time advance with more precision even when there are no crankshaft events.
        ldaA    TCNT
        andA    #0xFC                   ;extract the MS 6 bits of TCNT1...
        oraA    *L0024                  ;...and insert them into the unused upper 6 bits of the 10-bit VTA
        ldaB    *(L0024+1)              ;D now contains the combined time & VTA data
        stD     LW+LOGID_ECU_RAW_VTA_TYPE_U16
        bra     3f
#endif

1:      brclr   *L0010 #0b00100000 2f   ;if the sensor is not currently suspect, make it so

;The VTA sensor is currently marked as suspect.
        ldaA    L0133                   ;how many times in a row has the sensor been suspect?
        cmpA    #0x40
        blo     3f                      ;br if < 64.: we are not giving up on it just yet...

;The VTA sensor is buggered up:
        bset    *L000C #0b00001000      ;mark the VTA sensor as being "broken"
        ldD     L9A5D                   ;Use this value (0x0340)...
        stD     *L0024                  ;...as the default right-aligned VTA reading

;Presumably for debug purposes: L0022 still gets the raw left-aligned
;VTA A/D reading, even though we have decided that VTA is buggered:
        ldD     *L001B_tmp
        stD     *L0022
        bra     3f

2:      bset    *L0010 #0b00100000      ;mark VTA as being "suspect"

3:      ldD     #0x8000
        stD     *L0016                  ;debug: mark that we just did a VTA conversion
        rts

;-----------------------------------------------------------------
;AN2 - MAP ?average?
; This input is not used -  A/D result in D is ignored.
tbl1__01:
        bset    *L0000 #0b00100000      ;mark the fact that we have fresh AN2 data

        ldD     #0x4000                 ;debug: mark that we just did an AN2 conversion
        stD     *L0016
        rts


;-----------------------------------------------------------------
;AN3 - PM: Manifold Air Pressure ?peak-detect?
tbl1__02:
        staA    *L001D                  ;save the high-order 8 bits of the conversion

        ldD     #0x2000                 ;debug: mark that we just did a MAP/PM conversion
        stD     *L0016
        rts

;-----------------------------------------------------------------
;AN4 -  OX   The A/D result is totally ignored
tbl1__03:
        ldD     #0x1000                 ;debug: mark that we just did an OX conversion
        stD     *L0016
        rts

;-----------------------------------------------------------------
; AN5 - VM Voltage Monitor: From the main wiring diagram, it
; appears that this A/D input is connected to the nominal 12V
; signal used to power the ignition coils and the fuel injectors.

; The raw A/D input circuit is a voltage divider arranged such
; that the voltage on AN5 is 1/4 of the real voltage.
; VM volts = (AD_counts/256) * 5V * 4
; VM has a resolution of 1 count == approx 78 millivolts.

tbl1__04:
        staA    *L001B_tmp

        cmpA    *L002A
        beq     10f
        staA    LW+LOGID_ECU_RAW_VM_TYPE_U8  ; log the new VM conversion if it is different than before
10:
        staA    *L002A                  ; save the raw conversion
        cmpA    #38                     ; 38 means 2.97V
        blo     2f                      ; br if the reading was below 2.97V (probably bad!)

; Reading was above 2.97V (good!)
        brclr   *L0008 #0b10000000 0f
        brclr   *L0008 #0b00000001 1f
        bset    *L0008 #0b00000010
        bra     7f

0:      bset    *L0008 #0b10000000
1:      bset    *L0008 #0b00000001
        bra     7f

; Reading was below 2.97V (bad!):
2:      brset   *L0008 #0b10000000 4f   ;
        brset   *L0008 #0b00000001 5f
3:      bset    *L0008 #0b00000001
        bra     7f

4:      bclr    *L0008 #0b10000000
        bra     3b

5:      bclr    *L0008 #0b00000010
        bra     6f

6:      bclr    *L0008 #0b00000001

7:      ldD     #0x0800                 ;debug: mark that we just did a VM conversion
        stD     *L0016
        rts


;-----------------------------------------------------------------
;AN6 - THA - Air Temp Thermistor
tbl1__05:
        staA    *L001B_tmp
        cmpA    #0x07
        blo     0f
        cmpA    #0xF8
        blo     2f
0:      brclr   *L0010 #0b00001000 1f
        ldaA    L0135
        cmpA    #0x40
        blo     5f
        bset    *L000C #0b00000010

        ldaA    L9A60                           ; 0x70 maps to 23.5C using S-H conversion method
        staA    *L0031                          ; We use that as a default air temp if the air temp thermistor seems broken
        bra     5f

1:      bset    *L0010 #0b00001000
        bra     5f

2:      brclr   *L0010 #0b00001000 3f
        bclr    *L0010 #0b00001000
        bra     5f

3:      ldaA    L013A
        cmpA    #0x40
        blo     5f
        bclr    *L000C #0b00000010
        ldaA    *L001B_tmp
        staA    *L0029

;We only log rawTha data if it is different from what we last sent
        cmpA    RAW_THA_U8_prev
        beq     9f
        staA    RAW_THA_U8_prev
        staA    LW+LOGID_ECU_RAW_THA_TYPE_U8
9:
        subA    #0x00
        bhs     4f
        ldaA    #0x00
4:      ldaB    #0x20
        mul
        ldX     #LC018-3
        jsr     LF3D0
        staA    *L0031

5:      ldD     #0x0400                 ;debug: mark that we just did a THA conversion
        stD     *L0016
        rts

;-----------------------------------------------------------------
;MUX-Y:C2 AAP Ambient Air Pressure

; Data collected from a ride showed that the 8-bit AAP when I started
; the ride was 0xAC (172) counts.  For the purposes of tracing this
; code, we will assume that this is a typical 'good' value.

tbl1__14:
        staA    *L001B_tmp              ;We only use upper 8 bits of the 10-bit conversion

        cmpA    #0x0C
        blo     0f                      ;if new A/D reading < 0x0c, we are lower than our low limit: error!

        cmpA    #0xF8
        blo     2f                      ;if new A/D reading < 0xf8, we are in range.

; If we get here, the latest A/D was outside of our expected range.
; not happy with the AAP reading if we get here.  If our previous reading was OK,
; we treat this like a glitch.
0:      brclr   *L0010 #0b00010000 1f   ;br if the last AAP was OK (ie. this bad reading could be a glitch)

; If we get here, we are not seening a glitch: we are not happy with
; the current AAP, and we were not happy with its predecessor.
        ldaA    L0136
        cmpA    #0x40
        blo     4f                      ;branch if we have seen less than 64 bad readings in a row.

; We have seen at least 64 bad readings in a row.
        bset    *L000C #0b00000001      ;mark the AAP sensor as being 'broken'
        ldaA    L9A61
        staA    *L0027
        ldaA    #0x80
        staA    *L0041
        bra     4f

1:      bset    *L0010 #0b00010000      ;the AAP reading is 'suspect'
        bra     4f

2:      brclr   *L0010 #0b00010000 3f   ;the AAP reading has been 'good' at least twice in a row
        bclr    *L0010 #0b00010000      ;the AAP reading is good this time, but was not good last time
        bra     4f

3:      ldaA    L013B                   ;check the "good" AAP count
        cmpA    #0x40
        blo     4f                      ;br if we have seen less than 64 "good" readings in a row

; After 64 good readings in a row, the sensor is defined to be "good".
        bclr    *L000C #0b00000001      ;clear the 'broken' bit.

        ldaA    *L001B_tmp              ;get the 8 MSbits of the original AAP A/D reading
        cmpA    RAW_AAP_U8_prev
        beq     9f
        staA    RAW_AAP_U8_prev         ;only log if it has changed
        staA    LW+LOGID_ECU_RAW_AAP_TYPE_U8
9:
        staA    *L0027                  ;but always save the new reading here
        brset   *L0000 #0b00010000 4f

        sei
        bset    *L0000 #0b00010000      ;mark the fact that we have a fresh AAP conversion

        ldaA    *L0027                  ;get the raw AAP reading
        staA    *L0033                  ;save it as the filtered reading.
        cli

4:      ldD     #0x0008                 ;debug: mark that we just did an AAP conversion
        stD     *L0016
        rts

;-----------------------------------------------------------------
;MUX-X:C0 CO1 CO Pot #1 (right)

; Do some math to artificially give the trim pots a "dead zone" around the 0x80 half-way point.

tbl1__08:
        staA    *L002B                  ;save the raw A/D reading for debug purposes

        cmpA    #0x90
        bhs     0f                      ;br if reading is >= 0x90
        cmpA    #0x70
        bhi     2f                      ;br if (reading >  0x70) && (reading < 0x90)

;The reading is in the range [0x00..0x70]:
        ldX     #0x9153                 ;use a table to massage the reading into the range [0x20..0x80]
        bra     1f

;The reading is in the range [0x90..0xFF]:
0:      ldX     #0x915D                 ;use a table to massage the reading into the range [0x80..0xE0]

1:      jsr     LF3C2                   ;massage it
        bra     3f

;For any reading in the range [0x71..8F], we force the result to be 0x80 (mid-band).
;This creates the dead zone.
2:      ldaA    #0x80

3:      staA    *L00C5

        ldD     #0x0200                 ;debug: mark that we just did a CO1 conversion
        stD     *L0016
        rts

;-----------------------------------------------------------------
;MUX-Y:C0 CO2 CO Pot #2 (left)
tbl1__12:
        staA    *L002C
        cmpA    #0x90
        bhs     0f
        cmpA    #0x70
        bhi     2f
        ldX     #0x9153
        bra     1f

0:      ldX     #0x915D
1:      jsr     LF3C2
        bra     3f

2:      ldaA    #0x80
3:      staA    *L00C6

        ldD     #0x0020                 ;debug: mark that we just did a CO2 conversion
        stD     *L0016
        rts

;-----------------------------------------------------------------
;MUX-X:C1  THW Water Temp Thermistor

;According to the service manual, the temp thermistors for water
;(and air) have the following characteristics:

; deg C   range (ohms)   Center ohms  A/D V       8-bit hex
;-------  ------------   -----------  -----       ---------
;  20     1960-2940      2700         2.50        0x80
;  40      800-1200      1000         1.35        0x45
;  60      400-700        550         0.84        0x2B
;  80      200-400        300         0.50        0x19
; 100      120-250        185         0.32        0x10

;The A/D volts is the volts that the A/D will see if the value
;for "center ohms" is assumed to be what the conversion circuit
;sees.  The volts that the A/D sees can be calculated from

;       Vcc * Rthw / (Rthw + R751)

;where R751 is 2700 ohms.  A very common NTC Resistor value is
;2700 ohms at 25C, so that kind of fits with things too.

tbl1__09:
        staA    *L001B_tmp              ;save upper 8 bits in the tmp variable

; We compare our reading to a pair of min/max endpoints.
; The min/max values were converted to temperatures using Steinhart-Hart NTC resistor calculations.
; They may not be completely precise, but you get the picture.
        cmpA    #0x07                   ; S-H calculations indicate that this value represents about 141C
        blo     0f                      ;br if the new reading is < 7 (so hot that the temp sensor must be reading wrong)
        cmpA    #0xF8                   ; S-H calculations indicate that this value represents about -47C
        blo     2f                      ;br if the new reading is <0xF8 (the water is in a 'normal' range)

; The sensor response is out of our valid min/max range.
; We must assume that is not operating properly.
0:      brclr   *L0010 #0b00000010 1f   ;br if this is the first suspect THW conversion

        ldaA    L0134                   ;counts suspect THW readings
        cmpA    #0x40                   ;have we had 63 suspect readings in a row?
        blo     6f                      ;if not

; After so many failures in a row, we decide that the THW sensor is bad.
        bset    *L000C #0b00000100      ; mark the THW sensor as being "bad"
        ldaA    L9A5F                   ; 0xB0 (176.) which converts to about 0C using S-H conversion.
                                        ; This appears to be a default "limp" mode temp to use if the sensor seems broken
        staA    *L0030                  ; save this value as the converted, interpolated water temp.
        bra     5f

1:      bset    *L0010 #0b00000010      ;mark the fact that the THW conversion is suspect
        bra     6f                      ;(enables the suspect conversion counter L0134)

;Water temp is in the NORMAL range:
2:      brclr   *L0010 #0b00000010 3f   ;if our THW sensor is considered to be "good"

        bclr    *L0010 #0b00000010      ;else it _was_ bad, so mark it as good,
        bra     6f                      ;but skip the remaining temp processing anyway

;THW sensor had a good reading this time around.
3:      ldaA    L0139
        cmpA    #0x40                   ;have we had 64 consecutive "good" readings?
        blo     6f                      ;if not, skip the remaining processing

        bclr    *L000C #0b00000100      ;definitivly clear the "bad THW sensor" bit

        ldaA    *L001B_tmp              ;get the original A/D conversion back again
        staA    LW+LOGID_ECU_RAW_THW_TYPE_U8
        staA    *L0028                  ;for debug? never used again!

        subA    #0x00                   ;clip to a lower limit of 0x00 (useless, in this case)
        bhs     4f                      ; -- C will never be set!
        ldaA    #0x00
4:      ldaB    #0x20                   ;scale the raw 8 A/D to an 8.8 fixed point value in the range 00.00 to 1F.E0
        mul

;The "-3" is required because the particular entrypoint we are using thinks we are pointing at
;row data.  Instead, this is a table containing pure interpolation endpoints.
        ldX     #LC018-3

;Convert and interpolate our raw A/D using a 32 entry table (since A=1F max).
;B holds the fraction used to interpolate the selected table endpoint values.
        jsr     LF3D0                   ;convert and interpolate
        staA    *L0030                  ;save the converted, interpolated 8-bit THW (water temp) reading

;These next two calcs are performed exactly once.
5:      brset   *L0003 #0b00100000 6f   ;skip the calcs if we have ever done them before
        bset    *L0003 #0b00100000      ;do it this time, but not again
        ldaA    *L0030                  ;the converted, interpolated 8-bit THW (water temp) reading
        ldX     #L803A
        jsr     LF3C2
        staA    *L00C8                  ;used to stretch fuel injection times

        brset   *L0004 #0b00010000 6f   ;skip this one if we have already done it
        bset    *L0004 #0b00010000      ;else do it this time, but not again
        ldaA    *L0030                  ;the converted, interpolated 8-bit THW (water temp) reading
        ldX     #L9031
        jsr     LF3C2
        stD     *L00B0                  ;this value appears to "decay" from its start value
                                        ;(calculated here) down to a minimum value of 0
                                        ;Not sure what it is used for though...

6:      ldD     #0x0100                 ;debug: mark that we did a THW conversion
        stD     *L0016
        rts

;-----------------------------------------------------------------
;MUX-X:C2  AT  <not used>
tbl1__10:
        staA    *L002E                  ;never used

        ldD     #0x0080                 ;debug: mark that we did an AT conversion
        stD     *L0016
        rts

;-----------------------------------------------------------------
;MUX-X:C3  DON Tipover sensor
tbl1__11:
        staA    *L002D                  ;used in one spot only

        ldD     #0x0040                 ;debug: mark that we did a DON conversion
        stD     *L0016
        rts

;-----------------------------------------------------------------
;MUX-Y:C1&C3  REV  (not used)
tbl1__13:
        staA    *L002F                  ;never used

        ldD     #0x0010                 ;debug: mark that we did a REV conversion
        stD     *L0016
        rts

;*****************************************************************
;Looks like a disaster sort of resync.  Discard all engine params
;that we know about.

LC7D2:  ldaA    #14
        staA    *L0051                  ;something to do with crank rotation
        staA    *L0052
        staA    *L0053

        ldaA    #0x00
        staA    L010C
        staA    L010A

        bset    *L0000 #0b01000100
        bclr    *L0000 #0b00000010      ;?RPM period is not valid?
        bclr    *L0000 #0b00001000      ;have not seen a falling edge event on crankshaft sensor
        bset    *L000B #0b11001111
        bset    *L0001 #0b11010000
        bclr    *L0001 #0b00100000
        bclr    *L0003 #0b00010000      ;don't know where the crank is
        bclr    *L0003 #0b00100000      ;we don't have an injector stretch based on THW (water temp)

        ldD     #0x0000
        stD     *L004D                  ; we don't know the current engine 8.8 RPM50

        ldaA    #0x80                   ;mark the crank posn as being "unknown, and never seen a cam event yet'
        staA    *L0055

        ldD     #0xFFFF
        stD     *L00D2
        stD     *L00D4
        stD     *L00D6
        stD     *L00D0
        stD     *L00CE
        stD     *L00D8
        stD     *L00DA
        stD     *L00EF
        stD     *L00F1
        stD     *L00F5
        stD     *L00F7
        stD     *L00FB
        stD     *L00FD

        staA    *L0054

        ldD     LC016                   ;0x014A
        stD     *L00F3
        stD     *L00F9

        brclr   *L0003 #0b00000001 0f
        bset    *L0003 #0b00001000
        bclr    *L0003 #0b00000001
0:      rts

; *****************************************************************
; This subroutine calculates the crankshaft period and half period.
; It is called every time through the main loop, which means it is
; called faster than even the fastest scheduled routines.

; The crankshaft has 6 reference indicators, 5 of which are small and
; one of which is long.  The long one deson't matter much since it is
; only the leading edge that matters.  The half period is calc'd from
; the sum of the most recent three 1/6 period measurements.

LC834:  bclr    *L0002 #0b00000100

;This subr is the only code which cares about this particular bit.
;We set it on entry, and clear it on exit.
        ldaA    *L0012
        oraA    #0b00010000             ;set L0012:B4 on entry
        staA    *L0012

        ldaA    *L0054
        cmpA    #0x40                   ;if L0054 < 64.
        blo     0f                      ;we are OK

        ldD     #0xFFFF                 ;else set the max period...
        stD     *L00D8                  ;...as the new 1/2 crankperiod

        ldD     #0x01E0
        bra     5f

0:      ldD     *L00D2                  ;1/6 crank period
        addD    *L00D4                  ;plus 1/6 crank period
        bhs     2f                      ;if no overflow

1:      ldD     #0xFFFF                 ;else set the max period
        bra     3f

2:      addD    *L00D6                  ;plus 1/6 crank period
        blo     1b                      ;if we overflowed

3:      stD     *L00D8                  ;equals the new 1/2 crankperiod
        lslD                            ;double it to get the actual crankperiod
        bhs     4f                      ;if no overflow
        ldD     #0xFFFF                 ;else max it out

4:      stD     *L00DA                  ;write the full crankperiod, or 0xFFFF if it cannot be represented.

; Examples to help me figure out what follows.
; From the analysis towards the start of this program:
;   halfPeriod (in timer ticks) = 500,000 * (30/RPM)
;   halfPeriod (in timer ticks) = (15,000,000/RPM)
; At 1000 RPM, half period would be 500,000 * (30/1000) = 15000 ticks
; At 2000 RPM, half period would be 500,000 * (30/2000) =  7500 ticks
; At 4000 RPM, half period would be 500,000 * (30/4000) =  3750 ticks
; At 8000 RPM, half period would be 500,000 * (30/8000) =  1875 ticks


        ldD     #0x927C                 ;Numerator: 37500.  As a crank half-period, it represents 400 RPM (15,000,000 / 37500)
        ldX     *L00D8                  ;Denominator: the most recent 1/2 crankperiod
        idiv                            ;Integer divide X16 = D16/X16; D16 = remainder
        stX     L0153                   ;X contains the quotient: (mostRecentHalfPeriod / periodAt400RPM)

        ldX     *L00D8                  ;Reload the original denominator (the most recent 1/2 crankperiod)
        fdiv                            ;compute the fractional result of the original divide operation into X
        xgDX                            ;result to A
        tAB                             ;Put MS 8 bits of fraction into B
        ldaA    L0154                   ;Load A with LS 8 bits of quotient

; at this point (in 8.8 notation), A represents the number of 400 RPM intervals:
;   for 1000 RPM, D = 2.5
;   for 2000 RPM, D = 5.0
;   for 4000 RPM, D = 10.0
;   for 8000 RPM, D = 20.0

        lslD
        lslD
        lslD

; at this point (in 8.8 notation), A represents the number of 50 RPM intervals:
;   for 1000 RPM, D = 20
;   for 2000 RPM, D = 40
;   for 4000 RPM, D = 80.0
;   for 8000 RPM, D = 160.0

; C would only be set if D was 0x2000 or more before the *8 (12800 RPM)
        bhs     5f
        ldD     #0xFFFF                 ; signal an overflow (RPM too fast)

5:      stD     *L004D                  ; current engine speed in multiples of 50 RPMs, represented as 8.8
        cmpD    L9A4B                   ; 0x1000, represents 16*50RPM or 800 RPM
        blo     6f                      ; br if current engine speed is < 800 RPM
        bset    *L0000 #0b00000010      ; we set this bit every time we observe the current engine speed >= 800 RPM

6:      ldD     *L004D                  ; a pointless instr
        subD    #0x1000                 ; bias current engine speed downwards by 800 RPM
        bhs     7f
        ldD     #0x0000                 ; clip result to 0 if it went negative
        bra     9f

7:      cmpD    #0xE000                 ; If original RPM was 0xF0 or more (12000 RPM), clip it
        blo     8f                      ; br if < 0xE000
        ldD     #0x1C00                 ; else clip to 0xE000/8 (biased 12000 RPM) and skip the divide by 8 that follows
        bra     9f

8:      lsrD                            ;The biased current eng spd as a multiple of 50 gets divided by 8
        lsrD
        lsrD

; This is the current engine speed as an 8.8 value representing a multiple of 400 RPM,
; biased so that a value of 0.0 means 800 RPM.
9:      stD     *L004F                  ;save this magic number

        ldaA    *L0012
        andA    #0b11101111             ;clear L0012:B4 on exit
        staA    *L0012

        jmp     LC298                   ;acts just like a 'rts'.

;*****************************************************************
; This is the big mofo mainloop routine.

;L90D1:  .byte   0x53, 0x80, 0x08
;        .byte   0xD2, 0x70, 0x48, 0x30, 0x21

LC8AF:  ldaA    *L002A                  ;get the raw VM A/D reading
        ldX     #L90D1

;Reg A is reduced by the 0[X] to a min of 0, clipped to a max of 1[X], and then multiplied by 2[X].
        jsr     LF3C2
        tAB
        clrA
        lslD
        lslD
        lslD
        stD     *L0079

        ldaA    *L0040                  ;something to do with air pressure calc's
        subA    *L003A
        bhs     LC8AF_0
        ldaA    #0x00
LC8AF_0:
        staA    *L0034
        ldaA    *L0040
        subA    *L003C
        bhs     LC8AF_1
        ldaA    #0x00
LC8AF_1:
        staA    *L0035
        ldaB    *L004D                  ;Integer part of 8.8 RPM50

        ldaA    *L0049_PORTG_debounced
        andA    #0b00101000
        cmpA    #derestricted
        bne     LC8AF_2

;Derestricted
        cmpB    L9A6E                   ;const: 0xD2 (210.), in 8.8RPM50 == 10500 RPM
        bhs     LC8AF_4                 ;br if current RPM >= 10500
        cmpB    L9A6D                   ;const: 0xD2 (210.), in 8.8RPM50 == 10500 RPM
        bhs     LC8AF_5                 ;br if current RPM >= 10500
        bra     LC8AF_3

;Restricted
LC8AF_2:
        cmpB    L9173                   ;const: 0xD2 (210.), in 8.8RPM50 == 10500 RPM
        bhs     LC8AF_4                 ;br if current RPM >= 10500
        cmpB    L9172                   ;const: 0xD2 (210.), in 8.8RPM50 == 10500 RPM
        bhs     LC8AF_5                 ;br if current RPM >= 10500

LC8AF_3:
        bclr    *L0006 #0b10000000      ;clear this bit if 8.8RPM50 < 10500 RPM
        bra     LC8AF_5

LC8AF_4:
        bset    *L0006 #0b10000000      ;set this bit if 8.8RPM50 < 10500 RPM

LC8AF_5:
        brset   *L000C #0b00001000 LC8AF_9      ;br if VTA sensor is bad
        brset   *L0006 #0b00010000 LC8AF_7

        ldaB    L0125
        cmpB    L9189                   ;const 0x04
        blo     LC8AF_6
        bset    *L0006 #0b00010000
LC8AF_6:
        bra     LC8AF_9

LC8AF_7:
        ldaB    L0126
        cmpB    L918A                   ;const 0x04
        blo     LC8AF_9
        brclr   *L0006 #0b01000000 LC8AF_8
        brclr   *L0006 #0b00010000 LC8AF_8
        bset    *L0007 #0b00000001
        bset    *L0007 #0b00001000
LC8AF_8:
        bclr    *L0006 #0b00010000
LC8AF_9:
        brclr   *L000C #0b00110000 LC8AF_10     ;br if both ?MAP? sensors are 0 (OK)
        bra     LC8AF_14

LC8AF_10:
        brset   *L000C #0b00001000 LC8AF_11
        ldaA    *L0048
        cmpA    *L00C2
        bhs     LC8AF_13
LC8AF_11:
        ldaA    *L0034
        cmpA    L917B
        bhs     LC8AF_12
        cmpA    L917A
        bls     LC8AF_13
        bra     LC8AF_15

LC8AF_12:
        bset    *L0006 #0b00001000
        bra     LC8AF_15

LC8AF_13:
        brclr   *L0006 #0b00001000 LC8AF_15
        brclr   *L0006 #0b01000000 LC8AF_14
        bset    *L0007 #0b00000010
        bset    *L0007 #0b00010000
LC8AF_14:
        bclr    *L0006 #0b00001000

; if ((THW <= 0xFF) || (L010A <= 0x7D) || (L0124 <= 0x0A)) goto LC8AF_21

LC8AF_15:
        ldaB    *L0030                  ;the converted, interpolated 8-bit THW (water temp) reading
        cmpB    L9175                   ;const 0xFF
        bls     LC8AF_21                ;therefore, always true?

        ldaB    L010A
        cmpB    L9174                   ;0x7D
        bls     LC8AF_21

        ldaB    L0124
        cmpB    L9177                   ;0x0A
        blo     LC8AF_21

        ldaB    *L004D                  ;8.8 RPM50
        brset   *L0006 #0b01000000 LC8AF_16
        cmpB    L9179                   ;const 0x42, in 8.8RPM50 == 3300 RPM
        bhi     LC8AF_18
        bra     LC8AF_25

LC8AF_16:
        cmpB    L9178                   ;const 0x3C, in 8.8RPM50 == 3000 RPM
        bhs     LC8AF_18
        brclr   *L0006 #0b01000000 LC8AF_22
        brset   *L0007 #0b00000100 LC8AF_17
        bset    *L0007 #0b00000100
        bset    *L0007 #0b00100000
LC8AF_17:
        brclr   *L0006 #0b01000000 LC8AF_22
        jmp     LC8AF_26

LC8AF_18:
        brclr   *L0006 #0b00010000 LC8AF_19
        brclr   *L000C #0b00001000 LC8AF_20
LC8AF_19:
        brset   *L000C #0b00100000 LC8AF_24
        brclr   *L0006 #0b00001000 LC8AF_24
LC8AF_20:
        ldaB    L0123
        cmpB    L918D
        bls     LC8AF_23
        bset    *L0006 #0b01000000
        bclr    *L0005 #0b00000001
        jmp     LC8AF_40

LC8AF_21:
        brclr   *L0006 #0b01000000 LC8AF_22
        bra     LC8AF_31

LC8AF_22:
        jmp     LC8AF_38

LC8AF_23:
        jmp     LC8AF_39

LC8AF_24:
        brclr   *L0006 #0b01000000 LC8AF_22
        ldaA    L9A26                   ;const 0x10
        cmpA    #0x00
        bne     LC8AF_25                ;odd: always taken

; never executed
        ldaA    *L00E2
        cmpA    #0x80
        bne     LC8AF_25
        ldaA    L9A27                   ;const 0x80
        staA    *L00E2

LC8AF_25:
        brclr   *L0006 #0b01000000 LC8AF_22
LC8AF_26:
        ldaB    *L008E
        cmpB    #0x80
        bne     LC8AF_31
        brset   *L0007 #0b00000100 LC8AF_27
        brset   *L0007 #0b00000010 LC8AF_27
        brset   *L0007 #0b00000001 LC8AF_28
        bra     LC8AF_29

LC8AF_27:
        ldaA    *L004D                  ;8.8 RPM50
        ldX     #0x9A7F
        jsr     LF3C2
        staA    L0153
        ldaA    *L004D                  ;8.8 RPM50
        ldX     #0x9AE9
        jsr     LF3C2
        ldaB    L0153
        bra     LC8AF_30

LC8AF_28:
        ldaA    *L004D                  ;8.8 RPM50
        ldX     #0x9A8C
        jsr     LF3C2
        staA    L0153
        ldaA    *L004D                  ;8.8 RPM50
        ldX     #0x9AF6
        jsr     LF3C2
        ldaB    L0153
        bra     LC8AF_30

LC8AF_29:
        jsr     LF734
LC8AF_30:
        bclr    *L0007 #0b00000010
        bclr    *L0007 #0b00000100
        bclr    *L0007 #0b00000001
        staB    *L008E
        cmpB    #0x80
        bhs     LC8AF_31
        staA    *L008F
LC8AF_31:
        ldaB    *L00E2
        cmpB    #0x80
        bne     LC8AF_36
        brset   *L0007 #0b00100000 LC8AF_32
        ldaA    L9A26
        cmpA    #0x00
        beq     LC8AF_36
        brset   *L0007 #0b00010000 LC8AF_32
        brset   *L0007 #0b00001000 LC8AF_33
        bra     LC8AF_34

LC8AF_32:
        ldaB    L9A27                   ;const 0x80
        ldaA    L9A2B                   ;const 0x04
        bra     LC8AF_35

LC8AF_33:
        bclr    *L0007 #0b00001000
        ldaB    L9A28
        ldaA    L9A2C
        bset    *L0005 #0b00000001
        bra     LC8AF_35

LC8AF_34:
        ldaA    L9A2D
        ldaB    L9A29
LC8AF_35:
        bclr    *L0005 #0b00000001
        bclr    *L0007 #0b00010000
        bclr    *L0007 #0b00100000
        staB    *L00E2
        cmpB    #0x80
        bhs     LC8AF_37
        staA    *L00E3
        bra     LC8AF_37

LC8AF_36:
        bclr    *L0005 #0b00000001
LC8AF_37:
        clr     L0123
LC8AF_38:
        bclr    *L0006 #0b00010000
        bclr    *L0006 #0b00001000
LC8AF_39:
        bclr    *L0006 #0b01000000
        ldaB    *L004D                  ;8.8 RPM50
        cmpB    L90D0                   ;const 0x18 == 1200 8.8RPM50
        bhs     LC8AF_41                ;br if current RPM >= 1200

LC8AF_40:
        ldaB    #0x80
        staB    *L008E

LC8AF_41:
        ldaB    *L004D                  ;8.8 RPM50
        cmpB    L9A2F                   ;const 0x18, == 1200 8.8RPM50
        blo     LC8AF_42                ;br if current RPM < 1200

        cmpB    L91AA                   ;const 0x12, == 900 8.8RPM50
        bhs     LC8AF_43                ;br if current RPM >= 900 -- makes no sense if prev branch was taken

        cmpB    L91A9                   ;const 0x1B, == 1350 8.8RPM50 -- weird: this constant is also used as a 1/6 period RPM
        blo     LC8AF_43                ;br if current RPM < 1350

LC8AF_42:
        ldaB    #0x80
        staB    *L00E2
LC8AF_43:
        ldaA    *L000C
        bitA    #0b00100000
        bne     LC8AF_44
        bitA    #0b00010000
        bne     LC8AF_44
        bitA    #0b00001000
        beq     LC8AF_45

LC8AF_44:
        ldaB    *L004D                  ;8.8 RPM50
        cmpB    L918F                   ;const 0xC8, == 10000 8.8RPM50
        bhs     LC8AF_46                ;br if current RPM >= 10000
        cmpB    L918E                   ;const 0xC4, == 9800 8.8RPM50
        bhs     LC8AF_47                ;br if current RPM >= 9800

; Current RPM must be < 9800 RPM
LC8AF_45:
        bclr    *L0006 #0b00100000      ;cleared if RPM is < 9800
        bra     LC8AF_47

LC8AF_46:
        bset    *L0006 #0b00100000      ;set if RPM is >= 10000

LC8AF_47:
        ldX     #L8021                  ;const 0[x] = 0x80
        ldD     *L0022                  ;the raw, unfiltered left-aligned VTA (Throttle Angle)
        subD    *L0044                  ;I think this is a filtered VTA
        bhs     LC8AF_48                ;br if raw VTA >= filtered VTA

;raw VTA is < filtered VTA, so deltaVTA is negative

;Atari basic stored negative fixed pt values with a positive fraction.  So: -32.25 is stored as -33 + 0.75
;0x0000 -> 0x0000
;0x0001 -> 0x01FF
;0x0100 -> 0xFF00
;0xFF00 -> 0x0100         -1 + 0/256 -> 1 + 0/256       this is right
;0xFFFF -> 0x0201         -1 + 255/256 -> 2 + 1/256     but this is not right

;how about signed int.signed frac
;2.75 - 1.25 = 02.C0 - 01.40 = 01.80

;1.50 - 2.75 = 01.80 - 02.C0 = FE.C0    -2 and 192/256  (real answer -1.25)
;after idiom: 0x0340 then divide by 2, result after 2nd idiom is 00.E0 (0.875)
                                        ; This next comA/negB/sbcA idiom takes the 2's complement of D (AABB)
        comA                            ; A = 1's complement of A
        negB                            ; B = 0-B where C=(B!=0)
        sbcA    #0xFF                   ; A = 0xFF - C - A

        jsr     LF3E4                   ;D8.8 = D8.8 * 0[X]0.8
        comA
        negB
        sbcA    #0xFF
        bra     LC8AF_49

; raw VTA >= filtered VTA, so deltaVTA is positive
LC8AF_48:
        jsr     LF3E4                   ;D8.8 = D8.8 * 0[X]0.8

LC8AF_49:
        addD    *L0044
        stD     *L0044

        ldaA    *L0049_PORTG_debounced
        andA    #0b00101000
        cmpA    #derestricted
        bne     LC8AF_50                ;if restricted
        jmp     LC8AF_104               ;if derestricted

;------------------------------------------------------------
;Restricted
LC8AF_50:
        ldaB    *L00AF                  ;?
        cmpB    L806D
        bhs     LC8AF_51
        ldD     *L0022                  ; the raw, left-aligned VTA
        stD     *L0044
LC8AF_51:
        ldaB    L010C
        cmpB    #0x64
        bhs     LC8AF_52
        ldaA    *L0021                  ;?
        clrB
        stD     *L0036                  ;?
        bra     LC8AF_55

LC8AF_52:
        ldX     #L9068
        ldaA    *L0021
        clrB
        subD    *L0036
        bhs     LC8AF_53
        comA
        negB
        sbcA    #0xFF
        jsr     LF3E4                   ;D8.8 = D8.8 * 0[X]0.8
        comA
        negB
        sbcA    #0xFF
        bra     LC8AF_54

LC8AF_53:
        jsr     LF3E4                   ;D8.8 = D8.8 * 0[X]0.8
LC8AF_54:
        addD    *L0036
        stD     *L0036
LC8AF_55:
        ldaB    *L0021
        subB    *L0036
        bhs     LC8AF_56
        negB
        ldaA    #0x80
        staA    L0153
        bra     LC8AF_57

LC8AF_56:
        ldaA    #0x00
        staA    L0153
LC8AF_57:
        cmpB    #0x80
        blo     LC8AF_58
        ldaB    #0x7F
LC8AF_58:
        staB    L0154
        ldD     L0153
        bitA    #0b10000000
        beq     LC8AF_59
        ldaB    #0x80
        subB    L0154
        bra     LC8AF_60

LC8AF_59:
        addB    #0x80
LC8AF_60:
        staB    *L00B8
        ldaA    L9069
        cmpA    L0154
        blo     LC8AF_61
        ldaA    #0x80
        staA    *L00B9
        ldaA    #0x00
        staA    L0154
        bra     LC8AF_62

LC8AF_61:
        staB    *L00B9
LC8AF_62:
        ldD     *L004D                  ;8.8 RPM50
        subA    #0x18
        bhs     LC8AF_63
        ldD     #0x0000
LC8AF_63:
        subD    #0xD000
        blo     LC8AF_64
        ldD     #0x0000
LC8AF_64:
        addD    #0xD000
        cmpA    #0x30
        bhs     LC8AF_65
        lsrD
        lsrD
        lsrD
        bra     LC8AF_66

LC8AF_65:
        lsrD
        lsrD
        lsrD
        lsrD
        addD    #0x0300
LC8AF_66:
        stD     L0179
        ldX     #0x9BA1
        jsr     LF3D0
        staA    *L00BE
        ldaB    L0154
        mul
        stD     L0154
        ldaA    L0153
        bitA    #0b10000000
        beq     LC8AF_67
        ldD     #0x8000
        subD    L0154
        bhs     LC8AF_68
        ldD     #0x0000
        bra     LC8AF_68

LC8AF_67:
        ldD     #0x8000
        addD    L0154
        bhs     LC8AF_68
        ldD     #0xFFFF
LC8AF_68:
        stD     *L00BA
        brclr   *L0049_PORTG_debounced #0b00000001 LC8AF_69          ;PG0: CLT clutch switch
        ldaB    L905F                   ;60.
        bra     LC8AF_70

LC8AF_69:
        ldaB    L9060                   ;60.
LC8AF_70:
        staB    L0156
        ldX     #L0156
        ldD     *L0022
        subD    *L0046
        bhs     LC8AF_71
        comA
        negB
        sbcA    #0xFF
        jsr     LF3E4                   ; D8.8 = D8.8 * 0[X]0.8
        comA
        negB
        sbcA    #0xFF
        bra     LC8AF_72

LC8AF_71:
        jsr     LF3E4                   ;D8.8 = D8.8 * 0[X]0.8
LC8AF_72:
        addD    *L0046
        stD     *L0046
        ldaA    L9065
        subA    #0x01
        cmpA    L010F
        blo     LC8AF_73
        ldaA    L0110
        addA    #0x01
        sbcA    #0x00
        cmpA    L9063
        bhs     LC8AF_77
        ldaA    L0110
        addA    #0x01
        sbcA    #0x00
        staA    L0110
        bra     LC8AF_74

LC8AF_73:
        bclr    *L0005 #0b00000010
        ldaA    L9061
        cmpA    *L0044
        blo     LC8AF_77
        ldaA    *L00C7
        cmpA    L9062
        blo     LC8AF_75
        clr     L010F
        clr     L0110
LC8AF_74:
        bset    *L0005 #0b00000010
LC8AF_75:
        brclr   *L0005 #0b00000010 LC8AF_77
        ldaB    *L0046
        subB    L9064
        bhs     LC8AF_76
        ldaB    #0x00
LC8AF_76:
        bra     LC8AF_78

LC8AF_77:
        ldaB    *L0046
LC8AF_78:
        ldaA    *L0044
        sBA
        bhs     LC8AF_79
        tAB
        negB
        ldaA    #0x80
        staA    L0156
        bra     LC8AF_80

LC8AF_79:
        tAB
        ldaA    #0x00
        staA    L0156
LC8AF_80:
        cmpB    #0x80
        blo     LC8AF_81
        ldaB    #0x7F
LC8AF_81:
        staB    L0157
        ldD     L0156
        bitA    #0b10000000
        beq     LC8AF_82
        ldaB    #0x80
        subB    L0157
        bra     LC8AF_83

LC8AF_82:
        addB    #0x80
LC8AF_83:
        staB    *L00B4
        ldaA    *L0044
        cmpA    L9066
        bhs     LC8AF_84
        ldaB    L010C
        cmpB    #0x64
        bhs     LC8AF_85
LC8AF_84:
        clrA
        staA    L0157
        ldaA    #0x80
        staA    *L00B4
LC8AF_85:
        ldaA    L9067
        cmpA    L0157
        blo     LC8AF_86
        ldaA    #0x80
        staA    *L00B5
        ldaA    #0x00
        staA    L0157
        bra     LC8AF_87

LC8AF_86:
        ldaB    *L00B4
        staB    *L00B5
LC8AF_87:
        ldD     *L004D                  ;8.8 RPM50
        subA    #0x18
        bhs     LC8AF_88
        ldD     #0x0000
LC8AF_88:
        subD    #0xD000
        blo     LC8AF_89
        ldD     #0x0000
LC8AF_89:
        addD    #0xD000
        cmpA    #0x30
        bhs     LC8AF_90
        lsrD
        lsrD
        lsrD
        bra     LC8AF_91

LC8AF_90:
        lsrD
        lsrD
        lsrD
        lsrD
        addD    #0x0300
LC8AF_91:
        stD     L0179
        ldX     #0x9B90
        jsr     LF3D0
        staA    *L00BD
        ldaB    L0157
        mul
        stD     L0157
        ldaA    L0156
        bitA    #0b10000000
        beq     LC8AF_92
        ldD     #0x8000
        subD    L0157
        bhs     LC8AF_93
        ldD     #0x0000
        bra     LC8AF_93

LC8AF_92:
        ldD     #0x8000
        addD    L0157
        bhs     LC8AF_93
        ldD     #0xFFFF
LC8AF_93:
        stD     *L00B6
        ldaA    L0156
        bitA    #0b10000000
        bne     LC8AF_95
        ldaA    L0153
        bitA    #0b10000000
        bne     LC8AF_94
        ldD     L0154
        addD    L0157
        cmpA    #0x80
        blo     LC8AF_97
        ldD     #0x7FFF
        bra     LC8AF_97

LC8AF_94:
        ldD     L0157
        subD    L0154
        bhs     LC8AF_97
        comA
        negB
        sbcA    #0xFF
        bra     LC8AF_98

LC8AF_95:
        ldaA    L0153
        bitA    #0b10000000
        beq     LC8AF_96
        ldD     L0154
        addD    L0157
        cmpA    #0x80
        blo     LC8AF_98
        ldD     #0x8000
        bra     LC8AF_98

LC8AF_96:
        ldD     L0154
        subD    L0157
        bhs     LC8AF_97
        comA
        negB
        sbcA    #0xFF
        bra     LC8AF_98

LC8AF_97:
        stD     L015A
        ldaA    #0x00
        staA    L0159
        bra     LC8AF_100

LC8AF_98:
        stD     L015A
        ldaA    #0x80
        staA    L0159
        ldD     L015A
        ldX     #0x00C0
        jsr     LF3E4                   ;D8.8 = D8.8 * 0[X]0.8
        cmpA    #0x08
        blo     LC8AF_99
        ldD     #0x0800
LC8AF_99:
        stD     L015C
        ldD     #0x8000
        subD    L015C
        bra     LC8AF_101

LC8AF_100:    ldD     L015A
        ldX     #0x00BF
        jsr     LF3E4                   ;D8.8 = D8.8 * 0[X]0.8
        addD    #0x8000
        bhs     LC8AF_101
        ldD     #0xFFFF
LC8AF_101:    stD     *L0094
        ldaA    L0159
        bitA    #0b10000000
        beq     LC8AF_102
        ldD     #0x8000
        subD    L015A
        bra     LC8AF_103

LC8AF_102:    ldD     L015A
        addD    #0x8000
LC8AF_103:    staA    *L00BC
        jmp     LC8AF_158

;------------------------------------------------------------
;Derestricted
LC8AF_104:    ldaB    *L00AF
        cmpB    L8089
        bhs     LC8AF_105
        ldD     *L0022                  ; the raw, left-aligned VTA
        stD     *L0044
LC8AF_105:    ldaB    L010C
        cmpB    #0x64
        bhs     LC8AF_106
        ldaA    *L0021
        clrB
        stD     *L0036
        bra     LC8AF_109

LC8AF_106:    ldX     #L909D
        ldaA    *L0021
        clrB
        subD    *L0036
        bhs     LC8AF_107
        comA
        negB
        sbcA    #0xFF
        jsr     LF3E4                   ;D8.8 = D8.8 * 0[X]0.8
        comA
        negB
        sbcA    #0xFF
        bra     LC8AF_108

LC8AF_107:    jsr     LF3E4             ;D8.8 = D8.8 * 0[X]0.8
LC8AF_108:    addD    *L0036
        stD     *L0036
LC8AF_109:    ldaB    *L0021
        subB    *L0036
        bhs     LC8AF_110
        negB
        ldaA    #0x80
        staA    L0153
        bra     LC8AF_111

LC8AF_110:    ldaA    #0x00
        staA    L0153
LC8AF_111:    cmpB    #0x80
        blo     LC8AF_112
        ldaB    #0x7F
LC8AF_112:    staB    L0154
        ldD     L0153
        bitA    #0b10000000
        beq     LC8AF_113
        ldaB    #0x80
        subB    L0154
        bra     LC8AF_114

LC8AF_113:    addB    #0x80
LC8AF_114:    staB    *L00B8
        ldaA    L909E
        cmpA    L0154
        blo     LC8AF_115
        ldaA    #0x80
        staA    *L00B9
        ldaA    #0x00
        staA    L0154
        bra     LC8AF_116

LC8AF_115:    staB    *L00B9
LC8AF_116:    ldD     *L004D            ;8.8 RPM50
        subA    #0x18
        bhs     LC8AF_117
        ldD     #0x0000
LC8AF_117:    subD    #0xD000
        blo     LC8AF_118
        ldD     #0x0000
LC8AF_118:    addD    #0xD000
        cmpA    #0x30
        bhs     LC8AF_119
        lsrD
        lsrD
        lsrD
        bra     LC8AF_120

LC8AF_119:    lsrD
        lsrD
        lsrD
        lsrD
        addD    #0x0300
LC8AF_120:    stD     L0179
        ldX     #0x9BC3
        jsr     LF3D0
        staA    *L00BE
        ldaB    L0154
        mul
        stD     L0154
        ldaA    L0153
        bitA    #0b10000000
        beq     LC8AF_121
        ldD     #0x8000
        subD    L0154
        bhs     LC8AF_122
        ldD     #0x0000
        bra     LC8AF_122

LC8AF_121:    ldD     #0x8000
        addD    L0154
        bhs     LC8AF_122
        ldD     #0xFFFF
LC8AF_122:    stD     *L00BA
        brclr   *L0049_PORTG_debounced #0b00000001 LC8AF_123          ;PG0: CLT clutch switch
        ldaB    L9094
        bra     LC8AF_124

LC8AF_123:    ldaB    L9095
LC8AF_124:    staB    L0156
        ldX     #0x0156
        ldD     *L0022
        subD    *L0046
        bhs     LC8AF_125
        comA
        negB
        sbcA    #0xFF
        jsr     LF3E4                   ;D8.8 = D8.8 * 0[X]0.8
        comA
        negB
        sbcA    #0xFF
        bra     LC8AF_126

LC8AF_125:    jsr     LF3E4             ;D8.8 = D8.8 * 0[X]0.8
LC8AF_126:    addD    *L0046
        stD     *L0046
        ldaA    L909A
        subA    #0x01
        cmpA    L010F
        blo     LC8AF_127
        ldaA    L0110
        addA    #0x01
        sbcA    #0x00
        cmpA    L9098
        bhs     LC8AF_131
        ldaA    L0110
        addA    #0x01
        sbcA    #0x00
        staA    L0110
        bra     LC8AF_128

LC8AF_127:    bclr    *L0005 #0b00000010
        ldaA    L9096
        cmpA    *L0044
        blo     LC8AF_131
        ldaA    *L00C7
        cmpA    L9097
        blo     LC8AF_129
        clr     L010F
        clr     L0110
LC8AF_128:    bset    *L0005 #0b00000010
LC8AF_129:    brclr   *L0005 #0b00000010 LC8AF_131
        ldaB    *L0046
        subB    L9099
        bhs     LC8AF_130
        ldaB    #0x00
LC8AF_130:    bra     LC8AF_132

LC8AF_131:    ldaB    *L0046
LC8AF_132:    ldaA    *L0044
        sBA
        bhs     LC8AF_133
        tAB
        negB
        ldaA    #0x80
        staA    L0156
        bra     LC8AF_134

LC8AF_133:    tAB
        ldaA    #0x00
        staA    L0156
LC8AF_134:    cmpB    #0x80
        blo     LC8AF_135
        ldaB    #0x7F
LC8AF_135:    staB    L0157
        ldD     L0156
        bitA    #0b10000000
        beq     LC8AF_136
        ldaB    #0x80
        subB    L0157
        bra     LC8AF_137

LC8AF_136:    addB    #0x80
LC8AF_137:    staB    *L00B4
        ldaA    *L0044
        cmpA    L909B
        bhs     LC8AF_138
        ldaB    L010C
        cmpB    #0x64
        bhs     LC8AF_139
LC8AF_138:    clrA
        staA    L0157
        ldaA    #0x80
        staA    *L00B4
LC8AF_139:    ldaA    L909C
        cmpA    L0157
        blo     LC8AF_140
        ldaA    #0x80
        staA    *L00B5
        ldaA    #0x00
        staA    L0157
        bra     LC8AF_141

LC8AF_140:    ldaB    *L00B4
        staB    *L00B5
LC8AF_141:    ldD     *L004D            ;8.8 RPM50
        subA    #0x18
        bhs     LC8AF_142
        ldD     #0x0000
LC8AF_142:    subD    #0xD000
        blo     LC8AF_143
        ldD     #0x0000
LC8AF_143:    addD    #0xD000
        cmpA    #0x30
        bhs     LC8AF_144
        lsrD
        lsrD
        lsrD
        bra     LC8AF_145

LC8AF_144:    lsrD
        lsrD
        lsrD
        lsrD
        addD    #0x0300
LC8AF_145:    stD     L0179
        ldX     #0x9BB2
        jsr     LF3D0
        staA    *L00BD
        ldaB    L0157
        mul
        stD     L0157
        ldaA    L0156
        bitA    #0b10000000
        beq     LC8AF_146
        ldD     #0x8000
        subD    L0157
        bhs     LC8AF_147
        ldD     #0x0000
        bra     LC8AF_147

LC8AF_146:    ldD     #0x8000
        addD    L0157
        bhs     LC8AF_147
        ldD     #0xFFFF
LC8AF_147:    stD     *L00B6
        ldaA    L0156
        bitA    #0b10000000
        bne     LC8AF_149
        ldaA    L0153
        bitA    #0b10000000
        bne     LC8AF_148
        ldD     L0154
        addD    L0157
        cmpA    #0x80
        blo     LC8AF_151
        ldD     #0x7FFF
        bra     LC8AF_151

LC8AF_148:    ldD     L0157
        subD    L0154
        bhs     LC8AF_151
        comA
        negB
        sbcA    #0xFF
        bra     LC8AF_152

LC8AF_149:    ldaA    L0153
        bitA    #0b10000000
        beq     LC8AF_150
        ldD     L0154
        addD    L0157
        cmpA    #0x80
        blo     LC8AF_152
        ldD     #0x8000
        bra     LC8AF_152

LC8AF_150:    ldD     L0154
        subD    L0157
        bhs     LC8AF_151
        comA
        negB
        sbcA    #0xFF
        bra     LC8AF_152

LC8AF_151:    stD     L015A
        ldaA    #0x00
        staA    L0159
        bra     LC8AF_154

LC8AF_152:    stD     L015A
        ldaA    #0x80
        staA    L0159
        ldD     L015A
        ldX     #0x00C0
        jsr     LF3E4                   ;D8.8 = D8.8 * 0[X]0.8
        cmpA    #0x08
        blo     LC8AF_153
        ldD     #0x0800
LC8AF_153:    stD     L015C
        ldD     #0x8000
        subD    L015C
        bra     LC8AF_155

LC8AF_154:    ldD     L015A
        ldX     #0x00BF
        jsr     LF3E4                   ;D8.8 = D8.8 * 0[X]0.8
        addD    #0x8000
        bhs     LC8AF_155
        ldD     #0xFFFF
LC8AF_155:    stD     *L0094
        ldaA    L0159
        bitA    #0b10000000
        beq     LC8AF_156
        ldD     #0x8000
        subD    L015A
        bra     LC8AF_157

LC8AF_156:    ldD     L015A
        addD    #0x8000
LC8AF_157:    staA    *L00BC

;------------------------------------------------------------
;Here is where the restricted/unrestricted code meets up again.

LC8AF_158:    brclr   *L000C #0b00111001 LC8AF_159
        bra     LC8AF_162

LC8AF_159:    ldaA    *L0044            ;left-aligned VTA, ?with some filtering?
        cmpA    L9190
        blo     LC8AF_162
        ldaA    *L004D                  ;8.8 RPM50
        cmpA    L9191                   ;const 0x78,  == 6000 RPM50
        blo     LC8AF_162               ;br if RPM < 6000

        ldX     #0x9193
        jsr     LF3C2
        tAB
        ldaA    L9192
        subA    *L0040                  ;used in ambient air pressure calc's
        bhs     LC8AF_160
        addA    *L0021
        blo     LC8AF_161
        ldaA    #0x00
LC8AF_160:    addA    *L0021
        bhs     LC8AF_161
        ldaA    #0xFF
LC8AF_161:    staA    *L0097
        cmpB    *L0097
        bhi     LC8AF_162
        sBA
        staA    *L0098
        ldX     #0x919E
        jsr     LF3C2
        bra     LC8AF_163

LC8AF_162:    ldaA    #0x80
LC8AF_163:    staA    *L0096
        brset   *L0001 #0b01000000 LC8AF_164
        jmp     LC8AF_169

LC8AF_164:    ldaA    *L0030            ;L0030 is the converted, interpolated THW water temp
        ldX     #L803A
        jsr     LF3C2
        stD     *L0065

        ldaA    *L0040
        ldX     #L8057
        jsr     LF3C2
        staA    *L009B

        ldaA    *L0022
        ldX     #L804E
        jsr     LF3C2
        staA    *L009A

        ldaA    *L0054                  ;has to do with RPM and 5 msec events in half rotations?
        ldX     #L8045
        jsr     LF3C2                   ;table interpolation
        staA    *L0099                  ;dbg: never used??

;Even so, the return value is used here:
        tAB
        clrA
        xgDX
        clrA
        ldaB    *L008D
        addD    #0x0180
        jsr     LF3F6                   ; D8.8 = D8.8 * X8.8
        xgDX
        clrA
        ldaB    *L009B
        jsr     LF3F6
        xgDX
        clrA
        ldaB    *L009A
        jsr     LF3F6
        ldX     *L0065
        jsr     LF3F6
        lslD
        lslD
        bhs     LC8AF_165
        ldD     #0xFFFF
LC8AF_165:    stD     *L0063
        ldaA    *L0030                  ;the converted, interpolated 8-bit THW (water temp) reading
        ldX     #L903C
        jsr     LF3C2
        stD     *L00B2
        ldD     *L0022
        stD     *L0044
        stD     *L0046
        ldD     *L0079
        addD    *L0063
        bhs     LC8AF_166
        ldD     #0xFFFF
LC8AF_166:    stD     *L005D
        stD     *L005F
        stD     *L0061
        brset   *L0015 #0b00000010 LC8AF_167
        bset    *L0001 #0b10000000
        bclr    *L0015 #0b00000001
        bra     LC8AF_168

LC8AF_167:    bset    *L0001 #0b00010000
        bclr    *L0015 #0b00000010
LC8AF_168:    jmp     LC2B6

LC8AF_169:    ldaA    *L0049_PORTG_debounced
        andA    #0b00101000
        cmpA    #derestricted
        beq     LC8AF_170
        bra     LC8AF_173

;Derestricted:
LC8AF_170:    brset   *L0015 #0b00000010 LC8AF_172

; Cyl 1 processing:
LC8AF_171:    ldX     #L8856-3          ; 0x8853 (Fuel Map: 16x29 derestricted MAP/N cyl 1)
        ldaA    *L0034
        jsr     LF5F9
        staA    *L00A0
        ldX     #0x8BF6-3               ; 0x8BF3 (Fuel Map: 18x29 derestricted ALPHA/N cyl 1)
        jsr     LF57B
        staA    *L00A2
        bra     LC8AF_176

; Cyl 2 processing:
LC8AF_172:    ldX     #0x8A26-3         ; 0x8A23 (Fuel Map: 16x29 derestricted MAP/N cyl 2)
        ldaA    *L0035
        jsr     LF5F9
        staA    *L00A4
        ldX     #0x8E00-3               ; 0x8DFD (Fuel Map: 18x29 derestricted ALPHA/N cyl 2)
        jsr     LF57B
        staA    *L00A6
        brclr   *L0001 #0b10000000 LC8AF_171
        bra     LC8AF_176

;Restricted:
LC8AF_173:    brset   *L0015 #0b00000010 LC8AF_175
LC8AF_174:    ldX     #0x80A2-3         ; 0x809F (Fuel map: 16x29 restricted MAP/N cyl 1)
        ldaA    *L0034
        jsr     LF5F9
        staA    *L00A0
        ldX     #0x8442-3               ; 0x843F (Fuel Map: 18x29 restricted ALPHA/N cyl 1)
        jsr     LF57B
        staA    *L00A2
        bra     LC8AF_176

LC8AF_175:    ldX     #0x8272-3         ; 0x826F (Fuel Map: 16x29 restricted MAP/N cyl 2)
        ldaA    *L0035
        jsr     LF5F9
        staA    *L00A4
        ldX     #0x864C-3               ; 0x8649 (Fuel Map: 18x29 restricted ALPHA/N cyl 2)
        jsr     LF57B
        staA    *L00A6
        brclr   *L0001 #0b10000000 LC8AF_174

LC8AF_176:    jsr     LC391
        ldaA    *L0049_PORTG_debounced
        andA    #0b00101000
        cmpA    #derestricted
        bne     LC8AF_177
        bra     LC8AF_178

;Restricted:
LC8AF_177:    ldX     #0x9B50
        ldD     *L00A9
        stD     L0153
        ldD     *L00AD
        stD     L0155
        ldaB    #0x08
        ldaA    #0x04
        jsr     LF436
        staA    *L00A7
        ldX     #0x806B                 ;0x2
        ldD     *L00A9
        stD     L0153
        ldD     *L00AB
        stD     L0155
        ldaB    #0x08
        ldaA    #0x03
        jsr     LF436
        bra     LC8AF_179

;Derestricted:
LC8AF_178:    ldX     #0x9B70
        ldD     *L00A9
        stD     L0153
        ldD     *L00AD
        stD     L0155
        ldaB    #0x08
        ldaA    #0x04
        jsr     LF436
        staA    *L00A7
        ldX     #0x8087
        ldD     *L00A9
        stD     L0153
        ldD     *L00AB
        stD     L0155
        ldaB    #0x08
        ldaA    #0x03
        jsr     LF436
LC8AF_179:    staA    *L00A8
        addA    *L00A2
        bhs     LC8AF_180
        ldaA    #0xFF
LC8AF_180:    staA    *L00A1

        ldaA    *L00A8
        addA    *L00A6
        bhs     LC8AF_181
        ldaA    #0xFF
LC8AF_181:    staA    *L00A5

        ldaA    *L00A7
        addA    *L00A0
        bhs     LC8AF_182
        ldaA    #0xFF
LC8AF_182:    staA    *L009F

        ldaA    *L00A7
        addA    *L00A4
        bhs     LC8AF_183
        ldaA    #0xFF
LC8AF_183:    staA    *L00A3

        ldD     *L00B0                  ;L00B0 is calc'd as part of THW water temp init
        lsrD                            ;divided by 2
        subB    L9047                   ;0x7D
        sbcA    #0x00
        bhs     LC8AF_184
        ldD     #0x0000                 ;underflow is clamped to 0
LC8AF_184:    lslD                      ;divided by 2 again
        stD     *L00B0                  ;becomes the new value

        ldD     *L00B2
        lsrD
        subB    L9048                   ;0x54
        sbcA    #0x00
        bhs     LC8AF_185               ;if no underflow

        ldD     #0x0000                 ;else underflow clips to 0

LC8AF_185:    lslD
        stD     *L00B2

        subD    *L00B0                  ;D = min?(L00B0, L00B2)
        bhs     LC8AF_186
        ldD     *L00B0
        bra     LC8AF_187

LC8AF_186:    ldD     *L00B2

LC8AF_187:    stD     *L0092            ;L0092 = min?(L00B0, L00B2)

        ldaA    *L004D                  ;8.8 RPM50
        ldX     #0x9167
        jsr     LF3C2
        staA    *L00C4
        brset   *L0015 #0b00000010 LC8AF_188
        ldaB    *L00C5
        jsr     LD5E4
        staA    *L006F
        bra     LC8AF_189

LC8AF_188:    ldaB    *L00C6
        jsr     LD5E4
        staA    *L0078
LC8AF_189:    ldaA    L8066
        cmpA    #0x01
        bne     LC8AF_194
        ldD     #0x0100
        stD     *L007B
        stD     *L007F
        stD     *L007D
        stD     *L0081
        clrA
        ldaB    L8067
        lslD
        lslD
        lslD
        lslD
        lslD
        cmpD    L8064
        blo     LC8AF_190
        ldD     L8064
        bra     LC8AF_191

LC8AF_190:    cmpD    L8062
        bhs     LC8AF_191
        ldD     L8062
LC8AF_191:    stD     *L0069
        addD    *L0079
        stD     *L0067
        stD     *L005D
        clrA
        ldaB    L8068
        lslD
        lslD
        lslD
        lslD
        lslD
        cmpD    L8064
        blo     LC8AF_192
        ldD     L8064
        bra     LC8AF_193

LC8AF_192:    cmpD    L8062
        bhs     LC8AF_193
        ldD     L8062
LC8AF_193:    stD     *L0072
        addD    *L0079
        stD     *L0070
        stD     *L005F
        bclr    *L0015 #0b00000001
        bclr    *L0015 #0b00000010
        bset    *L0001 #0b10000000
        bset    *L0001 #0b00010000
        jmp     LC8AF_231

LC8AF_194:    ldX     #LF424            ;start of some table

        ldaA    *L0049_PORTG_debounced
        andA    #0b00101000
        cmpA    #derestricted
        bne     LC8AF_195
        bra     LC8AF_204

;Restricted
LC8AF_195:    ldaB    L806B             ;0x02
        cmpB    #0x09                   ;valid indexes are [0..8]
        blo     LC8AF_196               ; br if 0x02 u< 0x09 (i.e always branch)
        ldaB    #0x08                   ;table is only 9 entries long
LC8AF_196:    lslB
        aBX
        ldaA    *L0044
        brclr   *L000C #0b00110000 LC8AF_197
        bra     LC8AF_202

LC8AF_197:    brclr   *L000C #0b00001000 LC8AF_198
        ldaB    #0x00
        bra     LC8AF_203

LC8AF_198:    subA    L806A             ;0x2a
        bhs     LC8AF_202
        ldaA    *L0044
        addA    0, X
        bhs     LC8AF_199
        staA    L0154
        ldaA    #0x01
        staA    L0153
        ldaA    #0x00
        staA    L0155
        ldaB    L806A                   ;0x2a
        staB    L0156
        ldD     L0153
        subD    L0155
        ldaA    1, X
        mul
        cmpA    #0x00
        beq     LC8AF_201
        ldaB    #0xFF
        bra     LC8AF_201

LC8AF_199:
        subA    L806A                   ;0x2a
        blo     LC8AF_200
        ldaB    1, X
        mul
        cmpA    #0x00
        beq     LC8AF_201
        ldaB    #0xFF
        bra     LC8AF_201

LC8AF_200:
        ldaB    #0x00
LC8AF_201:
        ldaA    *L00AF
        cmpA    L806D
        bhs     LC8AF_203
LC8AF_202:
        ldaB    #0xFF
        ldaA    *L00AF
        addA    #0x01
        sbcA    #0x00
        staA    *L00AF
LC8AF_203:
        staB    *L009C
        bra     LC8AF_213

;Derestricted
LC8AF_204:
        ldaB    L8087                   ;0x02
        cmpB    #0x09                   ;valid table indexes are [0..8]
        blo     LC8AF_205
        ldaB    #0x08                   ;too big maxs out at 8
LC8AF_205:
        lslB
        aBX
        ldaA    *L0044
        brclr   *L000C #0b00110000 LC8AF_206
        bra     LC8AF_211

LC8AF_206:    brclr   *L000C #0b00001000 LC8AF_207
        ldaB    #0x00
        bra     LC8AF_212

LC8AF_207:    subA    L8086             ;0x2a
        bhs     LC8AF_211
        ldaA    *L0044
        addA    0, X
        bhs     LC8AF_208
        staA    L0154
        ldaA    #0x01
        staA    L0153
        ldaA    #0x00
        staA    L0155
        ldaB    L8086                   ;0x2a
        staB    L0156
        ldD     L0153
        subD    L0155
        ldaA    1, X
        mul
        cmpA    #0x00
        beq     LC8AF_210
        ldaB    #0xFF
        bra     LC8AF_210

LC8AF_208:    subA    L8086             ;0x2a
        blo     LC8AF_209
        ldaB    1, X
        mul
        cmpA    #0x00
        beq     LC8AF_210
        ldaB    #0xFF
        bra     LC8AF_210

LC8AF_209:    ldaB    #0x00
LC8AF_210:    ldaA    *L00AF
        cmpA    L8089
        bhs     LC8AF_212
LC8AF_211:    ldaB    #0xFF
        ldaA    *L00AF
        addA    #0x01
        sbcA    #0x00
        staA    *L00AF
LC8AF_212:    staB    *L009C
LC8AF_213:    ldaA    L8066
        cmpA    #0x02
        bne     LC8AF_214
        clrA
        ldaB    L8069
        lslD
        stD     *L007B
        stD     *L007F
        stD     *L007D
        stD     *L0081
        jmp     LC8AF_216

LC8AF_214:    ldD     #0x0020
        addB    *L0092
        adcA    #0x00
        lslD
        lslD
        addB    *L0090
        adcA    #0x00
        lslD
        lslD
        lslD
        lslD
        subA    #0x80
        addD    *L0094
        lsrD
        lsrD
        stD     *L0087
        ldD     #0x0020
        addB    *L0092
        adcA    #0x00
        lslD
        lslD
        addB    *L0091
        adcA    #0x00
        lslD
        lslD
        lslD
        lslD
        subA    #0x80
        addD    *L0094
        lsrD
        lsrD
        stD     *L0089
        xgDX
        clrA
        ldaB    *L008D
        addD    #0x0180
        jsr     LF3F6
        xgDX
        clrA
        ldaB    *L0096
        jsr     LF3F6
        xgDX
        clrA
        ldaB    *L008E
        jsr     LF3F6
        stD     *L0085
        xgDX
        clrA
        ldaB    *L008B
        jsr     LF3F6
        lslD
        stD     *L007F
        ldX     *L0087
        clrA
        ldaB    *L008D
        addD    #0x0180
        jsr     LF3F6
        xgDX
        clrA
        ldaB    *L0096
        jsr     LF3F6
        xgDX
        clrA
        ldaB    *L008E
        jsr     LF3F6
        stD     *L0083
        xgDX
        clrA
        ldaB    *L008B
        jsr     LF3F6
        lslD
        stD     *L007B
        ldX     *L0083
        clrA
        ldaB    *L008C
        jsr     LF3F6
        lslD
        stD     *L007D
        ldX     *L0085
        clrA
        ldaB    *L008C
        jsr     LF3F6
        lslD
        stD     *L0081
        ldaB    *L008E
        cmpB    #0x80
        beq     LC8AF_216
        addB    *L008F
        cmpB    #0x80
        blo     LC8AF_215
        ldaB    #0x80
LC8AF_215:    staB    *L008E
LC8AF_216:    brclr   *L0015 #0b00000010 LC8AF_217
        jmp     LC8AF_224

LC8AF_217:    ldaA    *L009F
        clrB
        lsrD
        lsrD
        lsrD
        stD     L0157
        ldD     #0x0000
        ldaB    *L006F
        cmpB    #0x80
        blo     LC8AF_218
        subB    #0x80
        ldaA    #0x00
        lslD
        lslD
        addD    L0157
        bhs     LC8AF_219
        ldD     #0xFFFF
        bra     LC8AF_219

LC8AF_218:    ldaA    #0x80
        sBA
        tAB
        ldaA    #0x00
        lslD
        lslD
        stD     L0159
        ldD     L0157
        subD    L0159
        bhs     LC8AF_219
        ldD     #0x0000

LC8AF_219:    ldX     *L007B
        jsr     LF3F6
        stD     *L006B
        ldaA    *L00A1
        clrB
        lsrD
        lsrD
        lsrD
        stD     L0157
        ldD     #0x0000
        ldaB    *L006F
        cmpB    #0x80
        blo     LC8AF_220
        subB    #0x80
        ldaA    #0x00
        lslD
        lslD
        addD    L0157
        bhs     LC8AF_221
        ldD     #0xFFFF
        bra     LC8AF_221

LC8AF_220:    ldaA    #0x80
        sBA
        tAB
        ldaA    #0x00
        lslD
        lslD
        stD     L0159
        ldD     L0157
        subD    L0159
        bhs     LC8AF_221
        ldD     #0x0000
LC8AF_221:    ldX     *L007D
        jsr     LF3F6
        stD     *L006D
        ldX     #0x009C
        jsr     LF3E4                   ;D8.8 = D8.8 * 0[X]0.8
        stD     L0154
        ldaB    *L009C
        comB
        staB    L0153
        ldD     *L006B
        ldX     #0x0153
        jsr     LF3E4                   ;D8.8 = D8.8 * 0[X]0.8
        addD    L0154
        cmpD    L8064
        blo     LC8AF_222
        ldD     L8064
LC8AF_222:    cmpD    L8062
        bhs     LC8AF_223
        ldD     L8062
LC8AF_223:    stD     *L0069
        addD    *L0079
        stD     *L0067
        stD     *L005D
        bclr    *L0015 #0b00000001
        bset    *L0001 #0b10000000
        jmp     LC8AF_231

LC8AF_224:    ldaA    *L00A3
        clrB
        lsrD
        lsrD
        lsrD
        stD     L0157
        ldD     #0x0000
        ldaB    *L0078
        cmpB    #0x80
        blo     LC8AF_225
        subB    #0x80
        ldaA    #0x00
        lslD
        lslD
        addD    L0157
        bhs     LC8AF_226
        ldD     #0xFFFF
        bra     LC8AF_226

LC8AF_225:    ldaA    #0x80
        sBA
        tAB
        ldaA    #0x00
        lslD
        lslD
        stD     L0159
        ldD     L0157
        subD    L0159
        bhs     LC8AF_226
        ldD     #0x0000
LC8AF_226:    ldX     *L007F
        jsr     LF3F6
        stD     *L0074
        ldaA    *L00A5
        clrB
        lsrD
        lsrD
        lsrD
        stD     L0157
        ldD     #0x0000
        ldaB    *L0078
        cmpB    #0x80
        blo     LC8AF_227
        subB    #0x80
        ldaA    #0x00
        lslD
        lslD
        addD    L0157
        bhs     LC8AF_228
        ldD     #0xFFFF
        bra     LC8AF_228

LC8AF_227:    ldaA    #0x80
        sBA
        tAB
        ldaA    #0x00
        lslD
        lslD
        stD     L0159
        ldD     L0157
        subD    L0159
        bhs     LC8AF_228
        ldD     #0x0000
LC8AF_228:    ldX     *L0081
        jsr     LF3F6
        stD     *L0076
        ldX     #0x009C
        jsr     LF3E4                   ;D8.8 = D8.8 * 0[X]0.8
        stD     L0154
        ldaB    *L009C
        comB
        staB    L0153
        ldD     *L0074
        ldX     #0x0153
        jsr     LF3E4                   ;D8.8 = D8.8 * 0[X]0.8
        addD    L0154
        cmpD    L8064
        blo     LC8AF_229
        ldD     L8064
LC8AF_229:    cmpD    L8062
        bhs     LC8AF_230
        ldD     L8062
LC8AF_230:    stD     *L0072
        addD    *L0079
        stD     *L0070
        stD     *L005F
        bclr    *L0015 #0b00000010
        bset    *L0001 #0b00010000
        brset   *L0001 #0b10000000 LC8AF_231
        jmp     LC8AF_217

LC8AF_231:    jmp     LC2B6

;-----------------------------------------------------------------
; Trimpot processing.
;
; Inputs:
;    Reg B: the trimpot ADC value.
;           A trimpot value of 0x80 means that the trimpot will not affect the output.
;           Values less than 0x80 mean that the trimpot should attenuate the output.
;           Values greater than 0x80 mean that the trimpot should amplify the output.
;
;    L00C4: a sensitivity 'gain' ScaleFactor
;           This variable acts as a "multiplier" for the trimpots.
;           If this value is very low (e.g., 0x10), the trimpots will have a very subtle effect on the fuel.
;           If it is high, even a small turn of the pot will cause a large change in the output.
;
; Output:
;    Reg A = round( (TrimpotADC * ScaleFactor) / 128 )
;           Where positive values saturate at 127 and negative values saturate at -128
;
; By basing the scale factor on RPM, it can be arranged so that changes in RPM can have more
; or less effect on Trimpot settings.

LD5E4:  subB    #0x80
        bhs     0f                      ; Br if B was >= 0x80 to begin with

;B is negative now
        addB    #0x80                   ;put it back to its starting value, somewhere btw. 0x0..0x7F
        ldaA    #0x80
        sBA                             ;A=A-B
        tAB
        ldaA    *L00C4
        mul
        addD    #0x0080                 ;round the result into A
        tAB
        ldaA    #0x80
        sBA
        bhs     1f
        ldaA    #0x00
        bra     1f

0:      ldaA    *L00C4
        mul
        addD    #0x0080
        addA    #0x80
        bhs     1f
        ldaA    #0xFF
1:      rts

;*****************************************************************
; Called from main loop (every time?)

; We only get here if we have seen both CR3 and CR8

LD60B:  bset    *L0012 #0b00100000      ;dbg: mark that we entered this subr

        brset   *L0000 #0b01000000 LD60B_0      ;set to '1' at reset cleared by ??
        jmp     LD60B_1

; This is the post-reset or error case:
LD60B_0:
        ldaB    #0x55                   ;represents 0.0x55 or .332 * 90 degrees, or 30 degrees of rotation at current RPM
        staB    *L00DC                  ;store this as the default for both cyls
        staB    *L00DF

        ldaB    #0x80                   ;I think this is treated as a fraction (0.5)
        staB    *L00E2

        bclr    *L0002 #0b00000010      ;clears the 'we have seen start of CR3' flag
        bclr    *L0002 #0b00000001      ;clears the 'we have seen start of CR8' flag
        jmp     LD60B_38

; The 'normal' case.
LD60B_1:
        ldaA    *L0049_PORTG_debounced
        andA    #0b00101000
        cmpA    #derestricted
        beq     LD60B_2
        bra     LD60B_5

;Derestricted
LD60B_2:
        brset   *L0002 #0b00000001 LD60B_4      ;'1' when we have seen the start of CR8

; Derestricted cylinder 1 (Front) calculations:
LD60B_3:
        ldX     #L95F9-3                ;L95F9: Ignition map 18x29 Derestricted ALPHA/N cyl 1
        jsr     LF57B
        staA    *L00DE                  ;store result of Alpha/N cyl 1 Ignition map processing
        bra     LD60B_8

; Derestricted cylinder 2 (Rear) calculations:
LD60B_4:
        ldX     #L9803-3                ;L9803: Ignition map 18x29 Derestricted ALPHA/N cyl 2
        jsr     LF57B
        staA    *L00E1                  ;store result of Alpha/N cyl 2 Ignition map processing
        brset   *L0002 #0b00000010 LD60B_3      ;will be '1' when we see start of CR3
        bra     LD60B_8

;Restricted
LD60B_5:
        brset   *L0002 #0b00000001 LD60B_7      ;'1' when we have seen the start of CR8

; Restricted cylinder 1 (Front) calculations:
LD60B_6:
        ldX     #L91E5-3                ;L91E5: Ignition map 18x29 Restricted ALPHA/N cyl 1
        jsr     LF57B
        staA    *L00DE                  ;store result of Alpha/N cyl 1 Ignition map processing
        bra     LD60B_8

; Restricted cylinder 2 (Rear) calculations:
LD60B_7:
        ldX     #L93EF-3                ;L93EF: Ignition map 18x29 Restricted ALPHA/N cyl 2
        jsr     LF57B
        staA    *L00E1                  ;store result of Alpha/N cyl 2 Ignition map processing
        brset   *L0002 #0b00000010 LD60B_6      ;will be '1' when we see start of CR3

; All calculations (front/rear/restricted/derestricted) end up here:
LD60B_8:
        ldaA    *L004D                  ;8.8 RPM50 (RPM50 means 8.8 RPM in units of 50 RPMs, so 100.0 means 5000 RPM)

        ldaB    *L0049_PORTG_debounced
        andB    #0b00101000
        cmpB    #derestricted
        bne     LD60B_9
        jmp     LD60B_11

;Here is one of the few differences between restricted/derestricted mode.
;Given that the table values that follow are all identical (and all 0), it makes no difference anyway.

;Restricted
LD60B_9:
        cmpA    L91B0                   ;const 0x19 == 1250 RPM50
        blo     LD60B_10                ;br if current RPM < 1250
        ldaA    #0x00                   ;clear A if RPM >= 1250
        bra     LD60B_14

LD60B_10:
        ldX     #0x91B1-3
        jmp     LD60B_13

;Derestricted
LD60B_11:
        cmpA    L91C9                   ;const 0x64 == 5000 RPM50
        blo     LD60B_12                ;br if current RPM < 5000
        ldaA    #0x00                   ;clear A if RPM >= 5000
        bra     LD60B_14

LD60B_12:
        ldX     #0x91CA-3

;Interpolate
LD60B_13:
        ldD     *L00A9
        stD     L0153                   ;an 8.8 value to interpolate btw 2 cols on a particular row
        ldD     *L00AB
        stD     L0155                   ;an 8.8 value to interpolate between 2 rows
        ldaB    #0x08                   ;number of colums per row
        ldaA    #0x03                   ;mystery constant
        jsr     LF436                   ;double interpolate a 2D table

;Use the interpolation table to calculate a slight extra amount of spark delay.
;The tables all interpolate everything into the value 0x00, as do the RPM limit tests above.
LD60B_14:
        staA    *L00E5                  ;our final adjustment is always 0, as things turn out.
        addA    *L00DE                  ;holds result of Alpha/N cyl 1 ign processing (L00DE not used after this)
        bhs     LD60B_15
        ldaA    #0xFF                   ;peg adjusted result to 0xFF
LD60B_15:
        staA    *L00DD

        ldaA    *L00E5                  ;our final adjustment is always 0, as things turn out.
        addA    *L00E1                  ;holds result of Alpha/N cyl 2 ign processing (L00E1 not used after this)
        bhs     LD60B_16
        ldaA    #0xFF                   ;peg adjusted result to 0xFF
LD60B_16:
        staA    *L00E0

; At this point:
;   L00DD is the adjusted Alpha/N cyl 1 ign table result
;   L00E0 is the adjusted Alpha/N cyl 2 ign table result

        ldD     *L004F                  ;current 8.8RPM400
        stD     L0153                   ;use as the 2D column interpolation along a single row
        clrA
        clrB
        stD     L0155                   ;selects row 0 exactly: means that there will be no interpolation between rows.
        incA                            ;A=1: the mystery constant
        ldaB    #0x1D                   ;length of a row
        ldX     #LC039-3
        jsr     LF436

;The result is a value based entirely on RPM over the full RPM400 range: [800..12400].
;In rough terms, at 800RPM:0x00, 1200:0x18, ... , over 6000RPM, values gradually increase from about 0x80 to 0x8C.
        staA    *L00E8                  ;used by the crankshaft CR3 and CR8 interrupt handlers

        brclr   *L0005 #0b00100000 LD60B_20  ;This bit is '0' when the VTA sensor is broken.

;VTA sensor is OK:
        ldaB    L91AE                   ;const 0x55
        subB    #0x55                   ;
        bhs     LD60B_17                ;br if const 0x55 >= 0x55 --> always taken!

        addB    *L00E4                  ;dead code
        blo     LD60B_18
        ldaB    #0x00
        bra     LD60B_19

LD60B_17:
        addB    *L00E4                  ;some value based on water temp
        bhs     LD60B_18                ;if no overflow
        ldaB    #0xFF                   ;else peg to 0xFF
LD60B_18:
        cmpB    L91AF                   ;0xD5
        blo     LD60B_19                ;br if B<0xD5
        ldaB    L91AF                   ;else peg to 0xD5

;B is based on water temp now, and must be in the range 0x55..0xD5 at this point.
LD60B_19:
        staB    *L00DC                  ;water temp adjustment affects both cyls equally
        staB    *L00DF

        bclr    *L0002 #0b00000010      ;mark that we have NOT seen the start of CR3
        bclr    *L0002 #0b00000001      ;mark that we have NOT seen the start of CR8
        jmp     LD60B_38

; IF VTA sensor is broken, we use the middle of the range [0x55..0xD5], or 0x80.
LD60B_20:
        ldaA    L91E2                   ;0x80
        cmpA    #0x01
        bne     LD60B_23                ;will always take this branch

;This is dead code: the table at L91E2 is always 0x80.  Could this
;be a difference if the table contained derestricted data?
        ldaA    L91E3                   ;0x00
        cmpA    L91AF                   ;0xD5
        blo     LD60B_21
        ldaA    L91AF                   ;0xD5
LD60B_21:
        staA    *L00DC
        bclr    *L0002 #0b00000010
        ldaA    L91E4                   ;0x00
        cmpA    L91AF                   ;0xD5
        blo     LD60B_22
        ldaA    L91AF                   ;0xD5
LD60B_22:
        staA    *L00DF
        bclr    *L0002 #0b00000001
        jmp     LD60B_38

; Only executes if VTA is broken
LD60B_23:
        ldaA    *L00E2
        cmpA    #0x80
        bhs     LD60B_26
        brclr   *L0005 #0b00000001 LD60B_24
        ldaB    *L00C7
        cmpB    L9A2A
        blo     LD60B_24
        ldaB    L9A2E
        staB    *L00E3
LD60B_24:
        ldaB    *L00E3
        aBA
        cmpA    #0x80
        blo     LD60B_25
        ldaA    #0x80
LD60B_25:
        staA    *L00E2
LD60B_26:
        brset   *L0002 #0b00000001 LD60B_32
LD60B_27:
        ldaB    *L00DD
        mul
        lsrD
        lsrD
        lsrD
        lsrD
        lsrD
        lsrD
        lsrD
        cmpA    #0x00
        beq     LD60B_28
        ldaB    #0xFF
LD60B_28:
        staB    L0154
        ldaB    L91AE
        subB    #0x55
        bhs     LD60B_29
        addB    L0154
        blo     LD60B_30
        ldaB    #0x00
        bra     LD60B_31

LD60B_29:
        addB    L0154
        bhs     LD60B_30
        ldaB    #0xFF
LD60B_30:
        cmpB    L91AF                   ;0xD5
        blo     LD60B_31
        ldaB    L91AF                   ;0xD5
LD60B_31:
        staB    *L00DC
        bclr    *L0002 #0b00000010
        bra     LD60B_37

LD60B_32:
        ldaA    *L00E2
        ldaB    *L00E0
        mul
        lsrD
        lsrD
        lsrD
        lsrD
        lsrD
        lsrD
        lsrD
        cmpA    #0x00
        beq     LD60B_33
        ldaB    #0xFF
LD60B_33:
        staB    L0154
        ldaB    L91AE
        subB    #0x55
        bhs     LD60B_34
        addB    L0154
        blo     LD60B_35
        ldaB    #0x00
        bra     LD60B_36

LD60B_34:
        addB    L0154
        bhs     LD60B_35
        ldaB    #0xFF
LD60B_35:
        cmpB    L91AF                   ;0xD5
        blo     LD60B_36
        ldaB    L91AF                   ;0xD5
LD60B_36:
        staB    *L00DF
        bclr    *L0002 #0b00000001
        brclr   *L0002 #0b00000010 LD60B_37
        ldaA    *L00E2
        jmp     LD60B_27


LD60B_37:
        brclr   *L0000 #0b00000100 LD60B_38  ;br if we are doing 'normal' ign processing, ie. we know where the crank and cam are etc.

        ldaA    #0x80                   ;otherwise, this is a catchall
        staA    *L00E2

; Normal processing resumes here.
LD60B_38:
        brclr   *L0000 #0b01000000 LD60B_39  ;'0' means we know what is going on

;We are not sure what is happening: pick some sort of default here.
        ldaA    L9A38                   ;const 0x16
        addA    L9A35                   ;const 0x09, therefore A = 0x1F
        ldaB    #0x00                   ;D = 0x1F00
        lsrD                            ;D = 0x0F80
        bra     LD60B_43

;Everything is 'normal'.
LD60B_39:
        ldaA    *L002A                  ;get the raw VM A/D reading
        ldX     #0x9A30
        jsr     LF3C2                   ;adjust it into an 8.8 value
        stD     L0153                   ;I think that L0153 is being used as a temp

        ldX     #0x9A38-3
        ldD     *L004D                  ;8.8 RPM50
        subA    #0x10                   ;bias downwards by 800 RPM
        bhs     LD60B_40                ;br if result >= 0
        ldD     #0x0000                 ;else clip to 0
LD60B_40:
        subD    #0xE000                 ;clip to a biased 12000RPM on the top end
        blo     LD60B_41
        ldD     #0x0000
LD60B_41:
        addD    #0xE000

;D is now in the [biased] range of [800..12000] 8.8RPM50, or [0x00.00..0xE0.00]
        lsrD
        lsrD
        lsrD
        lsrD

;D is now in the range of [0x00.00..0x0E.00].  Each integer unit is 800RPM now.  Table is 15 entries long.
        jsr     LF3D0                   ;interpolate along a 1D table using D as an 8.8 value

;The value we calculated is added to an adjustment made based on Battery Voltage.
        addD    L0153
        lsrD                            ;divide by 2

        brset   *L0000 #0b01000000 LD60B_42  ;br if we are not sure about things
        brset   *L0000 #0b00000100 LD60B_42  ;br if normal interrupt-driven ign processing is disabled

        cmpD    *L00DA                  ;The full crank period for the current RPM
        blo     LD60B_43                ;br if the adjusted value is < a full crank period

        ldD     *L00DA                  ;else set the adjusted value to the full crank period
        bra     LD60B_43

;If we are not sure about things, we use
LD60B_42:
        stD     L0153                   ;save our current guess
        ldD     *L00D0                  ;get the most recent 1/6 crank period (60 degrees)
        lslD                            ;calc time for most recent 1/3 crank period (120 degrees)
        cmpD    L0153
        blo     LD60B_43                ;br if 120 degree period < our guess, then use the 120 degree period
        ldD     L0153                   ;else use our guess

;L00ED is a 16-bit period, perhaps a dwell calculation.
LD60B_43:
        stD     *L00ED

        bclr    *L0012 #0b00100000      ;clear our debug flag now that we are leaving this routine.
        jmp     LC2C4                   ;rts

;*****************************************************************
;Called like a subroutine from the main loop.
;This routine runs every 5 milliseconds.

LD82E:  bclr    *L0002 #0b01000000
        bset    *L0012 #0b00001000

        sei
        ldaB    PORTJ
        eorB    #0b00001000             ;toggle PJ3: the "i'm alive" bit at ~200 Hz
        staB    PORTJ
        cli

;This section of code "debounces" the reading on PORTG.  A change
;in any of the bits on PORTG must persist for two times through
;this code in order to be recognized.  The debounced PORTG is
;stored in L0049_PORTG_debounced.  Loc L004A_PORTG_prev holds the
;previous raw PORTG reading.

        ldaA    PORTG                   ;read current PORTG inputs
        pshA                            ;save them for later
        eora    *L004A_PORTG_prev       ;calculate what has changed from our debounced PORTG
        tAB
        andA    *L0049_PORTG_debounced
        comB
        andB    *L004A_PORTG_prev
        aBA
        pulB                            ;prev = current
        stD     *L0049_PORTG_debounced  ;write the debounced version to L0049_PORTG_debounced
                                        ;and the raw version to L004A_PORTG_prev

;We only log portG data if it is different from what we last sent
        cmpA    PORTG_DB_U8_prev
        beq     LD82E_999
        staA    PORTG_DB_U8_prev
        staA    LW+LOGID_ECU_PORTG_DB_TYPE_U8
LD82E_999:


;figure this: perhaps calc'ing throttle angle filtered against the last throttle posn

        ldX     #L8020                  ;0xC0 (192.), or 0.75 in 0.8 fixed point format

        ldD     *L0024                  ;get the 10-bit right-aligned VTA (throttle angle) reading
        subD    *L0042                  ;D = delta (instantaneous VTA - filtered VTA)
        bhs     LD82E_0                 ;br if instantaneous VTA (L0024) >= filtered VTA (L0042)

        comA                            ;else make the delta positive
        negB
        sbcA    #0xFF
        jsr     LF3E4                   ;D8.8 = D8.8 * 0[X]0.8
        comA
        negB
        sbcA    #0xFF
        bra     LD82E_1

;This multiply is effectively D16.0 = D16.0 * 0[X]0.8
;or D = delta * 0.75
LD82E_0:
        jsr     LF3E4                   ;D8.8 = D8.8 * 0[X]0.8

LD82E_1:
        addD    *L0042                  ;add (delta*0.75) + filtered VTA.
        stD     *L0042                  ;write the new filtered VTA.

        ldaB    L011A                   ;get our index
        ldX     #L011B                  ;index into the VTA rolling history array
        aBX
        pshX                            ;save a pointer for later

        ldD     *L0042                  ;get the filtered VTA
        subD    0, X                    ;subtract the selected array element
        bhs     LD82E_3                 ;if filtered VTA (new) >= selected array element (old)

        addA    #0x80
        cmpA    #0x80
        bhs     LD82E_2

        xgDX
        cmpX    #0x7F80
        blo     LD82E_2

        xgDX
        addB    #0x80
        bra     LD82E_5


LD82E_2:
        ldaB    #0x00
        bra     LD82E_5

;We get here if the new Filtered VTA >= the old selected array element
LD82E_3:
        addA    #0x80
        blo     LD82E_4

        xgDX
        cmpX    #0x8080
        bhs     LD82E_4

        xgDX
        addB    #0x80
        bra     LD82E_5


LD82E_4:
        ldaB    #0xFF
LD82E_5:
        brclr   *L0001 #0b01000000 LD82E_6
        ldaA    #0xFF
        staA    *L00AF
        bra     LD82E_7

LD82E_6:
        cmpB    L806C                   ;0x84
        blo     LD82E_7
        clr     *L00AF

LD82E_7:
        staB    *L00C7                  ;the only place that writes this value

        ldaA    L9176                   ;0x86
        cmpA    *L00C7
        blo     LD82E_8

        ldaA    L0124
        addA    #0x01
        sbcA    #0x00
        staA    L0124
        bra     LD82E_9

LD82E_8:
        clr     L0124

LD82E_9:
        pulX                            ;the pointer to the selected array element
        ldD     *L0042
        stD     0, X                    ;save the filtered VTA in the selected array element

        ldaB    L011A                   ;Point at the next rolling history element:
        addB    #0x02                   ;L011A = (L011A + 2) & 0x07
        andB    L8022                   ;0x07
        staB    L011A

        ldaA    *L00E2
        cmpA    #0x80
        blo     LD82E_10
        bclr    *L0008 #0b01000000
        bra     LD82E_11

LD82E_10:
        ldaA    *L00C7
        cmpA    L9A2A
        blo     LD82E_11
        bset    *L0008 #0b01000000
LD82E_11:
        ldaA    *L0042
        bne     LD82E_12
        ldD     *L0042
        bra     LD82E_13

LD82E_12:
        ldaB    #0xFF
LD82E_13:
        staB    *L0048
        ldaA    *L004D                  ;8.8 RPM50
        ldX     #L917C
        jsr     LF3C2
        staA    *L00C1
        jsr     LDE74                   ; only call is from here
        brset   *L000C #0b00001000 LD82E_18
        ldaA    *L0048
        cmpA    *L00C1
        bls     LD82E_14
        clrA
        bra     LD82E_15

LD82E_14:
        ldaA    L0125
        addA    #0x01
        sbcA    #0x00
LD82E_15:
        staA    L0125
        brclr   *L0006 #0b00010000 LD82E_16
        ldaA    *L0048
        cmpA    *L00C2
        blo     LD82E_16
        ldaA    L0126
        addA    #0x01                   ; increment, pegged to 0xFF
        sbcA    #0x00
        bra     LD82E_17

LD82E_16:
        clrA
LD82E_17:
        staA    L0126
LD82E_18:
        ldaA    L0123
        addA    #0x01
        sbcA    #0x00
        staA    L0123

        jsr     LF68F                   ;only invocation: tests state of PG1

        brset   *L000A #0b00000001 LD82E_19
        brclr   *L0000 #0b00000001 LD82E_20
        bset    *L000A #0b00000001
        bra     LD82E_20

LD82E_19:
        brset   *L0000 #0b00000001 LD82E_20

        clr     L0146
        bclr    *L000A #0b00000001

LD82E_20:
        brset   *L0000 #0b00000001 LD82E_21

        clr     L014E                   ;the only place this gets cleared
        bra     LD82E_26

LD82E_21:
        ldaA    *L00D0
        cmpA    L91AA                   ;0x12 == 0x1200 -> 1085 RPM
        blo     LD82E_22                ;br on period >= means RPM is less than 1085 RPM

        bset    *L0000 #0b01000100

LD82E_22:
        brset   *L0004 #0b00000001 LD82E_26  ;br if DON_FILT says that the bike has tipped over
        brclr   *L0004 #0b00000010 LD82E_26
        brclr   *L0003 #0b00010000 LD82E_26  ;br if we don't know where the crank is
        brset   *L0001 #0b00100000 LD82E_26

;-----------------------------------------------------------------
;We only do these things if all is right with the world:
        ldaA    *L0053                  ;clamp L0053 to a max value of 0xC0 (192.)
        cmpA    #0xC0
        blo     LD82E_23
        ldaA    #0xC0
        staA    *L0053

LD82E_23:
        ldaA    *L0052                  ;clamp L0052 to a max value of 0xC0 (192.)
        cmpA    #0xC0
        blo     LD82E_24
        ldaA    #0xC0
        staA    *L0052

LD82E_24:
        ldaA    *L0051                  ;clamp L0051 to a max value of 0xC0 (192.)
        cmpA    #0xC0
        blo     LD82E_25
        ldaA    #0xC0
        staA    *L0051

LD82E_25:
        ldD     *L0079
        addA    *L00C8
        stD     *L005B
        sei
        jsr     LF51B                   ;start the FRONT injector squirting
        cli
        nop
        nop
        sei
        jsr     LF54B                   ;start the REAR injector squirting
        cli
        bset    *L0001 #0b00100000

LD82E_26:
        ldD     #0x0000
        stD     L015C
        bclr    *L0001 #0b00000001
        ldaA    *L0049_PORTG_debounced
        andA    #0b00101000
        cmpA    #derestricted
        bne     LD82E_27
        jmp     LD82E_43

;Restricted:
LD82E_27:
        brclr   *L0006 #0b01000000 LD82E_31
        ldaA    *L00C7
        cmpA    L90DE
        blo     LD82E_31
        ldaA    L012B
        cmpA    L90E3
        blo     LD82E_31
        ldaA    #0x00
        staA    L012B
        ldaB    L90DF
        lslD
        lslD
        lslD
        lslD
        lslD
        stD     L015C
        bset    *L0001 #0b00000001
        ldaA    L9A26
        cmpA    #0x00
        beq     LD82E_29
        ldaB    *L00E2
        cmpB    #0x80
        bne     LD82E_28
        ldaB    L9A29
        staB    *L00E2
LD82E_28:
        cmpB    #0x80
        bhs     LD82E_29
        ldaA    L9A2D
        staA    *L00E3
LD82E_29:
        jsr     LF734
        ldaB    *L008E
        cmpB    #0x80
        bne     LD82E_30
        ldaB    L0173
        staB    *L008E
LD82E_30:
        cmpB    #0x80
        bhs     LD82E_31
        ldaA    L0174
        staA    *L008F
LD82E_31:
        ldaA    L012D
        cmpA    #0x01
        beq     LD82E_32
        dec     L012D
        bra     LD82E_33

LD82E_32:
        brset   *L0006 #0b01000000 LD82E_33
        brset   *L0001 #0b01000000 LD82E_33
        ldaA    L90D9
        cmpA    *L004D                  ;8.8 RPM50
        bls     LD82E_33
        ldaA    L90DA
        cmpA    *L0044
        bls     LD82E_33
        ldaA    *L00C7
        cmpA    L90DB
        bhs     LD82E_34
LD82E_33:
        jmp     LD82E_59

LD82E_34:
        ldaA    *L00C7
        ldX     #L90E4
        jsr     LF3C2
        staA    *L00CB
        ldaA    *L0022
        ldX     #L90EE
        jsr     LF3C2
        staA    *L00CC
        ldaB    *L00CD
        mul
        stD     L015A
        ldaA    *L0031
        ldX     #0x9101
        jsr     LF3C2
        tAB
        clrA
        addD    #0x0180
        ldX     L015A
        jsr     LF3F6
        stD     L015A
        ldaA    *L0040
        ldX     #L910B
        jsr     LF3C2
        tAB
        clrA
        ldX     L015A
        jsr     LF3F6
        ldX     #0x00CB
        jsr     LF3E4                   ;D8.8 = D8.8 * 0[X]0.8
        lslD
        lslD
        stD     *L00C9
LD82E_35:
        ldaA    #0x0A
        ldaB    L012D
        mul
        staB    L0159
        ldD     *L0079
        addD    *L00C9
        subA    L0159
        blo     LD82E_36
        ldaA    L012D
        addA    #0x01
        sbcA    #0x00
        staA    L012D
        bra     LD82E_35

LD82E_36:
        ldX     L90DC
        cmpX    *L00C9
        bhs     LD82E_39
        ldaA    L012C
        cmpA    L90E0
        blo     LD82E_37
        ldaA    #0x00
        staA    L012C
        staA    L0129
        bra     LD82E_41

LD82E_37:
        ldaA    L0129
        brset   *L0049_PORTG_debounced #0b00000001 LD82E_38          ;PG0: CLT clutch switch
        cmpA    L90E1
        blo     LD82E_40
        bra     LD82E_39

LD82E_38:
        cmpA    L90E2
        blo     LD82E_40
LD82E_39:
        jmp     LD82E_59

LD82E_40:
        ldaA    L0129
        addA    #0x01
        sbcA    #0x00
        staA    L0129
LD82E_41:
        ldD     *L00C9
        addD    L015C
        stD     L015C
        bset    *L0001 #0b00000001
        ldaA    L90CF
        cmpA    #0x80
        beq     LD82E_42
        jmp     LD82E_59

LD82E_42:
        ldaA    #0x80
        staA    *L008E
        jmp     LD82E_59

;Derestricted:
LD82E_43:
        brclr   *L0006 #0b01000000 LD82E_47
        ldaA    *L00C7
        cmpA    L911B
        blo     LD82E_47
        ldaA    L012B
        cmpA    L9120
        blo     LD82E_47
        ldaA    #0x00
        staA    L012B
        ldaB    L911C
        lslD
        lslD
        lslD
        lslD
        lslD
        stD     L015C
        bset    *L0001 #0b00000001
        ldaA    L9A26
        cmpA    #0x00
        beq     LD82E_45
        ldaB    *L00E2
        cmpB    #0x80
        bne     LD82E_44
        ldaB    L9A29
        staB    *L00E2
LD82E_44:
        cmpB    #0x80
        bhs     LD82E_45
        ldaA    L9A2D
        staA    *L00E3
LD82E_45:
        jsr     LF734
        ldaB    *L008E
        cmpB    #0x80
        bne     LD82E_46
        ldaB    L0173
        staB    *L008E
LD82E_46:
        cmpB    #0x80
        bhs     LD82E_47
        ldaA    L0174
        staA    *L008F
LD82E_47:
        ldaA    L012D
        cmpA    #0x01
        beq     LD82E_48
        dec     L012D
        bra     LD82E_49

LD82E_48:
        brset   *L0006 #0b01000000 LD82E_49
        brset   *L0001 #0b01000000 LD82E_49
        ldaA    L9116
        cmpA    *L004D                  ;8.8 RPM50
        bls     LD82E_49
        ldaA    L9117
        cmpA    *L0044
        bls     LD82E_49
        ldaA    *L00C7
        cmpA    L9118
        bhs     LD82E_50
LD82E_49:
        jmp     LD82E_59

LD82E_50:
        ldaA    *L00C7
        ldX     #L9121
        jsr     LF3C2
        staA    *L00CB

        ldaA    *L0022
        ldX     #L912B
        jsr     LF3C2
        staA    *L00CC

        ldaB    *L00CD
        mul
        stD     L015A

        ldaA    *L0031
        ldX     #L913E
        jsr     LF3C2
        tAB
        clrA
        addD    #0x0180
        ldX     L015A
        jsr     LF3F6
        stD     L015A

        ldaA    *L0040
        ldX     #L9148
        jsr     LF3C2
        tAB
        clrA
        ldX     L015A
        jsr     LF3F6
        ldX     #0x00CB
        jsr     LF3E4                   ;D8.8 = D8.8 * 0[X]0.8
        lslD
        lslD
        stD     *L00C9
LD82E_51:
        ldaA    #0x0A
        ldaB    L012D
        mul
        staB    L0159
        ldD     *L0079
        addD    *L00C9
        subA    L0159
        blo     LD82E_52
        ldaA    L012D
        addA    #0x01
        sbcA    #0x00
        staA    L012D
        bra     LD82E_51

LD82E_52:
        ldX     L9119
        cmpX    *L00C9
        bhs     LD82E_59
        ldaA    L012C
        cmpA    L911D
        blo     LD82E_53
        ldaA    #0x00
        staA    L012C
        staA    L0129
        bra     LD82E_57

LD82E_53:
        ldaA    L0129
        brset   *L0049_PORTG_debounced #0b00000001 LD82E_54          ;PG0: CLT clutch switch
        cmpA    L911E
        blo     LD82E_56
        bra     LD82E_55

LD82E_54:
        cmpA    L911F
        blo     LD82E_56
LD82E_55:
        bra     LD82E_59

LD82E_56:
        ldaA    L0129
        addA    #0x01
        sbcA    #0x00
        staA    L0129
LD82E_57:
        ldD     *L00C9
        addD    L015C
        stD     L015C
        bset    *L0001 #0b00000001
        ldaA    L90CF
        cmpA    #0x80
        beq     LD82E_58
        jmp     LD82E_59

LD82E_58:
        ldaA    #0x80
        staA    *L008E
LD82E_59:
        brclr   *L0001 #0b00000001 LD82E_63
        ldD     *L0079
        addD    L015C
        stD     *L005B
        brset   *L0004 #0b00000001 LD82E_63  ;br if DON_FILT says that the bike has tipped over
        brclr   *L0004 #0b00000010 LD82E_63

;theory: if we get here, the engine is good to go.  We are about
;to decide if we are shooting the front or the rear injector

        brclr   *L000D #0b00010000 LD82E_60  ;br if FRONT ign coil IG11 is OK
        brclr   *L000D #0b00001000 LD82E_60  ;br if FRONT ign coil IG12 is OK
        bra     LD82E_61                     ;else neither FRONT coil is working: do NOT fire the FRONT injector

LD82E_60:
        sei
        jsr     LF51B                   ;start the FRONT injector squirting
        cli

LD82E_61:
        brclr   *L000D #0b00000100 LD82E_62  ;br if REAR ign coil IG21 is OK
        brclr   *L000D #0b00000010 LD82E_62  ;br if REAR ign coil IG22 is OK
        bra     LD82E_63                     ;else neither REAR coil is working: do NOT fire the REAR injector

LD82E_62:
        sei
        jsr     LF54B                   ;start the REAR injector squirting
        cli

LD82E_63:
        ldX     #0x802B
        ldaA    *L0038
        clrB
        subD    *L003A
        bhs     LD82E_64

        comA
        negB
        sbcA    #0xFF
        jsr     LF3E4                   ;D8.8 = D8.8 * 0[X]0.8
        comA
        negB
        sbcA    #0xFF
        bra     LD82E_65

LD82E_64:
        jsr     LF3E4                   ;D8.8 = D8.8 * 0[X]0.8
LD82E_65:
        addD    *L003A
        stD     *L003A
        ldX     #0x802B
        ldaA    *L0039
        clrB
        subD    *L003C
        bhs     LD82E_66

        comA
        negB
        sbcA    #0xFF
        jsr     LF3E4                   ;D8.8 = D8.8 * 0[X]0.8
        comA
        negB
        sbcA    #0xFF
        bra     LD82E_67

LD82E_66:
        jsr     LF3E4                   ;D8.8 = D8.8 * 0[X]0.8
LD82E_67:
        addD    *L003C
        stD     *L003C                  ;this is the only write to this location

        jsr     LC46F                   ;Call the A/D converter driver

        jsr     LF6C1                   ;Check the side-stand switch input

        ldaA    L0142
        cmpA    L9A48                   ;0x32 (50.)
        blo     LD82E_70

;-----------------------------------------------------------------
;The circuitry for the DON sensor works as follows: The DON
;sensor is a 62K ohm resistor when upright, and 0 ohms when
;tipped over.  There is a 62K ohm pullup R to the DON sensor.
;This means that under normal conditions, the voltage being read
;by the A/D is divided by the pair or R's.  Since they are the
;same value, under normal conditions, DON=2.5V.  If the bike
;falls over, DON=0V.  If the DON sensor is missing or
;disconnected, DON=5.0V.

;DON (Tip over) sensor processing:
        ldaA    *L002D                  ;DON "tip over" sensor reading
        cmpA    L9A49                   ;0x0D
        blo     LD82E_71                ;if reading is close to 0V, the bike fell over.
        cmpA    L9A59                   ;0xF8
        bhi     LD82E_68                ;if reading is close to 5V, the DON sensor is disconnected.
        bra     LD82E_70                ;else it is somewhere between 0 and 5V, the bike is upright.

;On this iteration, the DON sensor appears to be disconnected:
LD82E_68:
        ldaA    L0119                   ;get out DON "consistency" count
        cmpA    L9A5A                   ;0xC8 (200.)
        blo     LD82E_69                ;br if we have had less than 200 '1' readings in a row.

;200 iterations represents 1.0 seconds since this code is executed on a 5 mSec schedule.
        bset    *L000D #0b00000001      ;we are really, really sure that the sensor is bad (200 times sure!)

LD82E_69:
        bclr    *L0005 #0b10000000      ;bad sensor default: DON_RAW indicates we have NOT tipped over
        bra     LD82E_72

;On this iteration, the bike appears to be upright:
LD82E_70:
        bclr    *L000D #0b00000001      ;the sensor is not bad (open)
        bclr    *L0005 #0b10000000      ;DON_RAW indicates we have NOT tipped over
        bra     LD82E_72

;On this iteration, the bike appears to have tipped over:
LD82E_71:
        bset    *L0005 #0b10000000      ;DON_RAW indicates we HAVE tipped over
        bclr    *L000D #0b00000001      ;we are sure that the sensor is NOT bad (open)

LD82E_72:
        jsr     LF6F3                   ;call the tip-over filter processor
;-----------------------------------------------------------------

        ldaA    *L00D0                  ;the current 1/6 crank period
        cmpA    L91AA                   ;0x12 == 0x1200 -> 1085 RPM
        bhi     LD82E_74

        cmpA    L91AB                   ;0x05 == 0x0500 -> 3906 RPM
        blo     LD82E_74

        ldaA    *L0030                  ;the converted, interpolated 8-bit THW (water temp) reading
        cmpA    L9A1A
        bhs     LD82E_74
        ldaA    L9A18
        cmpA    *L00C7
        bhs     LD82E_73
        clr     *L00E7
        bra     LD82E_74

LD82E_73:
        ldaA    *L00E7
        addA    #0x01
        sbcA    #0x00
        staA    *L00E7
        cmpA    L9A19
        blo     LD82E_74
        ldaA    *L00E6
        cmpA    *L0048
        blo     LD82E_74
        brset   *L000C #0b00001000 LD82E_74  ;'1' if VTA is not working
        bset    *L0005 #0b00100000      ;'1' means VTA is good
        bra     LD82E_75

LD82E_74:
        bclr    *L0005 #0b00100000      ;'0' means VTA is bad
LD82E_75:
        ldaA    L012C
        addA    #0x01
        sbcA    #0x00
        staA    L012C
        ldaA    L012B
        addA    #0x01
        sbcA    #0x00
        staA    L012B
        ldaA    L012A
        addA    #0x01
        sbcA    #0x00
        staA    L012A
        brset   *L0001 #0b01000000 LD82E_76
        ldaA    L010C
        addA    #0x01
        sbcA    #0x00
        bra     LD82E_77

LD82E_76:
        ldaA    #0x00
LD82E_77:
        staA    L010C
        brset   *L0000 #0b00000001 LD82E_78
        brset   *L0005 #0b01000000 LD82E_78

        ldaA    #0x00
        bra     LD82E_79                ;clears L014F


LD82E_78:
        ldaA    L014F                   ;inc L014F, but not past 0xFF
        addA    #0x01
        sbcA    #0x00
LD82E_79:
        staA    L014F

;What makes this critical code?
        sei
        ldaA    *L0051                  ;inc L0051, but not past 0xFF
        addA    #0x01
        sbcA    #0x00
        staA    *L0051

        ldaA    *L0052                  ;inc L0052, but not past 0xFF
        addA    #0x01
        sbcA    #0x00
        staA    *L0052

        ldaA    *L0053                  ;inc L0053, but not past 0xFF
        addA    #0x01
        sbcA    #0x00
        staA    *L0053
        cli

        brclr   *L0010 #0b00000100 LD82E_80
        ldaB    #0x00
        staB    L0137
        ldaB    L0132
        addB    #0x01
        sbcB    #0x00
        staB    L0132
        bra     LD82E_81

LD82E_80:
        ldaB    #0x00
        staB    L0132
        ldaB    L0137
        addB    #0x01
        sbcB    #0x00
        staB    L0137

LD82E_81:
        brclr   *L0010 #0b00010000 LD82E_82  ;br if the AAP sensor is NOT suspect

;AAP sensor is suspect:
        ldaB    #0x00                   ;clear the "good AAP" count
        staB    L013B

        ldaB    L0136                   ;increment the "bad AAP sensor event" count, pegging at 0xFF
        addB    #0x01
        sbcB    #0x00
        staB    L0136

        bra     LD82E_83

; AAP sensor is NOT suspect:
LD82E_82:
        ldaB    #0x00                   ;clear the "bad AAP" count
        staB    L0136

        ldaB    L013B                   ;increment the "good APP" count, pegging at 0xFF
        addB    #0x01
        sbcB    #0x00
        staB    L013B

LD82E_83:
        brclr   *L0010 #0b00100000 LD82E_84  ;br if the VTA sensor is NOT suspect

        ldaB    #0x00                   ;clear the "good VTA" count
        staB    L0138

        ldaB    L0133                   ;increment the "suspect VTA" count
        addB    #0x01
        sbcB    #0x00
        staB    L0133
        bra     LD82E_85

LD82E_84:
        ldaB    #0x00                   ;clear the "suspect VTA" count
        staB    L0133

        ldaB    L0138                   ;increment the "good VTA" count
        addB    #0x01
        sbcB    #0x00
        staB    L0138

LD82E_85:
        brclr   *L0010 #0b00001000 LD82E_86
        ldaB    #0x00
        staB    L013A
        ldaB    L0135
        addB    #0x01
        sbcB    #0x00
        staB    L0135
        bra     LD82E_87

LD82E_86:
        ldaB    #0x00
        staB    L0135
        ldaB    L013A
        addB    #0x01
        sbcB    #0x00
        staB    L013A

LD82E_87:
        brclr   *L0010 #0b00000010 LD82E_88  ;br if the THW conversion was OK (not suspect)

;The THW conversion was "suspect" (out of range)
        ldaB    #0x00                   ;clear the THW "good conversion" counter
        staB    L0139

        ldaB    L0134                   ;increment the suspect THW conversion counter
        addB    #0x01
        sbcB    #0x00
        staB    L0134
        bra     LD82E_89

;The THW conversion was "good" (in range)
LD82E_88:
        ldaB    #0x00                   ;clear the "suspect conversion" counter
        staB    L0134

        ldaB    L0139                   ;increment the "good conversion" counter
        addB    #0x01
        sbcB    #0x00
        staB    L0139

;-----------------------------------------------------------------
LD82E_89:
        ldaA    PORTH
        bitA    #0b00010000             ;test PH4
        beq     LD82E_91                ;skip selftest if PH4==0
        bitA    #0b10000000             ;test PH7 (Self Test)
        beq     LD82E_90                ;if PH7 is pulled low, we are being requested to enter self-test mode
        bra     LD82E_91

;We get here if (PH4==1) && (PH7==0)
LD82E_90:
        ldaA    *L000D
        andA    #0b00011110
        cmpA    #0x1E                   ;0x1e means ?no sparks fired lately?
        bne     LD82E_91

        ldaA    PORTH
        bitA    #0b00010000             ;skip selftest if PH4==0
        beq     LD82E_91

        ldaA    *L000D
        andA    #0b00011110
        cmpA    #0x1E                   ;0x1e means ?no sparks fired lately?
        bne     LD82E_91

;We have double checked that all injection processes are
;disabled.  One last double check that we should enter self-test
;mode:
        ldaA    PORTH
        bitA    #0b10000000             ;test PH7
        bne     LD82E_91                ;PH7==1 means skip the self test
        jmp     LDEB4_selfTest          ;PH7==0 means do a self test (never returns!)


        .byte   0x0F, 0x20, 0xFD

LD82E_91:
        bclr    *L0012 #0b00001000
        jmp     LC2BD                   ;acts basically like a rts


;*****************************************************************
; ???
LDE74:  brset   *L0006 #0b01000000 0f

        ldaA    *L00C1
        addA    L918B
        staA    *L00C2
        clr     L0127
        bra     4f

0:
        ldaA    *L0048
        cmpA    *L00C1
        bhi     4f

        cmpA    *L00C3
        beq     1f

        clrA
        bra     2f

1:
        ldaA    L0127
        addA    #0x01
        sbcA    #0x00
2:
        staA    L0127
        cmpA    L918C
        blo     4f

        ldaA    *L0048
        addA    L918B
        bhs     3f

        ldaA    #0xFF
3:
        cmpA    *L00C2
        bhs     4f
        staA    *L00C2

4:
        ldaA    *L0048
        staA    *L00C3
        rts

;*****************************************************************
; Perform a self test on the EPROM/RAM.
LDEB4_selfTest:
        sei

        ldaA    PORTH
        andA    #0b11110101
        staA    PORTH

        ldaA    #0x00
        staA    L0153

        ldX     #0x8000                 ;start of EPROM
        ldD     #0x0000                 ;clear the tmp read value
0:
        addD    0, X                    ;add the EPROM location to a 16-bit sum
        inc     L0153
        bne     1f                      ;in fact, add the EPROM location in 256 times!
        clr     L0153                   ;** unnecessary, since we know it is already 0 from the BNE
        pshB                           ;should be 00 always?

        ldaB    PORTJ
        eorB    #0b00001000             ;toggle PJ3
        staB    PORTJ

        pulB
1:
        inX                             ;point at the next 16-bit word
        inX
        bne     0b                      ;repeat until we wrap around to addr 0x0000

        stD     L017B                   ;store the cksum
        subD    #0xAA55                 ;test if the result was 0xAA55
        bne     4f                      ;if not, we failed.

;EPROM Cksum passed:
        ldaA    PORTH
        oraA    #0b00000010             ;PORTH:B1 == 1 indicates EPROM cksum passed
        staA    PORTH

        ldaA    PORTJ
        eora    #0b00001000             ;toggle PJ3
        staA    PORTJ

;Now do a stuck-bit RAM test:
        ldX     #0x0000                 ;start at location 0x0000
        ldaA    #0x55
2:
        staA    0, X                    ;write a test pattern
        ldaB    0, X                    ;read it back
        cmpB    #0x55
        bne     4f                      ;if the read doesn't match the write, we fail
        inX
        cmpX    #0x0200                 ;have we tested all of RAM yet?
        blo     2b                      ;if not

        ldaA    PORTJ
        eora    #0b00001000             ;toggle PJ3
        staA    PORTJ

;Do another stuck-bit test with the complement of the original pattern:
        ldX     #0x0000
        ldaA    #0xAA
3:
        staA    0, X
        ldaB    0, X
        cmpB    #0xAA
        bne     4f                      ;if the test failed
        inX
        cmpX    #0x0200
        blo     3b

        ldaA    PORTH
        oraA    #0b00001000             ;PORTH:B3 == 1 indicates RAM test passed
        staA    PORTH

;This next section toggles PJ3 as long as PH4 is 0.  As soon as
;PH4 is 1 for two consecutive reads, the program wedges itself
;forever.

4:
        ldaB    #0xFF
5:
        incB
        bne     6f

        clrB                            ;unnecessary: B must be 0 if we get here
        ldaA    PORTJ
        eora    #0b00001000             ;toggle PJ3
        staA    PORTJ

6:
        ldaA    PORTH
        bitA    #0b00010000
        beq     5b

        ldaA    PORTH
        bitA    #0b00010000
        beq     5b                     ;last chance to keep looping

7:
        sei                             ;else wedge permanently
        bra     7b

;*****************************************************************
;This routine is processed every 40 mSec.

LDF4D:  bclr    *L0002 #0b00100000
        bset    *L0012 #0b00000100

        ldaA    L010F
        addA    #0x01
        sbcA    #0x00
        staA    L010F

;L0004:%00000100 gets set to '1' every time the 1/6 crank period
;calculation shows that the most recent period is below 575 RPM.

        brclr   *L0004 #0b00000100 LDF4D_0   ;br if slow RPM flag is NOT set

;Slow RPM flag is set. Remember that the L010D counter gets
;cleared every time a the slow RPM flag is set.  The slow RPM
;flag gets cleared instantly (elsewhere) if the RPM is ever
;observed to be > 1000 RPM.  This section clears the slow RPM
;flag if the RPM is in the range [576..999] for 1.0 second.

        sei
        ldaA    L010D                   ;increment L010D, but not past 0xFF
        addA    #0x01
        sbcA    #0x00
        staA    L010D
        cli

        cmpA    L9A4E                   ;0x19 (25.) 25 times a 0.040 sec = 1.0 second
        blo     LDF4D_0                 ;br if the slow RPM flag was set less than 1 second ago

        bclr    *L0004 #0b00000100      ;clear the slow RPM condition 1 second later

        clr     L010D

LDF4D_0:
        sei

;-----------------------------------------------------------------
        ldaA    L010E                   ;this counter gets zeroed on every crankRef event interrupt
        addA    #0x01
        sbcA    #0x00
        staA    L010E

;I guess that the counter will never increment to 13 unless the
;crank has not produced a rotational event in 13*40mSec or 0.520
;seconds.

        cmpA    L9A47                   ;0x0D (13.)
        blo     LDF4D_1                 ;br if 0.5 second has not elapsed since the last crank event

;This is probably shutting things down since the motor appears to
;have stopped running.

;Quit charging the FRONT ign coil (if we were), also prevents
;future charge events from occuring.

        ldaA    #0x80                   ;clears any outstanding OC1F interrupt request
        staA    TFLG1

        ldD     TCNT
        addD    #0x000A                 ;generate an OC1 event 20 uSec from now...
        stD     TOC1

        ldaA    #0xF8                   ;...which will set PA7=1 on OC1 match event (1 == not charging)
        staA    OC1D

        ldaA    TMSK1
        andA    #0b01111111             ;disable OC1 interrupts
        staA    TMSK1


;Quit charging the REAR ign coil (if we were), also prevents
;future charge events from occuring.

        ldaA    #0x80                   ;PA7/OC1 is affected by OC1 compare events
        staA    OC1M

        ldaA    #0x40                   ;clears any outstanding OC2F interrupt request
        staA    TFLG1

        ldD     TCNT
        addD    #0x000A                 ;generate an OC2 event 20 uSec from now...
        stD     TOC2

        ldaA    TCTL1
        oraA    #0b11000000             ;...which will set OC2 to '1' on OC2 match event
        staA    TCTL1

        ldaA    TMSK1
        andA    #0b10111111             ;disable OC2 interrupts
        staA    TMSK1
LDF4D_1:
        cli

        ldaA    L0142                   ;increment L0142 to a max of 0xFF
        addA    #0x01
        sbcA    #0x00
        staA    L0142

        ldaA    L0146                   ;increment L1046 to a max of 0xFF
        addA    #0x01
        sbcA    #0x00
        staA    L0146

        ldaA    *L0049_PORTG_debounced
        bitA    #0b00010000             ;the TEST (diag) input
        beq     LDF4D_2                 ;Branch if the diag connector is active (grounded)

        ldaA    L0145                   ;increment L0145, but not past 0xFF
        addA    #0x01
        sbcA    #0x00
        staA    L0145

        cmpA    #25                     ;we need to see 25 consecutive requests (at .04 sec per means 1.0 seconds)
        blo     LDF4D_3                 ;if less than 1 second has elapsed

;Grounding the DIAG TEST input for 1 second will cause us to
;enter test mode (but only if engine is not running).
        bset    *L0005 #0b00001000      ;enter DIAG TEST mode
        bra     LDF4D_3

;If the switch reads as '0', we instantly clear the 1.0 second counter.
LDF4D_2:
        bclr    *L0005 #0b00001000      ;disable diag mode
        clr     L0145                   ;reset the 1.0 second counter


LDF4D_3:
        brclr   *L0008 #0b00000010 LDF4D_5
        ldaA    *L002A                  ;get the raw VM A/D reading
        cmpA    L9A56                   ;0x66 (102./256)*20V = 7.97V
        blo     LDF4D_5                 ;br if VM is < ~8V

        brset   *L0000 #0b00000001 LDF4D_4

        bclr    *L0003 #0b00000100      ;this flag is normally set on every falling edge crank event

LDF4D_4:
        ldaA    L014E
        cmpA    L9A54                   ;0x19 (25.)
        blo     LDF4D_5

        brset   *L0003 #0b00000100 LDF4D_5
        bset    *L0011 #0b10000000      ;this flag gets cleared on every falling edge on the crank sensor
                                        ;this is the only place that sets it

LDF4D_5:
        ldaB    *L0011
        bitB    #0b11000000             ;test the crank error bit and the ? error bit
        beq     LDF4D_6                 ;br if neither are in error

        bset    *L000C #0b01000000      ;set the "crank sensor broken" bit
        bra     LDF4D_7


LDF4D_6:
        bclr    *L000C #0b01000000      ;else clear the "crank sensor broken" bit

LDF4D_7:
        brclr   *L0008 #0b00000010 LDF4D_9
        ldaA    *L002A                  ;get the raw VM A/D reading
        cmpA    L9A56                   ;0x66 (102./256)*20V = 7.97V
        blo     LDF4D_9                 ;br if VM is < ~8V

        brset   *L0000 #0b00000001 LDF4D_8

        bclr    *L0003 #0b00001000

LDF4D_8:
        ldaA    L014E
        cmpA    L9A54                   ;0x19 (25.)
        blo     LDF4D_9

        brset   *L0003 #0b00001000 LDF4D_9
        bset    *L0011 #0b00100000

LDF4D_9:
        ldaB    *L0011
        bitB    #0b00110000
        beq     LDF4D_10

        bset    *L000C #0b10000000      ;cam sensor is broken
        bra     LDF4D_11

LDF4D_10:
        bclr    *L000C #0b10000000      ;cam sensor is not broken

LDF4D_11:
        sei
        ldaB    *L000C                  ;propagate all new error bits into L000E

        cmpB    ECU_ERROR_L000C_U8_prev
        beq     0f
        staB    ECU_ERROR_L000C_U8_prev
        staB    LW+LOGID_ECU_ECU_ERROR_L000C_TYPE_U8
0:
        oraB    *L000E                  ;This is a one-way operation: L000E bits never get cleared (except b4).
        staB    *L000E
        cli

        nop
        nop

        sei
        ldaB    *L000D                  ;propagate all new error bits into L000F

        cmpB    ECU_ERROR_L000D_U8_prev
        beq     0f
        staB    ECU_ERROR_L000D_U8_prev
        staB    LW+LOGID_ECU_ECU_ERROR_L000D_TYPE_U8
0:
        oraB    *L000F                  ;This is a one-way operation: L000F bits never get cleared
        staB    *L000F
        cli

        ldD     *L000C                  ;br to LDF4D_13 if D != 0
        cmpA    #0x00
        bne     LDF4D_13
        cmpB    #0x00
        bne     LDF4D_13

        bset    *L0010 #0b10000000
        brclr   *L0010 #0b00000001 LDF4D_12
        clr     L013C
        bclr    *L0010 #0b00000001
        bra     LDF4D_14

LDF4D_12:
        ldaA    L013C
        addA    #0x01
        sbcA    #0x00
        staA    L013C
        bra     LDF4D_14

LDF4D_13:
        ldaA    *L0010
        andA    #0b01111111
        oraA    #0b00000001
        staA    *L0010

        ldaA    #0xFF
        staA    L013C

LDF4D_14:
        brclr   *L0005 #0b00001000 LDF4D_17  ;br if we are NOT in DIAG TEST mode

        brset   *L0008 #0b00001000 LDF4D_18
        brclr   *L0008 #0b00000100 LDF4D_15
        jmp     LDF4D_31

LDF4D_15:
        clr     L0143
        ldD     *L000E

        cmpA    #0x00                   ;br to LDF4D_18 if any of the sensor error bits are set
        bne     LDF4D_18
        cmpB    #0x00
        bne     LDF4D_18

LDF4D_16:
        jmp     LDF4D_31                ;no sensor error bits are set

;We get here if we are NOT in DIAG TEST mode
LDF4D_17:
        brset   *L0008 #0b00001000 LDF4D_18
        brset   *L0008 #0b00000100 LDF4D_16
        jmp     LDF4D_43


LDF4D_18:
        ldaA    L0144
        bne     LDF4D_19
        addA    #0x01
        sbcA    #0x00
        staA    L0144
        ldaA    PORTH
        andA    #0b11111011             ;clear the WL bit
        staA    PORTH
LDF4D_19:
        ldaA    L014B
        beq     LDF4D_20
        ldaB    L014D
        bne     LDF4D_21
        bra     LDF4D_23

LDF4D_20:
        brclr   *L0008 #0b00001000 LDF4D_24

        ldaA    L014D
        bne     LDF4D_21

        ldaB    L014C
        beq     LDF4D_22

        bra     LDF4D_23

LDF4D_21:
        dec     L014D
        jmp     LDF4D_30

LDF4D_22:
        bclr    *L0008 #0b00001000      ;the only place that clears this bit
        jmp     LDF4D_30

LDF4D_23:
        jmp     LDF4D_29

LDF4D_24:
        ldaA    *L000E                  ;L000E contains the 'sticky' version of the L000C error bits
        ldaB    *L000F                  ;L000F contains the 'sticky' version of the L000D error bits
        stD     L013F                   ;

        ldaA    L013F
        oraA    L0140
        beq     LDF4D_30                ;br if no ?error bits? are set (skips all WL processing)

LDF4D_25:
        ldX     #0x0000
        ldaB    L013E
        incB
        aBX
        ldD     L013F
LDF4D_26:
        dex
        beq     LDF4D_27
        lslD
        bra     LDF4D_26

LDF4D_27:
        inc     L013E
        lslD
        blo     LDF4D_28
        tstA
        bne     LDF4D_27
        tstB
        bne     LDF4D_27
        clr     L013E
        bra     LDF4D_25

;We get here if a fault code bit is set.  L013E contains the bit # we use to index
;into the fault code table.
LDF4D_28:
        ldX     #L9FE0-1                ;start of fault codes
        ldaB    L013E
        aBX                             ;index into the table
        ldaA    0, X                    ;get the corresponding fault code
        staA    L014B                   ;save it in L014B
        bset    *L0008 #0b00001000      ;this might mean that we have a fault code to transmit

LDF4D_29:
        ldX     #L9FF6
        jsr     LF621

LDF4D_30:
        jmp     LDF4D_47                ;skips all the WL processing


LDF4D_31:
        ldaA    L0144
        addA    #0x01
        sbcA    #0x00
        staA    L0144

        bset    *L0008 #0b00000100
        ldaA    *L0048
        cmpA    L9A63                   ;0x84 (132.)
        bls     LDF4D_33
        brset   *L0005 #0b00000100 LDF4D_32

        ldaA    L9A64                   ;0x0A (10.)
        staA    L013D

LDF4D_32:
        bset    *L0005 #0b00000100
        ldaA    L013D
        beq     LDF4D_37
        dec     L013D
        jmp     LDF4D_38

LDF4D_33:
        cmpA    L9A62                   ;0x80 (128.)
        blo     LDF4D_34
        ldaA    L9A64                   ;0x0A (10.)
        staA    L013D
        jmp     LDF4D_38


LDF4D_34:
        brclr   *L0005 #0b00000100 LDF4D_35

        ldaA    L9A64                   ;0x0A (10.)
        staA    L013D

LDF4D_35:
        bclr    *L0005 #0b00000100
        ldaA    L013D
        beq     LDF4D_36                ;br if L013D==0
        dec     L013D                   ;else decrement L013D
        bra     LDF4D_38

LDF4D_36:
        bclr    *L0008 #0b00000100
        clr     L0141
        ldaB    PORTH
        oraB    #0b00000100             ;sets the WL bit
        staB    PORTH
        bra     LDF4D_42

LDF4D_37:
        clr     L0141
        ldaA    PORTH
        andA    #0b11111011             ;clears the WL bit
        staA    PORTH
        bclr    *L0008 #0b00000100
        bra     LDF4D_42

LDF4D_38:
        ldaA    L0144
        cmpA    L9FF6                   ;0x48
        bhs     LDF4D_39
        staA    L0141
        jmp     LDF4D_42

LDF4D_39:
        inc     L0141
        ldaA    L9FF0                   ;0x0b (11.)
        cmpA    L0141
        bhs     LDF4D_41

        ldaA    PORTH
        bitA    #0b00000100             ;test WL
        beq     LDF4D_40                ;if clear, we are done

        clr     L0141
        ldaA    PORTH                   ;if WL is set,
        andA    #0b11111011             ;we clear WL
        staA    PORTH

        bclr    *L0008 #0b00000100
        bra     LDF4D_41


LDF4D_40:
        ldaA    L9FF1                   ;0x0B (11.)
        cmpA    L0141
        bhs     LDF4D_41

        clr     L0141
        ldaA    PORTH
        oraA    #0b00000100             ;sets WL
        staA    PORTH

LDF4D_41:
        bra     LDF4D_42


LDF4D_42:
        jmp     LDF4D_47


LDF4D_43:
        clr     L0144

        ldaA    L0143
        addA    #0x01
        sbcA    #0x00
        staA    L0143

        ldaA    L0143
        cmpA    #0x19
        blo     LDF4D_44

        brclr   *L0010 #0b10000000 LDF4D_45
        brset   *L0000 #0b00000001 LDF4D_45

        ldaA    L0142
        cmpA    #0x23
        blo     LDF4D_46

        ldaA    L013C
        cmpA    L9A5B                   ;0x19 (25.)
        blo     LDF4D_46

LDF4D_44:
        ldaA    PORTH
        andA    #0b11111011             ;clear WL
        staA    PORTH
        bra     LDF4D_47

LDF4D_45:
        brset   *L0004 #0b01000000 LDF4D_47
LDF4D_46:
        ldaA    PORTH
        oraA    #0b00000100             ;set WL
        staA    PORTH

;-----------------------------------------------------------------
LDF4D_47:
        ldaA    *L002D                  ;DON "tip over" sensor reading
        cmpA    L9A49                   ;0x0D
        blo     LDF4D_48                ;br if < 0x0D. 0V means we tipped over.
        cmpA    L9A59                   ;0xF8
        bhi     LDF4D_48                ;br if > 0xF8, 5V means the DON sensor is open or disconnected.

        bra     LDF4D_49                ;otherwise, somewhere in the middle means the bike is upright.

;We get here if the DON sensor is reading at either extreme,
;either tipped over or bad sensor:
LDF4D_48:
        ldaA    L0119                   ;inc L0119, but not past 0xFF
        addA    #0x01
        sbcA    #0x00
        staA    L0119
        bra     LDF4D_50

;The bike is upright:
LDF4D_49:
        clrA                            ;clear our the "bad sensor" counter
        staA    L0119

LDF4D_50:
        bclr    *L0012 #0b00000100
        jmp     LC2CB

;*****************************************************************
;Do some processing on the VM (Voltage Monitor) A/D reading.
;This routine is processed every 40 mSec.

LE26C:  bclr    *L0002 #0b00010000
        bset    *L0012 #0b00000010

        ldaA    *L002A                  ;get the raw VM (Voltage Monitor) reading
        ldX     #L90D1
        jsr     LF3C2

        clrB
        lsrD
        lsrD
        lsrD
        lsrD
        lsrD
        stD     *L0079

        ldaA    *L0031                  ;get the converted, interpolated THA (air temp) reading
        ldaB    L900A                   ;0x00
        cmpB    #0x80
        bne     LE26C_1
        cmpA    L900B                   ;0x70
        blo     LE26C_1
        ldaB    *L0030                  ;L0030 is the converted, interpolated THW water temp
        cmpB    L900C
        blo     LE26C_1
        brset   *L0001 #0b01000000 LE26C_0
        brset   *L0001 #0b00000100 LE26C_4
        brset   *L0001 #0b00000010 LE26C_2

LE26C_0:
        ldaA    L9010                   ;0x76 == default?
        bra     LE26C_3

LE26C_1:
        bclr    *L0001 #0b00000010
        bclr    *L0001 #0b00000100
        ldaB    #0x00
        staB    L0130
        staB    L0131

LE26C_2:
        ldX     #L9011
        jsr     LF3C2

LE26C_3:
        staA    *L008D

LE26C_4:
        ldaA    *L0030                  ;converted, interpolated THW (water temp)

        ldaB    *L0049_PORTG_debounced
        andB    #0b00101000
        cmpB    #derestricted
        bne     LE26C_5

;Derestricted:
        ldX     #L909F
        jsr     LF3C2
        staA    *L00BF

        ldaA    *L0030                  ;the converted, interpolated 8-bit THW (water temp) reading
        ldX     #L90AA
        jsr     LF3C2
        staA    *L00C0
        bra     LE26C_6

;Restricted:
LE26C_5:
        ldX     #L906A
        jsr     LF3C2
        staA    *L00BF

        ldaA    *L0030                  ;the converted, interpolated 8-bit THW (water temp) reading
        ldX     #L9075
        jsr     LF3C2
        staA    *L00C0

LE26C_6:
        ldaA    L0128
        cmpA    L802D
        bhi     LE26C_13
        brclr   *L0000 #0b00001000 LE26C_7
        ldaA    #0x80
        staA    *L0041
        ldaB    #0xFF
        staB    L0128
        bra     LE26C_9


LE26C_7:
        brclr   *L0000 #0b00100000 LE26C_13  ;1 means there is a fresh AN2 MAP reading
        brclr   *L0000 #0b00010000 LE26C_13  ;1 means there is a fresh AAP reading

        ldaA    *L001D
        subA    *L0033                  ;the "filtered" AAP (Ambient Air Pressure) reading
        bhs     LE26C_11
        addA    #0x80
        cmpA    #0x80
        blo     LE26C_8
        ldaA    #0x00
LE26C_8:
        nop
        nop
        staA    *L0041
        ldaA    #0x80
        subA    *L0041
        cmpA    L9A57
        bhs     LE26C_10

LE26C_9:
        bclr    *L000C #0b00010000
        bclr    *L000E #0b00010000      ;this is the ONLY bit that can be cleared in L000E
        bra     LE26C_13                ;all the other bits are 'sticky': once set, they stay set

LE26C_10:
        bset    *L000C #0b00010000
        bset    *L000E #0b00010000
        bra     LE26C_13

LE26C_11:
        addA    #0x80
        bhs     LE26C_12
        ldaA    #0xFF
LE26C_12:
        nop
        nop
        staA    *L0041
        subA    #0x80
        cmpA    L9A57
        bhs     LE26C_10
        bra     LE26C_9


LE26C_13:
        brclr   *L000C #0b00110000 LE26C_14

        ldaA    L9A5C
        staA    *L0038
        staA    *L0039
        ldaA    #0x80
        staA    *L0041
        ldaA    #0xFF
        staA    *L009C

LE26C_14:
        ldaA    L0128
        addA    #0x01
        sbcA    #0x00
        staA    L0128

        ldX     #0x802C
        ldaA    *L0027
        subA    #0x80
        bhs     LE26C_15
        addA    *L0041
        blo     LE26C_16
        cmpA    #0x80
        blo     LE26C_16
        clrA
        bra     LE26C_16

LE26C_15:
        addA    *L0041
        bhs     LE26C_16
        ldaA    #0xFF
LE26C_16:
        clrB
        subD    *L003E
        bhs     LE26C_17
        comA
        negB
        sbcA    #0xFF
        jsr     LF3E4                   ;D8.8 = D8.8 * 0[X]0.8
        comA
        negB
        sbcA    #0xFF
        bra     LE26C_18

LE26C_17:
        jsr     LF3E4                   ;D8.8 = D8.8 * 0[X]0.8
LE26C_18:
        addD    *L003E
        stD     *L003E
        addD    #0x0080
        staA    *L0040
        ldaA    *L0040
        ldX     #0x901B
        jsr     LF3C2
        staA    *L008B
        ldaA    *L0040
        ldX     #0x9026
        jsr     LF3C2
        staA    *L008C
        bclr    *L0012 #0b00000010

        jmp     LC2D2

;*****************************************************************
;Called like a subr from the main loop.
;This routine is called every 80 mSec (12.5 Hz).

;We calc 4 things that are based on water temp:
;       L00E4
;       L00E6
;       L0090
;       L0091
;       L00CD

;We test the DON for tipover events

;We do some WL processing
;       WL is parked at 0 (when L0111==255)
;       if L0004:%01000000 is '1', then WL will toggle every 2 times through this loop, or every 0.160 seconds

;Control the fuel pump drive:
;       if tipped over, shut it off
;       if engine stops turning (no crankref events), then turn it off after 2.72 seconds

LE3BA:  bset    *L0012 #0b00000001      ;this bit gets set on entry, cleared on exit

        bclr    *L0002 #0b00001000
        brset   *L0001 #0b01000000 LE3BA_0   ;if set, clear the L010A counter

        ldaA    L010A                   ;else just increment the L010A counter
        addA    #0x01
        sbcA    #0x00
        bra     LE3BA_1


LE3BA_0:
        ldaA    #0x00
LE3BA_1:
        staA    L010A

        brclr   *L0000 #0b00000001 LE3BA_2

        ldaA    L014E                   ;inc L014E to max 0xFF
        addA    #0x01
        sbcA    #0x00
        staA    L014E

LE3BA_2:
        ldaA    *L0030                  ;converted, interpolated THW (water temp)
        ldX     #L9A1B
        jsr     LF3C2
        staA    *L00E4

        ldaA    *L0030                  ;converted, interpolated THW (water temp)
        ldX     #L9A0D
        jsr     LF3C2
        staA    *L00E6

        ldX     #L9049
        ldaA    *L0030                  ;converted, interpolated THW (water temp)
        jsr     LF3C2
        staA    *L0090

        ldX     #L9054
        ldaA    *L0030                  ;converted, interpolated THW (water temp)
        jsr     LF3C2
        staA    *L0091

        ldaA    *L0030                  ;converted, interpolated THW (water temp)
        ldaB    *L0049_PORTG_debounced
        andB    #0b00101000
        cmpB    #derestricted
        bne     LE3BA_3

;Restricted
        ldX     #L9133
        bra     LE3BA_4

;Derestricted:
LE3BA_3:
        ldX     #L90F6

LE3BA_4:
        jsr     LF3C2
        staA    *L00CD

        brclr   *L0004 #0b00000001 LE3BA_5   ;br if DON_FILT says that the bike is not on its side

;DON_FILT indicates that the bike is on its side:
        sei
        ldaA    #0xFF                   ;max out the counter
        staA    L010B                   ;it will be cleared if we have a crank event when the bike is upright
        cli
        bra     LE3BA_8                 ;in this case, we always disable the fuel pump driver

;The bike is NOT on its side:
LE3BA_5:
        ldaA    L010B                   ;this counter gets cleared on every crank ref falling edge interrupt
        cmpA    #0x22                   ;(34.) or 34*0.080 sec or 2.72 seconds
        blo     LE3BA_6                 ;therefore, if the engine is running it will always take this branch

;If the engine is NOT running, we will get here after 2.72 seconds:
        brclr   *L0000 #0b00000001 LE3BA_8   ;br will disable fuel pump driver
        bra     LE3BA_7                      ;else we enable the fuel pump driver

LE3BA_6:
        sei
        ldaA    L010B                   ;increment, but not past 0xFF
        addA    #0x01
        sbcA    #0x00
        staA    L010B
        cli

LE3BA_7:
        ldaA    PORTH
        andA    #0b10111111             ;enable the fuel pump driver
        staA    PORTH
        ldaB    #1
        cmpB    FUEL_PUMP_B_prev
        beq     0f
        staB    FUEL_PUMP_B_prev
        staB    LW+LOGID_ECU_FUEL_PUMP_TYPE_B
0:
        bra     LE3BA_9


LE3BA_8:
        ldaA    PORTH
        oraA    #0b01000000             ;disable the fuel pump driver
        staA    PORTH
        ldaB    #0
        cmpB    FUEL_PUMP_B_prev
        beq     0f
        staB    FUEL_PUMP_B_prev
        staB    LW+LOGID_ECU_FUEL_PUMP_TYPE_B
0:
LE3BA_9:
        brset   *L0005 #0b00001000 LE3BA_12  ;br if we are in DIAG TEST mode
        brclr   *L0000 #0b00000001 LE3BA_12

        ldaB    L014E
        cmpB    L9A52                   ;0x08
        blo     LE3BA_10

        brclr   *L0008 #0b00000010 LE3BA_11

LE3BA_10:
        brset   *L0004 #0b00000001 LE3BA_11  ;br if DON_FILT says that the bike tipped over
        brclr   *L0004 #0b00000010 LE3BA_11

;Br to LE3BA_12 if any of the ign coils are good
        brclr   *L000D #0b00010000 LE3BA_12  ;br if the FRONT ign coil IG11 is OK
        brclr   *L000D #0b00001000 LE3BA_12  ;br if the FRONT ign coil IG12 is OK
        brclr   *L000D #0b00000100 LE3BA_12  ;br if the REAR  ign coil IG21 is OK
        brclr   *L000D #0b00000010 LE3BA_12  ;br if the REAR  ign coil IG22 is OK

;None of the ign coils has produced a spark lately.
LE3BA_11:
        bset    *L0004 #0b01000000
        ldaA    #0xFF
        staA    L0143
        bra     LE3BA_13


LE3BA_12:
        bclr    *L0004 #0b01000000

LE3BA_13:
        brclr   *L0004 #0b01000000 LE3BA_15  ;branching will max out L0111 to 255

        ldaA    L0111
        cmpA    #0xFF
        blo     LE3BA_14                ;br if L0111 < 255 (hasn't maxed out)

        ldaA    PORTH
        andA    #0b11111011             ;if L0111 has maxed out, this drives WL=0
        staA    PORTH

LE3BA_14:
        ldaA    L0111                   ;inc L0111 to max 0xFF
        addA    #0x01
        sbcA    #0x00
        staA    L0111

        cmpA    L9A53                   ;0x02
        blo     LE3BA_16                ;br if L0111 < 0x02 (ie. 0 or 1)

        ldaA    PORTH
        eora    #0x04                   ;%00000100 WL = !WL
        staA    PORTH

        clr     L0111                   ;reset the counter
        bra     LE3BA_16


LE3BA_15:
        ldaA    #0xFF                   ;set L0111 to max val
        staA    L0111

;-----------------------------------------------------------------
LE3BA_16:
        brset   *L0001 #0b01000000 LE3BA_23

        ldaB    *L004D                  ;8.8 RPM50
        cmpB    L900F
        blo     LE3BA_17
        brclr   *L0049_PORTG_debounced, #0b00000001, LE3BA_20          ;PG0: CLT clutch switch
LE3BA_17:
        bset    *L0006 #0b00000001
        ldaA    L0130
        bne     LE3BA_18
        brset   *L0006 #0b00000010 LE3BA_18
        bra     LE3BA_24

LE3BA_18:
        ldaB    #0x00
        staB    L0131
        bclr    *L0006 #0b00000010
        addA    #0x01
        sbcA    #0x00
        staA    L0130
        cmpA    L900D
        bhs     LE3BA_19
        bra     LE3BA_25

LE3BA_19:
        bset    *L0001 #0b00000100
        bclr    *L0001 #0b00000010
        bra     LE3BA_25

LE3BA_20:
        bset    *L0006 #0b00000010
        ldaA    L0131
        bne     LE3BA_21
        brset   *L0006 #0b00000001 LE3BA_21
        bra     LE3BA_24

LE3BA_21:
        ldaB    #0x00
        staB    L0130
        bclr    *L0006 #0b00000001
        addA    #0x01
        sbcA    #0x00
        staA    L0131
        cmpA    L900E
        bhs     LE3BA_22
        bra     LE3BA_25

LE3BA_22:
        bset    *L0001 #0b00000010
        bclr    *L0001 #0b00000100
        bra     LE3BA_25

LE3BA_23:
        bclr    *L0006 #0b00000010
        bclr    *L0006 #0b00000001
LE3BA_24:
        bclr    *L0001 #0b00000010
        bclr    *L0001 #0b00000100
        ldaA    #0x00
        staA    L0130
        staA    L0131

LE3BA_25:
        bclr    *L0012 #0b00000001
        jmp     LC27F


;*****************************************************************
;The crankshaft sensor interrupt.  All crank events come through
;here.

;The logic analyzer shows that the leading edge of a protrusion
;on the rotor causes a falling edge on IC2.  The trailing edge of
;the protrusion causes a rising edge on IC2.

IC2F_ISR:
        ldaA    #0x02                   ;enable IC2F
        staA    TFLG1

        ldaA    TCTL2
        andA    #0b00001100             ;test the Input Capture Edge control bits for IC2
        cmpA    #0x08                   ;is it set to capture a FALLING edge event (leading edge of protrusion)?

; Oops: ran out of branch range here: reverse the test
;       beq     IC2F_ISR_4              ;if so, this interrupt must have been triggered by a falling edge event (the leading edge of a timing protrusion)
        bne     IC2F_ISR_999
        jmp     IC2F_ISR_4              ;if so, this interrupt must have been triggered by a falling edge event
IC2F_ISR_999:

;-----------------------------------------------------------------
;This interrupt must have been triggered by a rising edge event (the trailing edge of a timing protrusion).

        ldaA    TCTL2
        eora    #0b00001100             ;reconfigure the edge control to look for a falling edge event
        staA    TCTL2

        brset   *L000B #0b00001000 IC2F_ISR_2   ;if engine is turning < 400 RPM we fire front on cr5 (rising)
IC2F_ISR_0:
        brset   *L000B #0b00000100 IC2F_ISR_3   ;if engine is turning < 400 RPM we fire rear on cr10 (rising)

IC2F_ISR_1:
        jmp     IC2F_ISR_10             ;all done

;-----------------------------------------------------------------
IC2F_ISR_2:
        ldaA    *L0055                  ;get the current crank reference position
        andA    #0b00001111
        cmpA    #0x05                   ;is this CR5?
        bne     IC2F_ISR_0              ;no

; This is crankRef cr5 (rising).  The Rotax shop manual says that when
; the bike is being started, the ignition spark occurs at 5 degrees
; BTDC.  Since we are firing the spark now during the start condition,
; it would indicate that CR5 (rising) must represent 5 degrees BTDC.

; In either case, we got here because the F coil was charging.  We
; fire it by generating an OC1 event that will drive OC1 to 1, which
; turns off the current charging the F coil.

        brset   *L0004 #0b00000100 IC2F_ISR_0   ;1 means "slow RPM" flag is set
        brset   *L0005 #0b01000000 IC2F_ISR_0

        ldaA    #0x80                   ;PA7/OC1 is affected by OC1 compare events
        staA    OC1M

        ldaA    #0xF8                   ;PA7 will be set to 1 on OC1 compare events (1 == stop charging; fire spark)
        staA    OC1D

        ldaA    TMSK1
        andA    #0b01111111             ;inhibit OC1 interrupts
        staA    TMSK1

        ldD     TCNT                    ;generate an OC1 event 32 uSec into the future
        addD    #0x0010                 ;which will fire the Front coil
        stD     TOC1

        stD     LW+LOGID_ECU_F_COIL_MAN_OFF_TYPE_PTS

        ldaA    #0x80                   ;writing B7 clears the OC1I interrupt request bit
        staA    TFLG1

        bclr    *L000B #0b00001000      ;we have fired the front
        jmp     IC2F_ISR_10             ;all done

;-----------------------------------------------------------------
;We get here if the Rear coil is charging and needs to be fired
;'manually'.

IC2F_ISR_3:
        ldaA    *L0055
        andA    #0b00001111
        cmpA    #0x0A                   ;is this crankRef cr10?
        bne     IC2F_ISR_1

        brset   *L0004 #0b00000100 IC2F_ISR_1   ;1 means "Slow RPM" flag is asserted
        brset   *L0005 #0b01000000 IC2F_ISR_1

        ldaA    TCTL1
        oraA    #0b11000000             ;set OC2 output line to '1' on OC2 compare event
        staA    TCTL1                   ;this will cause the coil to fire

        ldaA    TMSK1
        andA    #0b10111111             ;inhibit OC2 interrupts
        staA    TMSK1

        ldD     TCNT                    ;generate an OC2 event 32 uSec from now
        addD    #0x0010
        stD     TOC2

        stD     LW+LOGID_ECU_R_COIL_MAN_OFF_TYPE_PTS

        ldaA    #0x40                   ;setting B6 clears the OC2 interrupt request flag
        staA    TFLG1

        bclr    *L000B #0b00000100      ;we have fired the rear.
        jmp     IC2F_ISR_10             ;all done

;-----------------------------------------------------------------
;This CR interrupt must have been triggered by a falling edge event
;(the leading edge of a timing protrusion).

IC2F_ISR_4:
        ldaB    PORTJ
        ldaA    *L0055
        bitA    #0b00000001             ;this bit changes 3 times per revolution.  This means that we are
                                        ;driving TAC at current RPM period / 3
        beq     IC2F_ISR_5
        andB    #0b11111110             ;set Tach drive (TAC) to 0
        bra     IC2F_ISR_6

IC2F_ISR_5:
        oraB    #0b00000001             ;set Tach drive (TAC) to 1
IC2F_ISR_6:
        staB    PORTJ
        bset    *L0003 #0b00000100
        bset    *L0000 #0b00001000

;This counter gets incremented on each cam ISR, and cleared right here on every crank ISR.
;If the cam ISR increments the value past a threshold, it deems that the crank sensor is broken.
        clrB
        staB    L0152

        bclr    *L0011 #0b10000000      ;cleared due to this falling edge event on the crank sensor

        brset   *L0004 #0b00000001 IC2F_ISR_7   ;br if DON_FILT says that the bike tipped over
        staB    L010B                   ;bike is upright on this crank event: set L010B = 0

IC2F_ISR_7:
        brset   *L0004 #0b00000100 IC2F_ISR_9   ;br if the 'slow RPM' flag is set

        ldaA    *L0055                  ;get current crankRef
        cmpA    #0x0B
        bhs     IC2F_ISR_8              ;br if current crankref is >= 11 (meaning we don't know where the crank is)

        inc     L0150
        jmp     IC2F_ISR_15             ;Everything is fine. Go increment the crankRef ID value!

;We get here if the crankref ID is > 11.
;If we saw a camshaft event since the last crank ISR, everything is fine, and this really is CR0.
;If not, we are in trouble!
IC2F_ISR_8:
        brset   *L0003 #0b00000001 IC2F_ISR_13  ;br if a cam event occurred since the last crank event, all is good

;Things are not good: we should have seen a cam event since the last crank event!
        ldaA    *L0055
        cmpA    #0x80                   ;Have we *ever* seen a cam event yet? If so, A will be < 0x80
        blo     IC2F_ISR_11             ;br if we have

;We have never seen a cam event since the ECU booted.
;If the crankref counter hits a max value of 0xFF, we decide that the cam sensor is broken
        addA    #0x01
        sbcA    #0x00                   ;Limit the crankshaft event counter 0xFF, max
        staA    *L0055
        cmpA    #0xFF                   ;have we maxed out at 0xFF yet?
        blo     IC2F_ISR_9              ;if not

;If we get here, the crankref event counter incremented from 0x80 to 0xFF without seeing a cam event, the cam sensor is broken.
;127 cranks events means (127/6) or just over 21 full rotations of the crank.
;If the starter cranks at roughly 300 RPM, it would take about 4 seconds of cranking
;without a cam signal for the ECU to decide that the cam sensor was broken:
        bset    *L0011 #0b00010000
        bset    *L000C #0b10000000      ;set the 'cam sensor is broken' bit
        bset    *L000E #0b10000000      ;set the 'cam sensor is broken' sticky bit

IC2F_ISR_9:
        clr     *L0051                  ;why?
        clr     *L0052
        clr     *L0053

IC2F_ISR_10:
        bclr    *L0003 #0b00000001      ;clear the cam-event-since-last-crank-event flag

        ldaA    #0x02                   ;make sure that IC2 events are enabled
        staA    TFLG1
        rti                             ;all done

;-----------------------------------------------------------------
;We get here if:
;       - the crankref went past 11
;       - and we did not see a cam event since the last crank like we should have
;       - but we have seen the cam sensor in the past
;I believe that this code is trusting that the engine still knows where it is even though
;it didn't see a cam event. It marks its unhappiness with the cam sensor by adding 0x30
;to the CRID. So CRID 0x30 is the same as CR0, except that we never did see a cam event
;that would have triggered the normal CR11->CR0 processing.

IC2F_ISR_11:
        andA    #0b00111111
        oraA    #0b00110000
        staA    L015E

        cmpA    #0b00111011
        blo     IC2F_ISR_12     ;br if it has been less than 12 CR periods since the missing cam event

        ldaA    #0b00101111     ;arrange for the CRID to get set to $30
        staA    L015E

IC2F_ISR_12:
        ldaB    L0150
        addB    #0x01
        staB    L0150
        cmpB    #0x0C
        blo     IC2F_ISR_15             ;CRid will be set to A+1
        clr     L0150

        ldaB    L0151                   ;Holds number of crank events since we saw a cam event
        addB    #0x01                   ;increment, but clamp max value to 0xFF
        sbcB    #0x00
        staB    L0151

        ldaA    L015E
        cmpB    L9A55                   ;0x1E
        blo     IC2F_ISR_15             ;CRid will be set to A+1

        bset    *L0011 #0b00010000
        bset    *L000C #0b10000000      ;Mark that the CAM sensor is broken
        bset    *L000E #0b10000000      ;Also mark the sticky copy of L000C
        dec     L0150
        bra     IC2F_ISR_15             ;CRid will be set to A+1

;-----------------------------------------------------------------
;We get here when everything is fine: We know were the crank is, we were expecting to see a CAM event
;and we did see a cam event since the last crank event!
IC2F_ISR_13:
        bset    *L0003 #0b00001000      ;mark that a cam event has occurred since last crank event

        ldaA    *L0011
        andA    #0b11001111
        staA    *L0011

        ldaA    #0x00
        staA    L0150                   ;zero the crank event counter that is used to a detect bad cam sensor
        staA    L0151                   ;we saw a cam event before this crank event so we rezero the counter

        ldaA    *L0055                  ;Have we ever seen a cam signal yet?
        cmpA    #0x80
        blo     IC2F_ISR_14             ;br if so: the cam event signals that it is time to
                                        ;reset the crank posn counter to 0

;If we get here, it means that we just found out where the crank
;was for the first time since booting.  We set the crank posn counter to 0, but
;we also mark the event by setting some flags:
        bset    *L0000 #0b10000000      ;we have achieved initial crank sync
        bset    *L0001 #0b00001000

;If we get here, it's normal operation and all is good: we know that the this ISR represents CR0:
IC2F_ISR_14:
        ldaA    #0xFF                   ;Arrange for crank posn counter to get set to 0

IC2F_ISR_15:
        incA                            ;inc the current crank posn
        staA    *L0055                  ;the only legitimate store to the crank posn

        brclr   *L0000 #0b10000000 IC2F_ISR_16  ;if we achieved initial crank sync more than 1/2 a rotation ago
        cmpA    #0x03
        blo     IC2F_ISR_16             ;br if we haven't seen 3 crank events since finding the crank
        bclr    *L0000 #0b10000000      ;if we have seen 3 events (1/2 a rotation), we clear the initial sync flag

IC2F_ISR_16:
        brclr   *L0001 #0b00001000 IC2F_ISR_17

        cmpA    #0x08
        blo     IC2F_ISR_17             ;br if this is crankRef [cr0..cr7]

        bclr    *L0001 #0b00001000

IC2F_ISR_17:
        andA    #0b00001111
        cmpA    L8028                   ;const 0x04
        bne     IC2F_ISR_18             ;br if not cr4

;CR ISR: This is cr4:
        ldaA    #0x02
        bra     IC2F_ISR_19             ;convert AN3 (PM/MAP)

IC2F_ISR_18:
        cmpA    L8029                   ;const 0x0B
        bne     IC2F_ISR_20             ;br if not cr11

;This is cr4 (start of the REAR cyl intake stroke) or cr11 (start
;of the FRONT cyl intake stroke).  In either case, we measure the
;manifold air pressure:

        ldaA    #0x02                   ;select channel AN3 (PM/MAP)
IC2F_ISR_19:
        ldaB    ADCTL                   ;get the current channel selector
        staB    *L0018                  ;stash it somewhere safe
        staA    ADCTL                   ;start a MAP conversion
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        bset    *L000B #0b00010000      ;a MAP conversion is in progress
        bra     IC2F_ISR_21

IC2F_ISR_20:
        bclr    *L000B #0b00010000      ;a MAP conversion is NOT in progress

; CR ISR: All CR events pass through here.  Figure out how long this
; last CR period was.  The range of time we can get from the hardware
; timer is 2**16 * 2 uSec or 131 mSec.  To see if we overflowed the
; counter, we use a counter that increments in the 5 msec scheduler.
; If it indicates overflow, we set the period to MAX (0xFFFF).

IC2F_ISR_21:
        ldD     TIC2
        stD     L015E                   ;L015E (a temp) holds the time of this IC2 event

        ldaA    *L0054
        cmpA    #0x21                   ;if 0x21 represents 33 5 mSec counts this would be 165 mSec
        blo     IC2F_ISR_22             ;br if L0054 < 0x21

        staA    LW+LOGID_ECU_CRANK_P6_MAX_TYPE_V
        bra     IC2F_ISR_23

IC2F_ISR_22:
        ldD     L015E                   ;
        subD    *L0059                  ;CR Period = current_IC2_count - prev_IC2_count

IC2F_ISR_23:
        stD     L0160                   ;the delta would be the period of the last 1/6 crank rotation
        cmpA    #0x01
        bhs     IC2F_ISR_24             ;>=

        jmp     IC2F_ISR_10             ;all done: leave the ISR


IC2F_ISR_24:
        ldD     L015E                   ;get the current_IC2_count
        stD     *L0059                  ;  - save it as start time for the current crank ref
        stD     LW+LOGID_ECU_CRANKREF_START_TYPE_TS

        ldaA    *L0055                  ;get the current crank ID and log it
        staA    LW+LOGID_ECU_CRANKREF_ID_TYPE_U8
        cmpA    #0x00
        beq     IC2F_ISR_27             ;if crankRef == cr0
        decA
        beq     IC2F_ISR_25             ;if crankRef == cr1
        decA
        beq     IC2F_ISR_26             ;if crankRef == cr2
        decA
        beq     IC2F_ISR_27             ;if crankRef == cr3
        decA
        beq     IC2F_ISR_25             ;if crankRef == cr4
        decA
        beq     IC2F_ISR_26             ;if crankRef == cr5
        decA
        beq     IC2F_ISR_27             ;if crankRef == cr6
        decA
        beq     IC2F_ISR_25             ;if crankRef == cr7
        decA
        beq     IC2F_ISR_26             ;if crankRef == cr8
        decA
        beq     IC2F_ISR_27             ;if crankRef == cr9
        decA
        beq     IC2F_ISR_25             ;if crankRef == cr10

        bra     IC2F_ISR_26             ;must be crankRef cr11

;By saving the 1/6 periods for each of sets of cranks ID's as
;below, we can calc a crank period for the last half rotation by
;adding up the last 3 1/6 periods.

;If A in [1,4,7,10]:
IC2F_ISR_25:
        ldD     L0160                   ;L0160 holds the 1/6 crank period in 2 uSec ticks
        stD     *L00D2                  ;
        stD     *L00D0                  ;update the 1/6 rotation period
        lsrD
        addD    *L00D0
        stD     *L00CE                  ;L00CE = 1/6 crank period * 1.5, or the last 90 degrees of rotation

        ldaA    *L0051                  ;transfer L0051 to L0054, then clear L0051
        staA    *L0054
        clrA
        staA    *L0051
        bra     IC2F_ISR_28

;if A in [2,5,8,[11..15]]
IC2F_ISR_26:
        ldD     L0160
        stD     *L00D4
        stD     *L00D0                  ;update the 1/6 rotation period
        lsrD
        addD    *L00D0
        stD     *L00CE                  ;L00CE = 1/6 crank period * 1.5, or the last 90 degrees of rotation

        ldaA    *L0052                  ;transfer L0052 to L0054, then clear L0052
        staA    *L0054
        clrA
        staA    *L0052
        bra     IC2F_ISR_28

;If A in [0,3,6,9]
IC2F_ISR_27:
        ldD     L0160
        stD     *L00D6                  ;update the 1/6 rotation period
        stD     *L00D0
        lsrD
        addD    *L00D0
        stD     *L00CE                  ;L00CE = 1/6 crank period * 1.5, or the last 90 degrees of rotation

        ldaA    *L0053                  ;transfer L0053 to L0054, then clear L0053
        staA    *L0054
        clrA
        staA    *L0053

IC2F_ISR_28:
        ldaA    *L00D0                  ;get MS byte of current 1/6 period
        brset   *L0000 #0b00000001 IC2F_ISR_30
        brclr   *L0000 #0b00000010 IC2F_ISR_29
        cmpA    L9A4D                   ;0x44 0x4400 would be a 1/6 period corresponding to 575 RPM
        blo     IC2F_ISR_29             ;br if period is less than that (i.e. we are going faster than 575 RPM)

        bset    *L0004 #0b00000100      ;the 1/6 crank period indicates that the engine is turning less than 575 RPM
        clr     L010D                   ;restart the 1 second timer that clears the above flag

        jsr     LC7D2                   ;disaster: we discard everything we know about the engine params and reinit
        jmp     IC2F_ISR_10             ;all done: leave the ISR

IC2F_ISR_29:
        ldaB    L014F
        cmpB    L9A50                   ;0x28 (40.)
        blo     IC2F_ISR_32             ;br if this we have ?performed less than 40 of the 1/6 period calcs?

        bclr    *L0005 #0b01000000
        bra     IC2F_ISR_32

IC2F_ISR_30:
        cmpA    L9A4F                   ;0x27 0x2700 would be a period corresponding to 1000 RPM
        blo     IC2F_ISR_31             ;br if the period is less than that (i.e. we are going faster than 1000 RPM)

        ldaB    L014F
        cmpB    L9A50                   ;0x28 (40.)
        bhs     IC2F_ISR_31
        bset    *L0005 #0b01000000
        bra     IC2F_ISR_32

IC2F_ISR_31:
        bclr    *L0005 #0b01000000
        bclr    *L0004 #0b00000100      ;instantly clear the Slow RPM flag if we ever see the engine turn over 1000 RPM

IC2F_ISR_32:
        bset    *L0002 #0b00000100      ;this schedules a routine in the mainloop every time we see a crank ref pulse
        clr     L010E
        bclr    *L0003 #0b00000001
        brset   *L0000 #0b00000001 IC2F_ISR_34
        ldaA    L0146
        cmpA    L9A58
        bhs     IC2F_ISR_34
IC2F_ISR_33:
        jmp     IC2F_ISR_49

IC2F_ISR_34:
        brset   *L0000 #0b10000000 IC2F_ISR_33  ;br if this is the first "tracked" rotation of the crank
        brset   *L0001 #0b00001000 IC2F_ISR_33
        brset   *L0005 #0b01000000 IC2F_ISR_33
        brset   *L0004 #0b00000100 IC2F_ISR_33  ;1 means "Slow RPM" flag is asserted

        ldaB    *L0055
        andB    #0b00001111
        cmpB    #0x06
        beq     IC2F_ISR_35             ;br if crankRef == cr6

        cmpB    #0x0B
        beq     IC2F_ISR_42             ;br if crankRef == cr11

        jmp     IC2F_ISR_50

;-----------------------------------------------------------------
; This is crankRef CR6 (midway through front cyl power stroke)
; Verify that both FRONT coils generated sparks prior to CR6, i.e. during CR4 or CR5.
;
; As per the circuit diagrams, a spark event on either the front or rear #1 coil
; will trigger IC3, and a spark event on either the front or rear #2 coil
; will trigger IC4. We can figure out if it was a front coil by testing
; for the input capture event in either CR6. Tests for rear coils would occur
; in CR11.

IC2F_ISR_35:
        brclr   *L0005 #0b00010000 IC2F_ISR_33  ;(actually br's to IC2F_ISR_49) br if there was not TOC1 front coil charging event scheduled
        bclr    *L0005 #0b00010000      ;there was, but now indicate that TOC1 'start charging' event is NOT activated

;CR6 Front Cyl Part 1: Test if coil #1 fired (front or rear):
#if !defined SPARK_ISR
        ldaA    TFLG1
#else
        ldaA    FAKE_TFLG1
#endif
        bitA    #0b00000001             ;tests IC3F: if set, we saw a rising edge (coil IGX1 fired: must have been the front one)
        bne     IC2F_ISR_36             ;br if IC3F==1

;no spark fired:
        ldaA    L0147                   ;else increment the IC3 error count
        addA    #0x01
        sbcA    #0x00
        staA    L0147

         cmpA   #0x01
         bne    1f
         ldaB   #11                     ; we didn't see front coil #1 fire
         staB   LW+LOGID_ECU_NOSPARK_TYPE_U8
1:
        cmpA    #0x02
        blo     IC2F_ISR_38             ;having < 2 errors is OK

;Having >= 2 errors is bad:
        bset    *L000D #0b00010000      ;set the 'bad ign coil IG11' error bit (the only place that sets this bit)
        bset    *L000F #0b00010000      ;sets the 'sticky' error bit, too
        bra     IC2F_ISR_38

; If we get here, we saw a spark on the front cyl plug #1 prior to this active period CR6:
IC2F_ISR_36:
#if !defined SPARK_ISR
         ldD    TIC3                    ; log the timestamp when coil #1 fired
         stD    LW+LOGID_ECU_SPRK_X1_TYPE_PTS
#endif
        ldaA    L0147                   ;had we seen an error previously?
        bne     IC2F_ISR_37             ;if so, then don't clear the error indicator just yet

        bclr    *L000D #0b00010000      ;no error on previous cycle: clear the error indicator
        bra     IC2F_ISR_38

IC2F_ISR_37:
        clr     L0147


;CR6 Front Cyl Part 2: Test coil IGX2:
IC2F_ISR_38:
#if !defined SPARK_ISR
        ldaA    TFLG1
#else
        ldaA    FAKE_TFLG1
#endif
        bitA    #0b00001000             ;test IC4: if set, we saw a rising edge (coil IGX2 fired: must have been the front one)
        bne     IC2F_ISR_39             ;br if it is a '1'

        ldaA    L0148                   ;else increment the IC4 error count
        addA    #0x01
        sbcA    #0x00
        staA    L0148

         cmpA   #0x01
         bne    1f
         ldaB   #12                     ; we didn't see front coil #2 fire
         staB   LW+LOGID_ECU_NOSPARK_TYPE_U8
1:
        cmpA    #0x02
        blo     IC2F_ISR_41             ;having < 2 errors is OK

;Having >= 2 errors is bad:
        bset    *L000D #0b00001000      ;set the 'bad ign coil IG12' error bit (the only place that sets this bit)
        bset    *L000F #0b00001000      ;sets the 'sticky' error bit, too
        jmp     IC2F_ISR_48             ; was 'bra' but out of range after adding spark logging

; If we get here, we saw a spark on the front cyl plug #2 prior to this active period CR6:
IC2F_ISR_39:
#if !defined SPARK_ISR
         ldD    TO5I4                   ; log the timestamp when coil #2 fired
         stD    LW+LOGID_ECU_SPRK_X2_TYPE_PTS
#endif
        ldaA    L0148                   ;had we seen an error previously?
        bne     IC2F_ISR_40             ;if so, then don't clear the error indicator just yet

        bclr    *L000D #0b00001000      ;no error on previous cycle: clear the error indicator
        bra     IC2F_ISR_48

IC2F_ISR_40:
        clr     L0148

IC2F_ISR_41:
        bra     IC2F_ISR_48

;-----------------------------------------------------------------
; This is crankRef CR11 (midway through rear cyl power stroke)
; Verify that both FRONT coils generated sparks prior to CR6, i.e. during CR4 or CR5.

IC2F_ISR_42:
        brclr   *L0006 #0b00000100 IC2F_ISR_49  ;br if the REAR coil did NOT have a charge event registered

        bclr    *L0006 #0b00000100      ;clear the last charge event

;CR11 Rear Cyl Part 1: Test coil IG21:
#if !defined SPARK_ISR
        ldaA    TFLG1
#else
        ldaA    FAKE_TFLG1
#endif
        bitA    #0b00000001             ;tests IC3F: if set, we saw a rising edge (coil IGX1 fired: must have been the rear one)
        bne     IC2F_ISR_43             ;br if it is a '1'

        ldaA    L0149                   ;else increment the IC3 error count
        addA    #0x01
        sbcA    #0x00
        staA    L0149

         cmpA   #0x01
         bne    1f
         ldaB   #21                     ; we didn't see rear coil #1 fire
         staB   LW+LOGID_ECU_NOSPARK_TYPE_U8
1:
        cmpA    #0x02
        blo     IC2F_ISR_45             ;having < 2 errors is OK

;Having >= 2 errors is bad:
        bset    *L000D #0b00000100      ;set the 'bad REAR ign coil IG21' error bit (the only place that sets this bit)
        bset    *L000F #0b00000100      ;sets the 'sticky' error bit, too
        bra     IC2F_ISR_45

; If we get here, we saw a spark on the rear cyl plug #1 prior to this active period CR11:
IC2F_ISR_43:
#if !defined SPARK_ISR
         ldD    TIC3                    ; log the timestamp when coil #1 fired
         stD    LW+LOGID_ECU_SPRK_X1_TYPE_PTS
#endif
        ldaA    L0149                   ;had we seen an error previously?
        bne     IC2F_ISR_44             ;if so, then don't clear the error indicator just yet

        bclr    *L000D #0b00000100      ;no error on previous cycle: clear the error indicator
        bra     IC2F_ISR_45

IC2F_ISR_44:
        clr     L0149

;CR11 Rear Cyl Part 2: Test coil IG22:
IC2F_ISR_45:
#if !defined SPARK_ISR
        ldaA    TFLG1
#else
        ldaA    FAKE_TFLG1
#endif
        bitA    #0b00001000             ;test IC4: if set, we saw a rising edge (coil IGX2 fired: must have been the rear one)
        bne     IC2F_ISR_46             ;br if it is a '1'

        ldaA    L014A                   ;else increment the IC3 error count
        addA    #0x01
        sbcA    #0x00
        staA    L014A

         cmpA   #0x01
         bne    1f
         ldaB   #0x22                   ; we didn't see rear coil #2 fire
         staB   LW+LOGID_ECU_NOSPARK_TYPE_U8
1:
        cmpA    #0x02
        blo     IC2F_ISR_48             ;having < 2 errors is OK

        bset    *L000D #0b00000010      ;set the 'bad REAR ign coil IG22' error bit (the only place that sets this bit)
        bset    *L000F #0b00000010      ;sets the 'sticky' error bit, too
        bra     IC2F_ISR_48

; If we get here, we saw a spark on the rear cyl plug #2 prior to this active period CR11:
IC2F_ISR_46:
#if !defined SPARK_ISR
         ldD    TO5I4                   ; log the timestamp when coil #2 fired
         stD    LW+LOGID_ECU_SPRK_X2_TYPE_PTS
#endif
        ldaA    L014A                   ;had we seen an error previously?
        bne     IC2F_ISR_47             ;if so, then don't clear the error indicator just yet

        bclr    *L000D #0b00000010      ;no error on previous cycle: clear the error indicator
        bra     IC2F_ISR_48

IC2F_ISR_47:
        clr     L014A

IC2F_ISR_48:
#if !defined SPARK_ISR
        ldaA    #0x09
        staA    TFLG1                   ;clear the IC3F and IC4F interrupt request bits.
#else
        clr     FAKE_TFLG1
#endif
        jmp     IC2F_ISR_64

;-----------------------------------------------------------------

IC2F_ISR_49:
        ldaB    *L0055                  ;read the current crankRef
        andB    #0b00001111

IC2F_ISR_50:
        cmpB    #0x04
        beq     IC2F_ISR_51             ;br if cr4
        cmpB    #0x09
        beq     IC2F_ISR_57             ;br if cr9

        jmp     IC2F_ISR_64             ;all other crankRef positions

;-----------------------------------------------------------------
;We get here if this is a cr4 crank event.
IC2F_ISR_51:
        ldD     TCNT
        nop
        subD    TCNT2
        addD    #0x0003

; Calculate the tick-time when we want to fire the spark.  This is
; CR4, which by my reckoning/measurement starts at 72 degrees BTDC.
; This is because I believe that the trailing edge of CR5 is 5 degrees
; BTDC, and by measurement, the timing protrusion is 7 degrees wide.

; The fire event time = startOfCurrentCR + (time for most recent 90 degrees) - L00E9
        addD    *L0059                  ;the start time of this crank period
        addD    *L00CE                  ;holds (1/6th crank period * 1.5), or time to rotate 90 degrees
        subD    *L00E9                  ;subtract the 'advance', ie. how much earlier to fire than 90 degrees from now
                                        ;A smaller value for L00E9 == higher advance; larger values == less advance.
        stD     *L00FF

        bclr    *L0003 #0b00000001      ;clear the 'cam event observed' flag

        brset   *L0000 #0b01000000 IC2F_ISR_52  ;
        jmp     IC2F_ISR_218

IC2F_ISR_52:
        brset   *L0004 #0b00000100 IC2F_ISR_53  ;1 means "Slow RPM" flag is asserted
        brset   *L0005 #0b01000000 IC2F_ISR_53
        brset   *L0007 #0b10000000 IC2F_ISR_53  ;If set the injectors are disabled
        bra     IC2F_ISR_54

IC2F_ISR_53:
        bclr    *L0005 #0b00010000      ;indicate that TOC1 'start charging' event is NOT activated
        jmp     IC2F_ISR_105

IC2F_ISR_54:
        ldaA    #0x80
        staA    TFLG1
        ldD     TCNT                    ;get the current time
        addD    #0x000C                 ;add 24 uSec
        stD     TOC1                    ;arrange to start charging the coil when the 24 uSec delay expires
        ldaA    #0x7F                   ;PA7=0 on OC1 match event (0 == start charging)
        staA    OC1D

; This confuses me: we are in an interrupt routine, so we can't
; service the interrupt we just created until we leave this interrupt
; handler?  Maybe it makes sense: this routine starts the coil
; charging (as soon as the OC1 count matches), and then when we get
; around to it, the OC1 ISR programs the coil fire time.

        ldaA    TMSK1
        oraA    #0b10000000             ;enable OC1 ints.
        staA    TMSK1

        ldaA    #0x80                   ;PA7/OC1 is affected by OC1 compare events
        staA    OC1M

        bset    *L0005 #0b00010000      ;indicate that the next TOC1 event will start charging the coil

        ldaA    *L00D0                  ;get the most recent 1/6 crank rotation period
        cmpA    L91AC                   ;0x61 == 0x6100 -> 200 RPM
        bhs     IC2F_ISR_55             ;br if engine is turning slower than 200 RPM

        cmpA    L91AD                   ;0x24 == 0x2400 -> 540 RPM
        bhs     IC2F_ISR_56             ;br if the engine is turning slower than 540 RPM (i.e. between [200..540])

;The engine must be turning over 540 RPM:
        bclr    *L000B #0b00000010      ;note that the engine did make it over 540 RPM
        jmp     IC2F_ISR_105

;Engine is turning < 200 RPM
IC2F_ISR_55:
        bset    *L000B #0b00001000      ;schedule a spark event on cr5 (rising)
        bset    *L000B #0b00000010      ;engine is not turning over 540 RPM
        jmp     IC2F_ISR_105

;Engine is turning between [200..540] RPM.  If the engine has
;never been observed to spin faster than 540 RPM yet, schedule
;another 'slow' (post TDC) ignition event.

IC2F_ISR_56:
        brset   *L000B #0b00000010 IC2F_ISR_55  ;we have not made it past 540 RPM yet
        jmp     IC2F_ISR_105

;-----------------------------------------------------------------
;We get here if this is a cr9 crank event.
IC2F_ISR_57:
        ldD     TCNT                    ;what is this doing?
        nop
        subD    TCNT2
        addD    #0x0003

        addD    *L0059                  ;the start time of this crank period
        addD    *L00CE
        subD    *L00EB
        stD     L0101                   ;L0101 is stored (later) as the TOC2 value.  TOC2 is rear ign coil driver.

        bclr    *L0003 #0b00000001
        brset   *L0000 #0b01000000 IC2F_ISR_58
        jmp     IC2F_ISR_223

IC2F_ISR_58:
        brset   *L0004 #0b00000100 IC2F_ISR_59  ;1 means "Slow RPM" flag is asserted
        brset   *L0005 #0b01000000 IC2F_ISR_59
        brset   *L0007 #0b01000000 IC2F_ISR_59  ;if set the ign is disabled
        bra     IC2F_ISR_61

IC2F_ISR_59:
        bclr    *L0006 #0b00000100      ;clear any outstanding 'rear coil is charging' event flag
IC2F_ISR_60:
        jmp     IC2F_ISR_105

IC2F_ISR_61:
        ldaA    #0x40                   ;setting B6 clears any outstanding OC2 interrupt request flag
        staA    TFLG1

        ldD     TCNT
        addD    #0x000C
        stD     TOC2
        ldaA    TCTL1
        oraA    #0b10000000
        andA    #0b10111111
        staA    TCTL1                   ;10xxxxxx means OC2 will be driven to '0' on OC2 match

        ldaA    TMSK1
        oraA    #0b01000000             ;enable OC2 interrupts (rear spark)
        staA    TMSK1

        bset    *L0006 #0b00000100      ;1==rear coil 'start charging' event has been scheduled
        ldaA    *L00D0
        cmpA    L91AC                   ;0x61 == 0x6100 -> 200 RPM
        bhs     IC2F_ISR_62
        cmpA    L91AD                   ;0x24 == 0x2400 -> 540 RPM
        bhs     IC2F_ISR_63
        bclr    *L000B #0b00000001
        bra     IC2F_ISR_60

IC2F_ISR_62:
        bset    *L000B #0b00000100
        bset    *L000B #0b00000001
        bra     IC2F_ISR_60


IC2F_ISR_63:
        brset   *L000B #0b00000010 IC2F_ISR_62  ;BUG: I think this should test %00000001
        bra     IC2F_ISR_60

;-----------------------------------------------------------------
;Done with the ignition coil testing.
;When we get here, the crankRef ID is in reg B.
;unfinished
IC2F_ISR_64:
        ldaA    *L00D0                  ;get the MSB of the most recent 1/6 crank period

        cmpA    L8030                   ;0x5B == 0x5B00 -> 215 RPM
        bhs     IC2F_ISR_65             ;br if period is >=, meaning RPM is < 215

        cmpA    L8031                   ;0x1A == 0x1A00 -> 750 RPM
        bhs     IC2F_ISR_66             ;br if period is >=, meaning RPM is < 750

        bclr    *L0001 #0b01000000      ;RPM is over 750: clear the low RPM flag
        bra     IC2F_ISR_66


IC2F_ISR_65:
        bset    *L0001 #0b01000000      ;mark that RPM is below 215 RPM

IC2F_ISR_66:
        cmpA    L91A9                   ;0x1B == 0x1B00 -> 725 RPM
        bhs     IC2F_ISR_69             ;br if period is >=, meaning RPM is < 725

        cmpB    #0x0B                   ;is this CR11?
        bne     IC2F_ISR_67             ;if not

        bclr    *L0000 #0b01000000      ;?

IC2F_ISR_67:
        cmpA    L91AA                   ;0x12 == 0x1200 -> 1085 RPM
        blo     IC2F_ISR_68

        brset   *L0000 #0b00000001 IC2F_ISR_69

IC2F_ISR_68:
        cmpB    #0x0B                   ;is this CR11?
        bne     IC2F_ISR_70             ;if not

        bclr    *L0000 #0b00000100
        bra     IC2F_ISR_70


IC2F_ISR_69:
        bset    *L0000 #0b01000000
        bset    *L0000 #0b00000100

        ldaA    #0x80
        staA    *L00E2

IC2F_ISR_70:
        cmpB    #0x03                   ;is this CR3?
        bne     IC2F_ISR_71
        jmp     IC2F_ISR_80             ;if so

IC2F_ISR_71:
        cmpB    #0x08                   ;is this CR8?
        bne     IC2F_ISR_72
        jmp     IC2F_ISR_90             ;if so

IC2F_ISR_72:
        cmpB    #0x0A                   ;is this CR10?
        beq     IC2F_ISR_73             ;if so
        jmp     IC2F_ISR_94             ;all others go here

; -----------------------------------------------------------------
;This is CR10 (Approximately at the end of the front cyl exhaust stroke)
IC2F_ISR_73:
        brclr   *L0000 #0b01000100 IC2F_ISR_74  ;br if we know where things are and ign processing is normal
        bra     IC2F_ISR_79

;Things are good.
IC2F_ISR_74:
        brset   *L0004 #0b00000100 IC2F_ISR_78  ;1 means "Slow RPM" flag is asserted
        brset   *L0005 #0b01000000 IC2F_ISR_78
        brset   *L0007 #0b10000000 IC2F_ISR_78  ;if set, the injectors are disabled

        ldaA    #0x80                   ;PA7/OC1 is affected by OC1 compare events
        staA    OC1M
        ldaA    #0x7F                   ;PA7=0 on OC1 match event (0 == start charging)
        staA    OC1D
        ldaA    TMSK1
        oraA    #0b10000000             ;enable OC1 interrupts (front cyl ign coil driver)
        staA    TMSK1

        ldD     *L00D8                  ;the most recent 1/2 crank period
        cmpA    #0x40                   ;0x4000 16384 ticks or 915 RPM
        bhs     IC2F_ISR_76             ;br if most recent 1/2 crank period >= 915 RPM

        lslD
        addD    *L00CE                  ;time for the last 90 degrees of rotation
        blo     IC2F_ISR_76
        subD    *L00E9
        subD    *L00ED
        blo     IC2F_ISR_75
        cmpD    *L00D0                  ;most recent 1/6 crankperiod
        bhs     IC2F_ISR_77
IC2F_ISR_75:
        ldD     *L00D0                  ;most recent 1/6 crankperiod,ie. how long did the last CR event take?
        bra     IC2F_ISR_77

IC2F_ISR_76:
        ldD     #0xFFFF

IC2F_ISR_77:
        stD     *L00EF                  ;store in a temp
        ldD     TCNT
        nop
        subD    TCNT2
        addD    #0x0003
        addD    *L0059                  ;the start time of this crank period
        addD    *L00EF                  ;add from the temp
        stD     TOC1                    ;TOC1 is the front cyl ign coil driver

        bset    *L0005 #0b00010000      ;indicate that the next TOC1 event will start charging the coil
        jmp     IC2F_ISR_94


IC2F_ISR_78:
        bclr    *L0005 #0b00010000      ;indicate that there is no TOC1 'start charging' event scheduled
IC2F_ISR_79:
        jmp     IC2F_ISR_94

; -----------------------------------------------------------------
; This is CR3 (compression cycle of front cyl)
IC2F_ISR_80:
        ldD     *L00CE                  ;holds (1/6 crank period * 1.5), or the time for the last 90 degrees of rotation
        ldX     #L00DC
        jsr     LF3E4                   ;Performs D8.8 = D8.8 * 0[X]0.8
        stD     *L00E9                  ;this is the delay (in timer ticks) that defines the spark advance for the FRT cyl

        ldaB    *L00DC                  ;log the ignition delay in terms of a fraction of 90 degrees
        staB    LW+LOGID_ECU_F_IGN_DLY_TYPE_0P8

;In rough terms, *L00E8 is based strictly on RPM:
;  At 800RPM:0x00, 1200:0x18, ... , over 6000RPM, values gradually increase from about 0x80 to 0x8C.
        ldaB    *L00E8
        clrA
        addD    *L00E9
        subD    LC056                   ;const 0x007D
        bhs     IC2F_ISR_81             ;if result >= 0

        ldD     #0x0000                 ;else underflow: peg to 0
IC2F_ISR_81:
        stD     *L00E9
        ldD     *L00CE                  ;time in ticks for the last 90 degrees of rotation
        subD    *L00E9
        bhs     IC2F_ISR_82

        ldD     #0x0000
IC2F_ISR_82:
        cmpD    #0x009B
        bhs     IC2F_ISR_83

        ldD     *L00CE                  ;time in ticks for the last 90 degrees of rotation
        subD    #0x009B
        stD     *L00E9

IC2F_ISR_83:
        ldD     *L00E9
        jsr     LF1A1
        stD     *L00F3
        brclr   *L0000 #0b01000100 IC2F_ISR_84  ;br if we are doing 'normal' ignition processing
        bra     IC2F_ISR_89

IC2F_ISR_84:
        brset   *L0004 #0b00000100 IC2F_ISR_88  ;1 means "Slow RPM" flag is asserted
        brset   *L0005 #0b01000000 IC2F_ISR_88
        brset   *L0007 #0b01000000 IC2F_ISR_88  ;if set, the ign is disabled

        ldaA    TCTL1
        oraA    #0b10000000             ;10xxxxxx means OC2 will be driven to '0' on OC2 match (starts coil charging)
        andA    #0b10111111
        staA    TCTL1
        ldaA    TMSK1
        oraA    #0b01000000             ;enable OC2 interrupts
        staA    TMSK1

        ldD     *L00D8                  ;the 1/2 crank period over the last 1/2 rotation
        cmpA    #0x40                   ;0x4000 == 915 RPM
        bhs     IC2F_ISR_86             ;br if current RPM >= 915

; Engine is running below 915 RPM
        lslD                            ;calc full crank period
        addD    *L00CE                  ;plus 90 degrees
        blo     IC2F_ISR_86             ;br on overflow: will peg to 0xFFFF

        subD    *L00EB
        subD    *L00ED
        blo     IC2F_ISR_85
        cmpD    *L00D0
        bhs     IC2F_ISR_87
IC2F_ISR_85:
        ldD     *L00D0
        bra     IC2F_ISR_87

IC2F_ISR_86:
        ldD     #0xFFFF

IC2F_ISR_87:
        stD     *L00F1                  ;used as a temp

;There are 4 E-clocks per timer tick (E = 2 MHz)
;When counting at E-clk rate (prescale 1), this produces (time N) - (time N+6)
;  4 possible count sequences (vertically, with a 4-bit counter example):
;  0010    0010    0010   0010
;  0010    0010    0010   0011
;  0010    0010    0011   0011
;  0010    0011    0011   0011
;  0011    0011    0011   0011
;  0011    0011    0011   0100
;  0011    0011    0100   0100


        ldD     TCNT                    ;5 D = count at time N (count is latched on cycle 4 of 5 in this instr
                                        ;because the HC11 latches the count on the read of the high byte)
        nop                             ;count goes up another 2 cycles
        subD    TCNT2                   ;value to be subtracted is obtained on cycle 4 of this instr,
                                        ;which is 7 cycles after the initial read

; This means that D must contain either the value -1 or -2 depending
; on where the execution stream is in relation to the counter ticking.
; Is this possibly trying to ensure that TOC2 will trigger
; appropriately if it turns out to match as soon as we store it?

        addD    #0x0003
        addD    *L0059                  ;6 the start time of this crank period
        addD    *L00F1                  ;6
        stD     TOC2                    ;5 until the moment of the store (TOC2 is the rear Ign Coil driver)

        bset    *L0006 #0b00000100      ;1==rear coil 'start charging' event has been scheduled
        bra     IC2F_ISR_94

IC2F_ISR_88:
        bclr    *L0006 #0b00000100      ;0==rear coil 'start charging' event has NOT been scheduled
        bra     IC2F_ISR_94

IC2F_ISR_89:
        bra     IC2F_ISR_94

; -----------------------------------------------------------------
; This is CR8 (compression cycle rear cyl)
IC2F_ISR_90:
        ldD     *L00CE
        ldX     #L00DF
        jsr     LF3E4                   ;D8.8 = D8.8 * 0[X]0.8
        stD     *L00EB

        ldaB    *L00DF                  ;log the ignition delay in terms of a fraction of 90 degrees
        staB    LW+LOGID_ECU_R_IGN_DLY_TYPE_0P8

        ldaB    *L00E8
        clrA
        addD    *L00EB
        subD    LC056
        bhs     IC2F_ISR_91
        ldD     #0x0000
IC2F_ISR_91:
        stD     *L00EB
        ldD     *L00CE
        subD    *L00EB
        bhs     IC2F_ISR_92
        ldD     #0x009B
IC2F_ISR_92:
        cmpD    #0x009B
        bhs     IC2F_ISR_93
        ldD     *L00CE
        subD    #0x009B
        stD     *L00EB
IC2F_ISR_93:
        ldD     *L00EB
        jsr     LF1A1
        stD     *L00F9

; -----------------------------------------------------------------
; The following CR sub-handlers arrive back here again after completing their own stuff:
;    CR3, CR8, CR10
; The following CR events get here even though they had nothing else to do:
;    ?


IC2F_ISR_94:
        brset   *L0004 #0b00000100 IC2F_ISR_95  ;1 means "Slow RPM" flag is asserted
        brset   *L0005 #0b01000000 IC2F_ISR_102

        ldaB    *L0055
        andB    #0b00001111
        cmpB    #0x0A
        beq     IC2F_ISR_99             ;br if crankRef == cr10

        cmpB    #0x05
        beq     IC2F_ISR_96             ;br if crankRef == cr5

IC2F_ISR_95:
        bra     IC2F_ISR_102

; -----------------------------------------------------------------
;We get here due to the leading edge of the CR5 protrusion (tail end of compression stroke frt cyl).

;It is my estimation that the leading edge of CR5 is 12 degrees BTDC.
IC2F_ISR_96:
        brclr   *L000B #0b00001000 IC2F_ISR_97
        ldaA    TCTL2
        eora    #0b00001100
        staA    TCTL2                   ;Schedule an additional interrupt on the rising (trailing) edge of CR5
        bra     IC2F_ISR_103

IC2F_ISR_97:
        ldaA    PORTA
        bitA    #0b10000000             ;PA7 is the output driving the front ign coils (0 == charging)
        bne     IC2F_ISR_102            ;br if we are NOT currently driving the front ign coil

        ldaA    *L00DC                  ;the 0.8 fraction of 90 degrees that represents the ign advance
        cmpA    #0x55                   ;represents 0.0x55 or .332 * 90 degrees, or 30 degrees of rotation at current RPM
        bhs     IC2F_ISR_98             ;if L00DC >= 0x55, then fire the spark now.

        brclr   *L0000 #0b01000100 IC2F_ISR_102 ;br if everything is normal (both bits will be '0' under those circumstances)
                                        ;else fall through to fire the spark immediately

;Fire the spark right now.
IC2F_ISR_98:
        ldaA    #0x80                   ;writing B7 clears the OC1I interrupt request bit
        staA    TFLG1

        ldD     TCNT                    ;Arrange for a OC1 interrupt 20 uSec (40 Eclk) from now (will fire front cyl)
        addD    #0x000A                 ;4
        stD     TOC1                    ;5 Front ign coil driver event time

        ldaA    #0xF8                   ;2 PA7=1 on OC1 match event (1 == stop charging)
        staA    OC1D                    ;4
        ldaA    #0x80                   ;2 PA7/OC1 is affected by OC1 compare events
        staA    OC1M                    ;4
        bra     IC2F_ISR_102            ;3 total 25 to this point

; -----------------------------------------------------------------
; We get here during CR10 (tail end of compression stroke rear cyl)
IC2F_ISR_99:
        brclr   *L000B #0b00000100 IC2F_ISR_100
        ldaA    TCTL2
        eora    #0b00001100
        staA    TCTL2
        bra     IC2F_ISR_103

IC2F_ISR_100:
        ldaA    *L00DF
        cmpA    #0x55
        bhs     IC2F_ISR_101
        brclr   *L0000 #0b01000100 IC2F_ISR_102
IC2F_ISR_101:
        ldaA    PORTA
        bitA    #0b01000000
        bne     IC2F_ISR_102
        ldaA    #0x40
        staA    TFLG1
        ldD     TCNT                    ;Arrange for a OC2 interrupt 20 uSec from now (will fire rear cyl)
        addD    #0x000A
        stD     TOC2
        ldaA    TCTL1
        oraA    #0b11000000
        staA    TCTL1

;both cr5/cr10 get here
IC2F_ISR_102:
        brclr   *L0000 #0b01000000 IC2F_ISR_104
IC2F_ISR_103:
        ldaA    TMSK1
        andA    #0b01111111             ;disable OC1 interrupts
        staA    TMSK1
        ldaA    #0x80                   ;PA7/OC1 is affected by OC1 compare events
        staA    OC1M
        ldaA    TMSK1
        andA    #0b10111111             ;disable OC2 interrupts
        staA    TMSK1
        ldaA    #0x40
        staA    TFLG1
        jmp     IC2F_ISR_105

IC2F_ISR_104:
        ldaA    TMSK1
        oraA    #0b10000000             ;enable OC1 ints
        staA    TMSK1
        ldaA    #0x80                   ;PA7/OC1 is affected by OC1 compare events
        staA    OC1M
        ldaA    TMSK1
        oraA    #0b01000000             ;enable OC2 interrupts
        staA    TMSK1
        ldaA    #0x40
        staA    TFLG1

IC2F_ISR_105:
        brset   *L000B #0b00010000 IC2F_ISR_106 ;br if a MAP conversion is in progress
        jmp     IC2F_ISR_121

;-----------------------------------------------------------------
;We get here if a MAP conversion was in progress.
IC2F_ISR_106:
        ldaA    ADCTL                   ;check on the AN3 PM/MAP conversion we started back at IC2F_ISR_19
        bitA    #0b10000000             ;is it done?
        bne     IC2F_ISR_107            ;if so

        bset    *L000B #0b00100000      ;else mark that the MAP is invalid
        bra     IC2F_ISR_108

IC2F_ISR_107:
        ldaA    ADR5                    ;read the upper 8 bits of PM/MAP
        bclr    *L000B #0b00100000      ;mark that the PM/MAP is valid

IC2F_ISR_108:
        ldaB    *L0018                  ;restore the original A/D channel
        staB    ADCTL                   ;restart the conversion
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        bclr    *L000B #0b00010000
        ldaB    *L0055
        andB    #0b00001111
        cmpB    L8028
        bne     IC2F_ISR_109
        brset   *L000B #0b00100000 IC2F_ISR_112

        staA    *L001E                  ;save the PM/MAP
        staA    LW+LOGID_ECU_RAW_MAP_TYPE_U8
        bra     IC2F_ISR_111

IC2F_ISR_109:
        cmpB    L8029                   ;0x0B
        beq     IC2F_ISR_110
        jmp     IC2F_ISR_121

IC2F_ISR_110:
        brset   *L000B #0b00100000 IC2F_ISR_112
        staA    *L001F
IC2F_ISR_111:
        staA    *L0020
IC2F_ISR_112:
        ldaA    #0x00
        staA    L015E
        ldaB    *L001E
        staB    L015F                   ;never used!
        ldaB    *L001F
        addD    L015E
        lsrD
        staB    *L0021

        ldaB    PORTD                   ;This is the only code that sets PD3 to 1
        oraB    #0b00001000             ;sets PD3 to '1'
        staB    PORTD

        ldaB    #0x01                   ;debug: mark that we just did a MAP conversion
        staB    *L0016

        brclr   *L000C #0b00110000 IC2F_ISR_113
        ldaA    L9A5C
        staA    *L0038
        staA    *L0039
        bra     IC2F_ISR_116

IC2F_ISR_113:
        ldaB    L802A
        beq     IC2F_ISR_114
        cmpB    #0x01
        beq     IC2F_ISR_115
IC2F_ISR_114:
        ldaA    *L001E
        staA    *L0038
        ldaB    *L001F
        staB    *L0039
        bra     IC2F_ISR_116

IC2F_ISR_115:
        ldaB    *L0020
        staB    *L0038
        staB    *L0039
IC2F_ISR_116:
        ldaB    *L0020
        cmpB    #0x1A
        blo     IC2F_ISR_117
        cmpB    #0xE6
        bhs     IC2F_ISR_117
        bra     IC2F_ISR_119

IC2F_ISR_117:
        brclr   *L0010 #0b00000100 IC2F_ISR_118
        ldaA    L0132
        cmpA    #0x40
        blo     IC2F_ISR_121
        bset    *L000C #0b00100000
        ldaA    L9A5C
        staA    *L0038
        staA    *L0039
        ldaA    #0x80
        staA    *L0041
        ldaA    #0xFF
        staA    *L009C
        bra     IC2F_ISR_121

IC2F_ISR_118:
        bset    *L0010 #0b00000100
        bra     IC2F_ISR_121

IC2F_ISR_119:
        brclr   *L0010 #0b00000100 IC2F_ISR_120
        bclr    *L0010 #0b00000100
        bra     IC2F_ISR_121

IC2F_ISR_120:
        ldaA    L0137
        cmpA    #0x40
        blo     IC2F_ISR_121
        bclr    *L000C #0b00100000

IC2F_ISR_121:
        ldaA    *L0055                  ;get current crankref
        andA    #0b00001111
        cmpA    #0x03
        beq     IC2F_ISR_122            ;br if cr3

        cmpA    #0x08
        beq     IC2F_ISR_123            ;br if cr8

        bra     IC2F_ISR_124

;If this is cr3 (mid compression stroke, frt cyl):
IC2F_ISR_122:
        bset    *L0002 #0b00000010
        bra     IC2F_ISR_124

;If this is cr8 (mid compression stroke, rear cyl):
IC2F_ISR_123:
        bset    *L0002 #0b00000001


IC2F_ISR_124:
        ldaB    *L0049_PORTG_debounced
        andB    #0b00101000
        cmpB    #derestricted
        bne     IC2F_ISR_125
        jmp     IC2F_ISR_156

;Restricted:
IC2F_ISR_125:
        addA    L8033                   ;const 0x05.  A already contains the current crankref (could be 3, 8 or something else)
        andA    #0b00001111
        staA    *L0056

        ldaB    L8033                   ;const 0x05
        beq     IC2F_ISR_128            ;will never be taken
        cmpB    #0x05
        bhs     IC2F_ISR_126            ;will always be taken since L8033(0x05) >= #0x05
        addA    #0x04
        andA    #0b00001111
        staA    *L0056

;Derestricted:
IC2F_ISR_126:
        cmpA    #0x0F
        bne     IC2F_ISR_130
IC2F_ISR_127:
        ldaA    *L004D                  ;8.8 RPM50
        cmpA    L8035                   ;0x3C
        blo     IC2F_ISR_129
        bclr    *L000B #0b10000000
        bra     IC2F_ISR_130

IC2F_ISR_128:
        cmpA    #0x0B
        beq     IC2F_ISR_127
        bra     IC2F_ISR_130

IC2F_ISR_129:
        cmpA    L8034                   ;0x38
        bhs     IC2F_ISR_130
        bset    *L000B #0b10000000
IC2F_ISR_130:
        ldaA    *L0055
        addA    L8032
        andA    #0b00001111
        staA    *L0057
        ldaB    L8032
        beq     IC2F_ISR_131
        cmpB    #0x05
        bhs     IC2F_ISR_131
        addA    #0x04
        andA    #0b00001111
        staA    *L0057
IC2F_ISR_131:
        ldaA    *L0056
        ldaB    L8033
        beq     IC2F_ISR_133
        cmpB    #0x09
        bhs     IC2F_ISR_133
        cmpA    #0x08
        bne     IC2F_ISR_135
IC2F_ISR_132:
        brclr   *L000B #0b10000000 IC2F_ISR_134
        bset    *L000B #0b01000000
        bra     IC2F_ISR_135

IC2F_ISR_133:
        cmpA    #0x04
        beq     IC2F_ISR_132
        bra     IC2F_ISR_135

IC2F_ISR_134:
        bclr    *L000B #0b01000000
IC2F_ISR_135:
        brclr   *L000B #0b10000000 IC2F_ISR_136
        ldaA    L0171
        staA    *L0058
        ldaA    *L0057
        ldaB    L8032
        bra     IC2F_ISR_137

IC2F_ISR_136:
        ldaA    L0170
        staA    *L0058

        ldaA    *L0056
        ldaB    L8033
IC2F_ISR_137:
        cmpB    #0x00
        beq     IC2F_ISR_145
        cmpB    #0x0F
        bne     IC2F_ISR_140
        cmpA    #0x0A
        bne     IC2F_ISR_138
        bclr    *L0001 #0b10000000
        bra     IC2F_ISR_147

IC2F_ISR_138:
        cmpA    #0x03
        bne     IC2F_ISR_139
        bclr    *L0001 #0b00010000
IC2F_ISR_139:
        bra     IC2F_ISR_147

IC2F_ISR_140:
        cmpA    #0x0E
        bne     IC2F_ISR_141
        bclr    *L0001 #0b10000000
IC2F_ISR_141:
        cmpB    #0x08
        bhs     IC2F_ISR_142
        cmpA    #0x07
        bne     IC2F_ISR_144
        bra     IC2F_ISR_143

IC2F_ISR_142:
        cmpA    #0x03
        bne     IC2F_ISR_144
IC2F_ISR_143:
        bclr    *L0001 #0b00010000
IC2F_ISR_144:
        bra     IC2F_ISR_147

IC2F_ISR_145:
        cmpA    #0x0A
        bne     IC2F_ISR_146
        bclr    *L0001 #0b10000000
        bra     IC2F_ISR_147

IC2F_ISR_146:
        cmpA    #0x03
        bne     IC2F_ISR_147
        bclr    *L0001 #0b00010000
IC2F_ISR_147:
        brclr   *L000B #0b10000000 IC2F_ISR_155
        brclr   *L000B #0b01000000 IC2F_ISR_154
        ldaB    L8032
        beq     IC2F_ISR_148
        cmpB    #0x0A
        bhs     IC2F_ISR_148
        ldaB    #0x09
        bra     IC2F_ISR_149

IC2F_ISR_148:
        ldaB    #0x05
IC2F_ISR_149:
        staB    L0171
        staB    *L0058
        ldaA    *L0057
        jmp     IC2F_ISR_188

IC2F_ISR_150:
        ldaB    L8033
        beq     IC2F_ISR_151
        cmpB    #0x0A
        bhs     IC2F_ISR_151
        ldaB    #0x09
        bra     IC2F_ISR_152

IC2F_ISR_151:
        ldaB    #0x05
IC2F_ISR_152:
        staB    L0170
        staB    *L0058
        ldaA    *L0056
IC2F_ISR_153:
        jmp     IC2F_ISR_188

IC2F_ISR_154:
        ldaA    *L0057
        beq     IC2F_ISR_153
        jmp     IC2F_ISR_189

IC2F_ISR_155:
        brclr   *L000B #0b01000000 IC2F_ISR_150
        jmp     IC2F_ISR_187

;Derestricted:
IC2F_ISR_156:
        addA    L8037                   ;0x05
        andA    #0b00001111
        staA    *L0056
        ldaB    L8037                   ;0x05
        beq     IC2F_ISR_159
        cmpB    #0x05
        bhs     IC2F_ISR_157
        addA    #0x04
        andA    #0b00001111
        staA    *L0056
IC2F_ISR_157:
        cmpA    #0x0F
        bne     IC2F_ISR_161
IC2F_ISR_158:
        ldaA    *L004D                  ;8.8 RPM50
        cmpA    L8039                   ;0x3C
        blo     IC2F_ISR_160
        bclr    *L000B #0b10000000
        bra     IC2F_ISR_161

IC2F_ISR_159:
        cmpA    #0x0B
        beq     IC2F_ISR_158
        bra     IC2F_ISR_161

IC2F_ISR_160:
        cmpA    L8038                   ;0x38
        bhs     IC2F_ISR_161
        bset    *L000B #0b10000000

IC2F_ISR_161:
        ldaA    *L0055                  ;get current crankRef posn
        addA    L8036                   ;0x06 add a full rotation
        andA    #0b00001111
        staA    *L0057

        ldaB    L8036                   ;0x06
        beq     IC2F_ISR_162            ;NEVER TAKEN!

        cmpB    #0x05
        bhs     IC2F_ISR_162            ;ALWAYS TAKEN!

        addA    #0x04
        andA    #0b00001111
        staA    *L0057

IC2F_ISR_162:
        ldaA    *L0056
        ldaB    L8037                   ;0x05
        beq     IC2F_ISR_164
        cmpB    #0x09
        bhs     IC2F_ISR_164
        cmpA    #0x08
        bne     IC2F_ISR_166
IC2F_ISR_163:
        brclr   *L000B #0b10000000 IC2F_ISR_165
        bset    *L000B #0b01000000
        bra     IC2F_ISR_166

IC2F_ISR_164:
        cmpA    #0x04
        beq     IC2F_ISR_163
        bra     IC2F_ISR_166

IC2F_ISR_165:
        bclr    *L000B #0b01000000
IC2F_ISR_166:
        brclr   *L000B #0b10000000 IC2F_ISR_167
        ldaA    L0171
        staA    *L0058
        ldaA    *L0057
        ldaB    L8036                   ;0x06
        bra     IC2F_ISR_168

IC2F_ISR_167:
        ldaA    L0170
        staA    *L0058
        ldaA    *L0056
        ldaB    L8037                   ;0x05
IC2F_ISR_168:
        cmpB    #0x00
        beq     IC2F_ISR_176
        cmpB    #0x0F
        bne     IC2F_ISR_171
        cmpA    #0x0A
        bne     IC2F_ISR_169
        bclr    *L0001 #0b10000000
        bra     IC2F_ISR_178

IC2F_ISR_169:
        cmpA    #0x03
        bne     IC2F_ISR_170
        bclr    *L0001 #0b00010000
IC2F_ISR_170:
        bra     IC2F_ISR_178

IC2F_ISR_171:
        cmpA    #0x0E
        bne     IC2F_ISR_172
        bclr    *L0001 #0b10000000
IC2F_ISR_172:
        cmpB    #0x08
        bhs     IC2F_ISR_173
        cmpA    #0x07
        bne     IC2F_ISR_175
        bra     IC2F_ISR_174

IC2F_ISR_173:
        cmpA    #0x03
        bne     IC2F_ISR_175
IC2F_ISR_174:
        bclr    *L0001 #0b00010000
IC2F_ISR_175:
        bra     IC2F_ISR_178

IC2F_ISR_176:
        cmpA    #0x0A
        bne     IC2F_ISR_177
        bclr    *L0001 #0b10000000
        bra     IC2F_ISR_178

IC2F_ISR_177:
        cmpA    #0x03
        bne     IC2F_ISR_178
        bclr    *L0001 #0b00010000
IC2F_ISR_178:
        brclr   *L000B #0b10000000 IC2F_ISR_186
        brclr   *L000B #0b01000000 IC2F_ISR_185
        ldaB    L8036                   ;0x06
        beq     IC2F_ISR_179
        cmpB    #0x0A
        bhs     IC2F_ISR_179
        ldaB    #0x09
        bra     IC2F_ISR_180

IC2F_ISR_179:
        ldaB    #0x05
IC2F_ISR_180:
        staB    L0171
        staB    *L0058
        ldaA    *L0057
        jmp     IC2F_ISR_188

IC2F_ISR_181:
        ldaB    L8037                   ;0x05
        beq     IC2F_ISR_182
        cmpB    #0x0A
        bhs     IC2F_ISR_182
        ldaB    #0x09
        bra     IC2F_ISR_183

IC2F_ISR_182:
        ldaB    #0x05
IC2F_ISR_183:
        staB    L0170
        staB    *L0058
        ldaA    *L0056
IC2F_ISR_184:
        bra     IC2F_ISR_188

IC2F_ISR_185:
        ldaA    *L0057
        beq     IC2F_ISR_184
        jmp     IC2F_ISR_189

IC2F_ISR_186:
        brclr   *L000B #0b01000000 IC2F_ISR_181

IC2F_ISR_187:
        jsr     LF309
        ldaA    L0171
        staA    *L0058
        ldaA    *L0057
        cmpA    *L0058
        bne     IC2F_ISR_189
IC2F_ISR_188:
        jsr     LF309

IC2F_ISR_189:
        ldaB    PORTD
        andB    #0b11110111             ;sets PD3 to '0' (the 'normal' state)
        staB    PORTD

        ldaA    *L0055
        andA    #0b00001111
        cmpA    #0x02
        beq     IC2F_ISR_196            ;br if crankRef == cr2

        cmpA    #0x07
        beq     IC2F_ISR_198            ;br if crankRef == cr7

        cmpA    #0x00
        bne     IC2F_ISR_190

        jmp     IC2F_ISR_207            ;jmp if crankRef == cr0

IC2F_ISR_190:
        cmpA    #0x05
        bne     IC2F_ISR_191
        jmp     IC2F_ISR_209            ;jmp if crankRef == cr5

; -----------------------------------------------------------------
; Must be one of CR1,3,4,6,8,9,10,11
IC2F_ISR_191:
        ldaA    *L0055
        cmpA    #0x80                   ;>= 0x80 means we do not know where the crank is.
        bhs     IC2F_ISR_195            ;br if we DO know where the crank is

        andA    #0b00001111
        brclr   *L0000 #0b01000000 IC2F_ISR_192 ;if we know what is going on

        jmp     IC2F_ISR_217            ;else reti

IC2F_ISR_192:
        cmpA    #0x03
        beq     IC2F_ISR_200            ;br if crankRef == cr3

        cmpA    #0x08
        bne     IC2F_ISR_193
        jmp     IC2F_ISR_206            ;jmp if crankRef == cr8

IC2F_ISR_193:
        cmpA    #0x01
        bne     IC2F_ISR_194
        jmp     IC2F_ISR_211            ;jmp if crankRef == cr1


IC2F_ISR_194:
        cmpA    #0x06
        bne     IC2F_ISR_195
        jmp     IC2F_ISR_215            ;jmp if crankRef == cr6

IC2F_ISR_195:
        jmp     IC2F_ISR_217            ;(reti) jmp if cr4, cr9, cr10, cr11

; -----------------------------------------------------------------
; We get here when crankRef == CR2 (Start of compression stroke, Frt cyl)
IC2F_ISR_196:
        ldaB    PORTA
        bitB    #0b10000000             ;PA7 drives the frt ign coil (0==charging)
        beq     IC2F_ISR_197            ;br if already charging

;else ?arrange to start charging the coil during CR3?
        ldD     *L00E9                  ;the advance timing for the frt cyl
        jsr     LF1D1
        stD     *L00F5
IC2F_ISR_197:
        jmp     IC2F_ISR_217            ;reti

; -----------------------------------------------------------------
; We get here when crankRef == CR7 (Start of compression stroke, Rear cyl)
IC2F_ISR_198:
        ldaB    PORTA
        bitB    #0b01000000
        beq     IC2F_ISR_199
        ldD     *L00EB
        jsr     LF1D1
        stD     *L00FB
IC2F_ISR_199:
        jmp     IC2F_ISR_217            ;reti

; -----------------------------------------------------------------
; We get here when crankRef == CR3 ()
IC2F_ISR_200:
        ldaB    PORTA
        bitB    #0b10000000
        bne     IC2F_ISR_201
        jmp     IC2F_ISR_217            ;reti

IC2F_ISR_201:
        brset   *L0004 #0b00000100 IC2F_ISR_202 ;1 means "Slow RPM" flag is asserted
        brset   *L0007 #0b10000000 IC2F_ISR_202 ;if set, the injectors are disabled
        brclr   *L0005 #0b01000000 IC2F_ISR_203

IC2F_ISR_202:
        bclr    *L0005 #0b00010000      ;indicate that TOC1 'start charging' event is NOT activated
        jmp     IC2F_ISR_217            ;reti

IC2F_ISR_203:
        ldaA    #0x80
        staA    TFLG1
        ldD     TCNT
        nop
        subD    TCNT2
        addD    #0x0003
        addD    *L0059                  ;the start time of this crank period
        addD    *L00F5
        stD     TOC1
        ldaA    #0x80
        staA    TFLG1
        ldaA    #0x7F                   ;PA7=0 on OC1 match event (0 == start charging)
        staA    OC1D
        ldaA    TMSK1
        oraA    #0b10000000             ;enable OC1 interrupts
        staA    TMSK1
        ldaA    #0x80                   ;PA7/OC1 is affected by OC1 compare events
        staA    OC1M

        bset    *L0005 #0b00010000      ;indicate that the TOC1 front coil 'start charging' event is in place
IC2F_ISR_204:
        jmp     IC2F_ISR_217            ;reti


IC2F_ISR_205:
        bclr    *L0006 #0b00000100      ;clear any outstanding 'REAR coil is charging' event
        jmp     IC2F_ISR_217            ;reti


; -----------------------------------------------------------------
; We get here when crankRef == CR8 ()
IC2F_ISR_206:
        ldaB    PORTA
        bitB    #0b01000000
        beq     IC2F_ISR_204
        brset   *L0004 #0b00000100 IC2F_ISR_205 ;1 means "Slow RPM" flag is asserted
        brset   *L0007 #0b01000000 IC2F_ISR_205 ;if set, the ign is disabled
        brset   *L0005 #0b01000000 IC2F_ISR_205
        ldaA    #0x40
        staA    TFLG1
        ldD     TCNT
        nop
        subD    TCNT2
        addD    #0x0003
        addD    *L0059                  ;the start time of this crank period
        addD    *L00FB
        stD     TOC2
        ldaA    #0x40
        staA    TFLG1
        ldaA    TCTL1
        oraA    #0b10000000
        andA    #0b10111111
        staA    TCTL1
        ldaA    TMSK1
        oraA    #0b01000000             ;enable OC2 interrupts
        staA    TMSK1
        bset    *L0006 #0b00000100      ;1==rear coil 'start charging' event has been scheduled
        jmp     IC2F_ISR_217            ;reti

; -----------------------------------------------------------------
; We get here when crankRef == CR0
IC2F_ISR_207:
        ldaB    PORTA
        bitB    #0b10000000             ;test FRONT ign coil drive
        beq     IC2F_ISR_208            ;'0' means coil is already charging

;Front coil is NOT charging:
        ldD     *L00E9
        jsr     LF1FC
        stD     *L00F7                  ;the Ign dwell delay to use during CR1
IC2F_ISR_208:
        jmp     IC2F_ISR_217            ;reti

; -----------------------------------------------------------------
; We get here when crankRef == CR5
IC2F_ISR_209:
        ldaB    PORTA
        bitB    #0b01000000             ;test REAR ign coil drive
        beq     IC2F_ISR_210            ;'0' means coil is already charging

;Rear coil is NOT charging:
        ldD     *L00EB
        jsr     LF1FC
        stD     *L00FD
IC2F_ISR_210:
        jmp     IC2F_ISR_217            ;reti

; -----------------------------------------------------------------
; We get here when crankRef == CR1 (final stage of intake stroke, frt cyl)
IC2F_ISR_211:
        ldaB    PORTA
        bitB    #0b10000000             ;PA7 is front cyl ign driver (0==coil is charging)
        bne     IC2F_ISR_212            ;br if coil is NOT charging
        jmp     IC2F_ISR_217            ;reti

IC2F_ISR_212:
        brset   *L0004 #0b00000100 IC2F_ISR_213 ;1 means "Slow RPM" flag is asserted
        brset   *L0007 #0b10000000 IC2F_ISR_213 ;if set, injectors are disabled
        brclr   *L0005 #0b01000000 IC2F_ISR_214

IC2F_ISR_213:
        bclr    *L0005 #0b00010000      ;indicate that TOC1 'start charging' event is NOT activated
        jmp     IC2F_ISR_217            ;reti


;We get here if front cyl is NOT charging, the slow RPM flag is NOT set, injectors are enabled, and something else in L0005.
;This would seem to be setting up a T0C1 event to start charging very soon
IC2F_ISR_214:
        ldaA    #0x80
        staA    TFLG1
        ldD     TCNT
        nop
        subD    TCNT2
        addD    #0x0003
        addD    *L0059                  ;the start time of this crank period
        addD    *L00F7
        stD     TOC1
        ldaA    #0x80
        staA    TFLG1
        ldaA    #0x7F                   ;PA7=0 on OC1 match event (0 == start charging)
        staA    OC1D
        ldaA    TMSK1
        oraA    #0b10000000             ;enable OC1 interrupts
        staA    TMSK1
        ldaA    #0x80                   ;PA7/OC1 is affected by OC1 compare events
        staA    OC1M

        bset    *L0005 #0b00010000      ;indicate that the next TOC1 event will start charging the coil
        bra     IC2F_ISR_217            ;reti

; -----------------------------------------------------------------
; We get here when crankRef == CR6 (final stage of intake stroke, rear cyl)
IC2F_ISR_215:
        ldaB    PORTA
        bitB    #0b01000000
        beq     IC2F_ISR_217            ;reti
        brset   *L0004 #0b00000100 IC2F_ISR_216 ;1 means "Slow RPM" flag is asserted
        brset   *L0007 #0b01000000 IC2F_ISR_216 ;if set, ign is disabled
        brset   *L0005 #0b01000000 IC2F_ISR_216
        ldaA    #0x40
        staA    TFLG1
        ldD     TCNT
        nop
        subD    TCNT2
        addD    #0x0003
        addD    *L0059                  ;the start time of this crank period
        addD    *L00FD
        stD     TOC2
        ldaA    #0x40
        staA    TFLG1
        ldaA    TCTL1
        oraA    #0b10000000
        andA    #0b10111111
        staA    TCTL1
        ldaA    TMSK1
        oraA    #0b01000000             ;enable OC2 interrupts
        staA    TMSK1
        bset    *L0006 #0b00000100      ;1==rear coil 'start charging' event has been scheduled
        bra     IC2F_ISR_217

IC2F_ISR_216:
        bclr    *L0006 #0b00000100      ;0==rear coil 'start charging' event has NOT been scheduled
IC2F_ISR_217:
        rti

; This routine would appear to terminate the charge operation on the
; FRONT ignition coil, with the intent being to NOT create a spark.
IC2F_ISR_218:
        ldaB    PORTA
        bitB    #0b10000000
        bne     IC2F_ISR_220            ;br if PA7 == 1 (front IGN coil is NOT charging)

        brset   *L0004 #0b00000100 IC2F_ISR_219 ;1 means "Slow RPM" flag is asserted
        brset   *L0005 #0b01000000 IC2F_ISR_219
        brset   *L0000 #0b00000100 IC2F_ISR_219

        ldaA    #0x80
        staA    TFLG1

        ldD     *L00FF
        stD     TOC1

        ldaA    #0xF8                   ;PA7=1 on OC1 match event (1 == stop charging)
        staA    OC1D

        ldaA    TMSK1
        oraA    #0b10000000             ;enable OC1 ints
        staA    TMSK1

        ldaA    #0x80                   ;PA7/OC1 is affected by OC1 compare events
        staA    OC1M

IC2F_ISR_219:
        jmp     IC2F_ISR_105

IC2F_ISR_220:
        brset   *L0004 #0b00000100 IC2F_ISR_221 ;1 means "Slow RPM" flag is asserted
        brset   *L0007 #0b10000000 IC2F_ISR_221 ;if set, the injectors are disabled
        brclr   *L0005 #0b01000000 IC2F_ISR_222

IC2F_ISR_221:
        bclr    *L0005 #0b00010000      ;indicate that TOC1 'start charging' event is NOT activated
        jmp     IC2F_ISR_105


IC2F_ISR_222:
        ldaA    #0x80
        staA    TFLG1
        ldD     TCNT
        nop
        subD    TCNT2
        addD    #0x0003
        addD    *L0059                  ;the start time of this crank period
        addD    *L00F3
        stD     TOC1
        ldaA    #0x80
        staA    TFLG1
        ldaA    #0x7F                   ;PA7=0 on OC1 match event (0 == start charging)
        staA    OC1D
        ldaA    TMSK1
        oraA    #0b10000000             ;enable OC1 ints
        staA    TMSK1
        ldaA    #0x80                   ;PA7/OC1 is affected by OC1 compare events
        staA    OC1M

        bset    *L0005 #0b00010000      ;indicate that the next TOC1 event will start charging the coil
        jmp     IC2F_ISR_105


IC2F_ISR_223:
        ldaB    PORTA
        bitB    #0b01000000
        bne     IC2F_ISR_226
        brset   *L0004 #0b00000100 IC2F_ISR_225 ;1 means "Slow RPM" flag is asserted
        brset   *L0005 #0b01000000 IC2F_ISR_225
        brset   *L0000 #0b00000100 IC2F_ISR_225
        ldaA    #0x40
        staA    TFLG1
        ldD     L0101
        stD     TOC2
        ldaA    TCTL1
        oraA    #0b11000000
        staA    TCTL1
        ldaA    TMSK1
        oraA    #0b01000000             ;enable OC2 ints
        staA    TMSK1
        bra     IC2F_ISR_225

IC2F_ISR_224:
        bclr    *L0006 #0b00000100      ;0==rear coil 'start charging' event has NOT been scheduled
IC2F_ISR_225:
        jmp     IC2F_ISR_105

IC2F_ISR_226:
        brset   *L0004 #0b00000100 IC2F_ISR_224 ;1 means "Slow RPM" flag is asserted
        brset   *L0007 #0b01000000 IC2F_ISR_224 ;if set, ign is disabled
        brset   *L0005 #0b01000000 IC2F_ISR_224
        ldaA    #0x40
        staA    TFLG1
        ldD     TCNT                    ;2 Eclks pass after the high byte is read (latching the low byte)
        nop                             ;2 Eclk
        subD    TCNT2                   ;3 Eclk pass before the high byte is read.
                                        ;TCNT and TCNT2 should have the same value in them at all times...
        addD    #0x0003
        addD    *L0059                  ;the start time of this crank period
        addD    *L00F9
        stD     TOC2
        ldaA    #0x40
        staA    TFLG1
        ldaA    TCTL1
        oraA    #0b10000000
        andA    #0b10111111
        staA    TCTL1
        ldaA    TMSK1
        oraA    #0b01000000             ;enable OC2 ints
        staA    TMSK1
        ldaA    #0x40
        staA    TFLG1
        bset    *L0006 #0b00000100      ;1==rear coil 'start charging' event has been scheduled
        jmp     IC2F_ISR_105

;*****************************************************************
; Called during the CR3 and CR8 compression stroke events during the crank position processing interrupt.

LF1A1:  brclr   *L0000 #0b00000100 3f   ;'0' means that 'normal' ignition processing is in effect (set to '1' at reset)
        ldD     *L00D0                  ;time (in 2us ticks) of most recent crank 1/6 period
        subD    *L00ED
        bhs     1f
0:
        ldD     LC014                   ;const 0x00FF
        bra     2f

1:
        cmpD    LC014                   ;const 0x00FF
        blo     0b
2:
        rts

3:
        stD     L015E
        ldD     *L00CE
        subD    LC014                   ;const 0x00FF
        subD    L015E
        blo     4f
        subD    *L00ED
        bhs     5f
4:
        ldD     LC014                   ;const 0x00FF
        bra     6f

5:
        addD    LC014                   ;const 0x00FF
6:
        rts

;*****************************************************************
; See next sub for doc.
; Called twice.
LF1D1:  brclr   *L0000 #0b01000000 0f
        ldD     #0xFFFF
        rts

0:
        stD     L015E
        ldD     *L00D0
        addD    *L00CE
        bhs     1f
        ldD     #0xFFFF
1:
        subD    L015E
        blo     2f
        subD    LC016                   ;const 0x014A
        blo     2f
        subD    *L00ED
        bhs     3f
2:
        ldD     LC016                   ;const 0x014A
        bra     4f

3:
        addD    LC016                   ;const 0x014A
4:
        rts

;*****************************************************************
; Called for both frt and rear cyls.  I think this might be
; calculating a DWELL period, returning it in reg D.

; Takes 1 param in reg D: ?
; returns D: ?

LF1FC:  brclr   *L0000 #0b01000000 0f   ;br if we know what is going on

        ldD     #0xFFFF                 ;else return this
        rts

0:
        stD     L015E                   ;Temp?

        ldD     *L00D8                  ;the most recent 1/2 crank period (180 degrees of rotation)
        addD    *L00CE                  ;amount of time (in 2 usec ticks) for the last 90 degrees of rotation
        bhs     1f                      ;br if the time for the last 270 degrees did not overflow a uint16

        ldD     #0xFFFF                 ;else it did overflow, so peg the 270 degree period to 0xFFFF

; if (270Period < inputParamD) return D = 660 uSec;
1:
        subD    L015E                   ;D = 270period - inputParamD
        blo     2f                      ;branch if 270 period < the inputParamD

; if (((270Period - inputParamD) < 660 uSec) return D = 660 uSec;
;Now calc D = (270Period - inputParamD) - 660 uSec
        subD    LC016                   ;const 0x014A, in timer ticks would be 660 uSec
        blo     2f                      ;br if (270Period - inputParamD) < 660 uSec

        subD    *L00ED                  ;D = (270Period - inputParamD - 660 uSec) - L00ED
        bhs     3f                      ;Br if D >= 0

2:
        ldD     LC016                   ;else force result to be 660 uSec, min
        bra     4f

3:
        addD    LC016                   ;D = (270Period - InputParamD - 660 uSec) - L00ED + 660 uSec

4:
        rts


;*****************************************************************
;The OC1 Flag ISR drives the front cyl ignition coils (IG11,IG12)
;under 'normal' (i.e. non-starting) conditions.

;General operation:
;       - do nothing if the interrupt is inhibited
;       - do nothing if the coil is not charging
;       - do nothing if crankRef period is not cr4
;       - if the 'starting' bit is set, then do nothing even if this is cr4
;       - else since this is cr4, get the pre-calc'd delay and schedule a match event to fire the coil

OC1F_ISR:
        sei

        ldaA    TMSK1
        bitA    #0b10000000             ;test OC1I (1==interrupt is enabled)
        bne     0f                      ;br if interrupt is enabled

        bra     5f                      ;else exit since the interrupt is inhibited

0:
        brset   *L0000 #0b00000100 5f   ;br if OC1F ignition processing is disabled

        ldaA    PORTA
        bitA    #0b10000000             ;test PA7/OC1
        beq     1f                      ;br if PA7/OC1==0 (coil is being driven)
        bra     5f                      ;exit since the coil is not being driven

;Coil is being driven.
1:
        ldaA    *L0055                  ;check the crank posn
        andA    #0b00001111
        cmpA    #0x04
        beq     3f                      ;br if this is crank posn 4

;All other crankref id's defer firing the coil by setting TOC1 to
;be as far away as possible (one count less than the current
;match time).
2:
        ldD     TOC1
        subD    #0x0001
        bra     4f

;Coil is being driven, crank is at Posn #4
3:
        brset   *L000B #0b00001000 2b   ;this flag seems to say that we will fire the spark 'manually'
                                        ;on the rising edge of crankRef cr5 (a bit after TDC?)

;Otherwise, a normal ignition event is that we fire the spark
;some time during this crankref period.

        ldD     *L00FF                  ;get the precalculated time from start of CR4 event to spark fire
        stD     LW+LOGID_ECU_F_COIL_OFF_TYPE_PTS

4:
        stD     TOC1                    ;use that time to fire the spark

        ldaA    #0x80                   ;PA7/OC1 is affected by OC1 compare events
        staA    OC1M

        ldaA    #0xF8                   ;OC1 match will drive PA7 to 1 (1 == stop charging, fire spark)
        staA    OC1D

;The OC1 match event will fire the spark.  We don't need the
;interrupt routine to run when it fires though:

        ldaA    TMSK1                   ;therefore, disable (mask) OC1 interrupts for next time
        andA    #0b01111111
        staA    TMSK1

5:
        ldaA    #0x80                   ;flush the OC1 interrupt request
        staA    TFLG1

        cli
        rti

;*****************************************************************
;The OC2 handler drives the rear cyl ignition coils (IG21,IG22)
OC2F_ISR:
        sei
        ldaA    TMSK1
        bitA    #0b01000000
        bne     0f
        jmp     5f

0:
        brset   *L0000 #0b00000100 5f

        ldaB    PORTA
        bitB    #0b01000000
        beq     1f
        bra     5f

1:
        ldaA    *L0055                  ;get the current crankRef
        andA    #0b00001111
        cmpA    #0x09                   ;test for cr9 (not cr10!) because this is a 60 degree twin so perhaps the rear
        beq     3f                      ;event occurs 60 degrees sooner than the front!
2:
        ldD     TOC2
        subD    #0x0001
        bra     4f

3:
        brset   *L000B #0b00000100 2b   ;branch if a spark is NOT armed?
        ldD     L0101
        stD     LW+LOGID_ECU_R_COIL_OFF_TYPE_PTS

4:
        stD     TOC2
        ldaA    TCTL1
        oraA    #0b11000000
        staA    TCTL1
        ldaA    TMSK1
        andA    #0b10111111
        staA    TMSK1
5:
        ldaA    #0x40
        staA    TFLG1
        cli
        rti

;*****************************************************************
;The IC1F Interrupt Service Routine: camshaft sensor cam event.
;  - The cam sensor is normally a 0 output
;  - The 0->1 leading edge is not interesting
;  - The 1->0 trailing edge marks that the next crank event will be CR0
;FYI: Scope traces show that the camshaft sensor falling-edge appears to be generated by a zero-cross circuit.
;
; Scope traces also show that ignition noise can cause spurious positive pulses on the PA2/IC1F pin.
; It is conceivable that this code would respond to the falling edge of the noise event.
; As things stand, this is not detectable.
; It might be better if this code responded to both edges, and measured the width of the cam pulse.
; If the pulse was too narrow, it could discard the falling edge as being spurious.
; The scope shows that the spurious pulses are very narrow, like less than 500 nSec.
; At idle, a cam pulse is about 5 mSec wide. At 10 times that speed (well past redline), the pulse
; would be 500 uSec wide.
; It would be safe to discard anything less than say 100 uSec.
; It would be worth logging the fact that a spruious cam pulse was detected.

IC1F_ISR:
        ldD     TIC1                    ;Log the timestamp of this most recent camshaft event
        stD     LW+LOGID_ECU_CAMSHAFT_TYPE_TS

        ldaA    TCTL2                   ;read TCTL2
        andA    #0b00110000             ;only interested in the T1 edge control bits
        cmpA    #0b00100000             ;was it set to %xx10xxxx (T1 falling edge)?
        beq     1f                      ;yes

;We should never get here since we never program TCTL2/T1 for
;anything other than a falling edge interrupt.  In any case, we
;don't care about rising edge interrupts:

#if 0
;BUG FIX
;The ECU firmware initializes TCTL2 for a falling edge interrupt ONLY.
;If we get here, it means that the edge1 control bits are wrong, and yet the original ISR
;simply exited without resetting them back to the desired state.
        ldaA    TCTL2                   ;set the edge1 control bits back to 'falling edge only'
        andA    0b11001111
        oraA    0b00100000
        staA    TCTL2
;End bug fix
#endif
        ldaA    #0x04                   ;clear the IC1F flag
        staA    TFLG1
0:
        staA    LW+LOGID_ECU_CAM_ERR_TYPE_U8 ;if (A bit 2 == 1) then edge control bits were wrong, if (A bit 2 == 0) then TFLG1.ICF1 was unexpectedly not set
        rti

;We are processing a falling edge interrupt, the trailing edge of the cam sensor.
1:
        ldaA    TFLG1                   ;read TFLG1
        bitA    #0b00000100             ;test if IC1F is set
        beq     0b                      ;if not (but why should it not be?), bail immediately

        brclr   *L0000 #0b00001000 2f
        bset    *L0003 #0b00000001      ;we have seen a cam event
        brclr   *L0000 #0b00000001 2f

        ldaA    *L0055
        cmpA    #0x80                   ;Have we ever seen a crank event yet?
        blo     2f                      ;if A<0x80, we have!

        bset    *L0003 #0b00010000      ;mark that we don't know where the crank is?
2:
        bset    *L0003 #0b00001000

;Sensor error check: if we see 30 cam events without seeing a single crank event,
;then our crank sensor is not working:
        ldaA    L0152                   ;this var gets cleared on every falling edge of IC2F crank sensor
        incA                            ;inc L0152 on every cam event
        staA    L0152

        cmpA    L9A55                   ;0x1E==30.
        bls     3f                      ;br if L0152<30

;else set error flags:
        bset    *L0011 #0b01000000      ;sets crank error
        bset    *L000C #0b01000000
        bset    *L000E #0b01000000
        dec     L0152                   ;set the error flags on each subsequent cam event
        bra     4f

3:
        bclr    *L0011 #0b01000000      ;no crank error

4:
        ldaA    #0x04                   ;clear the IC1F interrupt request flag
        staA    TFLG1
        rti                             ;and return

;*****************************************************************
;Called as part of crankshaft interrupt processing.

LF309:  brset   *L0000 #0b10000000 LF309_2   ;br if this is the first "tracked" rotation of the crank
        brclr   *L0004 #0b00000101 LF309_0   ;br if DON_FILT says bike is upright and some other flag meaning ??
        bra     LF309_2                 ;else exit

LF309_0:
        brclr   *L0004 #0b00000010 LF309_2
        brset   *L0005 #0b01000000 LF309_2
        brclr   *L0006 #0b11100000 LF309_1   ;br if ((!100==?) && (!010==?) && (001==RPM<9800))
        bra     LF309_2                 ;else exit

LF309_1:
        cmpA    #0x00
        beq     LF309_3                 ;br if crankRef == cr0

        cmpA    *L0058
        beq     LF309_11                ;br if crankRef== <some other predetermined ref point>
LF309_2:
        rts                             ;else exit

;-----------------------------------------------------------------
;We get here if the crankRef indicates we are at position 0:
LF309_3:
        brclr   *L000D #0b00010000 LF309_4   ;br if the FRONT ign coil IG11 is OK
        brclr   *L000D #0b00001000 LF309_4   ;br if the FRONT ign coil IG12 is OK
        rts                             ;else neither coil is working: abort


LF309_4:
        ldaA    PORTA
        bitA    #0b00100000             ;PA5/OC3 controls the front fuel injector
        beq     LF309_5                 ;br if ?already squirting?

        ldD     *L005D
        stD     L0166
        ldD     #0x0000
        stD     L016A
        jmp     LF309_10


LF309_5:
        ldD     TOC3                    ;TOC3 controls front injector
        subD    TCNT
        blo     LF309_6                 ; <
        subD    LC012                   ; 96 uSec
        bhs     LF309_7                 ; >=
LF309_6:
        ldD     #0x0000
LF309_7:
        stD     L0162
        subD    L016A
        bls     LF309_8
        ldD     L016A
        bne     LF309_9
LF309_8:
        ldD     L0162
        stD     L016A
LF309_9:
        addD    *L005D
        stD     L0166
LF309_10:
        jsr     LF48C
        rts

;-----------------------------------------------------------------
;We get here if the crankref is at some other predetermined reference point
LF309_11:
        brset   *L0001 #0b00001000 LF309_12

        brclr   *L000D #0b00000100 LF309_13  ;br if the REAR ign coil IG21 is OK
        brclr   *L000D #0b00000010 LF309_13  ;br if the REAR ign coil IG22 is OK
LF309_12:
        rts

LF309_13:
        ldaA    PORTA
        bitA    #0b00010000             ;PA4/OC4 drives the rear fuel injector
        beq     LF309_14
        ldD     *L005F
        stD     L0168
        ldD     #0x0000
        stD     L016C
        jmp     LF309_19

LF309_14:
        ldD     TOC4
        subD    TCNT
        blo     LF309_15
        subD    LC012                   ; 96 uSec
        bhs     LF309_16
LF309_15:
        ldD     #0x0000
LF309_16:
        stD     L0162
        subD    L016C
        bls     LF309_17
        ldD     L016C
        bne     LF309_18
LF309_17:
        ldD     L0162
        stD     L016C
LF309_18:
        addD    *L005F
        stD     L0168
LF309_19:
        jsr     LF4D1
        rts

;*****************************************************************
;Perform a linear interpolation.
;
; There are 3 entry points:
;   LF3C2:
;      Reg A: the value to interpolate
;      Reg X: points at a 5-entry table
;               0, X	Input Offset	The "Base" value. This is subtracted from your input (Reg A). Anything below this value is treated as 0.
;               1, X	Input Limit	The "Max" value. This defines the upper boundary of the table. Anything above this is clamped to this value.
;               2, X	Table Stride	Usually the length of a row. It is multiplied by the input to find the correct memory offset for the data.
;               3, X	Data Point 0	The first actual data value in the lookup table (the "Floor").
;               4, X	Data Point 1	The second data value in the table (the "Ceiling")
;
;      Function: Reg A is reduced by the 0[X] (but not below 0), then clipped to a max of 1[X], and then multiplied by 2[X].

LF3C2:  subA    0, X                    ;Reduce A by (0,X)
        bhs     0f                      ;A > 0
        clrA                            ;else clamp A to 0

0:
        cmpA    1, X                    ;compared the reduced value of A against the second table entry
        bls     1f                      ;br if A <= the second table entry
        ldaA    1, X                    ;else clamp A to the second entry

1:
        ldaB    2, X                    ;multiply A by the table stride (length)
        mul                             ;D contains an 8.8 fixed point number: A is the table entry, B is the fraction to interpolate

;-----------------------------------------------------------------
;A separate entry point (used 9 times).
;Entry:
;  X  - points 3 bytes before the first entry of a 1D table of interpolation data.

;  AB - contains an 8.8 fixed-point value to convert using the table.
;       Reg A must have a value which is known to be <= the table length
;       Reg B has a fractional value.  If the table values are
;       increasing, the fractional value gets multiplied against the
;       delta of the selected table value and the next entry in
;       the table.  If the delta is positive, the result is added to the
;       sum.  If the result is negative, the table values must be decreasing,
;       so the result is subtracted from the sum.

;Returns:
;  D  - An 8.8 fixed point value corresponding to the interpolation between the
;       proper pair of 8.0 bytes in the table.


LF3D0:  pshA                            ;swap A and B
        tBA
        pulB

        aBX                             ;X=X+B (the integer part of the 8.8 frac)

; X now points at the desired table entry

;--------------------------------------------------------------------------------

;This final entry point is used 1 time.

;Entry:
;  A - Contains an 8-bit fraction representing the amount to
;      interpolate between table column entries 3[X] and 4[X].
;      The column entries can be increasing or decreasing.

;  X - points at a specific entry within a row of a table.

;Returns:

;  D - An 8.8 fixed point value representing the interpolated result.
;      A value of 0x.00 would map to 100% of entry 3.
;      A value of 0x.FF would map to (255/256)% of table entry 4.

;For example, if 3[X]=0x20 and 4[X]=0x40, and on entry, A=0xC0
;(0.75), the result will be ((0x40-0x20) * (0xC0 / 256) or 0x18 added
;to the 3[X] table entry of 0x20 = 0x38.

LF3D4:  tAB
        beq     1f                      ;fraction is 0.  Result is 100% of entry 3[X].

        ldaB    4, X                    ;delta = 4[X] - 3[X]
        subB    3, X
        bhs     0f                      ;if (delta>0), table is increasing

        negB                            ;for tables that are decreasing, we negate delta and the fraction
        negA                            ;thus arranging to subtract the multiplication result from the hi entry
        inX                             ;the add 3[X] (below) will really access 4[X]

0:
        mul                             ;scale the delta by the original fraction
1:
        addA    3, X                    ;add the integer part of the scaled delta to the appropriate
                                        ;table entry (3[X] for increasing tables, 4[X] for decreasing tables)
        rts

;*****************************************************************
;Performs: D8.8 = D8.8 * 0[X]0.8

;Multiply D by a 0.8 fraction.
;Confirmed via simulation.

LF3E4:  pshA
        ldaA    0, X
        mul                             ;D = 0[X] * Dlo
        adcA    #0x00                   ;rounds the multiply result
        pulB                            ;original Dhi
        pshA                            ;save result.hi in a stack temp
        ldaA    0, X
        mul                             ;D = 0[X] * Dhi
        tSX                             ;point X at result.hi
        addB    0, X
        adcA    #0x00                   ;round it
        inS                             ;discard our stack temp
        rts

;*****************************************************************
;Perform a fixed-point multiplication of the form:
;   8.8 = 8.8 * 8.8

;Basically, we do a 16x16 multiply and return the middle two
;bytes after verifying that there was no overflow into the
;highest-order byte.

;        Dh  Dl  16
;     x  Xh  Xl  16
;     ---------
;         A1  B1  R1. (Xl*Dl)
;     A2  B2      R2. (Xl*Dh)
;     A3  B3      R3. (Xh*Dl)
; A4  B4          R4. (Xh*Dh)

;The code tosses out the highest order 8 bits and the lowest
;order 8 bits and returns the middle 16 bits, or 0xFFFF if there
;was an overflow into the uppermost 8.  The middle two columns
;are generated and summed just like the MPY, above.

LF3F6:  stX     L0155                   ;155=+2=Xh, 156=+3=Xl
        stD     L0153                   ;153=+0=Dh, 154=+1=Dl
        ldX     #0x0153
        ldaA    3, X                    ;A=Xl, B=Dl
        mul                             ;D=Xl*Dl
        staA    5, X                    ;L0158 = A1 (R1.hi) (r1.lo is discarded)

        ldaA    0, X                    ;A=Dh
        ldaB    2, X                    ;B=Xh
        mul                             ;calc R4 (A4/B4)
        tstA
        bne     0f                      ;if A4 != 0, we have overflowed into the uppermost 8 bits

        staB    4, X                    ;4,x = B4

        ldaA    0, X                    ;Dh
        ldaB    3, X                    ;Xl
        mul                             ;calc r2 (A2B2)
        addD    4, X                    ;D = A2B2 + B4A1
        blo     0f                      ;overflow error

        stD     4, X                    ;the running sum so far
        ldD     1, X                    ;A=Dl, B=Xh
        mul                             ;calc R3 (A3B3)
        addD    4, X                    ;add to running sum, giving us (A3B3) + (A2B2 + B4A1)
        bhs     1f                      ;if no overflow, we have in fact added the middle 3 columns

0:
        ldD     #0xFFFF
1:
        rts

;*****************************************************************

LF424:  .byte   0x01, 0xFF
        .byte   0x02, 0x80
        .byte   0x04, 0x40
        .byte   0x08, 0x20
        .byte   0x10, 0x10
        .byte   0x20, 0x08
        .byte   0x40, 0x04
        .byte   0x80, 0x02
        .byte   0xFF, 0x01

;*****************************************************************
; Called 10 times.
; Performs a double-interpolation operation on a 2D table as follows:
;   - Locate the proper row, then interpolate the column result.
;   - If we need to interpolate between rows, then interpolate the column result
;     on the next column.
;   - Take the two column values and interpolate between
;     them based on the fractional row information.

;Takes five params:
;  reg A:       A mystery constant, at the moment.
;  reg B:       number of columns per table row (ie. length of a row)
;  reg X:       points at the start of a 2D interpolation table.
;  Mem[L0153]:  An 8.8 value used to interpolate between two columns on a particular row.
;  Mem[L0155]:  An 8.8 value used to interpolate between two rows.


; Returns:
;  reg A contains an 8-bit integer corresponding to the rounded interpolation result.

LF436:  staB    L0158                   ;length of a table row
        staA    L0157                   ;mystery constant

        mul                             ;length of table row * mystery constant, all we care about is B
        ldaA    L0157

0:
        cmpA    L0155                   ;the integer part of the 8.8 row selector
        bhi     1f                      ;br if mystery constant is > integer part of 8.8 row selector
        aBX
        addA    L0157
        bra     0b

; There is no way out of the above except through here.
1:
        subA    L0157
        staA    L0157
        ldaA    L0155                   ;the integer part of the 8.8 row selector
        subA    L0157
        ldaB    L0158                   ;length of a table row
        mul                             ;calc integer portion of row selector * length of a row
        aBX

;Regardless of how the mystery constant works, in all cases, X now points at the row we should be using.
        pshX

        ldD     L0153                   ;An 8.8 fixed point value we will use
        jsr     LF3D0                   ;to interpolate along a row
        addD    #0x0080                 ;round the LSbit of the interpolation result to an 8-bit int in A

        tst     L0156                   ;check the fraction on the original row index.
        pulX
        beq     2f                      ;if the fration was 0, then there is no interpolation needed
                                        ;the result will be equal to the current row value.

        staA    L0157                   ;save the interpolated value from the first row

;Otherwise, we need to do another interpolation along the next row
        ldaB    L0158                   ;length of a row
        aBX                             ;point X at the next row
        ldD     L0153
        jsr     LF3D0

        addD    #0x0080                 ;round the result to an 8-bit int in A
        staA    L0158

        ldaA    L0156
        ldX     #0x0154                 ;point at a fake table to interpolate the results obtained from each row

; Reg A contains a 0.8 fraction describing how the desired level of interpolation
; between the endpoints stored at 3[X] (the interpolated value from the first row)
; and 4[X] (the interpolated value from the second row)
        jsr     LF3D4

; Reg D contains an 8.8 fraction that describes the interpolated result

        addD    #0x0080                 ;round the LSbit to return the interpolated value in A
2:
        rts

;-----------------------------------------------------------------
; 8 NOPs here causes the bike to not start.

;*****************************************************************
;OC3 controls the FRONT cyl fuel injector.

;See next routine for comments.

LF48C:  ldaA    TCTL1
        oraA    #0b00100000
        andA    #0b11101111
        staA    TCTL1
        ldaA    #0x20
        staA    TFLG1
        ldaA    TMSK1
        andA    #0b11011111             ;disable OC3 interrupts
        staA    TMSK1

#if 1
        ldD     TCNT
        addD    #0x0005
        stD     TOC3

        stD     LW+LOGID_ECU_F_INJ_ON_TYPE_PTS ; 5E [14]  +7.0 uSec: log the injector ON timestamp
        addD    TOC3                    ;6E [20] +10.0 uSec: pointless, except as a NOP
#else
        ldD     TCNT
        addD    #0x0005
        stD     TOC3

        ldD     TCNT                   ;5E [14]  +7.0 uSec: seemingly pointless, except as a NOP
        addD    TOC3                   ;6E [20] +10.0 uSec: also pointless, except as a NOP
#endif

        ldaA    #0x20
        staA    TFLG1
        ldaA    TMSK1
        andA    #0b11011111             ;disable IC3 ints
        staA    TMSK1
        ldaA    TCTL1
        oraA    #0b00110000
        staA    TCTL1
        ldD     L0166                   ; Injector pulse duration (in ticks)
        stD     LW+LOGID_ECU_F_INJ_DUR_TYPE_U16
        addD    TOC3
        stD     TOC3

        rts

;*****************************************************************
; OC4 controls operation of the REAR cyl fuel injector.

; OC4 match function drives PA4 that controls injector PNP driver transistor T461.
; OC4 interrupts are never used.

LF4D1:  ldaA    TCTL1
        oraA    #0b00001000
        andA    #0b11111011             ; xxxx10xx means that an OC4 match event will drive PA4=0 (means injector squirting)
        staA    TCTL1

        ldaA    #0x10                   ; clear the OC4 interrupt request flag
        staA    TFLG1

        ldaA    TMSK1
        andA    #0b11101111             ; inhibit OC4 interrupts
        staA    TMSK1

#if 1
        ldD     TCNT                    ; mark time T0
        addD    #0x0005                 ; 4E [ 4]  +2.0 uSec: 10 uSec from T0, an OC4 match event will occur...
        stD     TOC4                    ; 5E [ 9]  +4.5 uSec: ...that that enables the REAR fuel injector

; D contains the injector ON timestamp:
        stD     LW+LOGID_ECU_R_INJ_ON_TYPE_PTS ; 5E [14]  +7.0 uSec: log it
        addD    TOC4                    ; 6E [20] +10.0 uSec: a NOP, since D is not used
#else
        ldD     TCNT                    ; mark time T0
        addD    #0x0005                 ; 4E [ 4]  +2.0 uSec: 10 uSec from T0, an OC4 match event will occur...
        stD     TOC4                    ; 5E [ 9]  +4.5 uSec: ...that that enables the REAR fuel injector

        ldD     TCNT                    ; 5E [14]  +7.0 uSec: these two instrs are NOPs since we never use D
        addD    TOC4                    ; 6E [20] +10.0 uSec: OC4 should match during this instruction, driving PA4=0
#endif

        ldaA    #0x10                   ;2E [22] +11.0 uSec:  clear OC4F interrupt flag that was set by the OC4 match
        staA    TFLG1                   ;4E [26] +13.0 uSec

        ldaA    #0x10                   ;2E [28] +14.0 uSec: weird: why do this twice in a row?
        staA    TFLG1                   ;4E [32] +16.0 uSec: paranoia perhaps?

        ldaA    TMSK1
        andA    #0b11101111             ; inhibit OC4 interrupts (more paranoia? We already inhibited them, above)
        staA    TMSK1

        ldaA    TCTL1                   ; Program the next OC4 match to disable the fuel injector
        oraA    #0b00001100             ; xxxx11xx means that an OC4 match event will set PA4 back to 1
        staA    TCTL1

; Calculate the future time at which to stop the injector from squirting
        ldD     L0168                   ; get the previously calculated injection pulse duration
        stD     LW+LOGID_ECU_R_INJ_DUR_TYPE_U16 ;log the injector duration
        addD    TOC4                    ; add the duration to the injection pulse start time (T0 + 10 uSec)
        stD     TOC4                    ; the result becomes the new OC4 event match time

        rts

;*****************************************************************
;Start the Front injector injecting.
;Mainline code always disables INTs before calling this routine.
LF51B:  ldaA    PORTA
        bitA    #0b00100000             ;test OC3/PA5 (Front injector)
        beq     0f                      ;if it is already injecting?

;It is not already injecting:
        ldD     *L005B
        stD     L0166                   ;L0166 specifies how long to squirt the FRONT injector
        bra     3f

;The injector is injecting:
0:
        ldD     TOC3
        subD    TCNT                    ;how long has it been injecting?
        blo     1f
        subD    LC010                   ;68 uSec
        bhs     2f
1:
        ldD     #0x0000
2:
        stD     L0162
        addD    *L005B
        subD    L016A
        stD     L0166

3:
        jsr     LF48C                   ;drive the front injector for the amount of time in L0166
        clr     L016A
        rts

;*****************************************************************
;Looks like an indentical operation except for the REAR injector
LF54B:  ldaA    PORTA
        bitA    #0b00010000             ;test OC4/PA4 (Rear injector)
        beq     0f

        ldD     *L005B
        stD     L0168
        bra     3f

0:
        ldD     TOC4
        subD    TCNT
        blo     1f
        subD    LC010
        bhs     2f
1:
        ldD     #0x0000
2:
        stD     L0162
        addD    *L005B
        subD    L016C
        stD     L0168

3:
        jsr     LF4D1                   ;start the rear injector injecting
        clr     L016C
        rts

;*****************************************************************
; Called during both fuel and ignition processing for ALPHA/N tables.

; One param:
;  reg X: pointer to a table (front or rear cyl, Ign or ALPHA/N map)

LF57B:  ldaA    *L0044                  ;Get the upper 8 bits of the left-aligned 10-bit VTA A/D reading

        brclr   *L000C #0b00001000 1f   ;branch if the VTA (throttle) sensor is GOOD (ie. not reporting as 'broken')

; The throttle position sensor is reporting as broken:
        xgDX
        cmpD    #L91E5-3                ;L91e5: Ignition map 18x29 Restricted ALPHA/N cyl 1
        xgDX
        beq     0f

        xgDX
        cmpD    #L93EF-3                ;L93ef: Ignition map 18x29 Restricted ALPHA/N cyl 2
        xgDX
        beq     0f

        xgDX
        cmpD    #L95F9-3                ;L95F9: Ignition map 18x29 Derestricted ALPHA/N cyl 1
        xgDX
        beq     0f

        xgDX
        cmpD    #L9803-3                ;L9803: Ignition map 18x29 Derestricted ALPHA/N cyl 2
        xgDX
        beq     0f

        bra     1f                      ;X is not pointing at any of the ignition maps: therfore, X must point at a fuel map

0:
        ldaA    #0xD0                   ;This would have to be the default throttle angle for when
                                        ;the throttle position sensor is broken

1:
        clrB                            ;D now holds VTA in the form of an 8-bit left aligned value

; We now massage the throttle angle data so that it can be used to index the ALPHA/N ignition tables.
; tables based on throttle angle.
; Remember that the throttle sensor does not span the entire ADC range.
; The ADC reading when the throttle is completely closed will be above 0.
; Similarly, the ADC reading when the throttle is wide open will be less than the max ADC count.
; This allows the ECU to detect things like open or shorted throttle sensors because those situations
; would result in extreme ADC readings.
; The process of using diag mode to calibrate the throttle position sensor to read '0' on the dash
; is what ensures that the throttle-closed ADC reading matches what these tables expect.
; There is no point in calibrating at the high end of throttle openings because small
; differences in throttle openings make almost no difference to the airflow when the throttle
; is almost completely open.
;
; Sensor ranges:
;
; The lowest 10-bit right-aligned raw throttle ADC reading I have ever seen in a log is 129 ADC counts.
; Most are in the very low 130 counts.
; In 10-bit left-shifted ADC terms, 129 counts would be 0x2040
;
; The highest 10-bit right-aligned raw throttle ADC reading I have seen in a log is 801 ADC counts.
; In 10-bit left-shifted ADC terms, 801 counts would be 0xC840.
;
; This next section biases the 8-bit left-aligned VTA downwards by 0x2000,
; then clips the biased VTA to the range: [0000..B000].
; This lines up perfectly with a fully-closed throttle being calibrated to
; have a value right around 0x2000, as per above.
; After subtracting 0x2000, the value 0x0000 means 'throttle closed'.
; One would have to expect that 0xB000 must mean wide open.
; After subtracting 0x2000, my 801 ADC counts at WOT would have a value of 0xA840.
; That is well within the upper limit of 0xB000 table limit.
; It does give some indication though of how sloppy the table is at the upper throttle opening ranges.

        subA    #0x20                   ;Bias VTA downwards by 0x2000
        bhs     2f                      ; all good if D >= 0

        ldD     #0x0000                 ; else D went negative so clip it to 0

2:
        subA    #0xB0                   ;After subtracting 0x2000 (above), make sure that A is <= 0xB000
        blo     3f                      ; all good

        ldD     #0x0000
3:
        addA    #0xB0                   ; else clip D to a max value of 0xB000

; D is now known to be in the range [0x0000..0xB000].

; Assumptions:
;   0x0000 means 0% open (this is a safe assumption)
;   0xB000 means 100% open (this is a reasonably safe assumption)
;
; The ADC response of the TPS appears to be linear, but the table indexing is most certainly not linear.
; At small throttle openings, changes in throttle position matter significantly more than at large throttle openings.
; This means that the tables will be biased towards having more entries devoted to small throttle openings.
;
; We now need to convert our biased, clipped VTA into a value suitable of indexing the ALPHA/N tables.
; There are 18 table column entries. We will be interpolating between them.
; This means that we need to convert our biased, clipped VTA into a fixed point value ranging from 0.00 to 17.00 (for our 18 columns)
; The following code breaks the throttle response into 5 basic ranges:
;    0xB000  45056  100.00% open
;    0x7000  31744   70.45% open
;    0x3000  12288   27.27% open
;    0x1000   4096    9.09% open
;    0x0400   1024    2.27% open
;    0x0000      0    0.00% open
; Note: "% open" is defined in a rotational aspect (assuming a linear TPS response),
; meaning 0% means throttle butterflies are fully closed, and 100% means wide-open throttle butterflies.

        cmpA    #0x70
        blo     4f                      ; branch if D < 0x7000

; Large throttle openings don't matter much.
; The ADC range from 0x7000 to 0xB000 gets compressed into the table range 15.0 to 17.0
; if ((D >= 0x7000) && (D <= 0xB000)) D = 0x0B80 + (D/32)
        lsrD
        lsrD
        lsrD
        lsrD
        lsrD                            ; 0x7000 turns into 0x380, 0xB000 turns into 0x580
        addD    #0x0B80                 ; 0x7000 turns into 0x0F00 (15.0), 0xB000 turns into 0x1100 (17.0)
        bra     7f

4:
        cmpA    #0x30
        blo     5f

; The ADC range from 0x3000 to 0x6FFF gets compressed into the table range 11.0 to 14.99
; if ((D >= 0x3000) && (D < 0x7000)) D = 0x0800 + (D/16)
        lsrD
        lsrD
        lsrD
        lsrD                            ; 0x3000 turns into 0x300, 0x6FFF turns into 0x6FF
        addD    #0x0800                 ; 0x3000 turns into 0xB00 (11.0), 0x6FFF turns into 0xEFF (14.99)
        bra     7f

5:
        cmpA    #0x10
        blo     6f

; The ADC range from 0x1000 to 0x2FFF gets compressed into the table range 7.0 to 10.99
; if ((D >= 0x1000) && (D < 0x3000)) D = 0x0500 + (D/8)
        lsrD
        lsrD
        lsrD                            ; 0x1000 turns into 0x200, 0x2FFF turns into 0x5FF
        addD    #0x0500                 ; 0x1000 turns into 0x700, 0x2FFF turns into 0xAFF
        bra     7f

6:
        cmpA    #0x04
        blo     7f

; The ADC range from 0x0400 to 0x1000 gets compressed into the table range 4.0 to 6.99
; if ((D >= 0x0400) && (D < 0x1000)) D = 0x0300 + (D/4)
        lsrD
        lsrD                            ; 0x400 turns into 0x100, 0x1000 turns into 0x3FF
        addD    #0x0300                 ; 0x400 turns into 0x400, 0x1000 turns into 0x6FF

; The ADC range from 0x0000 to 0x0FFF gets compressed into the table range 0.0 to 3.99
; if (D <0x0400) then leave it alone

                                        ; 0x0000 is still 0x0000, 0x03FF is still 0x03FF

; Conversion of raw VTA Throttle angle to an interpolation table column index.
; "% open" is defined as a linear relationship between the min and max raw VTA ADC readings
; corresponding to when the butterflies are fully closed and fully opened.
;
; Table    Massaged 8P8       Throttle Opening
; Column                      (rotational sense)
;   0:     00.00 .. 00.FF     0.00% to  0.56% open \
;   1:     01.00 .. 01.FF     0.57% to  1.13% open  \  linear across the range of 0.00 to 2.26%
;   2:     02.00 .. 02.FF     1.14% to  1.69% open  /  Column step size is 0.57%
;   3:     03.00 .. 03.FF     1.70% to  2.26% open /

;   4:     04.00 .. 04.FF     2.27% to  4.53% open \
;   5:     05.00 .. 05.FF     4.54% to  6.80% open  |  linear across the range of 2.27 to 9.08%
;   6:     06.00 .. 06.FF     6.81% to  9.08% open /   Column step size is 2.27%

;   7:     07.00 .. 07.FF     9.09% to 12.72% open \
;   8:     08.00 .. 08.FF    12.73% to 16.36% open  \
;   9:     09.00 .. 09.FF    16.37% to 19.99% open   | linear across the range of 9.09 to 27.26%
;  10:     0a.00 .. 0a.FF    20.00% to 23.63% open  /  Column step size is 3.64%
;  11:     0b.00 .. 0b.FF    23.64% to 27.26% open /

;  12:     0c.00 .. 0c.FF    27.27% to 41.65% open \
;  13:     0d.00 .. 0d.FF    41.66% to 56.05% open  |  linear across the range of 27.27% to 70.44%
;  14:     0e.00 .. 0e.FF    56.06% to 70.44% open /   Column step size is 14.39%

;  15:     0f.00 .. 0f.FF    70.45% to 80.29% open \
;  16:     10.00 .. 10.FF    80.30% to 90.14% open  |  linear across the range of 70.45% to 100%
;  17:     11.00 .. 11.FF    90.15% to 100.0% open /   Column step size is 9.85%

; Finally!  VTA, massaged into an 8.8 value suitable for indexing into the columns for an 18-entry interpolation table.

; The integer part of the massaged VTA value is used to select a table column for the lower interpolation value.
; The upper interpolation value always comes from the next table column.
; The fractional part of the massaged VTA value is used to interpolate between the two interpolation column values.
; For example, assume that the massaged VTA was 4.25, that the value in table column 4 was 10,
; and the table value in column 5 was 18.
; The integer part of 4.25 means we will interpolate between the entries in column 4 and 5.
; In this case, we need to interpolate 0.25 of the distance between the column 4 value (10) and the column 5 value (18).
; The interpolation calculation result would be ((upper_value - lower_value) * distance) + lower_value:
;       ((18-10) * .25) + 10
;       12

7:
        stD     L0153                   ; store our column interpolation parameter value where the interpolation routine expects it

        stD     L016E                   ;never used: must be for debugging

        ldD     *L004F                  ; 8.8 RPM in units of 400 RPM
        stD     L0155                   ; store the RPM parameter value where the interpolation routine expects it

        ldaB    #18                     ;tell the interpolation routine the length of the ALPHA/N fuel table row
        ldaA    #0x0E
        jsr     LF436

        rts

;*****************************************************************
; Called during MAP/N fuel map processing

;two params: X, A
LF5F9:  clrB                            ;D = AA00
        cmpA    #0x78                   ;is D < 0x7800
        blo     0f                      ;if yes

        ldD     #0x0000                 ;D is > 0x7800: clear it
        bra     1f

0:
        stD     L0153
        ldD     #0x7800
        subD    L0153                   ;calc 0x7800 - D
        lsrD
        lsrD
        lsrD                            ;D= (0x7800-D)/8

1:
        stD     L0153
        stD     *L009D

        ldD     *L004F
        stD     L0155

        ldaB    #16
        ldaA    #0x0D
        jsr     LF436

        rts

;*****************************************************************
;Takes a parameter X, where X points at a 5-element byte array
;starting at L9FF6:  db 0x48, 0x24, 0x1A, 0x0B, 0x0B

;Only called from one location.

LF621:  ldaB    PORTH
        bitB    #0b00000100             ;test WL
        beq     0f                      ;br if WL=0

;WL is currently a '1':
        andB    #0b11111011             ;WL is 1: clear it
        staB    PORTH

        ldaB    L014C
        andB    #0b11110000
        bne     1f

        ldaA    L014B
        andA    #0b00001111
        bne     2f

;We get here if (MS nibble of L014C==0) && (LS nibble of LL014B==0)
        clr     L014C
        bra     6f                      ;exit with no more machinations

;WL is currently a '0':
0:
        ldaB    L014B
        cmpB    L014C
        bne     4f

        ldaB    PORTH
        oraB    #0b00000100             ;sets WL=1
        staB    PORTH

        ldaB    L014B
        andB    #0b11110000
        beq     5f

        ldaA    L014B                   ;decrement MS nybble of L014B
        subA    #0x10
        staA    L014B

        ldaB    2, X                    ;0x1A (26.)
        staB    L014D
        bra     6f                      ;exit


1:
        ldaA    1, X                    ;0x24 (36.)
        staA    L014D
        bra     3f


2:
        ldaB    3, X                    ;0x0B (11.)
        staB    L014D

3:
        ldaA    L014B
        staA    L014C
        bra     6f                      ;exit


4:
        ldaA    0, X                    ;0x48 (72.)
        staA    L014D
        bra     3b


5:
        ldaA    L014B                   ;decrements LS nybble of L014B
        subA    #0x01
        staA    L014B

        ldaB    4, X                    ;0x0B (11.)
        staB    L014D

6:
        rts


;*****************************************************************
;PG1 state determination.  Unlike the sidestand computation, this
;one has asymetrical thresholds.

;The circuit for PG1 is kind of weird: it appears to be a NOR
;gate wired as an inverter with the output fed back to the input
;through a large R (330K).  The input is biased with a 47K R to
;Vcc, and a 10K R to GND.  This would work out to about 18%/82%.
;For Vcc of 5V, the input would be biased to:
;       5 - (.82 * 5)
;       5 - 4.125 = 0.875V

LF68F:  ldaB    *L0049_PORTG_debounced
        bitB    #0b00000010
        bne     0f                      ;if PG1==1

;PG1==0
        clr     L0115                   ;clear the counter for the opposite state
        ldaA    L0114                   ;inc the counter for this state up to a max of 255
        addA    #0x01
        sbcA    #0x00
        staA    L0114
        cmpA    L802E                   ;0x04
        blo     1f                      ;if the counter is < the threshold, then leave
        bset    *L0000 #0b00000001      ;else set the appropriate flag
        rts

;PG1==1
0:
        clr     L0114
        ldaA    L0115
        addA    #0x01
        sbcA    #0x00
        staA    L0115
        cmpA    L802F                   ;0x06
        blo     1f
        bclr    *L0000 #0b00000001      ;what does this indicate?
1:
        rts

;*****************************************************************
;Sidestand position determination.  We must see the sidestand
;read in the same position for 16 times in a row before we change
;our mind about its position.

;I suspect that a '0' means that the stand is down, but shouldn't
;be.  This is based on looking at the DON_FILT code.  When the
;code branches on DON_FILT=1, it makes the identical branch on
;SS=0.

LF6C1:  ldaB    *L0049_PORTG_debounced
        bitB    #0b00000100             ;test the SS (Side Stand) switch
        beq     0f

;Sidestand sw reads as '1' meaning ??
        ldaA    L0117                   ;inc the counter to a max of 255
        addA    #0x01
        sbcA    #0x00
        staA    L0117

        clr     L0116                   ;clear the counter for the opposite state
        cmpA    L9A51                   ;have we hit the threshold [0x0F]?
        blo     1f                      ;if <, then no

        bset    *L0004 #0b00000010
        rts

;Sidestand sw reads as '0' meaning ??
0:
        ldaA    L0116                   ;inc the counter to a max of 255
        addA    #0x01
        sbcA    #0x00
        staA    L0116
        clr     L0117                   ;clear the counter for the opposite state
        cmpA    L9A51                   ;have we hit the threshold?
        blo     1f                      ;if <, then no

        bclr    *L0004 #0b00000010

1:
        rts

;*****************************************************************
;The DON tip-over filter code.  Basically, we need to see DON
;asserted and then still asserted 100 times through the main loop
;later before we decide that the bike is on its side.

LF6F3:  ldaA    L0118                   ;inc L0118, but not past 255
        addA    #0x01
        sbcA    #0x00
        staA    L0118

        ldaA    L0118
        cmpA    L9A4A                   ;100. means 0.5 seconds since this routine is called every 5 mSec
        blo     4f                      ;br if counter < 100 (unsigned)

        brset   *L0004 #0b00100000 2f   ;1 means that the bike appears to have fallen over
        brset   *L0005 #0b10000000 0f   ;DON_RAW=1 indicates bike fell over
        brclr   *L0004 #0b00000001 4f   ;0 means we are sure that the bike is upright
        bra     1f                      ;else the we are sure the bike fell over

;DON is asserted.
0:
        brset   *L0004 #0b00000001 4f   ;if we already know that the bike tipped over leave now

1:
        bset    *L0004 #0b00100000      ;the bike appears to have fallen over
        clr     L0118                   ;check again 100 iterations from now
        bra     4f

;100 times ago, the bike appeared to have fallen over.
2:
        bclr    *L0004 #0b00100000      ;clear the "appears to have fallen over" flag
        brset   *L0005 #0b10000000 3f   ;DON_RAW=1 means bike fell over

;If we get here, the bike appears to be upright.
        brclr   *L0004 #0b00000001 4f   ;we are not really, really sure that the bike fell over
        bra     4f

;If we get here, the bike appeared to have fallen over 100 times
;ago, and still appears to be on its side:
3:
        brset   *L0004 #0b00000001 4f   ;if we already knew that, just leave (superfluous actually)
        bset    *L0004 #0b00000001      ;DON_FILT: we are really, really sure that the bike fell over.

4:
        rts

;*****************************************************************
; Called 3 times.

LF734:  ldaA    *L00C7
        clrB
        subA    #0x80
        bhs     0f
        ldD     #0x0000
        bra     2f

0:
        cmpA    #0x70
        blo     1f
        ldD     #0x0700
        bra     2f

1:
        lsrD
        lsrD
        lsrD
        lsrD
2:
        stD     L0175
        stD     L0153
        ldD     *L004D                  ;8.8 RPM50
        subA    #0x10                   ;16 == 800 RPM means bias the current 8.8RPM50 downwards by 800
        bhs     3f                      ;br if current RPM >= 800
        ldD     #0x0000                 ;RPM below 800 is clipped to 0.0
        bra     5f

3:
        cmpA    #0x90                   ;0x90 biased by 0x10 == 0xA0 would be 8000 RPM
        blo     4f                      ;if RPM < 8000
        ldD     #0x0900                 ;else clip to 8000 RPM800
        bra     5f

4:
        lsrD
        lsrD
        lsrD
        lsrD                            ;convert to 8.8RPM800

5:
        stD     L0155
        stD     L0177
        ldX     #0x9A96                 ;This table is full of nothing???
        ldaB    #0x08                   ;length of a row (number of columns in table)
        ldaA    #0x0A                   ;mystery constant
        jsr     LF436
        staA    L0173
        ldD     L0177
        stD     L0155
        ldD     L0175
        stD     L0153
        ldX     #0x9B00                 ;This table also appears to be full of nothing???
        ldaB    #0x08
        ldaA    #0x0A
        jsr     LF436
        staA    L0174
        ldaB    L0173
        rts

;-----------------------------------------------------------------
; Put in some handlers to notify the UltraMod log if bad things happen to the ECU

#if !defined SPARK_ISR
_oc5ic4f:
        ldaA    #LOGID_ECU_CPU_EVENT_VAL_OC5F
        bra     _log

_ic3:   ldaA    #LOGID_ECU_CPU_EVENT_VAL_IC3
        bra     _log
#else
; IC4: Coil #2 event
_oc5ic4f:
        ldD     TO5I4
        stD     LW+LOGID_ECU_SPRK_X2_TYPE_PTS   ; log when coil #2 spark occurred
        ldaA    #0x08                           ; clear interrupt request for bit O5I4
        staA    TFLG1
        oraA    FAKE_TFLG1                      ; crankshaft processing needs to see this bit
        staA    FAKE_TFLG1
        rti

; IC3: Coil #1 event
_ic3:
        ldD     TIC3
        stD     LW+LOGID_ECU_SPRK_X1_TYPE_PTS   ; log when coil #1 spark occurred
        ldaA    #0x01                           ; clear interrupt request for bit IC3
        staA    TFLG1
        oraA    FAKE_TFLG1                      ; crankshaft processing needs to see this bit
        staA    FAKE_TFLG1
        rti
#endif

_oc4f:  ldaA    #LOGID_ECU_CPU_EVENT_VAL_OC4F
        bra     _log

_oc3f:  ldaA    #LOGID_ECU_CPU_EVENT_VAL_OC3F
        bra     _log

_rti:   ldaA    #LOGID_ECU_CPU_EVENT_VAL_RTI
        bra     _log

_irq:   ldaA    #LOGID_ECU_CPU_EVENT_VAL_IRQ
        bra     _log

_xirq:  ldaA    #LOGID_ECU_CPU_EVENT_VAL_XIRQ
        bra     _log

_swi:   ldaA    #LOGID_ECU_CPU_EVENT_VAL_SWI
        bra     _log

_illegalOp:
        ldaA    #LOGID_ECU_CPU_EVENT_VAL_IOP
        bra     _log

_cop:   ldaA    #LOGID_ECU_CPU_EVENT_VAL_COP
        bra     _log

_cmf:   ldaA    #LOGID_ECU_CPU_EVENT_VAL_CMF
        bra     _log

_por:   ldaA    #LOGID_ECU_CPU_EVENT_VAL_RESET
; Intentional fall through!

_log:   ldaB    #LOGID_GEN_ECU_LOG_VER_VAL_V0
        staB    LW+LOGID_GEN_ECU_LOG_VER_TYPE_U8        ; #1: log version information before anything else

        staA    LW+LOGID_ECU_CPU_EVENT_TYPE_U8          ; #2: log the specific CPU event that got us here
        jmp     reset                                   ; Boot the ECU


; To help with tracking of elapsed time in the log, we log all timer1 overflow events
; Notes: there is no possibility of a tie as to servicing OFLO versus input capture.
; If the timer overflowed and we had an input capture on the same timer value (0x0000),
; both of the input capture ISRs have higher priority than this overflow ISR.
T1_OFLO_ISR:
#if 1
        ldaA    #0x80                                   ; ACK this overflow interrupt
        staA    TFLG2
        staA    half_overflow                           ; this value just needs to be non-zero

        clrA
        clrB
        stD     LW+LOGID_ECU_T1_OFLO_TYPE_TS            ; The overflow event occurred at timer count 0x0000
        rti
#elif 0
; If we see a pending capture ISR in this routine, it MUST have happened after the overflow.
; That means we don't even have to log this rollover event.

        ldaA    #0x80                                   ; ACK the overflow interrupt
        staA    TFLG2
        staA    half_overflow                           ; this value just needs to be non-zero

        ldaA    TFLG1                                   ; check if either IC1 (cam) or IC2 (crank) has become pending
        andA    #0x06                                   ; between the time that the counter rolled over and the time we got here
        bne     99f                                     ; yes: there is no need to even emit this marker

        clrA
        clrB
        stD     LW+LOGID_ECU_T1_OFLO_TYPE_TS            ; log the overflow event as having occurred at timer count 0x0000
        rti
#else
        ldD     TCNT
        stD     LW+LOGID_ECU_T1_OFLO_TYPE_TS            ; log the overflow event

        ldaA    #0x80                                   ; ACK the overflow interrupt
        staA    TFLG2
        staA    half_overflow                           ; just needs to be non-zero
        rti
#endif

;----------------------------------------------------------------
lastInstr:

;*****************************************************************
; This expression assumes that this file was org'd to relocatable address $0000
; but will get relocated during the link phase to the range 0x8000..0xFFFF.
        .org    (0xFFC0 - 0x8000) - 4, 0x3f    ; The 0x3F is the HC11 opcode "SWI"

; I don't know why these 4 bytes are sitting right in front of the vector table, but we leave them untouched.
; The code does not reference them in any way.
        .byte   0x00, 0x00, 0xB9, 0x73

;-----------------------------------------------------------------
;Start of the 68HC11 vector table (64 bytes long )
LFFC0:      .word   reset               ;reserved FFC0/FFC1
            .word   reset               ;reserved FFC2/FFC3
            .word   reset               ;reserved FFC4/FFC5
            .word   reset               ;reserved FFC6/FFC7
            .word   reset               ;reserved FFC8/FFC9
            .word   reset               ;reserved FFCA/FFCB

            .word   reset               ;Event 2
            .word   reset               ;Event 1
            .word   reset               ;Timer Overflow 2
            .word   reset               ;Timer OC7/IC6
            .word   reset               ;Timer OC6/IC5
            .word   reset               ;SCI serial system
            .word   reset               ;SPI serial transfer complete
            .word   reset               ;Pulse Accumulator Input Edge
            .word   reset               ;Pulse Accumulator Overflow
            .word   T1_OFLO_ISR         ;Timer Overflow 1
OC5IC4F:    .word   _oc5ic4f            ;Timer Output Compare 5 / Input Capture 4
OC4F:       .word   _oc4f               ;Timer Output Compare 4
OC3F:       .word   _oc3f               ;Timer Output Compare 3
OC2F:       .word   OC2F_ISR            ;Timer Output Compare 2
OC1F:       .word   OC1F_ISR            ;Timer Output Compare 1
IC3F:       .word   _ic3                ;Timer Input Capture 3
IC2F:       .word   IC2F_ISR            ;Timer Input Capture 2
IC1F:       .word   IC1F_ISR            ;Timer Input Capture 1
RTII:       .word   _rti                ;Real Time Interrupt
IRQ:        .word   _irq                ;external interrupt pin
XIRQ:       .word   _xirq               ;pseudo-NMI
SWI:        .word   _swi                ;Sofware Interrupt
ILLEGAL_OP: .word   _illegalOp          ;illegal opcode trap
COP:        .word   _cop                ;COP failure (reset)
CMF:        .word   _cmf                ;Clock Fail Monitor (reset)
POR:        .word   _por                ;Reset

        end
