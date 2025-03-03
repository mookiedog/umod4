// To Do:
//  - get rid of calls to panic(): the system needs to do *something* no matter what.
//    It can't just strand a rider at the side of the road!
//  - Maybe panic() should log something, like a panic code
//    ?

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
extern uint32_t __BSON_IMAGE_PARTITION_START_ADDR;
extern uint32_t __BSON_IMAGE_PARTITION_SIZE_BYTES;
extern uint32_t __FS_PARTITION_START_ADDR;
extern uint32_t __FS_PARTITION_SIZE_BYTES;

// Note "xxx_addr" refers to byte addresses in the RP2040 address space, not block addresses in the flash device!
const uint32_t fsPartitionStart_addr = (uint32_t)&__FS_PARTITION_START_ADDR;
const uint32_t fsPartitionSize_bytes = (uint32_t)&__FS_PARTITION_SIZE_BYTES;

// This circular buffer holds all the HC11 bus activity
// It would be better to use the linker to place this in its own fixed RAM bank
// to avoid any possible access contention issues.
// We will put the ecu_busLog into RAM bank2 for the exclusive use of core1.
// It might be better to assign this variable using the linker:
const uint8_t* ecu_busLog = (uint8_t*)0x21020000;

// Temp: for testing, we make this a bit longer. It really doesn't need to be very big at all
#define ECU_EVENTLOG_LENGTH_BYTES 64
uint32_t ecu_eventLog[ECU_EVENTLOG_LENGTH_BYTES];
uint32_t eventLogIdx;

// For general elapsed time measurements
absolute_time_t t0;
absolute_time_t t1;
volatile uint64_t elapsed;

#if defined LFS
// This stuff is for littlefs:
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
  __breakpoint();
}

// --------------------------------------------------------------------------------------------
// Decoding protected EPROMs is not a public feature.

int32_t descrambler(bool descramble, const uint8_t* inEprom, uint8_t* outEprom) __attribute__((weak));
int32_t descrambler(bool descramble, const uint8_t* inEprom, uint8_t* outEprom)
{
  // Not Implemented
  return 1;
}

// --------------------------------------------------------------------------------------------
// Decoding scrambled EPROMs is not a public feature.
// Implementing this routine is left as an exercise for the student.
uint8_t readEpromViaDaughterboard(uint32_t ecuAddr, uint8_t* scrambledEpromImage) __attribute__((weak));
uint8_t readEpromViaDaughterboard(uint32_t ecuAddr, uint8_t* scrambledEpromImage)
{
  uint8_t data;

  ecuAddr &= 0x7FFF;
  data = scrambledEpromImage[ecuAddr];
  return data;
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
// Check every BSON document in the BSON partition to see if it defines a
// key called "eprom" where the key value has a type of "BSON_TYPE_EMBEDDED_DOC".
// If so, look inside the embedded doc and check if it defines a "name" key
// with a value that matches the epromName parameter.
//
// Normally, multiple BSON documents could be stored as a sequence of
// document objects with no space between them. At the moment (and
// perhaps forever), the compiler/linker pads the space between
// documents forcing every document to start on a word boundary.
// Because of this, as we move from one document to the next, we
// need to account for this padding.
uint8_t* findEprom(const char* epromName)
{
  uint8_t* docP = (uint8_t*)&__BSON_IMAGE_PARTITION_START_ADDR;

  while (1) {
    // Docs are placed in the image partition starting on a word alignment.
    // Force the docPtr into word alignment before using the document:
    docP = (uint8_t*)(((uint32_t)(docP)+3) & ~3);

    uint32_t docLength = Bson::read_unaligned_uint32(docP);
    if (docLength == 0xFFFFFFFF) {
      break;
    }

    // Check the doc, looking for a top-level element named "eprom"
    element_t e;
    bool found = Bson::findElement(docP, "eprom", e);

    if (found) {
      // Make sure that "eprom" element's data type is 'embedded document':
      if (e.elementType == BSON_TYPE_EMBEDDED_DOC) {
        // The element data represents the start of the embedded doc
        uint8_t* epromDoc = e.data;
        // Search for the "name" element inside the eprom doc:
        element_t name_e;
        found = Bson::findElement(epromDoc, "name", name_e);
        if (found) {
          if (name_e.elementType == BSON_TYPE_UTF8) {
            const char* nameP = (const char*)name_e.data+4;
            if (0==strcmp(epromName, nameP)) {
              // Found it!
              return epromDoc;
            }
          }
        }
      }
    }

    // We didn't find what we wanted in this doc.
    // Try the next one in the BSON partition
    docP += docLength;
  }

  return nullptr;
}

// --------------------------------------------------------------------------------------------
// This routine is executed by Core0:
//  - Get Core1 started
//  - Wait for Core1 to signal us that it is running
//  - Release the HC11 processor from RESET
void startCore1(void)
{
  // Core0 is responsible for presenting core1 with the EPROM image to be served to the ECU.

  uint8_t* epromDoc_8796539;
  uint8_t* epromDoc_UM4;

  bool success;

  t0 = get_absolute_time();
  // This load operation is only here to test the descramble methods
  // Measurements show it takes 90 mSec to get an scrambled image loaded.
  epromDoc_8796539 = findEprom("8796539");
  if (!epromDoc_8796539) {
    panic("Should have found 8796539!!");
  }
  else {
    // Load the image using the "mem" info found in the epromDoc
    success = EpromLoader::loadImage(epromDoc_8796539);
    if (!success) {
      panic("Unable to load image 8796539!");
    }
  }
  t1 = get_absolute_time();
  elapsed = absolute_time_diff_us(t0, t1);

  // For now, we will load from BSON every time.
  // Measurements show it takes 11 mSec to get an unscrambled image loaded.
  // In the future, it might be better to load the image to RAM but then
  // write the "compiled" EPROM image to a special default image location in flash where it is simply
  // copied instead of being built from parts.
  t0 = get_absolute_time();
  epromDoc_UM4 = findEprom("UM4");
  if (!epromDoc_UM4) {
    panic("UM4 eprom doc should exist!");
  }
  // Load the image using the "mem" info found in the epromDoc
  success = EpromLoader::loadImage(epromDoc_UM4);
  if (!success) {
    panic("Unable to load image UM4!");
  }
  t1 = get_absolute_time();
  elapsed = absolute_time_diff_us(t0, t1);

  // Now try loading 8796539 maps on top of our UM4 image:
  t0 = get_absolute_time();
  success = EpromLoader::loadMapblob(epromDoc_8796539);
  if (!success) {
    panic("Unable to load 8796539 maps!");
  }
  t1 = get_absolute_time();
  elapsed = absolute_time_diff_us(t0, t1);

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

  // Now that core1 is serving memory transactions, we can finally release the HC11 out of RESET.
  // Driving the HC11 reset output signal to '0' deasserts the HC11 RESET
  sio_hw->gpio_clr = HC11_RESET_BITS;
}

// --------------------------------------------------------------------------------------------
// Perform the remainder of the EPROM support tasks that Core1 is not capable of doing.
//   - Send incoming log messages from Core1 over to the WiFi Processor (WP)
//   - Process any commands that might arrive from the WP
static void core0Mainloop(void) __attribute__((noreturn));
void core0Mainloop(void)
{
  // Explicitly disable all interrupts inside the core0 NVIC
  for (uint32_t i=0; i<32; i++) {
    irq_set_enabled(i, false);
  }

  // We only get here if the ECU processor is off and running.
  // Peform any final initialization before we drop into the mainloop.

#if defined LFS
  // Mount the filesystem. On a freshly formatted flash, this takes 0.5 mSec
  t0 = get_absolute_time();
  int err = lfs_mount(&lfs, &cfg);
  t1 = get_absolute_time();
  elapsed = absolute_time_diff_us(t0, t1);

  // Reformat if we can't mount the filesystem. This should only happen on the first boot
  if (err) {
    // We should probably log a message to the WP if we ever have to reformat!

    // FYI: these two operations totalled out at 97 mSec.
    // We know that the mount only takes 0.5 mSec, so format is the big one.
    t0 = get_absolute_time();
    lfs_format(&lfs, &cfg);
    lfs_mount(&lfs, &cfg);
    t1 = get_absolute_time();
    elapsed = absolute_time_diff_us(t0, t1);
  }
#endif

  while (1) {
    // Wait for messages to arrive in the inter-core FIFO from Core1
    if (multicore_fifo_rvalid()) {
      uint32_t msg = multicore_fifo_pop_blocking();

      // We should only be logging writes
      if ((msg & HC11_WR_BITS) != 0) {
        if (eventLogIdx < ECU_EVENTLOG_LENGTH_BYTES) {
          ecu_eventLog[eventLogIdx++] = msg;
        }
        else {
          // overflow!
          NOP();
        }

        volatile uint32_t addr = (msg & HC11_AB_BITS) >> HC11_AB_LSB;
        volatile uint32_t data = (msg & HC11_DB_BITS) >> HC11_DB_LSB;

        // This is a test to see if the firmware ever does read anything other than $00 from address L4000 (see ultraMod firmware for details)
          if (addr == LOG_L4000_EVENT_U8) {
            if (data != 0x00) {
              data = data;
              __asm volatile ("bkpt #0");
            }
          }
          else if (addr > LOG_LAST_ADDR) {
            // This should never happen!
            data = data;
            __asm volatile ("bkpt #1");
          }
      }

      // To do:
      //  - Resend the incoming ECU log messages to the WP board where they will get logged to the SD card
      //  - Process incoming commands from the WP board, such as things dealing with EPROM image management:
      //     - add image
      //     - remove image
      //     - get image info
      //     - set default image
    }
  }
}

// --------------------------------------------------------------------------------------------
// As per the RP2040 Bootrom, main() gets executed by core0 only.
// Core1 is stalled by the Pico bootloader until explicitly started by core0.
int main(void)
{
  initCpu();
  hello(3);
  initPins();
  startCore1();
  core0Mainloop();
}