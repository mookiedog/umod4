#include "stdio.h"
#include <string.h>

#include "pico/stdlib.h"
#include "pico/mutex.h"
#include "pico/multicore.h"


#include "FreeRTOS.h"
#include "task.h"

#define LFS 1

#include "Gps.h"
#include "hardware.h"
#include "Heap.h"
#if defined LFS
#include "lfs.h"
#endif
#include "log_base.h"
#include "Logger.h"
#include "NeoPixelConnect.h"
#include "SdCard.h"
#include "Shell.h"
#include "Spi.h"
#include "Uart.h"
#include "umod4_WP.h"
#include "uart_rx32.pio.h"
#include "WP_log.h"

#ifdef CYW43_WL_GPIO_LED_PIN
#include "pico/cyw43_arch.h"
#endif

NeoPixelConnect* rgb_led;

Spi* spiLcd;
Logger* logger;
Shell* dbgShell;

#if defined LFS

// Configuration of the filesystem is provided by this struct
struct lfs_config lfs_cfg;

// LittleFS has a serious problem where worst case write times can be extremely long (minutes!)
// apparently due to having to scan the entire disk when it needs to find free blocks.
// See: https://github.com/littlefs-project/littlefs/issues/75#issuecomment-410065792

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

// return number of bytes that should be written before fsync for optimal
// streaming performance/robustness. if zero, any number can be written.
// LittleFS needs to copy the block contents to a new one if fsync is called
// in the middle of a block. LittleFS also is guaranteed to not remember any
// file contents until fsync is called!
int32_t lfs_bytes_until_fsync(const struct lfs_config *lfs_cfg, lfs_file_t* fp)
{
    //FS_CHECK_ALLOWED(0);
    //WITH_SEMAPHORE(fs_sem);
    
    if (fp == nullptr) {
        return 0;
    }
    
    uint32_t file_pos = fp->pos;
    uint32_t block_size = lfs_cfg->block_size;
    
    // first block exclusively stores data:
    // https://github.com/littlefs-project/littlefs/issues/564#issuecomment-2555733922
    if (file_pos < block_size) {
        return block_size - file_pos; // so block_offset is exactly file_pos
    }
    
    // see https://github.com/littlefs-project/littlefs/issues/564#issuecomment-2363032827
    // n = (N − w/8 ( popcount( N/(B − 2w/8) − 1) + 2))/(B − 2w/8))
    // off = N − ( B − 2w/8 ) n − w/8popcount( n )
    #define BLOCK_INDEX(N, B) \
    (N - sizeof(uint32_t) * (__builtin_popcount(N/(B - 2 * sizeof(uint32_t)) -1) + 2))/(B - 2 * sizeof(uint32_t))
    
    #define BLOCK_OFFSET(N, B, n) \
    (N - (B - 2*sizeof(uint32_t)) * n - sizeof(uint32_t) * __builtin_popcount(n))
    
    uint32_t block_index = BLOCK_INDEX(file_pos, block_size);
    // offset will be 4 (or bigger) through (block_size-1) as subsequent blocks
    // start with one or more pointers; offset will never equal block_size
    uint32_t block_offset = BLOCK_OFFSET(file_pos, block_size, block_index);
    
    #undef BLOCK_INDEX
    #undef BLOCK_OFFSET
    
    return block_size - block_offset;
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
    int32_t mount_err;
    
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
    
    printf("Filesystem Configuration\n");
    printf("  sd read_size: %d\n", lfs_cfg.read_size);
    printf("  sd prog_size: %d\n", lfs_cfg.prog_size);
    printf("  sd page_count: 0x%08X\n", capacity_blocks);
    printf("  lfs block_size: %d\n", lfs_cfg.block_size);
    printf("  lfs cache_size: %d\n", lfs_cfg.cache_size);
    
    // This disables wear-leveling because SD cards do it themselves.
    // See https://github.com/joltwallet/esp_littlefs/issues/211#issuecomment-2585285239
    lfs_cfg.block_cycles = -1;
    
    mutex_init(&lfs_mutex);
    lfs_cfg.lock = lfs_mutex_take;
    lfs_cfg.unlock = lfs_mutex_give;
    
    // As a development aid, reformat the filesystem if GPIO SPARE2 is grounded.
    bool formatRequest = !gpio_get(SPARE2_PIN);
    
    if (formatRequest) {
        printf("\n%s: *** External request to reformat filesystem via GPIO SPARE2\n\n", __FUNCTION__);
    }
    else {
        // Attempt to mount an existing filesystem
        int32_t mountAttempts = 1;
        do {
            printf("%s: Mounting filesystem attempt %d\n", __FUNCTION__, mountAttempts);
            t0 = time_us_32();
            mount_err = lfs_mount(&lfs, &lfs_cfg);
            t1 = time_us_32();
            mountTime_us = t1 - t0;
            if (mount_err) {
                printf("%s: Mount failed! err=%d\n", __FUNCTION__, mount_err);
                if (mount_err == LFS_ERR_IO) {
                    if (mountAttempts > 5) {
                        return false;
                    }
                    vTaskDelay(pdMS_TO_TICKS(500));
                }
            }
        } while (mount_err == LFS_ERR_IO);
    }
    
    // If we were unable to mount the filesystem for some reason other than an IO error.
    // Try reformatting it. This should only happen on the first boot.
    if (mount_err || formatRequest) {
        // We should probably log a message if we ever have to reformat!
        
        printf("%s: Formatting a filesystem\n", __FUNCTION__);
        t0 = time_us_32();
        mount_err = lfs_format(&lfs, &lfs_cfg);
        t1 = time_us_32();
        formatTime_us = t1 - t0;
        if (mount_err < 0) {
            // Format operation failed
            printf("%s: Format failed! mount_err=%d\n", __FUNCTION__, mount_err);
            return false;
        }
        // Mount the freshly formatted device
        printf("%s: Mounting reformatted filesystem\n", __FUNCTION__);
        mount_err = lfs_mount(&lfs, &lfs_cfg);
        if (mount_err<0) {
            // Still unable to mount the device!
            printf("%s: Mount of reformatted filesystem failed! mount_err=%d\n", __FUNCTION__, mount_err);
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
        if (!logger->init(&lfs)) {
            logger->deinit();
            return false;
        }
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
// The ISR for the 32-bit UART receiving ECU logging data from the EP.
// The EP always sends over log events as a full word of data:
//      bits 0..8:      length (either 2 or 3, tells WP how many bytes to log NOT INCLUDING THIS ONE)
//      bits 8..15:     8-bit LogID
//      bits 16..23:    LSB of the log data
//      bits 24..31:    MSB of the log data (if any)
//
// This ISR moves ECU events from the receive FIFO to the merged log buffer.
void isr_rx32()
{
    while(!pio_sm_is_rx_fifo_empty(PIO_UART, PIO_UART_SM)) {
        uint32_t rxWord = uart_rx32_program_get(PIO_UART, PIO_UART_SM);
        logger->logEcuData(rxWord);
    }

    // The fifo-not-empty interrupt is cleared automatically when we empty out the FIFO
}


// ----------------------------------------------------------------------------------
// The EP PIO UART receives the ECU data stream.
// This UART is unidirectional, meaning receive only.
// It must be a PIO-based UART because we need it to receive data in 16-bit units
// corresponding to a LogID/Data byte pair and the RP2350 UART silicon only does 8-bit transfers.
void initEpUart() {

    // Set up the PIO unit to act as our UART
    uint offset = pio_add_program(PIO_UART, &uart_rx32_program);
    uart_rx32_program_init(PIO_UART, PIO_UART_SM, offset, EPLOG_RX_PIN, EP_TO_WP_BAUDRATE);

    // Assign an interrupt handler
    irq_set_exclusive_handler(PIO_UART_RX_IRQ, isr_rx32);
    
    // Leave interrupts off until we enable the flowcontrol signal  
}

// ----------------------------------------------------------------------------------
void allowEpToSendData()
{
    assert(logger != nullptr);
    
    // Make sure the rxQ is empty prior to giving the EP the OK to send
    // in case we received some garbage during boot.
    while(!pio_sm_is_rx_fifo_empty(PIO_UART, PIO_UART_SM)) {
        uint16_t logEvent = uart_rx32_program_get(PIO_UART, PIO_UART_SM);
    }
    
    // Enable RX FIFO Not Empty interrupt in this core's NVIC
    irq_set_enabled(PIO_UART_RX_IRQ, true);

    // Enable the RX-FIFO-not-empty interrupt inside the proper PIO unit
    #if (PIO_UART_SM == 0)
        PIO_UART->inte0 = PIO_INTR_SM0_RXNEMPTY_BITS;
    #else
        #error "Update this to the proper SMx bit!"
    #endif

    // Tell the EP we are ready for data
    gpio_put(EPLOG_FLOWCTRL_PIN, 0);
    gpio_set_dir(EPLOG_FLOWCTRL_PIN, GPIO_OUT);
}

// ----------------------------------------------------------------------------------
// This function is used to perform the FreeRTOS task initialization.
// FreeRTOS is known to be running when this routine is called so it is free
// to use any FreeRTOS constructs and call any FreeRTOS routines.
void bootSystem()
{
    initEpUart();
    
    // The logger may be started early since it has a big buffer to handle extremely long LittleFS write times.
    printf("%s: Creating the logger\n", __FUNCTION__);
    logger = new Logger(LOG_BUFFER_SIZE);

    // First thing we log is what version of the log we are generating:
    uint8_t v = LOGID_GEN_WP_LOG_VER_VAL_V0;
    logger->logData(LOGID_GEN_WP_LOG_VER_TYPE_U8, LOGID_GEN_WP_LOG_VER_DLEN, &v);
    
    printf("%s: Starting the filesystem\n", __FUNCTION__);
    startFileSystem();
    
    // Now that the UART and logger are up: signal the EP that is is OK to send us data
    allowEpToSendData();
    
    printf("%s: Starting the GPS\n", __FUNCTION__);
    startGps();
    
    printf("%s: Starting the debug shell\n", __FUNCTION__);
    dbgShell = new Shell(&lfs);
}


// ----------------------------------------------------------------------------------
// This task drives the NeoPixel LED as a status indicator.
// In addition, this task is used to boot the FreeRTOS-dependent portion of the system.
// Doing that from this task avoids having a dedicated boot task using stack space after it completes.
void vLedTask(void* arg)
{
    // Get the LED working first so that it can be used by the rest of the system
    rgb_led = new NeoPixelConnect(WS2812_PIN, WS2812_PIXCNT, PIO_WS2812, PIO_WS2812_SM);
    rgb_led->neoPixelSetValue(0, 16, 16, 16, true);
    
    // Take care of our system boot responsibilities...
    bootSystem();
    
    #if 0
    rgb_led->neoPixelSetValue(0, 0, 16, 0, true);
    
    // Now we are free to be the status LED task
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
    #else
    while (1) {
        vTaskDelay(1000);
    }
    #endif
}

// --------------------------------------------------------------------------------------------
// The on-board LED is treated differently between the plain pico boards and their wireless variants.
// These functions hides the differences.
int32_t pico_led_init(void)
{
    #if defined SPARE1_LED_PIN
    gpio_init(SPARE1_LED_PIN);
    gpio_put(SPARE1_LED_PIN, 0);
    gpio_set_dir(SPARE1_LED_PIN, GPIO_OUT);
    #else
    #if defined(PICO_DEFAULT_LED_PIN)
    // non-wireless boards access the LED GPIO directly
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    #endif
    #endif
    
    return PICO_OK;
}

void pico_set_led(bool led_on)
{
    #if defined SPARE1_LED_PIN
    gpio_put(SPARE1_LED_PIN, led_on);
    #else
    #if defined(PICO_DEFAULT_LED_PIN)
    gpio_put(PICO_DEFAULT_LED_PIN, led_on);
    #elif defined(CYW43_WL_GPIO_LED_PIN)
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_on);
    #endif
    #endif
}

// --------------------------------------------------------------------------------------------
// Fast flash the LED 'count' times as a most basic sign of life as we boot.
void hello(int32_t count)
{
    int32_t rval = pico_led_init();
    hard_assert(rval == PICO_OK);
    
    for (uint32_t i=0; i<count; i++) {
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
    #if defined SCOPE_TRIGGER_PIN
    // Scope trigger will be rising edge
    gpio_init(SCOPE_TRIGGER_PIN);
    gpio_put(SCOPE_TRIGGER_PIN, 0);
    gpio_set_dir(SCOPE_TRIGGER_PIN, GPIO_OUT);
    #else
    gpio_init(SPARE0_PIN);
    gpio_set_dir(SPARE0_PIN, GPIO_IN);
    gpio_set_pulls(SPARE0_PIN, false, true);    // pulldown
    #endif
    
    #if !defined SPARE1_LED_PIN
    gpio_init(SPARE1_PIN);
    gpio_set_dir(SPARE1_PIN, GPIO_IN);
    gpio_set_pulls(SPARE1_PIN, false, true);    // pulldown
    #endif
    
    gpio_init(SPARE2_PIN);
    gpio_set_dir(SPARE2_PIN, GPIO_IN);
    gpio_set_pulls(SPARE2_PIN, true, false);    // pullup
}

// ----------------------------------------------------------------------------------
// Before main() is called, the Pico boot code in pico-sdk/src/rp2_common/pico_standard_link/crt0.S
// calls function runtime_init() in pico-sdk/src/rp2_common/pico_runtime/runtime.c
// That is where all the behind-the-scenes initialization of the runtime occurs.
int main()
{
    // Simulate having pullup resistors on EPLOG_RX_PIN and EPLOG_FLOWCTRL_PIN.
    // Any future PCB4.2 rev of the PCB must add pullups to both of these signals!
    gpio_init(EPLOG_RX_PIN);
    gpio_set_dir(EPLOG_RX_PIN, GPIO_IN);
    gpio_set_pulls(EPLOG_RX_PIN, true, false);
    
    #if defined EPLOG_FLOWCTRL_PIN
    gpio_init(EPLOG_FLOWCTRL_PIN);
    gpio_set_pulls(EPLOG_FLOWCTRL_PIN, true, false);
    gpio_set_dir(EPLOG_FLOWCTRL_PIN, GPIO_IN);
    #endif
    
    #warning " ********** EXTREMELY TEMP - RESETTING THE EP **************"
    // Check if the debugger is attached by examining
    // the debug control registers
    if (1 /*sio_hw->dbgprcr & SIO_DBGPRCR_DBGPRCR_BITS */) {
        gpio_init(EP_RUN_PIN);
        gpio_set_dir(EP_RUN_PIN, GPIO_OUT);
        gpio_put(EP_RUN_PIN, 0);
        sleep_us(100);
        gpio_put(EP_RUN_PIN, 1);
    }

    #if defined SPARE1_LED_PIN
    hello(3);
    #endif
    
    // Pico2_W boards need to do this before we can even access the Pico's on-board LED
    cyw43_arch_init();
    #if !defined SPARE1_LED_PIN
    hello(3);
    #endif
    
    initSpareIos();
    
    stdio_init_all();
    
    printf("\n\nWP Booting on %s\n", STRINGIFY(PICO_BOARD));
    uint32_t f_clk_sys = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS);
    printf("WP System clock: %.1f MHz\n", f_clk_sys / 1000.0);
    printf("Heap Range: 0x%08x..0x%08X (%u bytes)\n", heap_start, heap_end, (heap_end-heap_start));
    printf("\n");
    
    #if 0
    {
        // WAY TEMP!!
        LogSource* testLogSource;
        uint8_t foo[] = {0x12, 0x34, 0x56, 0x78};
        uint8_t bar[4];
        testLogSource = new LogSource(128);
        testLogSource->log(4, foo);
        uint64_t ts = testLogSource->get_timestamp();
        uint16_t len = testLogSource->get_data_length();
        LogSource::error err = testLogSource->get_data(bar);
        
        printf("log test: %04X %02X%02X%02X%02X\n",len, bar[0], bar[1], bar[2], bar[3]);
    }
    #endif
    
    // The LED task will boot the rest of the system
    BaseType_t err = xTaskCreate(vLedTask, "LED Task", 2048, NULL, 1, NULL);
    if (err != pdPASS) {
        panic("Task creation failed!");
    }
    
    vTaskStartScheduler();
}