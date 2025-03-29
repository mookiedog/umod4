#if !defined LOGGER_H
#define LOGGER_H

#include "FreeRTOS.h"
//#include "queue.h"
#include "task.h"

#include "lfs.h"

class Logger {
  public:
    Logger();

    void init(lfs_t* lfs);
    void deinit();

    void logTask();
    void getDiskInfo();

  private:
    lfs_t* lfs;
    char tempName[16];
    lfs_file_t logf;

    struct lfs_fsinfo fsinfo;

    TaskHandle_t log_taskHandle;

    bool openNewLog();

    // Once we know the date and time, the log file can be renamed from the temp name
    // it was created with to a new name that contains a timestamp.
    bool renameLog(const char* newName);
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

