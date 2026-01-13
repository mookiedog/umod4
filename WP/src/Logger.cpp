#include "pico/stdlib.h"
#include "pico/sync.h"
#include "Logger.h"
#include "NeoPixelConnect.h"
#include "SdCard.h"
#include "umod4_WP.h"
#include "WP_log.h"
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
Logger::Logger(int32_t _size)
{
    buffer = (uint8_t*)malloc(_size);
    if (!buffer) {
        panic("Unable to malloc log buffer!");
    }
    memset(buffer, 0, _size);

    bufferLen = _size;
    lastBufferP = buffer + _size - 1;
    headP = tailP = buffer;

    // Claim a hardware spinlock for protecting buffer access
    // This works from both ISR and task context, and across cores
    int spinlock_num = spin_lock_claim_unused(true);
    bufferLock = spin_lock_init(spinlock_num);

    xTaskCreate(start_logger_task, "Log", 2048 /* words */, this, TASK_NORMAL_PRIORITY, &log_taskHandle);
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

// temp
uint32_t pps_isr_count, ecu_isr_count, isr1_err_cnt, isr2_err_cnt;

//bool __time_critical_func(Logger::logData_fromISR)(uint32_t dataWord)
bool Logger::logData_fromISR(uint32_t dataWord)
{
    // Don't use spin_lock_blocking() in ISR - it would save/restore interrupt state unnecessarily
    // In ISR context, the CPU already manages interrupt masking appropriately
    spin_lock_unsafe_blocking(bufferLock);

    // Update the new headP exactly once after doing the copy
    //uint8_t* hP = headP;

    uint32_t len = dataWord & 0xFF;
    int32_t spaceRemaining = bufferLen - inUse();
    if ((len < 1) || (len > 3) || (spaceRemaining < len)) {
        isr1_err_cnt++;
        spin_unlock_unsafe(bufferLock);
        return false;
    }

    for (uint32_t i=0; i<len; i++) {
        dataWord >>= 8;
        *headP++ = dataWord;
        if (headP > lastBufferP) {
            headP = buffer;
        }
    }

    if (len==1) {
        // PPS
        pps_isr_count++;
    }
    else {
        // ECU
        ecu_isr_count++;
    }
    //headP = hP;
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

    // Release spinlock first, then exit critical section
    spin_unlock_unsafe(bufferLock);
    taskEXIT_CRITICAL();

    return rVal;
}


// ----------------------------------------------------------------------------------
int32_t Logger::inUse()
{
    int32_t inUse = headP - tailP;
    if (inUse<0) {
        inUse += bufferLen;
    }

    if (inUse<0) {
        __breakpoint();
    }
    return inUse;
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



// ugh
extern uint32_t lfs_bytes_until_fsync(const struct lfs_config *lfs_cfg, lfs_file_t* fp);
extern lfs_t lfs;
extern struct lfs_config lfs_cfg;

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
                // The filesystem just got mounted
                // Perform any post-mount initialization that we would like to do right here
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
                    //rgb_led->neoPixelSetValue(0, 16, 0, 0, true);
                    vTaskDelay(pdMS_TO_TICKS(250));
                    state = CALC_WR_SIZE;
                }
                else {
                    //rgb_led->neoPixelSetValue(0, 0, 16, 0, true);
                    // TMP not sure this is right
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
            #if 0
            printf("%d/%d P%d E%d E1:%d E2:%d\n", logLength, bytesToWriteBeforeSyncing, pps_isr_count, ecu_isr_count, isr1_err_cnt, isr2_err_cnt);
            #endif
            if (logLength >= bytesToWriteBeforeSyncing) {
                state = WRITE_DATA;
            }
            else {
                vTaskDelay(pdMS_TO_TICKS(250));
            }
            break;

        case WRITE_DATA: {
                // If the block to be written extends past the end of the circular buffer
                // we will need to write it in two pieces


                // This loop gets executed once or twice, depending if the write needs to be broken
                // in two because it extends past the end of the circular buffer
                int32_t totalToWrite = bytesToWriteBeforeSyncing;
                char c='A';
                bool err = false;
                pico_set_led(1);

                uint8_t* tP = tailP;
                do {
                    int32_t bytesToEndOfBuffer = (lastBufferP - tP + 1);
                    int32_t len = (bytesToEndOfBuffer < totalToWrite) ? bytesToEndOfBuffer : totalToWrite;
                    //printf("%s: Write chunk %c [%d bytes]\n", __FUNCTION__, c++, len);
                    int32_t bytesWritten = writeChunk(tP, len);
                    if (bytesWritten < len) {
                        printf("%s: Write %d bytes failed: %d bytes written\n", __FUNCTION__, len, bytesWritten);
                        state = WRITE_FAILURE;
                        err = true;
                    }
                    else {
                        tP += bytesWritten;
                        if (tP > lastBufferP) {
                            tP = buffer;
                        }
                        totalToWrite -= bytesWritten;
                    }
                } while (!err && (totalToWrite > 0));

                if (!err) {
                    // The write[s] succeeded, but the way LittleFS works, the data that got written
                    // is not actually committed to flash until a sync succeeds (or the file gets closed):
                    int32_t lfs_err = syncLog();

                    if (lfs_err == LFS_ERR_OK) {
                        // Now that the log data is written & committed, we can finally remove it from the queue!
                        tailP = tP;

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

                pico_set_led(0);
                break;
            }

        case WRITE_FAILURE:
            // ignore all errors
            lfs_file_close(lfs, &logf);
            state = OPEN_LOG;
            break;
        }
    }
}