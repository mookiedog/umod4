#include "pico/stdlib.h"
#include "pico/sync.h"
#include "Logger.h"
#include "LogStore.h"
#include "lfsMgr.h"
#include "umod4_WP.h"
#include "log_ids.h"
#include "WiFiManager.h"
#include "Trace.h"

static uint8_t dbg = 0;
extern void led_disk_bsy_set(bool on);
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
    Trace::reg("logger", &dbg);
    buffer = _buffer;
    bufferLen = _size;

    lastBufferP = buffer + _size - 1;
    headP = tailP = buffer;
    inUse_max = 0;
    hwm_pending = false;
    hwm_reset_countdown = 20;
    idle = true;
    reset_requested = false;
    reset_done = false;
    reset_result = 0;

    // Claim a hardware spinlock for protecting buffer access
    // This works from both ISR and task context, and across cores
    int spinlock_num = spin_lock_claim_unused(true);
    bufferLock = spin_lock_init(spinlock_num);

    static StackType_t  s_stack[1536];
    static StaticTask_t s_tcb;
    log_taskHandle = xTaskCreateStatic(
        start_logger_task,
        "Log",
        sizeof(s_stack)/4,
        this,
        TASK_NORMAL_PRIORITY,
        s_stack, &s_tcb);
}

// ----------------------------------------------------------------------------------
void Logger::deinit()
{
    // Close out whatever log was active before the filesystem goes away -- otherwise
    // LogStore::active_log_number stays set forever (LogStore is a single object that
    // outlives mount cycles), and createLog() refuses every log on remount.
    logStore->closeActiveLog();

    lfs = nullptr;
    memset(logName, 0, sizeof(logName));

    if (dbg) printf("%s: Logging is disabled\n", __FUNCTION__);
}

// ----------------------------------------------------------------------------------
bool Logger::init(lfs_t* _lfs)
{
    if (!_lfs || !logStore) {
        return false;
    }

    lfs = _lfs;
    printf("Logger: LogStore has %lu free chunks\n",
           (unsigned long)logStore->getFreeChunks());
    return true;
}


// ----------------------------------------------------------------------------------
bool Logger::openNewLog()
{
    int32_t log_num = logStore->createLog();
    if (log_num < 0) {
        printf("%s: LogStore createLog failed\n", __FUNCTION__);
        return false;
    }

    snprintf(logName, sizeof(logName), "log_%ld.um4", (long)log_num);
    printf("%s: Created %s (LogStore log %ld)\n", __FUNCTION__, logName, (long)log_num);

    if (wifiMgr != nullptr) {
        printf("%s: Triggering server check-in for new log file\n", __FUNCTION__);
        wifiMgr->triggerCheckIn();
    }

    return true;
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
uint32_t isr1_err_cnt, isr2_err_cnt, isr3_err_cnt, g_total_received;

bool __no_inline_time_critical_func(Logger::logData_fromISR)(uint32_t dataWord)
{
    // Validate length before acquiring the lock - only depends on dataWord
    uint32_t len = dataWord & 0xFF;
    if (len < 1 || len > 3) {
        isr1_err_cnt++;
        return false;
    }

    g_total_received += len;

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
        hwm_pending = true;
    }

    headP = hP;
    spin_unlock_unsafe(bufferLock);
    return true;
}


// ----------------------------------------------------------------------------------
// For WP use ONLY!
bool __no_inline_time_critical_func(Logger::logData)(uint8_t logId, uint8_t len, uint8_t* data)
{
    bool rVal = true;

    // Enter critical section first to prevent FreeRTOS from preempting this task
    // This ensures we hold the spinlock for only a brief, atomic period
    taskENTER_CRITICAL();
    g_total_received += len;

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
        hwm_pending = true;
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
    int32_t bytesWritten = logStore->write(buf, len);
    absolute_time_t elapsed = get_absolute_time() - t0;
    static uint32_t writeCount;

    writeCount++;
    // printf("%s: %d: logStore->write() time: %lld uSec, bytes written: %d (%.1f KB/sec)\n",
    //        __FUNCTION__, writeCount, elapsed, bytesWritten,
    //        elapsed > 0 ? ((1000000.0f / elapsed) * bytesWritten) / 1024.0f : 0.0f);

    totalWriteEvents += 1;
    totalTimeWriting += elapsed;
    if (elapsed < minTimeWriting) minTimeWriting = elapsed;
    if (elapsed > maxTimeWriting) maxTimeWriting = elapsed;

    return bytesWritten;
}

// ----------------------------------------------------------------------------------
int32_t Logger::syncLog()
{
    absolute_time_t t0 = get_absolute_time();
    bool ok = logStore->syncMetadata();
    absolute_time_t elapsed = get_absolute_time() - t0;
    if (elapsed < minTimeSyncing) minTimeSyncing = elapsed;
    if (elapsed > maxTimeSyncing) maxTimeSyncing = elapsed;

    // printf("%s: syncMetadata(): %lld uSec (%lld/%lld)\n", __FUNCTION__, elapsed, minTimeSyncing, maxTimeSyncing);

    totalSyncEvents += 1;
    totalTimeSyncing += elapsed;
    if (elapsed < minTimeSyncing) minTimeSyncing = elapsed;
    if (elapsed > maxTimeSyncing) maxTimeSyncing = elapsed;

    return ok ? 0 : -1;
}


// ----------------------------------------------------------------------------------
void Logger::logTask()
{
    static uint32_t totalByteCount;
    static bool first_write_pending;    // true from the moment a log is opened until its first write

    enum {UNMOUNTED, OPEN_LOG, BOOT_SYNC, WAIT_FOR_DATA, WRITE_DATA, WRITE_FAILURE} state, state_prev;

    state_prev = UNMOUNTED;
    state = UNMOUNTED;

    deinit();

    totalTimeWriting = maxTimeWriting = 0;
    minTimeWriting = ~0;
    totalWriteEvents = 0;
    totalTimeSyncing = maxTimeSyncing = 0;
    minTimeSyncing = ~0;
    totalSyncEvents = 0;

    while (1) {
        if (!lfs) {
            state = UNMOUNTED;
        }

        idle = (state == UNMOUNTED);

        if (state != state_prev) {
            state_prev = state;
        }

        switch (state) {
        case UNMOUNTED:
            if (reset_requested) {
                printf("%s: performing reset\n", __FUNCTION__);
                extern lfs_t lfs;
                reset_result = logStore->deleteAllLogs();
                logStore->init(&lfs, sdCard);
                reset_requested = false;
                reset_done = true;
            }
            if (lfs) {
                state = OPEN_LOG;
            } else {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            break;

        case OPEN_LOG:
            if (openNewLog()) {
                first_write_pending = true;
                state = BOOT_SYNC;
            } else {
                state = UNMOUNTED;
            }
            break;

        case BOOT_SYNC:
            // Flush the boot header data (log version + WP reset reason) as soon as there is one
            // sector of it, so it survives if the WP crashes again before a full FLUSH_THRESHOLD
            // has accumulated.  This state only waits: WRITE_DATA decides how much to write.
            // That first write is whatever is waiting, any multiple of 512 up to FLUSH_THRESHOLD,
            // so it can be shorter than FLUSH_THRESHOLD.
            {
                int32_t available = inUse();
                if (available >= 512) {
                    state = WRITE_DATA;
                } else {
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
            }
            break;

        case WAIT_FOR_DATA:
            {
                int32_t available = inUse();
                if (available >= FLUSH_THRESHOLD) {
                    state = WRITE_DATA;
                } else {
                    vTaskDelay(pdMS_TO_TICKS(250));
                }
            }
            break;

        case WRITE_DATA:
            {
                int32_t available = inUse();
                int32_t totalToWrite = available & ~511;    // round down to sector boundary
                if (totalToWrite > FLUSH_THRESHOLD)
                    totalToWrite = FLUSH_THRESHOLD;
                if (totalToWrite < 512) {
                    state = WAIT_FOR_DATA;
                    break;
                }

                bool err = false;

                // Write directly from the ring buffer: no copy needed.
                // A write must never run past the end of the ring, so it stops there and the next
                // write starts at the beginning of the ring.  The tail is NOT always on a
                // FLUSH_THRESHOLD boundary: the first write of a log (BOOT_SYNC) is whatever was
                // waiting.  Every write is a multiple of 512 and so is the ring size, so the
                // distance to the end of the ring is a multiple of 512 too, as LogStore requires.
                uint8_t* tP = tailP;
                int32_t bytesToEnd = (int32_t)(lastBufferP - tP) + 1;
                if (((tP - buffer) & 511) != 0) {
                    printf("%s: ERROR: ring tail offset %d is not a multiple of 512\n",
                           __FUNCTION__, (int)(tP - buffer));
                }
                if (totalToWrite > bytesToEnd) {
                    printf("%s: write clamped from %d to %d bytes at the end of the ring\n",
                           __FUNCTION__, totalToWrite, bytesToEnd);
                    totalToWrite = bytesToEnd;
                }
                if (first_write_pending) {
                    first_write_pending = false;
                    printf("%s: first write of %s: %d bytes, ring tail offset %d\n",
                           __FUNCTION__, logName, totalToWrite, (int)(tP - buffer));
                }

                int32_t bytesWritten = writeChunk(tP, totalToWrite);
                if (bytesWritten < 0) {
                    printf("%s: Write %d bytes failed\n", __FUNCTION__, totalToWrite);
                    state = WRITE_FAILURE;
                    err = true;
                }

                if (!err) {
                    // Advance tail — wraps to start when it reaches the end
                    tP += totalToWrite;
                    if (tP > lastBufferP)
                        tP = buffer;
                    tailP = tP;
                    // Sync metadata to LFS (commits write offset)
                    int32_t sync_err = syncLog();
                    if (sync_err == 0) {
                        totalByteCount += totalToWrite;
                        if (totalByteCount > (1024 * 1024)) {
                            totalByteCount -= (1024 * 1024);
                            printf("logTask: Write %llu/%llu/%llu uS, Sync %llu/%llu/%llu uS\n",
                                   minTimeWriting, maxTimeWriting,
                                   totalTimeWriting / totalWriteEvents,
                                   minTimeSyncing, maxTimeSyncing,
                                   totalTimeSyncing / totalSyncEvents);
                        }
                        if (hwm_pending) {
                            hwm_pending = false;
                            uint8_t hwm = (inUse_max > 65280) ? 255 : (uint8_t)(inUse_max / 256);
                            logData(LOGID_WP_BUF_HWM_TYPE_U8, LOGID_WP_BUF_HWM_DLEN, &hwm);
                            if (hwm_reset_countdown > 0) {
                                hwm_reset_countdown--;
                                inUse_max = 0;
                            }
                        }
                        state = WAIT_FOR_DATA;
                    } else {
                        printf("%s: syncLog() failed\n", __FUNCTION__);
                        state = WRITE_FAILURE;
                    }
                }
                break;
            }

        case WRITE_FAILURE:
            logStore->closeActiveLog();
            vTaskDelay(pdMS_TO_TICKS(1000));
            state = OPEN_LOG;
            break;
        }
    }
}