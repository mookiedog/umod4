#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include <RP2040.h>

#include "pico/types.h"
#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "pico/multicore.h"
#include "pico/time.h"

#include "hardware/gpio.h"
#include "hardware/structs/scb.h"
#include "hardware/pio.h"
#include "hardware/irq.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/structs/systick.h"
#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/timer.h"

#include "tx_encoder.h"
#include "uart_tx32.pio.h"

#include "config.h"
#include "EpromLoader.h"
#include "hardware.h"
#include "epromEmulator.h"
#include "log_ids.h"
#include "bsonlib.h"

#include "RP58_memorymap.h"

// This can be handy in order to insert an instruction that will
// be guaranteed to exist for the purposes of setting a debugger breakpoint.
#define NOP()   __asm("nop")

// Linker generated symbols we need to know about
extern uint32_t __StackOneBottom;
extern uint32_t __StackOneTop;
extern uint8_t  __ram_core1_buslog_start__[];

// ECU bus log: lives in bank0 upper 32K (RAM_CORE1), same bank as the EPROM image.
// Written exclusively by core1's epromTask, only read by debugger. Zero contention.
uint8_t* ecu_busLog = __ram_core1_buslog_start__;

// Temp: for testing, we make this a bit longer. It really doesn't need to be very big at all
#define ECU_EVENTLOG_LENGTH_BYTES 256
uint32_t ecu_eventLog[ECU_EVENTLOG_LENGTH_BYTES];
uint32_t eventLogIdx;

uint16_t streamBuffer[32768];
uint32_t head, tail;
uint32_t inUse, inUse_max;
uint32_t totalStreamDrops;

PIO const uart_pio = pio0;
const uint uart_sm = 0;

// To track the amount of time it takes to get the ECU booted
uint64_t epoch;

// --------------------------------------------------------------------------------------------
// Decoding protected EPROMs is not a public feature.
// Implementing these routines is left as an exercise for the student.

uint8_t readEpromViaDaughterboard(uint32_t ecuAddr, uint8_t* scrambledEpromImage) __attribute__((weak));
uint8_t readEpromViaDaughterboard(uint32_t ecuAddr, uint8_t* scrambledEpromImage)
{
    return 0;
}

bool hasDescrambler() __attribute__((weak));
bool hasDescrambler()
{
    return false;
}

// --------------------------------------------------------------------------------------------
// dbId == 'N' means no daughterboard, so no address or data mangling required
// dbId == 'A' means a standard Aprilia daughterboard, address and data mangling is required
uint8_t readEprom(uint32_t ecuAddr, uint8_t* epromImage, char dbId)
{
    // Make sure we don't access off the end of the EPROM image
    ecuAddr &= 0x7FFF;

    uint8_t b;

    if (dbId == 'A') {
        b = readEpromViaDaughterboard(ecuAddr, epromImage);
    }
    else {
        // All other cases are treated as 'no daughterboard'.
        // Leave the input address and output data untouched
        b = epromImage[ecuAddr];
    }

    return b;
}


// --------------------------------------------------------------------------------------------
// Init all the Processor GPIO pins to proper, safe states.
// We put pulldowns on all input pins in case the driver ASICs driving those input pins
// might be unpowered.
void initPins(void)
{
    // The ECU control signal pins will always be inputs
    gpio_init(HC11_E_LSB);
    gpio_set_pulls(HC11_E_LSB, false, true);        // pulldown
    gpio_set_dir(HC11_E_LSB, GPIO_IN);

    gpio_init(HC11_CE_LSB);
    gpio_set_pulls(HC11_CE_LSB, false, true);       // pulldown
    gpio_set_dir(HC11_CE_LSB, GPIO_IN);

    gpio_init(HC11_WR_LSB);
    gpio_set_pulls(HC11_WR_LSB, false, true);       // pulldown
    gpio_set_dir(HC11_WR_LSB, GPIO_IN);

    // The address bus pins will always be inputs
    for (int i=HC11_AB_LSB; i<=HC11_AB_MSB; i++) {
        gpio_init(i);
        gpio_set_pulls(i, false, true);             // pulldown
        gpio_set_dir(i, GPIO_IN);
    }

    // The data bus pins get inited to a tri-state condition (input)
    for (int i=HC11_DB_LSB; i<=HC11_DB_MSB; i++) {
        gpio_init(i);
        gpio_set_pulls(i, false, true);             // pulldown
        gpio_set_dir(i, GPIO_IN);
    }

    // External circuitry guarantees that the HC11 RESET is asserted by default at power-up.
    // Init the pin we will use to control RESET, but leave RESET asserted
    // until such time as we are ready to serve HC11 bus cycles.
    gpio_init(HC11_RESET_LSB);
    gpio_put(HC11_RESET_LSB, 1);                // Driving a '1' keeps the HC11 in RESET
    gpio_set_dir(HC11_RESET_LSB, GPIO_OUT);

    // Init the debug output GPIO pin we will use to time our bus transactions
    // using an oscilloscope or logic analyzer.
    // Note: There is an active-low LED connected to this pin.
    gpio_init(DBG_BSY_LSB);
    gpio_put(DBG_BSY_LSB, 1);                   // Init LED to OFF state
    gpio_set_dir(DBG_BSY_LSB, GPIO_OUT);

    #if defined FLOWCTRL_GPIO
    // The WP will drive this line to '0' when it is ready to receive the ECU data stream
    gpio_init(FLOWCTRL_GPIO);
    gpio_set_dir(FLOWCTRL_GPIO, GPIO_IN);
    gpio_set_pulls(FLOWCTRL_GPIO, true, false);   // default state is '1'
    #endif
}

// --------------------------------------------------------------------------------------------
// Initialize the Core1-specific parts of the silicon.
// Core0 and 1 share many parts of the hardware such as RAM or GPIO control registers.
// Certain parts of the silicon are core-specific though.
// This routine sets up the things that only core1 can initialize, such as:
//    - its basic interrupt and NVIC settings in NVIC1
//    - its interprocessor FIFO settings applicable to Core1
//
// Notes:
//   - This code executes from flash - there is no point putting it in RAM as it only executes once.
//   - There is limited stackspace, so don't go wild with the local variables in this routine.
//
static void mainCore1(void) __attribute__((noreturn));
void mainCore1(void)
{
    // Completely disable ALL interrupts on core1 at the CPU PRIMASK level.
    // Core1 will not be servicing any interrupts. Instead, it will use GPIO
    // NVIC interrupt requests to wake itself from WFI sleep.
    __disable_irq();

    // Explicitly disable all interrupts inside the core1 NVIC except for GPIO interrupts.
    for (uint32_t i=0; i<32; i++) {
        irq_set_enabled(i, i == IO_IRQ_BANK0);
    }

    // Make sure that the Core0 <--> Core1 FIFO is completely flushed on the Core1 side
    multicore_fifo_drain();
    multicore_fifo_clear_irq();

    // Flush the ecu_buslog
    memset(ecu_busLog, 0, ECU_BUSLOG_LENGTH_BYTES);

    // Start serving HC11 EPROM bus requests, never to return!
    epromTask();
}

// --------------------------------------------------------------------------------------------
// Initialize any shared global CPU resources that need to get set up early on in the boot process.
void initCpu(void)
{
    #if 0
    // This appears to be unnecessary and just slows down the boot process
    // Reset Core1 into a known state while we initialize Core0
    multicore_reset_core1();
    #endif

    bi_decl(bi_program_description("Umod4 HC11 EPROM Emulator"));

    // We need to run at a specific frequency for the fake EPROM code timing to be accurate.
    // Explicitly set the clock rate to 125 Mhz resulting in a cycle time of 8 nS.
    // No need to check for errors because we know that a request for 125 MHz is always OK.
    set_sys_clock_khz(125000, true);

    #if defined CLKOUT_GPIO
    // Bringup Debug: Use a scope or freq counter to prove the sysclk is running at the right frequency.
    // We will drive a square wave of 125MHz/64 or 1.953125 MHz on the specified GPIO.
    clock_gpio_init(CLKOUT_GPIO, CLOCKS_CLK_GPOUT2_CTRL_AUXSRC_VALUE_CLK_SYS, 64);
    #endif

    // Core1 instruction timing must be completely deterministic.
    // This *should* be taken care of by loading the core1 code and the EPROM image
    // into their own private SRAM banks. To be absolutely sure, assign core1 to
    // have priority over core0 if bus contention ever arises between the two.
    bus_ctrl_hw->priority = 0x10;
}

// --------------------------------------------------------------------------------------------
// Make the DBG_BSY LED flicker "hello" for a most basic human-recognizable sign of life.
// It should flicker within a fraction of a second of applying power to the ECU.
// The flickering only takes 50 mSec per flash, so the delay of a few flashes
// is imperceptible to a rider turning the ignition key on.
void flicker(uint32_t flickerCount, uint32_t onDuration, uint32_t offDuration)
{
    gpio_init(DBG_BSY_LSB);
    gpio_put(DBG_BSY_LSB, 1);
    gpio_set_dir(DBG_BSY_LSB, GPIO_OUT);

    for (uint32_t i=0; i<flickerCount; i++) {
        // BDG_BSY LED is active low: '0' means LED lights up
        gpio_put(DBG_BSY_LSB, 0);
        busy_wait_us_32(onDuration);
        gpio_put(DBG_BSY_LSB, 1);
        busy_wait_us_32(offDuration);
    }

    // Leave LED in the OFF state
}

void hello(uint32_t flickerCount)
{
    flicker(3, 5000, 45000);
}

void blinkCode(uint32_t count)
{
    flicker(count, 10000, 290000);
    busy_wait_ms(500);
}

// --------------------------------------------------------------------------------------------
// This routine is executed by Core0:
//  - Get Core1 started
//  - Wait for Core1 to signal us that it is running and sync'ed to the HC11 E-clock
//  - Release the HC11 processor from RESET
void startCore1(void)
{
    printf("%s: Starting Core1\n", __FUNCTION__);
    // The SDK requires that we specify a tiny stack to get Core1 booted.
    // Once the fake EPROM code is running, it won't be used any more.
    uint32_t stackSizeBytes = (uint8_t*)&__StackOneTop - (uint8_t*)&__StackOneBottom;
    multicore_launch_core1_with_stack(mainCore1, &__StackOneBottom, stackSizeBytes);

    // Wait for core1 to signal us that it is actively servicing HC11 bus transactions
    bool core1Rdy = false;
    uint64_t t = time_us_64();
    bool msg = false;
    do {
        if (multicore_fifo_rvalid()) {
            uint32_t msg = multicore_fifo_pop_blocking();
            core1Rdy = (msg == 0x12345678);     // fix me: this should not be hardcoded
        }
        else {
            uint64_t tNow = time_us_64();
            if (tNow - t > 250000) {
                t = tNow;
                // toggle DBG_BSY LED at 2 Hz for a visual indicator that the ECU is not powered,
                // or Core1 has not started for some reason.
                gpio_xor_mask(DBG_BSY_BITS);
                if (!msg) {
                    msg = true;
                    printf("***\n*** Check ECU power!\n***\n");
                }
            }
        }
    } while (!core1Rdy);

    printf("%s: Core1 is running!\n", __FUNCTION__);

    // Now that core1 is serving memory transactions, we can finally release the HC11 out of RESET.
    // Driving the HC11 reset output signal to '0' deasserts the HC11 RESET
    uint64_t elapsed = time_us_64() - epoch;
    printf("%s: Releasing the ECU from RESET %lld mSecs after the EP booted\n", __FUNCTION__, (elapsed+500)/1000);
    sio_hw->gpio_clr = HC11_RESET_BITS;
}

// --------------------------------------------------------------------------------------------
void __time_critical_func(enqueue)(uint8_t id, uint8_t data)
{
    if (inUse < (int32_t)(sizeof(streamBuffer)/sizeof(streamBuffer[0]))-4) {
        uint16_t event = (data << 8) | id;

        streamBuffer[head] = event;
        head = (head+1) & ((sizeof(streamBuffer)/sizeof(streamBuffer[0]))-1);
        inUse++;
        if (inUse > inUse_max) {
            inUse_max = inUse;
        }
    }
    else {
        // Not enough room in the buffer. Drop the message.
        totalStreamDrops++;
    }
}


// --------------------------------------------------------------------------------------------
// Send the entire mapblop from the currently loaded EPROM image to the WP.
// This ensures that the log knows precisely what data was used to generate the log events.
static inline void __time_critical_func(logMapblob)()
{
    // Hmmm. We don't actually know if this is an RP58-comaptible image or not.
    // For the moment, we will assume that it is.

    // Verify that the buffer can handle a mapblob before we get going.
    // At this point, the buffer is known to be almost totally empty.
    assert(sizeof(streamBuffer) >= RP58_MAPBLOB_LENGTH);

    // Get a pointer to the start of the mapblob
    uint16_t* mapblobP = (uint16_t*)(EPROM_IMAGE_BASE + RP58_MAPBLOB_OFFSET);
    for (uint32_t i=0; i<(RP58_MAPBLOB_LENGTH)/2; i++) {
        uint16_t w = *mapblobP++;
        enqueue(LOGID_EP_LOAD_RP58MAPBLOB_TYPE_U16,   (w >> 8) & 0xFF);
        enqueue(LOGID_EP_LOAD_RP58MAPBLOB_TYPE_U16+1, w & 0xFF);
    }
}


// --------------------------------------------------------------------------------------------
// Prepare an EPROM image for Core1 to serve to the ECU.
//
// The image can be a simple EPROM image (protected or not), or it can be constructed from
// the codebase from one image (typically the UM4 ECU logging codebase) overlayed with the
// maps from any other RP58 map-style EPROM image. This allows any RP58 map-style EPROM
// to get the data-logging capability of the UM4 EPROM.
//
// The resulting EPROM image gets placed in RAM where Core1 expects to find it.
void prepEpromImage()
{
    uint32_t t0, t1, elapsed;

    t0 = get_absolute_time();
    EpromLoader::loadImage();
    t1 = get_absolute_time();
    elapsed = absolute_time_diff_us(t0, t1);
    printf("%s: loadImage() completed in %u mSec \n", __FUNCTION__, (elapsed+500) / 1000);

}

// --------------------------------------------------------------------------------------------
// If possible, send the oldest data in the streamBuffer to the WP.
//
// Data is transmitted as 32-bit words:
// bits 0..8:   length (either 2 or 3, tells WP how many of the bytes following this one to log)
// bits 8..15:  8-bit LogID
// bits 16..23: LSB of the log data
// bits 24..31: MSB of the log data (if any)
//
// The main issue is that we can't spend so much time in here that the inter-core FIFO overflows.
// We probably don't want to TX data as fast as we can because to may overflow the small amount
// or receive buffering on the WP end: it's PIO FIFO only has room for 8 entries.
//
// Note that under normal conditions, the ECU is generating events pretty far apart in time
// meaning tens if not hundreds of microseconds.
// We only really need to worry about swamping the WP at the very start when we have been buffering
// up EP and ECU events while waiting for the WP to signal us that it is ready to receive.
//
// Therefore:
// - Regardless of how full the stream buffer is, we will not send out more than two 16-bit messages
//   every 50 microseconds.
// SysTick is in the CPU core (PPB), NOT behind the APB bridge.
// Using the hardware timer (time_us_64) would cause APB bridge contention with core1's
// GPIO interrupt clear, adding up to 3 cycles of jitter to the EPROM service loop.
// SysTick counts down from reload value at 125 MHz. 24-bit counter wraps every ~134 ms.
const uint32_t tx_delay_ticks = 50 * 125;   // 50 uS * 125 ticks/uS = 6250 ticks

static inline void __time_critical_func(processOutgoing)(void)
{
    static uint32_t lastTx_tick = 0;
    static uint32_t tx32data;

    if (inUse > 0) {
        uint32_t now = systick_hw->cvr;
        // SysTick counts DOWN, so elapsed = last - now (mod 24-bit)
        uint32_t elapsed = (lastTx_tick - now) & 0x00FFFFFF;
        if (elapsed >= tx_delay_ticks) {
            // We only send data in groups of up to 2 ECU events
            uint32_t count = (inUse > 2) ? 2 : inUse;
            for (uint32_t i=0; i<count; i++) {
                uint16_t msg = streamBuffer[tail];

                uint8_t logid = msg & 0xff;
                uint8_t data8 = msg >> 8;

                uint8_t t = logEncoder[logid];
                if (t == D_BYTE) {
                    // It's a single byte to be logged,
                    // Create the word to be sent as 0x00|data|logid|len:2
                    uint32_t d32 = (data8<<16) | (logid<<8) | 2;
                    uart_tx32_program_put(uart_pio, uart_sm, d32);
                    tx32data = 0;
                }
                else if (t == D_MSB) {
                    // This is the MSB of a 16-bit quantity
                    // Prep the word for what we know (MSB, logid, length), but don't send it until we get the LSB
                    tx32data = (data8<<24) | (logid<<8) | 3;
                }
                else if (t == D_LSB) {
                    // Add the LSB to our existing word then send it
                    tx32data |= (data8<<16);
                    uart_tx32_program_put(uart_pio, uart_sm, tx32data);
                    tx32data = 0;
                }
                else {
                    //#warning "Remove this at some point because it should never occur"
                    //panic("Bad value in tx_encoder array!");
                }

                inUse--;
                tail = (tail+1) & ((sizeof(streamBuffer)/sizeof(streamBuffer[0]))-1);
            }

            // This enforces a minimum time between EP transmission bursts to allow the WP time to process its receive FIFO
            lastTx_tick = now;
        }
    }
}


// --------------------------------------------------------------------------------------------
// Both com lines between EP ane WP are init'd to have a pullup by default.
// WP indicates it is ready for ECU data when it drives the FLOWCTRL_GPIO to '0'.
static inline bool wpReady()
{
    return (gpio_get(FLOWCTRL_GPIO) == 0);
}

// --------------------------------------------------------------------------------------------
static inline void __time_critical_func(processIncoming)(void)
{
    if (multicore_fifo_rvalid()) {
        uint32_t busSigs = sio_hw->fifo_rd;

        // We convert the raw 32-bit data stream event into a 16-bit value where the
        // ID (LS 8 bits of HC11 bus address) is in the LS byte, and the 8 bits of HC11 data bus is in the MS byte:
        uint8_t id   = (busSigs & HC11_AB_BITS) >> HC11_AB_LSB;
        uint8_t data = (busSigs & HC11_DB_BITS) >> HC11_DB_LSB;

        // Enqueue the ECU data stream event until it can be transmitted out
        enqueue(id, data);
    }
}


// --------------------------------------------------------------------------------------------
// Core0's whole job is to forward the incoming ECU message stream from Core1 over to the
// WiFi Processor (WP).
//
// NOTE: The code within this mainloop must NEVER access any devices on the far side of
// the APB bridge. Doing so WILL create bus contention issues with Core1. The issue is that
// core1 must access the GPIO control registers to clear its GPIO interrupt on every
// falling E-clock edge. If core0 is accessing any APB-bridge devices at the same time,
// the resulting APB bridge contention will delay Core1's GPIO interrupt clear, disturbing
// the timing of the HC11 memory cycle.
//
// Note: do not make this function static because the -Os optimizer will inline it back into
// its caller main() causing it to get run from flash.
void __time_critical_func(core0Mainloop)(void) __attribute__((noreturn));
void core0Mainloop(void)
{
    // Do not delay entering the mainloop after starting core1 because
    // ECU logging data will arrive essentially immediately!
    startCore1();

    // If the WP is not ready, we just buffer incoming data.
    while (1) {
        processIncoming();
        if (wpReady()) {
            processOutgoing();
        }
    }
}

// --------------------------------------------------------------------------------------------
void showBootMessages()
{
    printf("\n\nEP Booting\n");
    uint32_t f_clk_sys = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS);
    printf("System clock: %.1f MHz\n", f_clk_sys / 1000.0);
    printf("\n");
}


// --------------------------------------------------------------------------------------------
// Explicitly disable all interrupts inside the core0 NVIC.
void disableInts()
{
    for (uint32_t i=0; i<NUM_IRQS; i++) {
        irq_set_enabled(i, false);
    }
}

// --------------------------------------------------------------------------------------------
void initSystick()
{
    // Configure SysTick as a free-running 24-bit down counter at processor clock speed.
    // Used by processOutgoing() for tx pacing instead of the hardware timer used by time_us_64()
    // because that timer is behind the APB bridge and would contend with core1.
    systick_hw->rvr = 0x00FFFFFF;   // max reload value
    systick_hw->cvr = 0;            // clear current value
    systick_hw->csr = 0x05;         // enable, use processor clock, no interrupt
}

// --------------------------------------------------------------------------------------------
// Prep the UART to forward the ECU log data to the WP.
// We use a TX-only UART implemented as a PIO state machine.
//
// The EPROM loop needs exclusive bus fabric access to the APB bridge so it can clear
// its GPIO interrupt.
// If this code used a real UART, there would potentially be bus contention for the APB bridge
// whenever we accessed a UART register.
//
// Using a PIO-based UART means that there can be no bus contention because the PIO unit
// is not located behind the APB bridge.
void initUart()
{
    #if defined CLKOUT_GPIO
        // The UART must be disabled if the system is configured to drive its sysclk
        // to CLKOUT_GPIO for testing/verification purposes since they share the same GPIO pad!
        printf("%s: \n****\n**** WARNING: UART functionality is disabled due to CLKOUT testing!\n****\n");
    #else
        // We will be implementing a PIO program to send data to the WP in 32-bit chunks
        uint offset = pio_add_program(uart_pio, &uart_tx32_program);
        uart_tx32_program_init(uart_pio, uart_sm, offset, EP_TO_WP_TX_GPIO, EP_TO_WP_BAUDRATE);
    #endif
}

// --------------------------------------------------------------------------------------------
int main(void)
{
    epoch = time_us_64();

    initCpu();

    #if defined EP_TO_WP_TX_GPIO
        // Before doing anything, we init the pin we will be transmitting on to the WP to have a pullup.
        // A future PCB rev will install a pullup resistor
        gpio_init(EP_TO_WP_TX_GPIO);
        gpio_set_dir(EP_TO_WP_TX_GPIO, GPIO_IN);
        gpio_set_pulls(EP_TO_WP_TX_GPIO, true, false);    // pullup
    #endif

    hello(3);
    initPins();

    enqueue(LOGID_GEN_EP_LOG_VER_TYPE_U8, LOGID_GEN_EP_LOG_VER_VAL_V0);

    stdio_init_all();
    showBootMessages();

    disableInts();

    initSystick();
    initUart();

    prepEpromImage();
    logMapblob();

    core0Mainloop();
}