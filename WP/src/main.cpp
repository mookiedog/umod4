#include <stdio.h>
#include <string.h>
#include <malloc.h>

#include "hardware.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include "pico/mutex.h"
#include "pico/multicore.h"
#include "pico/bootrom.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "FlashWp.h"
#include "Gps.h"
#include "lfs.h"
#include "log_base.h"
#include "Logger.h"
#include "NeoPixelConnect.h"
#include "SdCardBase.h"
#include "SdCard.h"
#include "SdCardSDIO.h"
#include "Shell.h"
#include "Spi.h"
#include "Swd.h"
#include "Uart.h"
#include "umod4_WP.h"
#include "uart_rx32.pio.h"
#include "WP_log.h"
#include "WiFiManager.h"
#include "NetworkManager.h"
#include "file_io_task.h"
#include "ota_flash_task.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"

// Linker-provided symbols for heap boundaries (for diagnostics)
extern uint32_t __end__;        // End of BSS, start of heap
extern uint32_t __HeapLimit;    // End of heap
static const char* heap_start = (char*)&__end__;
static const char* heap_end = (char*)&__HeapLimit;

const char* SYSTEM_JSON = "{\"BT\":\"" __DATE__ " " __TIME__ "\"}";

// Gah: this is duplicated in FlashWp.cpp. FIXME!
static __attribute__((aligned(4))) uint8_t workarea[4 * 1024];

// This is only global so we can attach the debugger later and see what
// was read at startup.
boot_info_t boot_info;
bool ota_available = false;

// SD Card Interface Selection
// Uncomment to use SDIO 4-bit mode (~20-25 MB/s) instead of SPI mode (~3 MB/s)
#define USE_SDIO_MODE 1

NeoPixelConnect* rgb_led;

Spi* spiLcd;
Logger* logger;
Shell* dbgShell;
Swd* swd;

int pio_sm_uart;

// WiFi Phase 1 components
WiFiManager* wifiMgr = nullptr;

// MDL (Motorbike Data Link) components
NetworkManager* networkMgr = nullptr;

// This array tracks the most recently-received data from the ECU data stream
// The array is indexed by the ECU log ID
//  * 8-bit log entries are stored in the lower byte of each 16-bit word
//  * 16-bit log entries are stored in the full 16-bit word
uint16_t ecuLiveLog[256];

// Configuration of the filesystem is provided by this struct
struct lfs_config lfs_cfg;

#ifdef configSUPPORT_STATIC_ALLOCATION
// If static allocation is allowed, then we are required to statically allocate
// the idle and timer task

// Core 0: Single variable
static StaticTask_t xIdleTaskTCB __attribute__((aligned(8)));
static StackType_t uxIdleTaskStack[configMINIMAL_STACK_SIZE] __attribute__((aligned(8)));

// Core 1+: Arrays
static StaticTask_t xPassiveIdleTaskTCBs[configNUMBER_OF_CORES - 1] __attribute__((aligned(8)));
static StackType_t uxPassiveIdleTaskStacks[configNUMBER_OF_CORES - 1][configMINIMAL_STACK_SIZE] __attribute__((aligned(8)));


void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer,
                                    StackType_t **ppxIdleTaskStackBuffer,
                                    uint32_t *pulIdleTaskStackSize )
{
    /* Use & to ensure we are passing the address, not the value */
    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = &uxIdleTaskStack[0];

    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

void vApplicationGetPassiveIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer,
                                           StackType_t **ppxIdleTaskStackBuffer,
                                           uint32_t *pulIdleTaskStackSize,
                                           BaseType_t xCoreID )
{
    // NOTE: xCoreID will be 0 for RP2350 core.1, meaning that xCoreID 0 is the zeroth __additional__ core beyond RP2350 core.0.
    /* Subscript [xCoreID][0] points to the start of the specific core's stack */
    *ppxIdleTaskTCBBuffer = &xPassiveIdleTaskTCBs[xCoreID];
    *ppxIdleTaskStackBuffer = &uxPassiveIdleTaskStacks[xCoreID][0];

    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

// And for the timer task
static StaticTask_t xTimerTaskTCB;
static StackType_t uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH];

void vApplicationGetTimerTaskMemory( StaticTask_t **ppxTimerTaskTCBBuffer,
                                     StackType_t **ppxTimerTaskStackBuffer,
                                     uint32_t *pulTimerTaskStackSize )
{
    *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;
    *ppxTimerTaskStackBuffer = uxTimerTaskStack;
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}
#endif

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
// FreeRTOS semaphore for LittleFS locking (instead of pico_sync mutex which uses event groups)
SemaphoreHandle_t lfs_semaphore;
bool lfs_mounted = false;  // Track whether filesystem is successfully mounted

// Performance tracking for SD card operations
typedef struct {
    uint32_t read_count;
    uint64_t read_bytes;      // Use 64-bit to prevent overflow
    uint64_t read_time_us;    // Use 64-bit to prevent overflow
    uint32_t read_min_us;
    uint32_t read_max_us;

    uint32_t write_count;
    uint64_t write_bytes;     // Use 64-bit to prevent overflow
    uint64_t write_time_us;   // Use 64-bit to prevent overflow
    uint32_t write_min_us;
    uint32_t write_max_us;
} sd_perf_stats_t;

sd_perf_stats_t sd_perf_stats = {0};

// Global SD card pointer (for access from Shell and other modules)
SdCardBase* sdCard = nullptr;

// C-compatible wrapper for checking if SD card is inserted
// Used by C code (api_handlers.c) to check card status
extern "C" bool sdcard_is_inserted(SdCardBase* card) {
    if (!card) return false;
    return card->cardPresent();
}

// --------------------------------------------------------------------------------------------
int lfs_read(const struct lfs_config *c, lfs_block_t block_num, lfs_off_t off, void *buffer, lfs_size_t size_bytes)
{
    SdErr_t err;

    // The context is a pointer to the SdCardBase instance that will process the operation
    SdCardBase* sd = static_cast<SdCardBase*>(c->context);

    // Translate LittleFS block/offset to SD card sector
    uint64_t byte_addr = (uint64_t)block_num * c->block_size + off;
    uint32_t sector = byte_addr / 512;
    uint32_t sector_offset = byte_addr % 512;
    uint32_t num_sectors = (sector_offset + size_bytes + 511) / 512;

    // Require sector-aligned access (for now)
    if (sector_offset != 0 || (size_bytes & 0x1FF) != 0) {
        printf("lfs_read: unaligned access block=%u off=%u size=%u\n",
               (uint32_t)block_num, (uint32_t)off, (uint32_t)size_bytes);
        return LFS_ERR_INVAL;
    }

    uint32_t t0 = time_us_32();
    err = sd->readSectors(sector, num_sectors, buffer);
    uint32_t elapsed = time_us_32() - t0;

    if (err != SD_ERR_NOERR) {
        printf("lfs_read ERROR: sector=%u count=%u err=%d\n", sector, num_sectors, err);
        return LFS_ERR_IO;
    }

    // Update performance statistics
    sd_perf_stats.read_count++;
    sd_perf_stats.read_bytes += size_bytes;
    sd_perf_stats.read_time_us += elapsed;
    if (sd_perf_stats.read_min_us == 0 || elapsed < sd_perf_stats.read_min_us) {
        sd_perf_stats.read_min_us = elapsed;
    }
    if (elapsed > sd_perf_stats.read_max_us) {
        sd_perf_stats.read_max_us = elapsed;
    }

    return LFS_ERR_OK;
}

// --------------------------------------------------------------------------------------------
int lfs_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size)
{
    SdErr_t err;

    // The context is a pointer to the SdCardBase instance that will process the operation
    SdCardBase* sd = static_cast<SdCardBase*>(c->context);

    // Translate LittleFS block/offset to SD card sector
    uint64_t byte_addr = (uint64_t)block * c->block_size + off;
    uint32_t sector = byte_addr / 512;
    uint32_t sector_offset = byte_addr % 512;
    uint32_t num_sectors = (sector_offset + size + 511) / 512;

    // Require sector-aligned access (for now)
    if (sector_offset != 0 || (size & 0x1FF) != 0) {
        printf("lfs_prog: unaligned access block=%u off=%u size=%u\n",
               (uint32_t)block, (uint32_t)off, (uint32_t)size);
        return LFS_ERR_INVAL;
    }

    uint32_t t0 = time_us_32();
    err = sd->writeSectors(sector, num_sectors, buffer);
    uint32_t elapsed = time_us_32() - t0;

    if (err != SD_ERR_NOERR) {
        printf("lfs_prog ERROR: sector=%u count=%u err=%d\n", sector, num_sectors, err);
        return LFS_ERR_IO;
    }

    // Update performance statistics
    sd_perf_stats.write_count++;
    sd_perf_stats.write_bytes += size;
    sd_perf_stats.write_time_us += elapsed;
    if (sd_perf_stats.write_min_us == 0 || elapsed < sd_perf_stats.write_min_us) {
        sd_perf_stats.write_min_us = elapsed;
    }
    if (elapsed > sd_perf_stats.write_max_us) {
        sd_perf_stats.write_max_us = elapsed;
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
    SdCardBase* sd = static_cast<SdCardBase*>(c->context);
    SdErr_t err = sd->sync();
    return (err == SD_ERR_NOERR) ? LFS_ERR_OK : LFS_ERR_IO;
}

// --------------------------------------------------------------------------------------------
int lfs_mutex_take(const struct lfs_config *c)
{
    // Use FreeRTOS semaphore instead of pico_sync mutex
    // (pico_sync mutex uses event groups which fail in ISR context)
    if (xSemaphoreTake(lfs_semaphore, portMAX_DELAY) == pdTRUE) {
        return 0;
    }
    return -1;
}

// --------------------------------------------------------------------------------------------
int lfs_mutex_give(const struct lfs_config *c)
{
    // Use FreeRTOS semaphore
    xSemaphoreGive(lfs_semaphore);
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
bool comingOnline(SdCardBase* sdCard)
{
    uint32_t t0, t1;
    static uint32_t mountTime_us;
    static uint32_t formatTime_us;
    int32_t mount_err;

    printf("%s: Bringing SD card online\n", __FUNCTION__);
    printf("  Interface: %s at %.1f MHz\n",
           sdCard->getInterfaceMode(),
           sdCard->getClockFrequency_Hz() / 1000000.0);

    // Default all fields to zero
    memset((void*)&lfs, 0, sizeof(lfs));
    memset((void*)&lfs_cfg, 0, sizeof(lfs_cfg));

    // Save an opaque pointer to the SdCard that will be servicing the LFS read/write requests
    lfs_cfg.context = static_cast<void*>(sdCard);

    lfs_cfg.read  = lfs_read;
    lfs_cfg.prog  = lfs_prog;
    lfs_cfg.erase = lfs_erase;
    lfs_cfg.sync  = lfs_sync;

    uint32_t sectorSize = sdCard->getSectorSize();
    uint32_t sectorCount = sdCard->getSectorCount();

    if (sectorSize != 512) {
        printf("ERROR: Unexpected sector size: %u\n", sectorSize);
        lfs_mounted = false;
        return false;
    }

    if (sectorCount == 0) {
        printf("ERROR: Card reports 0 sectors\n");
        lfs_mounted = false;
        return false;
    }

    // Configure LittleFS
    // Start with 512-byte blocks (proven to work), test larger blocks later
    const uint32_t LFS_BLOCK_SIZE = 16384;

    lfs_cfg.read_size = 512;              // SD sector size
    lfs_cfg.prog_size = 512;              // SD sector size
    lfs_cfg.block_size = LFS_BLOCK_SIZE;  // LFS logical block
    lfs_cfg.cache_size = LFS_BLOCK_SIZE;

    // Calculate block count (use 64-bit to avoid overflow)
    lfs_cfg.block_count = ((uint64_t)sectorCount * sectorSize) / LFS_BLOCK_SIZE;

    lfs_cfg.block_cycles = 500;
    lfs_cfg.lookahead_size = 128;

    printf("Filesystem Configuration\n");
    printf("  SD Card: %u sectors x 512 bytes = %.1f GB\n",
           sectorCount, (float)sectorCount * 512 / 1e9);
    printf("  LittleFS: %u blocks x %u bytes\n",
           (uint32_t)lfs_cfg.block_count, LFS_BLOCK_SIZE);
    printf("  read_size: %d\n", lfs_cfg.read_size);
    printf("  prog_size: %d\n", lfs_cfg.prog_size);
    printf("  cache_size: %d\n", lfs_cfg.cache_size);

    // This disables wear-leveling because SD cards do it themselves.
    // See https://github.com/joltwallet/esp_littlefs/issues/211#issuecomment-2585285239
    lfs_cfg.block_cycles = -1;

    // Create FreeRTOS semaphore for LittleFS locking
    // (don't use pico_sync mutex - it uses event groups which fail in ISR context)
    lfs_semaphore = xSemaphoreCreateMutex();
    configASSERT(lfs_semaphore != NULL);

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
                        lfs_mounted = false;
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
            lfs_mounted = false;
            return false;
        }
        // Mount the freshly formatted device
        printf("%s: Mounting reformatted filesystem\n", __FUNCTION__);
        mount_err = lfs_mount(&lfs, &lfs_cfg);
        if (mount_err<0) {
            // Still unable to mount the device!
            printf("%s: Mount of reformatted filesystem failed! mount_err=%d\n", __FUNCTION__, mount_err);
            lfs_mounted = false;
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
    lfs_mounted = true;  // Mark filesystem as successfully mounted

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
void goingOffline(SdCardBase* sdCard)
{
    if (logger) {
        logger->deinit();
    }

    // Unmount LittleFS to flush metadata
    if (lfs_mounted) {
        printf("goingOffline: Unmounting LittleFS\n");
        lfs_unmount(&lfs);
        lfs_mounted = false;
    }

    // Shutdown the SD card hardware
    if (sdCard) {
        printf("goingOffline: Shutting down SD card\n");
        #warning "fixme: missing shutdown"
        //sdCard->shutdown();
    }
}

// ----------------------------------------------------------------------------------
// C-compatible function for OTA task to call before reboot
// This performs a complete shutdown of the filesystem and SD card
extern "C" void sd_shutdown_for_reboot(void)
{
    printf("sd_shutdown_for_reboot: Starting filesystem/SD shutdown\n");

    // 1. Stop new filesystem operations
    lfs_mounted = false;

    // 2. Shutdown logger (closes log file)
    if (logger) {
        printf("sd_shutdown_for_reboot: Stopping logger\n");
        logger->deinit();
    }

    // 3. Give any pending file operations time to complete
    busy_wait_us_32(10000);  // 10ms

    // 4. Unmount LittleFS (flushes all metadata)
    printf("sd_shutdown_for_reboot: Unmounting LittleFS\n");
    lfs_unmount(&lfs);

    // 5. Shutdown SD card hardware
    if (sdCard) {
        printf("sd_shutdown_for_reboot: Shutting down SD card\n");
        #warning "FIXME: no shutdown"
        //sdCard->shutdown();
    }

    printf("sd_shutdown_for_reboot: Complete\n");
}


// ----------------------------------------------------------------------------------
void startFileSystem(void)
{
    static hotPlugMgrCfg_t cfg;

#ifdef USE_SDIO_MODE
    printf("%s: 4-bit SDIO mode\n", __FUNCTION__);

    // Create the SdCardSDIO object - uses proven SDIO_RP2350 library low-level functions
    sdCard = new SdCardSDIO(SD_CARD_PIN);
#else
    printf("%s: 1-bit SPI mode\n", __FUNCTION__);

    static Spi* spiSd;

    // Init the SPI port that is associated with the SD card socket
    spiSd  = new Spi(SD_SPI_PORT, SD_SCK_PIN, SD_MOSI_PIN, SD_MISO_PIN);

    // Create the SdCard object to be associated with the SD card socket
    sdCard = new SdCard(spiSd, SD_CARD_PIN, SD_CS_PIN);
#endif

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

// --------------------------------------------------------------------------------------------
// Stack overflow detection hook - called when FreeRTOS detects a stack overflow
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    printf("STACK OVERFLOW in task: %s\n", pcTaskName);
    panic("Stack overflow");
}


// ----------------------------------------------------------------------------------
// The ISR for the 32-bit UART receiving ECU logging data from the EP.
// The EP always sends over log events as a full word of data:
//      bits 0..8:      length, which can only be 1, 2, or 3:
//                          - 1 means LogID only
//                          - 2 means LogID and LSB
//                          - 3 means LogID and LSB and MSB
//      bits 8..15:     8-bit LogID
//      bits 16..23:    LSB of the log data
//      bits 24..31:    MSB of the log data
//
// This ISR moves ECU events from the receive FIFO to the merged log buffer.
void isr_rx32()
{
    while(!pio_sm_is_rx_fifo_empty(PIO_UART, pio_sm_uart)) {
        uint32_t rxWord = uart_rx32_program_get(PIO_UART, pio_sm_uart);
        logger->logData_fromISR(rxWord);

        // Update the live ECU log data array
        uint8_t logId = (rxWord >> 8) & 0xFF;
        ecuLiveLog[logId] = (rxWord >> 16) & 0xFFFF;
    }

    // The fifo-not-empty interrupt is cleared automatically when we empty out the FIFO
}


// ----------------------------------------------------------------------------------
// The EP PIO UART receives the ECU data stream.
// During normal system operation, this UART is unidirectional, meaning receive only.
// It must be a PIO-based UART because we need it to receive data in 32-bit units
// and the RP2350 UART silicon only does 8-bit transfers.
// All ECU data comes across as 32-bit words, each containing a complete log event
// that we will inserted into the log in atomic fashion.
void initEpUart() {

    // Set up the PIO unit to act as our UART
    pio_sm_uart = pio_claim_unused_sm(PIO_UART, true);
    uint offset = pio_add_program(PIO_UART, &uart_rx32_program);
    uart_rx32_program_init(PIO_UART, pio_sm_uart, offset, EPLOG_RX_PIN, EP_TO_WP_BAUDRATE);
    printf("UART_RX32: Using PIO%d, SM%d, program start @ offset %d (size: %d instructions)\n",
           pio_get_index(PIO_UART), pio_sm_uart, offset, uart_rx32_program.length);


    // Assign an interrupt handler
    irq_set_exclusive_handler(PIO_UART_RX_IRQ, isr_rx32);

    printf("%s: UART_RX32 ISR will be serviced by RP2350 core %d\n", __FUNCTION__, get_core_num());
    // Leave interrupts off until we enable the flowcontrol signal
}

// ----------------------------------------------------------------------------------
void allowEpToSendData()
{
    assert(logger != nullptr);

    // Make sure the rxQ is empty prior to giving the EP the OK to send
    // in case we received some garbage during boot.
    while(!pio_sm_is_rx_fifo_empty(PIO_UART, pio_sm_uart)) {
        uint16_t logEvent = uart_rx32_program_get(PIO_UART, pio_sm_uart);
    }

    // Enable RX FIFO Not Empty interrupt in this core's NVIC
    irq_set_enabled(PIO_UART_RX_IRQ, true);

    // Enable the statemachine-specific RX-FIFO-not-empty interrupt inside the proper PIO unit
    switch (pio_sm_uart) {
        case 0:
            PIO_UART->inte0 = PIO_INTR_SM0_RXNEMPTY_BITS;
            break;
        case 1:
            PIO_UART->inte0 = PIO_INTR_SM1_RXNEMPTY_BITS;
            break;
        case 2:
            PIO_UART->inte0 = PIO_INTR_SM2_RXNEMPTY_BITS;
            break;
        case 3:
            PIO_UART->inte0 = PIO_INTR_SM3_RXNEMPTY_BITS;
        default:
            panic("Invalid RX32 PIO state machine number");
    }

    // Tell the EP we are ready for data
    gpio_put(EPLOG_FLOWCTRL_PIN, 0);
    gpio_set_dir(EPLOG_FLOWCTRL_PIN, GPIO_OUT);
}

#ifdef HEAP_MON_TASK
// ----------------------------------------------------------------------------------
// A heap monitoring task that periodically prints out heap usage statistics.
//
// The Pico SDK/Newlib internal function is _sbrk
extern "C" {
    extern char __bss_end__;
    extern char __StackLimit;
    extern char __StackTop;
    void* _sbrk(ptrdiff_t incr);
}

void heap_monitor_task(void *pvParameters)
{
    uintptr_t heap_start = (uintptr_t)&__bss_end__;
    uintptr_t heap_limit = (uintptr_t)&__StackLimit;
    uintptr_t stack_top  = (uintptr_t)&__StackTop;
    static uint32_t min_remaining;

    const uint32_t max_heap_potential = heap_limit - heap_start;

    while (true) {
        // Use the older mallinfo struct
        struct mallinfo mi = mallinfo();

        // Get the current "top" of the heap from the system
        char* heap_top = (char*)_sbrk(0);

        uint32_t remaining = max_heap_potential - mi.arena;
        if (min_remaining != remaining) {
            min_remaining = remaining;

            printf("%s: Heap [max/remaining/inuse/free]: [%d/%d/%d/%d]\n",
                __FUNCTION__,
                max_heap_potential,
                remaining,
                mi.arena,
                mi.uordblks,
                mi.fordblks,
                (void*)heap_top
            );
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
#endif

// ----------------------------------------------------------------------------------
// This function is used to perform the FreeRTOS task initialization.
// FreeRTOS is known to be running when this routine is called so it is free
// to use any FreeRTOS constructs and call any FreeRTOS routines.
void boot_system(void* args)
{
    BaseType_t err;

    // Get the LED working first so that it can be used by the rest of the system.
    // We need to tell it which PIO to use, but it will claim its own state machine.
    rgb_led = new NeoPixelConnect(WS2812_PIN, WS2812_PIXCNT, PIO_WS2812);
    rgb_led->neoPixelSetValue(0, 16, 16, 16, true);

    initEpUart();

    // The logger may be started early since it has a big buffer to handle extremely long LittleFS write times.
    printf("%s: Creating the logger\n", __FUNCTION__);
    logger = new Logger(LOG_BUFFER_SIZE);

    // First thing we log is what version of the log we are generating:
    uint8_t v = LOGID_GEN_WP_LOG_VER_VAL_V0;
    logger->logData(LOGID_GEN_WP_LOG_VER_TYPE_U8, LOGID_GEN_WP_LOG_VER_DLEN, &v);

    #ifdef HEAP_MON_TASK
    TaskHandle_t heap_monitor_task_handle;
    err = xTaskCreate(heap_monitor_task, "Heap Monitor", 1024, NULL, 1, &heap_monitor_task_handle);
    #endif

    printf("%s: Starting the filesystem\n", __FUNCTION__);
    startFileSystem();

    // Now that the UART and logger are up: signal the EP that is is OK to send us data
    allowEpToSendData();

    printf("%s: Starting the GPS\n", __FUNCTION__);
    startGps();

    printf("%s: Starting the debug shell\n", __FUNCTION__);
    dbgShell = new Shell(&lfs);

    printf("%s: Creating WiFi manager\n", __FUNCTION__);
    wifiMgr = new WiFiManager();

    // Configure server address for check-in notifications (if defined at build time)
    // UMOD4_SERVER_HOST can be an IP address ("192.168.1.100") or hostname ("umod4-server.local")
    #ifdef UMOD4_SERVER_HOST
        #ifdef UMOD4_SERVER_PORT
            wifiMgr->setServerAddress(UMOD4_SERVER_HOST, UMOD4_SERVER_PORT);
        #else
            wifiMgr->setServerAddress(UMOD4_SERVER_HOST);  // Use default port 8081
        #endif
    #endif

    printf("%s: Creating Network manager (MDL HTTP server)\n", __FUNCTION__);
    networkMgr = new NetworkManager(wifiMgr);

    printf("%s: Initializing file I/O task\n", __FUNCTION__);
    file_io_task_init();

    printf("%s: Initializing OTA flash task\n", __FUNCTION__);
    ota_flash_task_init();

    // Instantiate an SWD object to reprogram the EP, if needed
    bool verbose = false;
    swd = new Swd(PIO_SWD, EP_SWCLK_PIN, EP_SWDAT_PIN, verbose);

    // All done booting: delete this task
    vTaskDelete(NULL);
}

// --------------------------------------------------------------------------------------------
// The Pico2W does not have a simple LED, but the umod4 board adds one for it to drive.
void pico_led_init(void)
{
    gpio_init(SPARE1_LED_PIN);
    gpio_put(SPARE1_LED_PIN, 0);
    gpio_set_dir(SPARE1_LED_PIN, GPIO_OUT);
}

void pico_set_led(bool led_on)
{
    gpio_put(SPARE1_LED_PIN, led_on);
}

void pico_toggle_led()
{
    gpio_xor_mask(1u<<SPARE1_LED_PIN);
}

// --------------------------------------------------------------------------------------------
// Fast flash the LED 'count' times as a most basic sign of life as we boot.
void hello(int32_t count)
{
    pico_led_init();

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
void epResetAndRun()
{
    gpio_init(EP_RUN_PIN);
    gpio_set_dir(EP_RUN_PIN, GPIO_OUT);
    gpio_put(EP_RUN_PIN, 0);
    sleep_us(100);
    gpio_put(EP_RUN_PIN, 1);
}

// ----------------------------------------------------------------------------------
// Helper functions for MDL API handlers

extern "C" const char* get_wp_version(void) {
    // Return the build system-generated version info (git hash + build time)
    return SYSTEM_JSON;
}

extern "C" bool wifi_is_connected(void) {
    return (wifiMgr != nullptr) && wifiMgr->isReady();
}

extern "C" const char* wifi_get_ssid(void) {
    return WIFI_SSID;
}

// ----------------------------------------------------------------------------------
// Helper functions for OTA flash task (C-compatible wrappers for C++ objects)

extern "C" bool ota_logger_valid(void) {
    return (logger != nullptr);
}

extern "C" void ota_shutdown_logger(void) {
    if (logger != nullptr) {
        logger->deinit ();
    }
}

// ----------------------------------------------------------------------------------
void partition_info()
{
    int err = rom_load_partition_table(workarea, sizeof(workarea), true);
    if (err != BOOTROM_OK) {
        printf("%s: rom_load_partition_table error %d\n", __FUNCTION__, err);
    }
    boot_info = {};
    bool ok = rom_get_boot_info(&boot_info);
    if (!ok) {printf("%s: rom_get_boot_info FAILED\n", __FUNCTION__);}
    int b = boot_info.partition;
    printf("WP  Boot partition: %d/%c\n", b, (b>=0) ? ('A'-1+b) : '?');
    int o = rom_get_b_partition(b);
    printf("WP Other partition: %d/%c\n", o, (o>=0) ? ('A'-1+o) : '?');


    // We can only do OTA if this system contains a valid partition table.
    // This will be the case UNLESS this app was flashed by the debugger
    // and there is no partition table available.
    ota_available = (ok && boot_info.partition >= 0);
    printf("WP OTA is%s available\n", ota_available ? "" : " NOT");
    printf("\n");
}

// ----------------------------------------------------------------------------------
// TBYB (Try Before You Buy) OTA Update Commitment
//
// If we just booted from a newly-flashed OTA image, we have ~16.7 seconds
// to call rom_explicit_buy() or the bootrom will revert to the previous
// partition on the next reboot.
void check_tbyb()
{
    if (!ota_available) {
        printf("%s: Skipping TBYB check: OTA not available - This is a debug session!\n", __FUNCTION__);
        return;
    }

    if (!FlashWp::isOtaPending()) {
        printf("%s: No commit required\n", __FUNCTION__);
    }
    else {
        extern void unpause_watchdog_tick();   // fixme

        // Get the WS2812 LED working again after the reboot.
        // Start off by resetting *all* PIO blocks - we don't need to figure out which one the WS2812 might be using
        reset_block(RESETS_RESET_PIO0_BITS | RESETS_RESET_PIO1_BITS | RESETS_RESET_PIO2_BITS);
        unreset_block_wait(RESETS_RESET_PIO0_BITS | RESETS_RESET_PIO1_BITS | RESETS_RESET_PIO2_BITS);
        rgb_led = new NeoPixelConnect(WS2812_PIN, WS2812_PIXCNT, PIO_WS2812);

        // Drive LED BLUE to indicate commit event
        rgb_led->neoPixelSetValue(0, 0,0,30, true);

        busy_wait_ms(1000);

        // This boot was a warm boot to perform a TBYB check on this image that is running right now.
        printf("%s: Committing OTA update\n", __FUNCTION__);
        // For now, we assume that this new image is working
        int32_t commit_result = FlashWp::commitOtaUpdate();
        if (commit_result == 0) {
            printf("%s:   Commit succeeded!\n", __FUNCTION__);
            rgb_led->neoPixelSetValue(0, 0,30,0, true);
        }
        else {
            printf("%s: Commit failed: %d\n", __FUNCTION__, commit_result);
            rgb_led->neoPixelSetValue(0, 30,0,0, true);
        }
        busy_wait_ms(1000);

        // We have an issue here. Because a TBYB boot event represents a 'warm' restart
        // of the system, the system hardware (CPU, wifi module, PIO, etc.) may
        // in an indeterminate state because there was no hard RESET event.
        // The simplest way to fix that is to perform a watchdog reboot here now
        // that we are committed. The watchdog reset event will select this new image
        // now that it is committed.
        unpause_watchdog_tick();        // Allow the watchdog to time out even if a debugger is connected
        watchdog_enable(1, false);
        while (true) {
            __wfi();
        }
    }
}

// ----------------------------------------------------------------------------------
// Before main() is called, the Pico boot code in pico-sdk/src/rp2_common/pico_standard_link/crt0.S
// calls function runtime_init() in pico-sdk/src/rp2_common/pico_runtime/runtime.c
// That is where all the behind-the-scenes initialization of the runtime occurs.
int main()
{
    // Note: With configNUMBER_OF_CORES=1, core 1 is never used.

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

    // Init PPS pin to have a pulldown so that it can't cause rising-edge interrupts
    // if no module is present
    gpio_init(GPS_PPS_PIN);
    gpio_set_dir(GPS_PPS_PIN, GPIO_IN);
    gpio_set_pulls(GPS_PPS_PIN, false, true);

    {
        // While bench testing, it is hugely useful to reset the EP here which
        // mimics both processors getting reset at ignition key ON.
        #warning " ********** EXTREMELY TEMP - RESETTING THE EP **************"
        epResetAndRun();
    }

    hello(3);

    initSpareIos();

    stdio_init_all();

    printf("\n\nWP Core %d booting on board %s\n", get_core_num(), STRINGIFY(PICO_BOARD));
    printf("WP Version JSON: %s\n", SYSTEM_JSON);
    uint32_t f_clk_sys = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS);
    printf("WP System clock: %.1f MHz\n", f_clk_sys / 1000.0);
    partition_info();
    check_tbyb();

    //printf("Heap Range: 0x%08x..0x%08X (%u bytes)\n", heap_start, heap_end, (heap_end-heap_start));

    // Create a transient task to boot the rest of the system
    BaseType_t err = xTaskCreate(
        boot_system,        // func ptr of task to run
        "boot_system",      // task name
        2048,               // stack size (words)
        NULL,               // args
        1,                  // priority
        NULL                // task handle (if any)
    );

    if (err != pdPASS) {
        panic("Boot Task creation failed!");
    }

    vTaskStartScheduler();
}