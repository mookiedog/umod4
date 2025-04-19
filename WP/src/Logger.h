#if !defined LOGGER_H
#define LOGGER_H

#include "FreeRTOS.h"
//#include "queue.h"
#include "task.h"

#include "lfs.h"

// The log buffer needs to be able to absorb incoming data while data in the
// buffer is being written to the file system. The LittleFS file system can
// be extremely slow under certain circumstances.
#define LOG_BUFFER_SIZE 32768

class Logger {
  public:
    Logger(int32_t size);

    void init(lfs_t* lfs);
    void deinit();

    void logTask();
    void getDiskInfo();
    void startTask();

    bool logData(uint8_t logId, int8_t len, uint8_t* buffer);
    bool logData(uint8_t logId, uint8_t data);
    bool logData(uint8_t logId, uint16_t data);

  private:
    lfs_t* lfs;
    char logName[16];
    bool tempName;
    lfs_file_t logf;
    int32_t logSize;

    struct lfs_fsinfo fsinfo;

    TaskHandle_t log_taskHandle;

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


#if 0

  // We will use SPARE1 as a development aid.
  // If shorted to ground, it will not perform any logging,
  // mainly to save wear and tear on the SD card during development.
  gpio_init(SPARE1_PIN);
  gpio_set_dir(SPARE1_PIN, GPIO_IN);
  // Enable pullup. If the pin is not grounded, it will read 'high'.
  gpio_set_pulls(SPARE1_PIN, true, false);
  vTaskDelay(1);
  uint32_t pinState = gpio_get(SPARE1);
#endif

extern Logger* logger;
