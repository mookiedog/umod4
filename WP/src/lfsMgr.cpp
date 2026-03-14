#include "pico/stdlib.h"

#include "umod4_WP.h"
#include "lfsMgr.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "SdCardSDIO.h"


extern void pico_set_led(bool on);

// SD card instance - owned by lfsMgr, created in startFileSystem()
SdCardBase* sdCard = nullptr;

// Helper function for checking if SD card is inserted
bool sdcard_is_inserted(SdCardBase* card) {
    if (!card) return false;
    return card->cardPresent();
}

// Mount/unmount callbacks (registered by main to connect Logger lifecycle)
static lfs_mount_cb_t on_mount_cb = nullptr;
static lfs_unmount_cb_t on_unmount_cb = nullptr;

void lfs_register_mount_callbacks(lfs_mount_cb_t on_mount, lfs_unmount_cb_t on_unmount)
{
    on_mount_cb = on_mount;
    on_unmount_cb = on_unmount;
}

// Configuration of the filesystem is provided by this struct
struct lfs_config lfs_cfg;


sd_perf_stats_t sd_perf_stats = {};

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
        printf("%s: unaligned access block=%u off=%u size=%u\n",
               __FUNCTION__, (uint32_t)block_num, (uint32_t)off, (uint32_t)size_bytes);
        return LFS_ERR_INVAL;
    }

    pico_set_led(true);
    uint32_t t0 = time_us_32();
    err = sd->readSectors(sector, num_sectors, buffer);
    uint32_t elapsed = time_us_32() - t0;
    pico_set_led(false);

    if (err != SD_ERR_NOERR) {
        printf("%s: ERROR: sector=%u count=%u err=%d\n", __FUNCTION__, sector, num_sectors, err);
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
        printf("%s: ERROR: unaligned access block=%u off=%u size=%u\n",
               __FUNCTION__, (uint32_t)block, (uint32_t)off, (uint32_t)size);
        return LFS_ERR_INVAL;
    }

    pico_set_led(true);
    uint32_t t0 = time_us_32();
    err = sd->writeSectors(sector, num_sectors, buffer);
    uint32_t elapsed = time_us_32() - t0;
    pico_set_led(false);


    if (err != SD_ERR_NOERR) {
        printf("%s: ERROR: sector=%u count=%u err=%d\n", __FUNCTION__, sector, num_sectors, err);
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

// --------------------------------------------------------------------------------------------
void lfs_lock(void)
{
    xSemaphoreTake(lfs_semaphore, portMAX_DELAY);
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
    // LittleFS has well-known problems when used with large flash devices like SD Cards.
    // The issue is that write operations can take literally minutes if the system
    // has poor configuration settings. Optimal configuration settings can make this
    // better, but not fix it totally.

    // Large blocks are the first line of defence in keeping LittleFS from disappearing during write operations.
    const uint32_t LFS_BLOCK_SIZE = 16384;              // big blocks are written efficiently using multisector writes
    const uint32_t LFS_CACHE_SIZE = 16384;
    const uint32_t LFS_LOOKAHEAD_SIZE_BITS  = 8192;

    // Statically allocate the main read/prog/lookahead buffers.
    // Avoids repeated heap malloc/free at mount time which fragments the heap.
    // Sizes: read_buf=4KB, prog_buf=4KB, lookahead_buf=1KB → 9KB BSS, zero heap.
    static uint8_t global_read_buf[LFS_CACHE_SIZE];
    static uint8_t global_prog_buf[LFS_CACHE_SIZE];
    static uint8_t lookahead_buf[LFS_LOOKAHEAD_SIZE_BITS/8];
    lfs_cfg.read_buffer = global_read_buf;
    lfs_cfg.prog_buffer = global_prog_buf;
    lfs_cfg.lookahead_buffer = lookahead_buf;

    lfs_cfg.lookahead_size = LFS_LOOKAHEAD_SIZE_BITS;   // This is the number of bits in the lookahead table.
    lfs_cfg.metadata_max  = 2048;                       // keep this much smaller than block size (default)

    // These settings do not really affect write performance:
    lfs_cfg.read_size = 512;                            // SD sector size
    lfs_cfg.prog_size = 512;                            // SD sector size
    lfs_cfg.block_size = LFS_BLOCK_SIZE;
    lfs_cfg.cache_size = LFS_CACHE_SIZE;

    // Calculate block count (use 64-bit to avoid overflow)
    lfs_cfg.block_count = ((uint64_t)sectorCount * sectorSize) / LFS_BLOCK_SIZE;

    printf("Filesystem Configuration\n");
    printf("  SD Card: %u sectors x 512 bytes = %.1f GB\n",
           sectorCount, (float)sectorCount * 512 / 1e9);
    printf("  LittleFS: %u blocks x %u bytes\n",
           (uint32_t)lfs_cfg.block_count, LFS_BLOCK_SIZE);
    printf("  read_size:      %d\n", lfs_cfg.read_size);
    printf("  prog_size:      %d\n", lfs_cfg.prog_size);
    printf("  cache_size:     %d\n", lfs_cfg.cache_size);
    printf("  lookahead_size: %d\n", lfs_cfg.lookahead_size);
    printf("  metadatas_max:  %d\n", lfs_cfg.metadata_max);

    // This disables wear-leveling because SD cards do it themselves.
    // See https://github.com/joltwallet/esp_littlefs/issues/211#issuecomment-2585285239
    lfs_cfg.block_cycles = -1;

    // Create FreeRTOS semaphore for LittleFS locking (once only)
    // (don't use pico_sync mutex - it uses event groups which fail in ISR context)
    if (lfs_semaphore == NULL) {
        lfs_semaphore = xSemaphoreCreateMutex();
        configASSERT(lfs_semaphore != NULL);
    }

    lfs_cfg.lock = lfs_mutex_take;
    lfs_cfg.unlock = lfs_mutex_give;

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

    // If we were unable to mount the filesystem for some reason other than an IO error.
    // Try reformatting it. This should only happen on the first boot.
    if (mount_err) {
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

    // Notify registered callback (e.g. Logger) that filesystem is now available
    if (on_mount_cb) {
        if (!on_mount_cb(&lfs)) {
            return false;
        }
    }

    return true;
}


// ----------------------------------------------------------------------------------
void goingOffline(SdCardBase* sdCard)
{
    if (on_unmount_cb) {
        on_unmount_cb();
    }

    // Unmount LittleFS to flush metadata
    if (lfs_mounted) {
        printf("%s: Unmounting LittleFS\n", __FUNCTION__);
        lfs_unmount(&lfs);
        lfs_mounted = false;
    }

    // Shutdown the SD card hardware
    if (sdCard) {
        printf("%s: Shutting down SD card\n", __FUNCTION__);
        sdCard->shutdown();
    }
}


// ----------------------------------------------------------------------------------
// OTA task should call this before reboot
// This performs a complete shutdown of the filesystem and SD card
void sd_shutdown_for_reboot(void)
{
    printf("%s: Starting filesystem/SD shutdown\n", __FUNCTION__);

    // 1. Stop new filesystem operations
    lfs_mounted = false;

    // 2. Shutdown logger (closes log file)
    if (on_unmount_cb) {
        printf("%s: Stopping logger\n", __FUNCTION__);
        on_unmount_cb();
    }

    // 3. Give any pending file operations time to complete
    busy_wait_us_32(10000);  // 10ms

    // 4. Unmount LittleFS (flushes all metadata)
    printf("%s: Unmounting LittleFS\n", __FUNCTION__);
    lfs_unmount(&lfs);

    // 5. Shutdown SD card hardware
    if (sdCard) {
        printf("%s: Shutting down SD card\n", __FUNCTION__);
        sdCard->shutdown();
    }

    printf("%s: Complete\n", __FUNCTION__);
}


// ----------------------------------------------------------------------------------
void startFileSystem(void)
{
    static hotPlugMgrCfg_t cfg;

    printf("%s: 4-bit SDIO mode\n", __FUNCTION__);

    // Create the SdCardSDIO object - uses proven SDIO_RP2350 library low-level functions
    sdCard = new SdCardSDIO(SD_CARD_PIN);

    // Set up a configuration object required by the hotPlugManager so that it knows what SdCard instance to
    // use, and what callback routines to call when that SdCard instance is coming up or going down.
    // The comingUp() callback would typically mount a filesystem that it found on the SdCard.
    cfg.sdCard = sdCard;
    cfg.comingUp = comingOnline;
    cfg.goingDown = goingOffline;

    // Start a hot plug manager task on core 0 to control the new SdCard instance.
    // If it runs on core1, there is a chance that core1 can be testing an SD card at the same time
    // as an LFS operation starts running on core0.
    printf("%s: Starting hotPlugManager task\n", __FUNCTION__);
    BaseType_t err = xTaskCreateAffinitySet(
        SdCardSDIO::hotPlugManager,
        "HotPlugMgr",
        HOTPLUG_MGR_STACK_SIZE_WORDS,
        &cfg,
        TASK_HIGH_PRIORITY,
        (1<<0),
        NULL);

    if (err != pdPASS) {
        panic("Unable to create hotPlugManager task");
    }
}
