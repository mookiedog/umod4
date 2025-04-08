// To Do:
//  - get rid of calls to panic(): the system needs to do *something* no matter what.
//    It can't just strand a rider at the side of the road!
//  - Maybe panic() should log something, like a panic code
//    ?

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

// Pico SDK related:
#include <RP2040.h>
#include "pico/types.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/structs/scb.h"
#include "pico/binary_info.h"
#include "hardware/irq.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/clocks.h"
#include "pico/multicore.h"
#include "pico/time.h"
#include "hardware/flash.h"
#include "hardware/timer.h"


// Eprom project related:
#include "config.h"
#include "EpromLoader.h"
#include "hardware.h"
#include "epromEmulator.h"
#include "ECU_log.h"
#include "bsonlib.h"

#include "RP58_memorymap.h"

#if defined LFS
  #include "../littlefs/lfs.h"
#endif

extern void _panic(void);

// This can be handy in order to insert an instruction that will
// be guaranteed to exist for the purposes of setting a debugger breakpoint.
#define NOP()   __asm("nop")

// Linker generated symbols we need to know about
extern uint32_t __StackOneBottom;
extern uint32_t __StackOneTop;

// This circular buffer holds all the HC11 bus activity
// It would be better to use the linker to place this in its own fixed RAM bank
// to avoid any possible access contention issues.
// We will put the ecu_busLog into RAM bank2 for the exclusive use of core1.
// It might be better to assign this variable using the linker:
uint8_t* ecu_busLog = (uint8_t*)0x21020000;

// Temp: for testing, we make this a bit longer. It really doesn't need to be very big at all
#define ECU_EVENTLOG_LENGTH_BYTES 256
uint32_t ecu_eventLog[ECU_EVENTLOG_LENGTH_BYTES];
uint32_t eventLogIdx;

// To track the amount of time it takes to get the ECU booted
absolute_time_t epoch;

#if defined LFS
extern uint32_t __FS_PARTITION_START_ADDR;
extern uint32_t __FS_PARTITION_SIZE_BYTES;

// Note "xxx_addr" refers to byte addresses in the RP2040 address space, not block addresses in the flash device!
const uint32_t fsPartitionStart_addr = (uint32_t)&__FS_PARTITION_START_ADDR;
const uint32_t fsPartitionSize_bytes = (uint32_t)&__FS_PARTITION_SIZE_BYTES;

int lfs_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size);
int lfs_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size);
int lfs_erase(const struct lfs_config *c, lfs_block_t block);
int lfs_sync(const struct lfs_config *c);

lfs_t lfs;
//lfs_file_t file;

// Configuration of the filesystem is provided by this struct
const struct lfs_config cfg = {
    // block device operations
    .read  = lfs_read,
    .prog  = lfs_prog,
    .erase = lfs_erase,
    .sync  = lfs_sync,

    // Block device configuration
    .read_size = 16,
    .prog_size = 256,                               // Needs to be 256 because the Pico flash writer demands writing full 256 byte pages
    .block_size = 4096,
    .block_count = fsPartitionSize_bytes / 4096,
    .block_cycles = 500,
    .cache_size = 512,
    .lookahead_size = 16,

};

// --------------------------------------------------------------------------------------------
int lfs_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size)
{
  // Convert the block address to where that block really sits in the flash device.
  uint32_t flashAddr = (block * 4096) + fsPartitionStart_addr;
  memcpy(buffer, (const void*)(flashAddr + off), size);

  return LFS_ERR_OK;
}

// --------------------------------------------------------------------------------------------
int lfs_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size)
{
  const uint32_t fsStartOffset = (fsPartitionStart_addr - XIP_BASE);

  if ((size & 0xFF) != 0) {
    return LFS_ERR_INVAL;
  }

  flash_range_program((block * 4096) + fsStartOffset, (uint8_t *)buffer, size);

  // verify the write???

  return LFS_ERR_OK;
}

// --------------------------------------------------------------------------------------------
int lfs_erase(const struct lfs_config *c, lfs_block_t block)
{
  // Calculate where the start of our file system sits relative to the start of the flash.
  const uint32_t fsStartOffset = (fsPartitionStart_addr - XIP_BASE);

  // The flash_range_erase() function wants an address relative to the start of flash
  uint32_t flashAddr = (block * 4096) + fsStartOffset;
  flash_range_erase(flashAddr, 4096);

  return LFS_ERR_OK;
}

// --------------------------------------------------------------------------------------------
int lfs_sync(const struct lfs_config *c)
{
  // Systems that don't support sync operation should just return OK
  return LFS_ERR_OK;
}
#endif


// --------------------------------------------------------------------------------------------
void _panic(void)
{
  //__breakpoint();
  while(1){}
}

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
void initPins(void)
{
  // The ECU control signal pins will always be inputs
  gpio_init(HC11_E_LSB);
  gpio_set_dir(HC11_E_LSB, GPIO_IN);

  gpio_init(HC11_CE_LSB);
  gpio_set_dir(HC11_CE_LSB, GPIO_IN);

  gpio_init(HC11_WR_LSB);
  gpio_set_dir(HC11_WR_LSB, GPIO_IN);

  // The address bus pins will always be inputs
  for (int i=HC11_AB_LSB; i<=HC11_AB_MSB; i++) {
    gpio_init(i);
    gpio_set_dir(i, GPIO_IN);
  }

  // The data bus pins get inited to a tri-state condition (input)
  for (int i=HC11_DB_LSB; i<=HC11_DB_MSB; i++) {
    gpio_init(i);
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
  gpio_put(DBG_BSY_LSB, 0);
  gpio_set_dir(DBG_BSY_LSB, GPIO_OUT);

  // To Do:
  //   - Init the pins we will use to communicate with the Pico W
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
  if (!set_sys_clock_khz(125000, true)) {
    _panic();
  }

  #if defined CLKOUT_GPIO
    // Bringup Debug: prove our clock is running at the right frequency
    // by driving a square wave of 125MHz/64 or 1.953125 MHz on the specified GPIO
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
void hello(uint32_t flickerCount)
{
  gpio_init(DBG_BSY_LSB);
  gpio_put(DBG_BSY_LSB, 1);
  gpio_set_dir(DBG_BSY_LSB, GPIO_OUT);

  for (uint32_t i=0; i<flickerCount; i++) {
    // BDG_BSY LED is active low: '0' means LED lights up
    gpio_put(DBG_BSY_LSB, 0);
    busy_wait_us_32(5000);
    gpio_put(DBG_BSY_LSB, 1);
    busy_wait_us_32(45000);
  }
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
  do {
    if (multicore_fifo_rvalid()) {
      uint32_t msg = multicore_fifo_pop_blocking();
      core1Rdy = (msg == 0x12345678);     // fix me: this should not be hardcoded
    }
  } while (!core1Rdy);

  printf("%s: Core1 is running!\n", __FUNCTION__);

  // Now that core1 is serving memory transactions, we can finally release the HC11 out of RESET.
  // Driving the HC11 reset output signal to '0' deasserts the HC11 RESET
  printf("%s: Releasing the ECU from RESET %ld uSecs after the EP booted\n", __FUNCTION__, get_absolute_time()-epoch);
  sio_hw->gpio_clr = HC11_RESET_BITS;
}

// --------------------------------------------------------------------------------------------
// Prepare an EPROM image for Core1 to serve to the ECU.
//
// The image can be a simple EPROM image (protected or not), or it can be constructed from
// the codebase from one image (typically the UM4 ECU logging codebase) overlayed with the
// maps from any other RP58-compatible EPROM image. This allows any RP58-compatible EPROM
// to get the data-logging capability of the UM4 EPROM.
//
// The resulting EPROM image gets placed in RAM where Core1 expects to find it.
void prepEpromImage()
{
  uint32_t t0, t1, elapsed;
  bool success;

  if (hasDescrambler()){
    // This initial load is just for testing that we can load a protected image
    success = EpromLoader::loadImage("8796539");
    if (!success) {
      printf("%s: failed!\n", __FUNCTION__);
    }
  }

  // This is the image that we really want to load:
  printf("%s: Loading UM4 image\n", __FUNCTION__);
  EpromLoader::bsonDoc_t b = EpromLoader::findEprom("UM4");
  if (!b) {
    panic("Unable to find UM4 image in BSON partition!");
  }
  success = EpromLoader::loadImage(b);
  if (!success) {
    panic("Unable to load UM4 eprom image!");
  }

  if (hasDescrambler()) {
    // Now try loading protected 8796539 maps on top of our UM4 image:
    t0 = get_absolute_time();
    success = EpromLoader::loadMapblob("8796539");

    if (!success) {
      printf("Unable to load protected 8796539 mapblob!\n");
    }
    else {
      t1 = get_absolute_time();
      elapsed = absolute_time_diff_us(t0, t1);
      printf("%s: Loaded protected mapblob in %u microseconds\n", __FUNCTION__, elapsed);
    }
  }

  // Reload the UM4 maps back on top of the UM4 base image
  t0 = get_absolute_time();
  success = EpromLoader::loadMapblob(b);
  if (!success) {
    printf("Unable to reload UM4 mapblob!\n");
    panic("mapblob load failed");
  }
  else {
    t1 = get_absolute_time();
    elapsed = absolute_time_diff_us(t0, t1);
    printf("%s: Loaded unprotected UM4 mapblob in %u microseconds\n", __FUNCTION__, elapsed);
  }
}

// --------------------------------------------------------------------------------------------
// Perform the remainder of the EPROM support tasks that Core1 is not capable of doing.
//   - Send incoming log messages from Core1 over to the WiFi Processor (WP)
//   - Process any commands that might arrive from the WP
static void core0Mainloop(void) __attribute__((noreturn));
void core0Mainloop(void)
{
  // Calculate this constant pointer address once:
  volatile io_rw_32* const txReg = &(uart_get_hw(EP_UART)->dr);

  // We are ready to start Core1 and process the ECU data stream it will send us:
  startCore1();

  // Do not delay entering the mainloop after starting core1 because
  // ECU logging data will arrive essentially immediately!

  while (1) {
    // Wait for messages to arrive in the inter-core FIFO from Core1
    if (multicore_fifo_rvalid()) {
      uint32_t msg = multicore_fifo_pop_blocking();

      // The fake eprom should only be sending us writes.
      // Discard any transfers that are not writes!
      if ((msg & HC11_WR_BITS) != 0) {
        // Save the initial log data we received for debugging purposes
        if (eventLogIdx < ECU_EVENTLOG_LENGTH_BYTES) {
          ecu_eventLog[eventLogIdx++] = msg;
        }
        else {
          // NOP allows for a debugger breakpoint here when the buffer fills
          NOP();
        }

        volatile uint32_t addr = (msg & HC11_AB_BITS) >> HC11_AB_LSB;
        volatile uint32_t data = (msg & HC11_DB_BITS) >> HC11_DB_LSB;

        #if defined EP_UART
          // The ECU can't generate data fast enough for the FIFO to overflow.
          // We transmit the data blindly without even checking if the FIFO has room.
          *txReg = addr & 0xFF;
          *txReg = data & 0xFF;
        #endif
      }
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
// Prep the UART to forward the ECU log data to the WP.
// Note that the UART is disabled if the system is configured to drive its sysclk
// to CLKOUT_GPIO for testing/verification purposes!
void initUart()
{
  #if defined EP_UART
    // Init the UART
    uart_init(EP_UART, EP_UART_BAUD_RATE);

    // Assign control of TX/RX GPIOs to the UART
    gpio_set_function(TX_GPIO, UART_FUNCSEL_NUM(EP_UART, TX_GPIO));
    gpio_set_function(RX_GPIO, UART_FUNCSEL_NUM(EP_UART, RX_GPIO));
  #else
    printf("%s: \n****\n**** WARNING: UART functionality is disabled due to CLKOUT testing!\n****\n")
  #endif
}

// --------------------------------------------------------------------------------------------
int main(void)
{
  epoch = get_absolute_time();

  initCpu();
  hello(3);
  initPins();

  stdio_init_all();
  showBootMessages();

  disableInts();
  initUart();
  prepEpromImage();
  core0Mainloop();
}