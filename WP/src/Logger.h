#if !defined LOGGER_H
#define LOGGER_H

#include "FreeRTOS.h"
#include "task.h"

#include "lfs.h"
#include "pico/sync.h"
#include "lfsMgr.h"


// Flush threshold: how many bytes to accumulate before writing to SD.
// Must be a multiple of 512 (sector size).
#define FLUSH_THRESHOLD 4096

// Ring buffer for incoming log data. Must be a multiple of FLUSH_THRESHOLD
// so that reads from the tail pointer never wrap past the end of the buffer.
#define LOG_BUFFER_SIZE (80*1024)

static_assert(LOG_BUFFER_SIZE % FLUSH_THRESHOLD == 0,
    "LOG_BUFFER_SIZE must be a multiple of FLUSH_THRESHOLD");
static_assert(FLUSH_THRESHOLD % 512 == 0,
    "FLUSH_THRESHOLD must be a multiple of 512 (sector size)");

class Logger {
    public:
        Logger(uint8_t* buffer, int32_t size);

        bool init(lfs_t* lfs);
        void deinit();

        void logTask();
        void startTask();

        // GPS uses this one
        bool logData(uint8_t logId, uint8_t len, uint8_t* buffer);

        bool logData_fromISR(uint32_t rxWord);

        // Get current log filename (for WiFi uploader to avoid uploading active file)
        const char* getCurrentLogName() const { return logName; }

        int32_t get_log_size() {return bufferLen;}
        int32_t get_inUse_max() {return inUse_max;}

        // I/O timing stats (microseconds)
        uint32_t getWriteMin()   const { return (uint32_t)minTimeWriting; }
        uint32_t getWriteMax()   const { return (uint32_t)maxTimeWriting; }
        uint32_t getWriteCount() const { return totalWriteEvents; }
        uint32_t getWriteAvg()   const { return totalWriteEvents ? (uint32_t)(totalTimeWriting / totalWriteEvents) : 0; }
        uint32_t getSyncMin()    const { return (uint32_t)minTimeSyncing; }
        uint32_t getSyncMax()    const { return (uint32_t)maxTimeSyncing; }
        uint32_t getSyncCount()  const { return totalSyncEvents; }
        uint32_t getSyncAvg()    const { return totalSyncEvents ? (uint32_t)(totalTimeSyncing / totalSyncEvents) : 0; }

    private:
        lfs_t* lfs;
        char logName[16];

        TaskHandle_t log_taskHandle;

        bool openNewLog();

        uint8_t* buffer;
        int32_t bufferLen;
        uint8_t* lastBufferP;           // always points to the last byte in the circular buffer
        volatile uint8_t* headP;        // needs to be volatile since RX32 ISR updates it
        uint8_t* tailP;

        // Hardware spinlock for protecting buffer access from both cores and ISR context
        spin_lock_t* bufferLock;

        int32_t inUse() {
            int32_t used = headP - tailP;
            if (used < 0) used += bufferLen;
            return used;
        }
        int32_t writeChunk(uint8_t* buffer, int32_t len);
        int32_t syncLog();

        uint64_t totalTimeWriting, minTimeWriting, maxTimeWriting;
        uint32_t totalWriteEvents;
        uint64_t totalTimeSyncing, minTimeSyncing, maxTimeSyncing;
        uint32_t totalSyncEvents;

        uint32_t inUse_max;
};

#endif

extern Logger* logger;
