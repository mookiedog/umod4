#if !defined LOGGER_H
#define LOGGER_H

#include "FreeRTOS.h"
#include "task.h"

#include "lfs.h"
#include "pico/sync.h"
#include "lfsMgr.h"


// Ring buffer for incoming log data. With LogStore, raw sector writes complete
// in under 1ms, so this no longer needs to absorb multi-second LFS stalls.
// Keeping it large for now; can be reduced once LogStore is proven.
#define LOG_BUFFER_SIZE ((96*1024)-LFS_BLOCK_SIZE)

class Logger {
    public:
        Logger(uint8_t* buffer, int32_t size, uint8_t* write_buffer);

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

        // Staging buffer for linearizing data from the circular buffer before
        // writing to SD. Must be at least 512 bytes (one sector).
        uint8_t* write_buff;

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
