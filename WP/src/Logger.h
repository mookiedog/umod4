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
#define LOG_BUFFER_SIZE 32768

class Logger {
    public:
        Logger(int32_t size);
        
        bool init(lfs_t* lfs);
        void deinit();
        
        void logTask();
        void startTask();
        
        // GPS uses this one
        bool logData(uint8_t logId, int8_t len, uint8_t* buffer);
        
        // This one is exclusively for the RX32 UART that the ECU uses.
        // It will be called by the RX ISR!
        bool logEcuData(uint32_t rxWord);
    
    private:
        lfs_t* lfs;
        char logName[16];
        bool tempName;
        lfs_file_t logf;
        int32_t logSize;
        
        struct lfs_fsinfo fsinfo;
        
        TaskHandle_t log_taskHandle;
        
        int32_t getDiskInfo(lfs_t* _lfs);
        
        bool openNewLog();
        
        // Once we know the date and time, the log file can be renamed from the temp name
        // it was created with to a new name that contains a timestamp.
        bool renameLog(const char* newName);
        
        uint8_t* buffer;
        int32_t bufferLen;
        uint8_t* lastBufferP;   // points to the last byte in the circular buffer
        uint8_t* headP;
        uint8_t* tailP;
        int32_t inUse();
        int32_t writeChunk(uint8_t* buffer, int32_t len);
        int32_t syncLog();
        
        uint64_t totalTimeWriting, minTimeWriting, maxTimeWriting;
        uint32_t totalWriteEvents;
        uint64_t totalTimeSyncing, minTimeSyncing, maxTimeSyncing;
        uint32_t totalSyncEvents;
};

#endif

extern Logger* logger;
