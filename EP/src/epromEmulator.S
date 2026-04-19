// -------------------------------------------------------------------------------------------------------------
// A 68HC11 EPROM emulator based on an RP2040
//
// General Timing Notes
//
// - HC11 Clock Rate: 2.0000 MHz
//   The HC11 E-cycle ostensibly has a period of 500 nS. Given that the E clock is allowed 20 nS
//   of rise or fall time, we really only have 500-20 or 480 nSec max from the time that the
//   initial edge can be detected to getting back to sleep to wait for the start of the next E cycle.
//
// - Basic RP2040 Clock Rate: 125 MHz
//   We will configure the RP2040 to run at 125 MHz to make a nice round 8.0 nS per clock.
//   This means that there are 480 nSec / 8nS per clock or 60 RP2040 clocks per E-cycle.
//   We will target having our loop complete within 58 cycles (worst case) in order to give
//   the processor time to execute the WFI.
//   In case we become desparate, the RP2040 is spec'd to run at 133 MHz, which would give us
//   an extra 4 instruction clocks.  We'll save that for a disaster when we might need 1 more cycle...
//
// - HC11 Crystal Inaccuracies
//   We can ignore the inaccuracies in the HC11 crystal circuitry.  The reasoning is as follows:
//   Crystals are typically accurate to 50 PPM, or 0.005%.  Given that our EPROM service code
//   has to be less than 60 instructions long, the HC11 crystal would need to be running roughly
//   2% fast before the inaccuracy would cause us to lose an entire RP2040 instruction cycle.
//
// - RP2040 Instruction Timing
//   General instruction timing info can be found in the Cortex M0+ Technical Reference Manual
//   rev R0P1, section 3.3 "Instruction set summary".
//
// - GPIO Delays In Recognizing Signal Changes
//   The RP2040 has input synchronizers to avoid metastabilities when sampling GPIOs.
//   The synchronizers introduce a 2 cycle latency into observing any changes on the GPIOs.
//   For more info on the synchronizers: see PROC_IN_SYNC_BYPASS, in RP2040 datasheet p305
//
// - Propagation Delays Through the Voltage Translation Buffer
//   The E signal passes through a 74LVC541 to translate it from the 5V HC11 domain to the 3.3V
//   RP2040 domain. This nominally costs approx 4 nS from the EP2040's point of view.
//   If you are scoping the E signal in the 5V domain (where the test point is located)
//   be aware that the signal will be delayed by approx 4 nS before the synchronizers
//   even get a chance to see it.
//
// - RP2040 wake from WFI is slower than expected
//   Scope measurements show that the latency from an event causing WFI to wake up to the time
//   that the first instruction after the WFI getting executed does NOT match the doc saying
//   it should execute in 2 cycles. Given that the behavior is undocumented, we will accept it
//   as a fact that the latency from falling E to WFI completing is 11 clocks (min) to 12 clocks (rounded, max).
//
// - Instruction Timing Accounting Mechanism
//   Timing within the processing loop will be accounted on a per-instruction basis.
//   Each instruction will document its timing operation in the form "x [y]" where:
//      'x' is the number of cycles it will take to execute the instruction, and
//      'y' is the cumulative number of cycles since the falling edge of E.
//   Branches will be accounted for as 1 clock (assume branch not taken).
//   To account for situations where the branches are taken, the target label of all branches
//   must appear on an otherwise empty source line (no opcode!).  The label itself will
//   get billed for the 1 cycle penalty to get there.
//
// - Core1 and the APB bridge
//   The original code used GPIO interrupts to detect falling E. This meant that core1 would
//   need to clear the GPIO interrupt by writing to a GPIO register located on the far side of
//   the APB bridge. This prevented core0 from accessing anything on the far side of the APB
//   bridge because APB bus contention could delay a core1 access from completing. This has been
//   The WFI wake mechanism has been updated to use a PIO state machine to detect falling E
//   and generate a PIO interrupt. Since PIO interrupt registers are core-specific, the interrupt clear
//   does not need to cross the APB bridge any more.
//   Core0 may now use APB silicon at its convenience.

        #include "pico/asm_helper.S"

        #include "hardware/regs/addressmap.h"
        #include "hardware/regs/sio.h"
        #include "hardware/regs/io_bank0.h"
        #include "hardware/regs/m0plus.h"

        #include "config.h"
        #include "hardware.h"

        .syntax unified
        .global epromTask

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Warning: we will be taking extreme liberties in the code that follows!
//
// - Core1 will not be calling any subroutines or using a stack in any way at all.
//   This means that every single Cortex register is available for use except the PC.
// - We will be using Core1's NVIC interrupt mechanisms to synchronize our timing to
//   the 68HC11 E-clk but we will never execute any interrupt handlers.
// - Core1 will be set up with exactly 1 interrupt source: falling edge interrupts on E.
//   Knowing this, we can save a couple cycles by using a single 'all ones' mask to clear
//   every possible source in the GPIO NVIC pending interrupt register
//   as well all possible forms of interrupt request in the GPIO interrupt register.
//
// The first three low registers (r0..r2) are reserved for general purpose computation/storage.
//
// The remaining 5 low registers (r3..r7) will be used as permanent storage for
// hold various constants and pointer values. Loading those constants from instruction RAM via
// the typical ARM "LDR rx, =<constant>" mechanism would cost 2 cycles per constant.
// If a constant is used more than once during the loop, it should be in a low register.
//
// The Cortex M0 high registers (r8..r12) are normally pretty useless.
// Generally, they can only be used as the target of a MOV.  Some can be used as the target of a CMP,
// which we will use to our advantage.  In spite of their general lameness, MOVing a constant from
// a high register to a low register where it can be used for computation takes only 1 cycle,
// which is twice as fast as loading a constant from instruction RAM.
//
// Define some register name aliases to make it easier to read the source that follows:
//                              r15     // (PC, not usable!)
#define rh_ECU_LOG_END          r14     // (LR, can be used with CMP) Points at the first address after the end of the ECU log buffer
#define rh_ECU_LOG_START        r13     // (SP, can be used as target of MOV) Points at the first address of the ECU log buffer. Used to reset the pointer after it falls off the end
#define rh_RAM_END_OFFSET       r12     // Holds the RAM window's end offset within the 15-bit EPROM address space
#define rh_RAM_START_OFFSET     r11     // holds the RAM window's start offset within the 15-bit EPROM address space
#define rh_NVIC_ICPR_PTR        r10     // Core 1 NVIC_ICPR address — used by post-WFI ack and pre-sync flush
#define rh_ALL_ONES             r9      // 0xFFFFFFFF — used by post-WFI ack and pre-sync flush
                                        // The mask of all 1's means it clears the PIO interrupt no matter which one actually got assigned
//unused                        r8
//-------------------------------------
#define r_SIO_BASE_PTR          r7      // SIO accesses through this pointer register take only 1 cycle (yay SIO!)
#define r_DBG_BSY_MSK           r6      // holds a bitmask for a test GPIO that gets driven to indicate the start and end of an HC11 transation
#define r_EPROM_DATA_PTR        r5      // points at the start of the 32K EPROM code image we are serving to the HC11
#define r_PIO_TXF_PTR           r4      // the address of the PIO FIFO waiting for read data
#define r_ECU_LOG_PTR           r3      // points at the location in the log to store the next ECU memory access
// R0..R2 are reserved for general-purpose usage in the code.


// Define RAM window's 15-bit start and end EPROM offsets.
#define RAM_WINDOW_START_OFFSET       ((RAM_WINDOW_START_ADDR) & 0x7FFF)
#define RAM_WINDOW_END_OFFSET         ((RAM_WINDOW_END_ADDR) & 0x7FFF)


        .thumb_func

// The epromTask code lives in scratch_y, copied from flash by the SDK crt0 at boot.
// The EPROM image data is in the lower 32K of non-striped SRAM bank0 (RAM_CORE1_EPROM).
// Bank0 is exclusive to core1, preventing bus fabric contention from core0.
// E-fall detection uses PIO1 SM0 (hc11_sync), which fires PIO1 IRQ flag 0 to wake core1 via CPU IRQ 9.
// The PIO SM auto-clears its own IRQ flag, so the post-WFI ack only writes NVIC_ICPR (core1's PPB —
// zero contention with core0). APB bus contention is fully eliminated.

        .section .scratch_y.epromTask.code, "ax"

// When epromTask gets called, the HC11 is being held in reset.
// An HC11 that is being reset will continually read location 0xFFFE
// (the high address byte of the HC11 reset vector).
// This means that the HC11 E-clock is already active.
// We will take advantage of that to synchronize ourselves to
// the falling E events before we signal core0 to release the HC11 reset signal.

        .align  2
epromTask:
// Start by initializing all our special registers that will contain permanent constants.
//
// Load a permanent pointer to the SIO (Single-cycle IO unit)
        ldr     r_SIO_BASE_PTR, =#SIO_BASE

// Load a permanent pointer to the start address of EPROM data
        ldr     r_EPROM_DATA_PTR, =__ram_core1_eprom_start__  // lower 32K of bank0: EPROM image buffer

// Load a permanent pointer to the proper PIO FIFO where we write our read results to be driven to the HC11 bus
        ldr     r0, =g_failsafe_txf_addr                // get a pointer to the variable that holds the FIFO address
        ldr     r_PIO_TXF_PTR, [r0]                     // deref to get the actual FIFO address

// Save some permanent constants identifying the start and end of our RAM window inside the fake EPROM
        ldr     r0, =#RAM_WINDOW_END_OFFSET
        mov     rh_RAM_END_OFFSET, r0

        ldr     r0, =#RAM_WINDOW_START_OFFSET
        mov     rh_RAM_START_OFFSET, r0

// Save permanent pointers to the first word of the log buffer and the first word after
// the end of the log buffer. Init the pointer where the next log item will get stored
// to the start of the buffer:
        ldr     r0, =ecu_busLog                         // this variable contains a pointer to the ecu_busLog
        ldr     r0,[r0]                                 // deref the ptr to get the actual buffer start address
        mov     rh_ECU_LOG_START, r0
        mov     r_ECU_LOG_PTR, r0
        ldr     r1, =#ECU_BUSLOG_LENGTH_BYTES
        adds    r0, r_ECU_LOG_PTR, r1
        mov     rh_ECU_LOG_END, r0

// Set up a mask corresponding to the GPIO we are using to signal our debug info as we process events.
        ldr     r_DBG_BSY_MSK,=#DBG_BSY_BITS

// Pre-load NVIC interrupt ack constants into free high registers.
        ldr     r0, =#0xFFFFFFFF
        mov     rh_ALL_ONES, r0
        ldr     r0, =(PPB_BASE + M0PLUS_NVIC_ICPR_OFFSET)
        mov     rh_NVIC_ICPR_PTR, r0

// E-fall wakeup is handled by PIO1 SM0 (hc11_sync), started by core0 before launching core1.
// Flush stale NVIC pending state before the initial sync WFI.
        mov     r0, rh_ALL_ONES
        mov     r1, rh_NVIC_ICPR_PTR
        str     r0, [r1]                                                        // Ack the NVIC interrupt

// Waking from this next WFI guarantees that we are in sync with the E clock.
// We don't process this first E clock, but just use it to drop into the main
// processing loop knowing that the timing is sync'd.
// *** If this program gets stuck in the debugger on this 'wfi' instruction, it means you forgot to turn on the ECU power!!!
sync:   wfi

// We have approx. 450 nSec to get to our _wfi label.
// Acknowledge the sync WFI, above:
        str     r0, [r1]                                                        // Ack the NVIC interrupt

// Signal the other core that we are ready to serve memory accesses:
        ldr     r1, =#0x12345678                                                // Fix Me: use a symbol here
        str     r1, [r_SIO_BASE_PTR, #SIO_FIFO_WR_OFFSET]

// Jump into the mainloop that services bus requests, never to return!
        b       _wfi

// -------------------------------------------------------------------------------------------------------------
// All bus transactions that need to be logged complete here.
// R0 must contain the bus cycle data to be logged where E is replaced by HC11 A15 (always 1).
// Timing note: No matter how we get here, the cycle count needs to be exactly 50 when the
// first instruction here begins to execute:
log:                                                                            // 1 [50] Account for the taken branch to get here
        str     r0, [r_ECU_LOG_PTR]                                             // 2 [52] Log the bus cycle info
        adds    r_ECU_LOG_PTR, #4                                               // 1 [53] Update the log pointer
        cmp     r_ECU_LOG_PTR,rh_ECU_LOG_END                                    // 1 [54] Check if we just went past the end of the buffer
        blt     1f                                                              // 1 [55] if not
        mov     r_ECU_LOG_PTR, rh_ECU_LOG_START                                 // 1 [56] else reset the log pointer back to the start of the log buffer
1:                                                                              //        Note that taking the branch above or not both result in 2 clocks to get here
#if 1
// Enabling these two instructions gives a visual indication of how close we are cutting it
// between getting to the WFI and the start of the next E cycle.
// An o'scope shows there are 24 nS (3 clocks) to spare before the next falling edge of E,
// not including execution time of the WFI.
// Don't forget that the falling E will not be recognized for 2 more clocks as it moves
// through the GPIO synchronizers, so there is even a bit more margin.
        str     r_DBG_BSY_MSK, [r_SIO_BASE_PTR,#SIO_GPIO_OUT_CLR_OFFSET]        // 1 [57]
        str     r_DBG_BSY_MSK, [r_SIO_BASE_PTR,#SIO_GPIO_OUT_SET_OFFSET]        // 1 [58]
#endif
_wfi:   wfi                                                                     // 1 [59] Wait for the next falling edge of E signifying the start of the next bus cycle

// See "wakeup budget" documentation at the start of this file for an explanation of
// why the first instruction after WFI begins executing 11 to 11.999999 clocks after the WFI event.
// WFI timing is not documented. If the single-cycle instr that drives DBG_BSY low is placed here,
// a scope shows the instruction completing in 12-13 cycles after falling E (E measured in the 3.3V domain).
// This means that WFI completes in 11-12 cycles after falling E.
                                                                                // - [12]  Assume worst case latency for waking up

// Acknowledge the interrupt that awoke us from WFI (PIO1 SM0 has already cleared PIO1->irq)
        mov     r0, rh_ALL_ONES                                                 // 1 [13]
        mov     r1, rh_NVIC_ICPR_PTR                                            // 1 [14]
// Drive DBG_BSY low to indicate that we are about to read the bus signals
        str     r_DBG_BSY_MSK, [r_SIO_BASE_PTR,#SIO_GPIO_OUT_CLR_OFFSET]        // 1 [15]
        str     r0, [r1]                                                        // 2 [17] clear NVIC_ICPR

// The HC11 bus spec guarantees that the ADDR and control signals will be valid no later than 95 nS (12 clocks) after falling E.
// We are well past that, so we have lots of margin.
// Our first decision point: discover if this bus cycle targets us or not.
        ldr     r1,[r_SIO_BASE_PTR, #SIO_GPIO_IN_OFFSET]                        // 1 [18] Read the entire set of bus signals CE, WR/!RD, E, A14..A0 (from 2 cycles ago due GPIO to syncronizer delays)
        lsls    r0,r1,#31-HC11_CE_LSB                                           // 1 [19] Shift the EPROM CE signal into b31
        bpl     no_CE                                                           // 1 [20] branch if the EPROM CE is '0' (not asserted, meaning we are NOT the target of this bus cycle)

// -------------------------------------------------------------------------------------------------------------
// The current bus cycle is targeting us.
// Extract the 15-bit HC11 address offset field from the set of bus signals:
        lsls    r2,r1,#31-HC11_AB_MSB                                           // 1 [21] Left-shift the address offset field so that its msb ends up in b31
        lsrs    r2,#31-(HC11_AB_MSB - HC11_AB_LSB)                              // 1 [22] Right shift in zeroes until the lsb of the left-shifted offset field ends up at b0
// Is this a read or a write operation?
        lsls    r0,r1,#31-HC11_WR_LSB                                           // 1 [23] Shift the HC11 WR signal into b31
        bpl     _readCycle                                                      // 1 [24] The HC11 R/!W signal is inverted by our logic level converter on the PCB, therefore '0' means a read

// This is a write. Is a standard log write or a write inside our RAM window?
        cmp     r2, rh_RAM_START_OFFSET                                         // 1 [25] Does the offset fall below the bottom of our RAM window?
        blt     _writeLog1                                                      // 1 [26] Yes, it is a normal log write
        cmp     r2, rh_RAM_END_OFFSET                                           // 1 [27] Does the offset fall above the top of our RAM window?
        bgt     _writeLog2                                                      // 1 [28] Yes, it is a normal log write

// -------------------------------------------------------------------------------------------------------------
// RAM Write Cycle: the write address is within the RAM window. We are allowed to update the RAM buffer inside the EPROM code image.
// Wait for write data to get driven onto the data bus by the HC11:
        movs    r0,#4                                                           // (4*3) [40]
1:      subs    r0,#1
        bne     1b

// We will drive our debug signal active, then sample 2 clocks later to account for the GPIO input synchronizer delay.
// An oscilloscope will show DBG_BSY going high just before the data gets sampled.
        nop                                                                     // 1 [41]
        nop                                                                     // 1 [42]
        str     r_DBG_BSY_MSK, [r_SIO_BASE_PTR,#SIO_GPIO_OUT_SET_OFFSET]        // 1 [43] Drive DBG_BSY high
        nop                                                                     // 1 [44]
        ldr     r0, [r_SIO_BASE_PTR, #SIO_GPIO_IN_OFFSET]                       // 1 [45] Sample the HC11 bus data from 2 clocks ago, note that E/A15 is guaranteed to be a '1'

// Update the RAM byte in the code image buffer:
        lsrs    r1,r0,#HC11_DB_LSB                                              // 1 [46] shift the data bits down to the lower byte of R1, leaving R0 untouched
        strb    r1,[r_EPROM_DATA_PTR, r2]                                       // 2 [48] Write the new byte to the EPROM image RAM buffer

// R0 contains the bus transaction including the write data, with E/A15='1'
        b       log                                                             // 1 [49] We need to be exactly at 49 cycles here +1 for taken branch!

// -------------------------------------------------------------------------------------------------------------
// Log Write Cycle: the address is NOT within the RAM window: RAM stays unchanged.
// We forward the log data to the other core.
// The two target labels are set up to ensure that the overall timing is
// identical regardless of which branch we took to get here.
_writeLog1:                                                                     // 1 [27] taken branch
        nop                                                                     // 1 [28] make up for the second compare we didn't need to do
        nop                                                                     // 1 [29]
_writeLog2:                                                                     //   [29] account for the taken branch, but not the fallthrough
// Writes require us to wait until the write data gets driven by the HC11. The HC11 bus spec says that
// this will be no later than 255+80=335 nSec (42 clocks) after the E clock event that woke us up.
        movs    r0,#4                                                           // (4*3) [41]
1:      subs    r0,#1
        bne     1b

// We will drive our debug signal active, then sample 2 clocks later to account for the input synchronizer delay.
// An oscilloscope will show DBG_BSY going low in the same cycle as the data gets sampled.
        nop                                                                     // 1 [42]
        str     r_DBG_BSY_MSK, [r_SIO_BASE_PTR,#SIO_GPIO_OUT_SET_OFFSET]        // 1 [43] Drive DBG_BSY high at 43 clocks/344 nS
        nop                                                                     // 1 [44]
        ldr     r0, [r_SIO_BASE_PTR, #SIO_GPIO_IN_OFFSET]                       // 1 [45] Reample the HC11 bus data from 2 clocks ago to include the write data
        str     r0, [r_SIO_BASE_PTR, #SIO_FIFO_WR_OFFSET]                       // 1 [46] Send the write info to the other core for further processing

// R0 must contain the bus transaction including the write data, with E/A15 as '1'
        nop                                                                     // 1 [47]
        nop                                                                     // 1 [48]
        b       log                                                             // 1 [49] We need to be exactly at 49 cycles here +1 for taken branch!

// -------------------------------------------------------------------------------------------------------------
// Perform an read cycle into the EPROM address space.
// The code is identical for both EPROM reads and EPROM RAM window reads
// because they share the same RP2040 SRAM buffer.
// READ data needs to be valid on the bus no later than 425 nS (53 clocks) after initial falling edge of E.
// On entry, R2 must contain the 15-bit EPROM offset
_readCycle:                                                                     // 1 [25]
// Read the proper byte from SRAM buffer holding the ECU code image:
        ldrb    r0,[r_EPROM_DATA_PTR,r2]                                        // 2 [27]

// FIFO push must occur at cycle 32 (max), preserving busFailsafe PIO countdown calibration.
        nop                                                                     // 1 [28]
        nop                                                                     // 1 [29]
        nop                                                                     // 1 [30]

// Send the byte to the PIO to be driven to the bus.
// Scope measurements of DBG_BSY show that the data bus will get released between
// 28 and 36 nS after the next falling E.
        str     r0,[r_PIO_TXF_PTR]                                              // 2 [32] 32 is worst case, 31 is best case

// The PIO machine needs 4 more cycles to get the data on the bus.
        nop                                                                     // 1 [33]
        nop                                                                     // 1 [34]
        nop                                                                     // 1 [35]
        nop                                                                     // 1 [36]

// Drive DBG_BSY high to indicate that read data is active (needs to be <=53 clocks, 425 nS)
        str     r_DBG_BSY_MSK, [r_SIO_BASE_PTR,#SIO_GPIO_OUT_SET_OFFSET]        // 1 [37]

        movs    r2,#3                                                           // (3*3) [46]
1:      subs    r2,#1
        bne     1b
        nop                                                                     // 1 [47]

// Re-read HC11 data bus now that it reflects our new output data and E/A15 is guaranteed to be '1'.
        ldr     r0, [r_SIO_BASE_PTR, #SIO_GPIO_IN_OFFSET]                       // 1 [48] Resample the HC11 bus data so that it includes the write data
        b       log                                                             // 1 [49] Needs to be exactly 49 cycles+1 for taken branch


// -------------------------------------------------------------------------------------------------------------
// If the transfer is not to the overall EPROM address space, we were still going to log the bus transaction
// because the HC11 is set up to drive internal transactions to the external bus for debug visibility.
//
// However: it is clear that the data associated with internal transactions is not showing up on the bus properly.
// The address bits and control signals are correct, but the data is not!
// We will not bother logging the bus cycle.
no_CE:                                                                          // 1 [21] taken branch to get here
// Drive DBG_BSY high to indicate we are done:
        str     r_DBG_BSY_MSK, [r_SIO_BASE_PTR,#SIO_GPIO_OUT_SET_OFFSET]        // 1 [22] will complete 168..176 nS after falling E
        b       _wfi                                                            // 1 [23] Nothing to log, so go back to sleep


// All the ROM constants generated by 'LDR Rx,='' pseudo-ops will populate after this next label.
_epromTask_data:
