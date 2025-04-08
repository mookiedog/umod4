#include "stdio.h"
#include "pico/stdlib.h"
#include "hardware/sync.h"
#include "pico/mutex.h"

#include "umod4_WP.h"
#include "NeoPixelConnect.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#define LFS

#if defined LFS
  #include "lfs.h"
#endif

#include "Uart.h"
#include "Gps.h"
#include "SdCard.h"
#include "Shell.h"
#include "Spi.h"
#include "Logger.h"

#ifdef CYW43_WL_GPIO_LED_PIN
  #include "pico/cyw43_arch.h"
#endif

Spi* spiLcd;
Logger* logger;
Shell* dbgShell;

#if defined LFS

// Configuration of the filesystem is provided by this struct
struct lfs_config lfs_cfg;

// littlefs needs these 4 "C" routines to be defined so that it can work with a block-based
// flash-based device:
int lfs_read(const struct lfs_config *c, lfs_block_t block_num, lfs_off_t off, void *buffer, lfs_size_t size_bytes);
int lfs_prog(const struct lfs_config *c, lfs_block_t block_num, lfs_off_t off, const void *buffer, lfs_size_t size_bytes);
int lfs_erase(const struct lfs_config *c, lfs_block_t block_num);
int lfs_sync(const struct lfs_config *c);

// This struct contains everything that littlefs needs to work with a mounted filesystem.
lfs_t lfs;
mutex_t lfs_mutex;

// --------------------------------------------------------------------------------------------
int lfs_read(const struct lfs_config *c, lfs_block_t block_num, lfs_off_t off, void *buffer, lfs_size_t size_bytes)
{
  SdErr_t err;

  // The context is a pointer to the SdCard instance that will process the operation
  SdCard* sd = static_cast<SdCard*>(c->context);

  err = sd->read(block_num, off, buffer, size_bytes);
  if (err != SD_ERR_NOERR) {
    // Any errors at the SdCard level are called IO errors
    return LFS_ERR_IO;
  }

  return LFS_ERR_OK;
}

// --------------------------------------------------------------------------------------------
int lfs_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size)
{
  SdErr_t err;

  // The context is a pointer to the SdCard instance that will process the operation
  SdCard* sd = static_cast<SdCard*>(c->context);


  if ((size & 0xFF) != 0) {
    return LFS_ERR_INVAL;
  }

  err = sd->prog(block, off, buffer, size);

  if (err != SD_ERR_NOERR) {
    // Any errors at the SdCard level are called IO errors
    return LFS_ERR_IO;
  }

  return LFS_ERR_OK;
}

// --------------------------------------------------------------------------------------------
int lfs_erase(const struct lfs_config *c, lfs_block_t block)
{
  // SD cards do not require an explicit 'erase' command before writing.
  // The card itself will erase a block before overwriting it.
  return LFS_ERR_OK;
}

// --------------------------------------------------------------------------------------------
int lfs_sync(const struct lfs_config *c)
{
  // SD cards don't support sync operation
  return LFS_ERR_OK;
}
#endif

// --------------------------------------------------------------------------------------------
int lfs_mutex_take(const struct lfs_config *c)
{
  uint32_t currentOwner;

  if (!mutex_try_enter(&lfs_mutex, &currentOwner)) {
    return -1;
  }

  return 0;
}

// --------------------------------------------------------------------------------------------
int lfs_mutex_give(const struct lfs_config *c)
{
  mutex_exit(&lfs_mutex);

  return 0;
}

// ----------------------------------------------------------------------------------
// This routine is called when the hotplug manager is bringing a card online.
// The SdCard is initialized and ready for access.
// We will mount the card's filesystem.
// If no filesystem exists, we will format the card and create one.
bool comingOnline(SdCard* sdCard)
{
  uint32_t t0, t1;
  static uint32_t mountTime_us;
  static uint32_t formatTime_us;

  printf("%s: Bringing SD card online\n", __FUNCTION__);

  // Default all fields to zero
  memset((void*)&lfs_cfg, 0, sizeof(lfs_cfg));

  // Save an opaque pointer to the SdCard that will be servicing the LFS read/write requests
  lfs_cfg.context = static_cast<void*>(sdCard);

  lfs_cfg.read  = lfs_read;
  lfs_cfg.prog  = lfs_prog;
  lfs_cfg.erase = lfs_erase;
  lfs_cfg.sync  = lfs_sync;

  uint32_t blockSize = sdCard->getBlockSize_bytes();
  uint32_t capacity_blocks = sdCard->getCardCapacity_blocks();

  if ((blockSize == 0) || (capacity_blocks == 0)) {
    // If we don't know the blocksize or overall capacity, we will be unable to boot littleFS.
    return false;
  }

  // Set up the configuration information for our SD card block device required by littleFs
  lfs_cfg.read_size = blockSize;
  lfs_cfg.prog_size = blockSize;
  lfs_cfg.block_size = blockSize;
  lfs_cfg.block_count = capacity_blocks;
  lfs_cfg.block_cycles = 500,
  lfs_cfg.cache_size = blockSize;
  lfs_cfg.lookahead_size = 16;

  mutex_init(&lfs_mutex);
  lfs_cfg.lock = lfs_mutex_take;
  lfs_cfg.unlock = lfs_mutex_give;

  // Mount the filesystem.
  printf("%s: Mounting filesystem\n", __FUNCTION__);
  t0 = time_us_32();
  int err = lfs_mount(&lfs, &lfs_cfg);
  t1 = time_us_32();
  mountTime_us = t1 - t0;

  // If we were unable to mount the filesystem, try reformatting it. This should only happen on the first boot.
  // As a development aid, reformat the filesystem if GPIO SPARE2 is grounded.
  bool formatRequest = gpio_get(SPARE2_PIN);

  if (err || formatRequest) {
    if (err) {
      printf("%s: Mount failed! err=%d\n", __FUNCTION__, err);
    }
    else {
      printf("%s: External request to reformat filesystem via GPIO SPARE2\n");
    }

    // We should probably log a message if we ever have to reformat!

    printf("%s: Formatting a filesystem\n", __FUNCTION__);
    t0 = time_us_32();
    err = lfs_format(&lfs, &lfs_cfg);
    t1 = time_us_32();
    formatTime_us = t1 - t0;
    if (err < 0) {
      // Format operation failed
      printf("%s: Format failed! err=%d\n", __FUNCTION__, err);
    return false;
    }
    // Mount the freshly formatted device
    printf("%s: Mounting new filesystem\n", __FUNCTION__);
    err = lfs_mount(&lfs, &lfs_cfg);
    if (err<0) {
      // Still unable to mount the device!
      printf("%s: Mount of new filesystem failed! err=%d\n", __FUNCTION__, err);
      return false;
    }
    mountTime_us = time_us_32() - t1;
  }

  #if 0
  // Very temp:
  // Reads take roughly 500 uSec, power consumption goes up by 30 mA
  // Writes take roughly 900 uSec, power consumption goes up by 50 mA
  // Read the card continuously to see how power it takes
  for (uint32_t i=0; i<10000; i++) {
    uint8_t scratch[512];
    lfs_read(&lfs_cfg, i, 0, scratch, 512);
  }
  for (uint32_t i=0; i<10000; i++) {
    uint8_t scratch[512];
    lfs_prog(&lfs_cfg, 1000+i, 0, scratch, 512);
  }
  #endif

  printf("%s: Filesystem mounted in %.2f milliseconds\n", __FUNCTION__, mountTime_us/1000.0);

  // Reinit the logger if for some reason a card got hotplugged after the system had booted.
  if (logger) {
    logger->init(&lfs);
  }

  return true;
}


// ----------------------------------------------------------------------------------
void goingOffline(SdCard* sdCard)
{
  if (logger) {
    logger->deinit();
  }
}


// ----------------------------------------------------------------------------------
void startFileSystem(void)
{
  static Spi* spiSd;
  static SdCard* sdCard;
  static hotPlugMgrCfg_t cfg;

  // Init the SPI port that is associated with the SD card socket
  spiSd  = new Spi(SD_SPI_PORT, SD_SCK_PIN, SD_MOSI_PIN, SD_MISO_PIN);

  // Create the SdCard object to be associated with the SD card socket
  sdCard = new SdCard(spiSd, SD_CARD_PIN, SD_CS_PIN);

  // Set up a configuration object required by the hotPlugManager so that it knows what SdCard instance to
  // use, and what callback routines to call when that SdCard instance is coming up or going down.
  // The comingUp() callback would typically mount a filesystem that it found on the SdCard.
  cfg.sdCard = sdCard;
  cfg.comingUp = comingOnline;
  cfg.goingDown = goingOffline;

  // Start a hot plug manager task to control the new SdCard instance
  printf("%s: Starting hotPlugManager task\n", __FUNCTION__);
  BaseType_t err = xTaskCreate(SdCard::hotPlugManager, "HotPlugMgr", HOTPLUG_MGR_STACK_SIZE_WORDS, &cfg, 1, NULL);
  if (err != pdPASS) {
    panic("Unable to create hotPlugManager task");
  }
}

// ----------------------------------------------------------------------------------
void startGps()
{
  static Gps* gps;
  static Uart* uart_gps;

// Set up the UART connected to the GPS
  uart_gps = new Uart(GPS_UART_ID, GPS_TX_PIN, GPS_RX_PIN);
  uart_gps->configFormat(8, 1, UART_PARITY_NONE);
  uart_gps->configFlowControl(false, false);
  uart_gps->configBaud(9600);
  uart_gps->enable();
  uart_gps->rxIntEnable();

  // Create the GPS object now that we have its UART
  // The Gps object will create its own FreeRTOS task to process incoming GPS data
  gps = new Gps(uart_gps);

  //initPpsInterrupt();
}


// --------------------------------------------------------------------------------------------
// Add an idle task where we sleep to provide a minor power savings.
void vApplicationIdleHook( void )
{
    __wfi();
}


// ----------------------------------------------------------------------------------
// This function is used to perform the FreeRTOS task initialization.
// FreeRTOS is known to be running when this routine is called so it is free
// to use any FreeRTOS constructs and call any FreeRTOS routines.
void bootSystem()
{
  // Create the Spi object that we could use to drive a local LCD.
  // There is no harm in creating it even if there is no display attached:
  spiLcd = new Spi(LCD_SPI_PORT, LCD_SCK_PIN, LCD_MOSI_PIN, LCD_MISO_PIN);

  // The logger object gets created here, but logging is not started until
  // the hotplug manager decides that the filesystem is coming up
  printf("%s: Creating the logger\n", __FUNCTION__);
  logger = new Logger();

  printf("%s: Starting the GPS\n", __FUNCTION__);
  startGps();

  printf("%s: Starting the filesystem\n", __FUNCTION__);
  startFileSystem();

  printf("%s: Starting the debug shell\n", __FUNCTION__);
  dbgShell = new Shell(&lfs);

}


// ----------------------------------------------------------------------------------
// This task drives the NeoPixel LED as a status indicator.
// In addition, this task is used to boot the FreeRTOS-dependent portion of the system.
// Doing that from this task avoids having a dedicated boot task using stack space after it completes.
void vLedTask(void* arg)
{
  // Take care of our system boot responsibilities...
  bootSystem();

  // Now we are free to be the status LED task
  static NeoPixelConnect* rgb_led;

  rgb_led = new NeoPixelConnect(WS2812_PIN, 1);

  int32_t count=0;
  int32_t incr=1;

  while (1) {
    const uint32_t maxCount = 31;
    count += incr;
    if (count<0) {
        count = 0;
        incr = 1;
    }
    else if (count>maxCount) {
      count = maxCount;
      incr = -1;
    }

    #if 1
      rgb_led->neoPixelSetValue(0, count, 0, 16, true);
      vTaskDelay(50);
    #else
      // Measure min/max power for the LED by putting it at min/max brightness for a few seconds at a time
      bool foo;
      foo = !foo;
      uint32_t bright = foo ? 0xFF : 0x00;
      rgb_led->neoPixelSetValue(0, bright, bright, bright, true);
      vTaskDelay(3000);
    #endif
  }
}

// --------------------------------------------------------------------------------------------
// The on-board LED is treated differently between the plain pico boards and their wireless variants.
// These functions hides the differences.
int32_t pico_led_init(void)
{
  #if defined(PICO_DEFAULT_LED_PIN)
    // non-wireless boards access the LED GPIO directly
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
  #endif

  return PICO_OK;
}

void pico_set_led(bool led_on)
{
  #if defined(PICO_DEFAULT_LED_PIN)
    gpio_put(PICO_DEFAULT_LED_PIN, led_on);
  #elif defined(CYW43_WL_GPIO_LED_PIN)
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_on);
  #endif
}

// --------------------------------------------------------------------------------------------
// Fast flash the LED 3 times as a most basic sign of life as we boot.
void hello()
{
  int32_t rval = pico_led_init();
  hard_assert(rval == PICO_OK);

  for (uint32_t i=0; i<3; i++) {
    pico_set_led(true);
    sleep_ms(10);
    pico_set_led(false);
    sleep_ms(50);
  }
}

// --------------------------------------------------------------------------------------------
// Init all three SPAREx GPIOs as inputs, with pullups.
void initSpareIos()
{
  gpio_init(SPARE0_PIN);
  gpio_set_dir(SPARE0_PIN, GPIO_IN);
  gpio_set_pulls(SPARE0_PIN, false, true);

  gpio_init(SPARE1_PIN);
  gpio_set_dir(SPARE1_PIN, GPIO_IN);
  gpio_set_pulls(SPARE1_PIN, false, true);

  gpio_init(SPARE2_PIN);
  gpio_set_dir(SPARE2_PIN, GPIO_IN);
  gpio_set_pulls(SPARE2_PIN, false, true);
}

// ----------------------------------------------------------------------------------
// Before main() is called, the Pico boot code in pico-sdk/src/rp2_common/pico_standard_link/crt0.S
// calls function runtime_init() in pico-sdk/src/rp2_common/pico_runtime/runtime.c
// That is where all the behind-the-scenes initialization of the runtime occurs.
int main()
{
  // Pico2_W boards need to do this before we can even access the LED
  cyw43_arch_init();
  hello();
  initSpareIos();
  stdio_init_all();

  printf("\n\nWP Booting on %s\n", STRINGIFY(PICO_BOARD));
  uint32_t f_clk_sys = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS);
  printf("WP System clock: %.1f MHz\n", f_clk_sys / 1000.0);
  printf("\n");

  // The LED task will boot the rest of the system
  BaseType_t err = xTaskCreate(vLedTask, "LED Task", 512, NULL, 1, NULL);
  if (err != pdPASS) {
    panic("Task creation failed!");
  }

  vTaskStartScheduler();
}