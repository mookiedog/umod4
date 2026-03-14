#include "pico/stdlib.h"
#include "pico/sync.h"
#include "Logger.h"
#include "lfsMgr.h"
#include "umod4_WP.h"
#include "log_ids.h"
#include "WiFiManager.h"

const uint32_t dbg = 1;
extern void pico_set_led(bool on);
extern WiFiManager* wifiMgr;

// ----------------------------------------------------------------------------------
extern "C" void start_logger_task(void *pvParameters);

void start_logger_task(void *pvParameters)
{
    // The task parameter is the specific Logger object whose task needs to be started
    Logger* l = static_cast<Logger*>(pvParameters);

    l->logTask();
    panic("Should never get here!");
}


// ----------------------------------------------------------------------------------
Logger::Logger(uint8_t* _buffer, int32_t _size)
{
    buffer = _buffer;
    bufferLen = _size;

    lastBufferP = buffer + _size - 1;
    headP = tailP = buffer;
    inUse_max = 0;

    // Claim a hardware spinlock for protecting buffer access
    // This works from both ISR and task context, and across cores
    int spinlock_num = spin_lock_claim_unused(true);
    bufferLock = spin_lock_init(spinlock_num);

    xTaskCreate(
        start_logger_task,
        "Log",
        1024+512,
        this,
        TASK_NORMAL_PRIORITY,
        &log_taskHandle);
}

// ----------------------------------------------------------------------------------
void Logger::deinit()
{
    lfs = nullptr;
    memset(logName, 0, sizeof(logName));
    memset(&logf, 0, sizeof(logf));

    if (dbg) printf("%s: Logging is disabled\n", __FUNCTION__);
}

// ----------------------------------------------------------------------------------
bool Logger::init(lfs_t* _lfs)
{
    if (!_lfs) {
        return false;
    }

    int32_t err = getDiskInfo(_lfs);
    bool ok = (err == 0);
    if (ok) {
        lfs = _lfs;
    }

    return ok;
}

// ----------------------------------------------------------------------------------
int32_t Logger::getDiskInfo(lfs_t* _lfs)
{
    int32_t err = lfs_fs_stat(_lfs, &fsinfo);
    if (err) {
        printf("%s: Unable to stat the filesystem: err=%d\n", __FUNCTION__, err);
    }
    else {
        printf("Filesystem information:\n");
        printf("  Disk Version: %d\n", fsinfo.disk_version);
        float size = ((float)fsinfo.block_count * (float)fsinfo.block_size) / 1000000000.0;
        printf("  Disk Size: %.1f gigabytes (%d blocks of %d bytes per block)\n",
            size,
            fsinfo.block_count,
            fsinfo.block_size
        );
        printf("  Max file name length: %d bytes\n", fsinfo.name_max);
        printf("  Max file length: %d bytes\n", fsinfo.file_max);
    }

    return err;
}


// ----------------------------------------------------------------------------------
// The log files we create will have the form:
//   "run_xx.um4"
// where xx is up to a 5 digit decimal integer in the range 0 to 99999 (no leading zeroes)
bool Logger::openNewLog()
{
    uint16_t id;
    lfs_file_t fp;
    lfs_dir_t dir;
    struct lfs_info info;
    int32_t lfs_err;
    int32_t len;
    const char* path="/";
    const char* prefix = "log_";
    uint32_t prefixLen = strlen(prefix);
    const char* suffix = ".um4";
    uint32_t suffixLen = strlen(suffix);

    lfs_err = lfs_dir_open(lfs, &dir, path);
    if (lfs_err < 0) {
        printf("unable to open directory %s\n", path);
    }
    else {
        // Scan through every file in the directory
        bool found = false;
        uint32_t maxValue = 0;
        do {
            uint32_t size;
            lfs_err = lfs_dir_read(lfs, &dir, &info);
            if (lfs_err > 0) {
                if (info.type == LFS_TYPE_REG) {
                    uint32_t value;

                    // Check if this name starts with the right prefix
                    if (strncmp(prefix, info.name, prefixLen) == 0) {
                        // Yes! Now make sure that we only see decimal digits up to a '.' character
                        char* p = &info.name[prefixLen];
                        uint32_t digitCount = 0;
                        uint32_t value = 0;
                        while ((*p>='0') && (*p<='9')) {
                            digitCount++;
                            value = (value*10) + (*p-'0');
                            p++;
                        }

                        // Pointer p is now pointing at the first non-digit char of the file name
                        // We are expecting that it should point at the suffix
                        if ((digitCount >= 1) && (digitCount<=5) && (strncmp(p, suffix, suffixLen)==0)) {
                            // We found a valid logfile:
                            //      - the expected prefix,
                            //      - followed by a numeric specifier between 1 and 5 digits long,
                            //      - followed by the proper suffix
                            found = true;
                            if (value > maxValue) {
                                maxValue = value;
                            }
                        }
                    }
                }
            }
        } while (lfs_err > 0);
        lfs_dir_close(lfs, &dir);

        if (!found) {
            maxValue = 0;
        }

        snprintf(logName, sizeof(logName), "%s%d%s", prefix, maxValue+1, suffix);
        printf("%s: Creating logfile with temporary name \"%s\"\n", __FUNCTION__, logName);
        lfs_err = lfs_file_open(lfs, &logf, logName, LFS_O_CREAT | LFS_O_TRUNC | LFS_O_RDWR);
        if (lfs_err != LFS_ERR_OK) {
            printf("%sw: Unable to open new logfile\"%s\": err=%d\n", __FUNCTION__, logName, lfs_err);
        } else {
            // Notify server that new log file is ready for download
            // (Previous file is now closed and complete, ready for transfer)
            if (wifiMgr != nullptr) {
                printf("%s: Triggering server check-in for new log file\n", __FUNCTION__);
                wifiMgr->triggerCheckIn();
            }
        }
    }

    return (lfs_err == LFS_ERR_OK);
}


// ----------------------------------------------------------------------------------
// This routine is strictly for the use of ISRs!
// It is expected to run at ISR level!
//
// The routine uses a spinlock to synchronize access to the log between:
//  - the ECU RX32 data stream interrupt
//  - the GPS PPS interrupt (could be running on a different core)
//  - the GPS non-interrupt data logging task
//
// A spinlock is required in case there is an ISR calling this same routine on the other core.
//
// The uint32_t dataWord paramater packs between 1 and 3 bytes to insert into the log.
// It has a very specific format:
//  - bits 0:7 contain the number of bytes to log from the remainder of this word. The only valid values are 1, 2, 3.
//  - bits 8:15 this byte will always get logged (since length must be >=1)
//  - bits 16..23 this byte will logged if length >= 2
//  - bits 24..31 this byte will be logged if length == 3

// temporary error counters for debugging
uint32_t isr1_err_cnt, isr2_err_cnt, isr3_err_cnt;

bool __time_critical_func(Logger::logData_fromISR)(uint32_t dataWord)
{
    // Validate length before acquiring the lock - only depends on dataWord
    uint32_t len = dataWord & 0xFF;
    if (len < 1 || len > 3) {
        isr1_err_cnt++;
        return false;
    }

    // Don't use spin_lock_blocking() in ISR - it would save/restore interrupt state unnecessarily
    // In ISR context, the CPU already manages interrupt masking appropriately
    spin_lock_unsafe_blocking(bufferLock);

    // Make sure there is enough room for this new entry
    if (inUse() + (int32_t)len > bufferLen) {
        isr2_err_cnt++;
        spin_unlock_unsafe(bufferLock);
        return false;
    }

    // Cast away volatile: only headP pointer needs volatile (re-read on each ISR entry),
    // satisfied by loading it once here under the spinlock.
    uint8_t* hP = (uint8_t*)headP;

    if (__builtin_expect((hP + len) <= lastBufferP, true)) {
        // Expected case:
        // All bytes land before end of buffer: unroll loop, no wrap checks needed
        *hP++ = dataWord >> 8;
        if (len >= 2) *hP++ = dataWord >> 16;
        if (len == 3) *hP++ = dataWord >> 24;
    }
    else {
        // Unusual case:
        // The write straddles end of buffer: unroll loop, but check hP wrap on every byte
        *hP = dataWord >> 8;
        if (++hP > lastBufferP) hP = buffer;
        if (len >= 2) { *hP = dataWord >> 16; if (++hP > lastBufferP) hP = buffer; }
        if (len == 3) { *hP = dataWord >> 24; if (++hP > lastBufferP) hP = buffer; }
    }

    int32_t i = inUse();
    if (i > inUse_max) {
        inUse_max = i;
    }

    headP = hP;
    spin_unlock_unsafe(bufferLock);
    return true;
}


// ----------------------------------------------------------------------------------
// For WP use ONLY!
bool __time_critical_func(Logger::logData)(uint8_t logId, uint8_t len, uint8_t* data)
{
    bool rVal = true;

    // Enter critical section first to prevent FreeRTOS from preempting this task
    // This ensures we hold the spinlock for only a brief, atomic period
    taskENTER_CRITICAL();

    // Now take the spinlock to synchronize with ISR on potentially different core
    // ISR will only wait microseconds because we can't be preempted while in critical section
    spin_lock_unsafe_blocking(bufferLock);

    //check if there is enough room left in the log buffer
    int32_t spaceRemaining = bufferLen - inUse();
    if ((len+1) <= spaceRemaining) {
        // First, insert the logID
        *headP++ = logId;
        if (headP > lastBufferP) {
            headP = buffer;
        }

        // Now copy the buffer
        while(len-- > 0) {
            *headP++ = *data++;
            if (headP > lastBufferP) {
                headP = buffer;
            }
        }
    }
    else {
        isr2_err_cnt++;
        rVal = false;
    }

    int32_t i = inUse();
    if (i > inUse_max) {
        inUse_max = i;
    }

    // Release spinlock first, then exit critical section
    spin_unlock_unsafe(bufferLock);
    taskEXIT_CRITICAL();

    return rVal;
}



// ----------------------------------------------------------------------------------
int32_t Logger::writeChunk(uint8_t* buf, int32_t len)
{
    absolute_time_t t0 = get_absolute_time();
    int32_t bytesWritten = lfs_file_write(lfs, &logf, buf, len);
    absolute_time_t elapsed = get_absolute_time() - t0;
    static uint32_t writeCount;

    writeCount++;
    #if 1
    printf("%s: %d: lfs_file_write() time: %lld uSec, bytes written: %d (%.1f KB/sec)\n",
           __FUNCTION__, writeCount, elapsed, bytesWritten, ((1000000.0f / elapsed) * bytesWritten) / 1024.0f);
    #endif

    totalWriteEvents += 1;
    totalTimeWriting += elapsed;
    if (elapsed < minTimeWriting) {
        minTimeWriting = elapsed;
    }
    if (elapsed > maxTimeWriting) {
        maxTimeWriting = elapsed;
        if (maxTimeWriting > 0) {
            #if 0
            printf("%s: New max write time: %lld uSec\n", __FUNCTION__, maxTimeWriting);
            #endif
            uint16_t mSecs;
            if (elapsed > 65535000) {
                mSecs = 65535;
            }
            else {
                mSecs = (uint32_t)elapsed / 1000;
            }
            // Nuke this for now: it makes the log decoding more complicated
            //logData(LOG_WR_TIME, mSecs);
        }
    }

    return bytesWritten;
}

// ----------------------------------------------------------------------------------
int32_t Logger::syncLog()
{
    absolute_time_t t0 = get_absolute_time();
    int32_t err = lfs_file_sync(lfs, &logf);
    absolute_time_t elapsed = get_absolute_time() - t0;
    printf("%s: lfs_file_sync() time: %lld uSec\n", __FUNCTION__, elapsed);

    totalSyncEvents += 1;
    totalTimeSyncing += elapsed;
    if (elapsed < minTimeSyncing) {
        minTimeSyncing = elapsed;
    }
    if (elapsed > maxTimeSyncing) {
        maxTimeSyncing = elapsed;
        if (maxTimeSyncing > 0) {
            #if 0
            printf("%s: New max sync time: %lld uSec\n", __FUNCTION__, maxTimeSyncing);
            #endif
            uint16_t mSecs;
            if (elapsed > 65535000) {
                mSecs = 65535;
            }
            else {
                mSecs = (uint32_t)elapsed / 1000;
            }
            // Nuke this for now: it makes the log decoding more complicated
            //logData(LOG_SYNC_TIME, mSecs);
        }
    }

    return err;
}



// Return number of bytes that should be written before fsync for optimal
// streaming performance/robustness. LittleFS needs to copy block contents
// to a new one if fsync is called mid-block, and doesn't persist file
// contents until fsync is called.
static uint32_t lfs_bytes_until_fsync(const struct lfs_config *lfs_cfg, lfs_file_t* fp)
{
    if (fp == nullptr) {
        return 0;
    }

    uint32_t file_pos = fp->pos;
    uint32_t block_size = lfs_cfg->block_size;

    // first block exclusively stores data:
    // https://github.com/littlefs-project/littlefs/issues/564#issuecomment-2555733922
    if (file_pos < block_size) {
        return block_size - file_pos;
    }

    // see https://github.com/littlefs-project/littlefs/issues/564#issuecomment-2363032827
    // n = (N - w/8 ( popcount( N/(B - 2w/8) - 1) + 2))/(B - 2w/8))
    // off = N - ( B - 2w/8 ) n - w/8popcount( n )
    #define BLOCK_INDEX(N, B) \
    (N - sizeof(uint32_t) * (__builtin_popcount(N/(B - 2 * sizeof(uint32_t)) -1) + 2))/(B - 2 * sizeof(uint32_t))

    #define BLOCK_OFFSET(N, B, n) \
    (N - (B - 2*sizeof(uint32_t)) * n - sizeof(uint32_t) * __builtin_popcount(n))

    uint32_t block_index = BLOCK_INDEX(file_pos, block_size);
    uint32_t block_offset = BLOCK_OFFSET(file_pos, block_size, block_index);

    #undef BLOCK_INDEX
    #undef BLOCK_OFFSET

    return block_size - block_offset;
}


// ----------------------------------------------------------------------------------
void Logger::logTask()
{
    static uint32_t totalByteCount;

    enum {UNUSED, UNMOUNTED, OPEN_LOG, RENAME_TMPLOG, CALC_WR_SIZE, WAIT_FOR_DATA, WRITE_DATA, WRITE_FAILURE} state, state_prev;
    const char* decodeState[]={
        "UNUSED", "UNMOUNTED", "OPEN_LOG", "RENAME_TMPLOG", "CALC_WR_SIZE", "WAIT_FOR_DATA", "WRITE_DATA", "WRITE_FAILURE"
    };

    uint32_t bytesToWriteBeforeSyncing;
    int32_t logLength = 0;

    state_prev = UNUSED;
    state = UNMOUNTED;

    deinit();

    // Clear our stats
    totalTimeWriting = maxTimeWriting = 0;
    minTimeWriting = ~0;
    totalWriteEvents = 0;
    totalTimeSyncing = maxTimeSyncing = 0;
    minTimeSyncing = ~0;
    totalSyncEvents = 0;

    while (1) {
        if (!lfs) {
            // There are no files to close or anything like that because the filesystem disappeared on us and is already gone
            state = UNMOUNTED;
        }

        if (state != state_prev) {
            state_prev = state;
        }

        switch (state) {
        case UNMOUNTED:
            // Our lfs pointer will become non-NULL if an SD card is detected, and a filesystem gets mounted
            if (lfs) {
                // The filesystem just got mounted.
                // The first LFS write will trigger a garbage collection, which can be incredibly expensive.
                // We hide some or all of that cost by immediately asking for a GC before
                // before the initial LFS write operation would trigger it.
                printf("%s: Garbage-collecting LFS\n", __FUNCTION__);
                uint32_t t0 = time_us_32();
                int err = lfs_fs_gc(lfs);
                printf("%s: Garbage collection took %d mSec\n", __FUNCTION__, (time_us_32()-t0)/1000);
                state = OPEN_LOG;
            }
            else {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            break;

        case OPEN_LOG:
            // Create a new logfile, either with a tmp-name or a timestamp-name
            {
                bool success = openNewLog();
                if (success) {
                    vTaskDelay(pdMS_TO_TICKS(250));
                    state = CALC_WR_SIZE;
                }
                else {
                    // fixme: not sure this is right
                    state = UNMOUNTED;
                }
            }

            break;

        case CALC_WR_SIZE:
            // Do the calculation that tells us how many bytes we should write before sync'ing
            bytesToWriteBeforeSyncing = lfs_bytes_until_fsync(&lfs_cfg, &logf);
            //printf("%s: bytes to write before next sync: %d\n", __FUNCTION__, bytesToWriteBeforeSyncing);
            state = WAIT_FOR_DATA;
            break;

        case WAIT_FOR_DATA:
            logLength = inUse();
            if (logLength >= bytesToWriteBeforeSyncing) {
                state = WRITE_DATA;
            }
            else {
                vTaskDelay(pdMS_TO_TICKS(250));
            }
            break;

        case WRITE_DATA:
            {
                // We copy the block to be written to our internal write buffer just in case it is split
                // across the end of the circular buffer. This allows us to always be able to write it
                // in a single LFS write call. It is way faster to copy RAM than to call lfs write twice.
                int32_t totalToWrite = bytesToWriteBeforeSyncing;
                char c='A';
                bool err = false;

                uint8_t* tP = tailP;
                int32_t bytesToEndOfBuffer = (lastBufferP - tP + 1);
                int32_t len = (bytesToEndOfBuffer < totalToWrite) ? bytesToEndOfBuffer : totalToWrite;
                int32_t len_remaining = totalToWrite-len;
                printf("%s: memcpy(1) %d bytes from 0x%08X..0x%08X\n", __FUNCTION__, len, tP, tP+len-1);
                memcpy(write_buff, tP, len);
                if (len_remaining == 0) {
                    tP = tP+len;
                }
                else {
                    // Copy the second half here:
                    printf("%s: memcpy(2) %d bytes from 0x%08X..0x%08X\n", __FUNCTION__, len_remaining, buffer, buffer+len_remaining-1);
                    memcpy(write_buff+len, buffer, len_remaining);
                    tP = buffer+len_remaining;
                }

                // Now that all the data is in the write_buff, we can free it from the circular buffer
                tailP = tP;

                // Write write_buff in one single write operation
                int32_t bytesWritten = writeChunk(write_buff, totalToWrite);
                if (bytesWritten < len) {
                    printf("%s: Write %d bytes failed: %d bytes written\n", __FUNCTION__, len, bytesWritten);
                    state = WRITE_FAILURE;
                    err = true;
                }

                if (!err) {
                    // The write[s] succeeded, but the way LittleFS works, the data that got written
                    // is not actually committed to flash until a sync succeeds (or the file gets closed):
                    int32_t lfs_err = syncLog();

                    if (lfs_err == LFS_ERR_OK) {
                        // Print some stats every megabyte written
                        totalByteCount += bytesToWriteBeforeSyncing;
                        if (totalByteCount > (1024*1024)) {
                            totalByteCount -= (1024*1024);
                            printf("%s: Writes: min: %llu uSec, max: %llu, avg: %llu\n", __FUNCTION__, minTimeWriting, maxTimeWriting, totalTimeWriting/totalWriteEvents);
                            printf("%s: Syncs:  min: %llu uSec, max: %llu, avg: %llu\n", __FUNCTION__, minTimeSyncing, maxTimeSyncing, totalTimeSyncing/totalSyncEvents);
                        }
                        // Calculate when to do the next sync:
                        state = CALC_WR_SIZE;
                    }
                    else {
                        printf("%s: syncLog() failed with error %d\n", __FUNCTION__, lfs_err);
                        state = WRITE_FAILURE;
                    }
                }

                break;
            }

        case WRITE_FAILURE:
            // ignore all errors
            lfs_file_close(lfs, &logf);
            // Delay before retrying: prevents hammering SDIO thousands of times/second
            // if the SD card or SDIO bus is in a bad state (e.g. after a false CMD13 timeout).
            vTaskDelay(pdMS_TO_TICKS(1000));
            state = OPEN_LOG;
            break;
        }
    }
}