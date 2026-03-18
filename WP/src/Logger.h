#if !defined LOGGER_H
#define LOGGER_H

#include "FreeRTOS.h"
//#include "queue.h"
#include "task.h"

#include "lfs.h"
#include "pico/sync.h"


// The log buffer needs to be able to absorb incoming data while data in the
// buffer is being written to the file system. The LittleFS file system can
// be extremely slow under certain circumstances.
// For the purposes of calculating how many bytes can be buffered before we
// overflow, remember that there is a dedicated 16K write buffer
// *after* this buffer where is extracted before writing to filesystem.
// This buffer does not need to be a power of two.
// TEST: temporarily shrunk from (96*1024) to (56*1024) to free 40KB for TCP buffer test.
// Restore to (96*1024) after test.
#define LOG_BUFFER_SIZE ((56*1024)-16384)

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

    private:
        lfs_t* lfs;
        char logName[16];
        bool tempName;
        lfs_file_t logf;

        struct lfs_fsinfo fsinfo;

        TaskHandle_t log_taskHandle;

        int32_t getDiskInfo(lfs_t* _lfs);

        bool openNewLog();

        // Once we know the date and time, the log file can be renamed from the temp name
        // it was created with to a new name that contains a timestamp.
        bool renameLog(const char* newName);

        uint8_t* buffer;
        int32_t bufferLen;
        uint8_t* lastBufferP;           // always points to the last byte in the circular buffer
        volatile uint8_t* headP;        // needs to be volatile since RX32 ISR updates it
        uint8_t* tailP;

        // FIXME: get this from lfs_config somehow!!
        // This buffer is used to hold data from the circular buffer before calling lfs_write.
        // The issue is if the data to be written as a chunk spans the end of the circular buffer.
        // It is way cheaper to call memcpy twice than to call lfs_write twice
        // so all writes get copied into a buffer where it is guaranteed we can write it with one call.
        uint8_t write_buff[16384];

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
