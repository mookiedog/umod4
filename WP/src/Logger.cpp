#include "pico/stdlib.h"
#include "pico/sync.h"
#include "Logger.h"
#include "NeoPixelConnect.h"
#include "SdCard.h"
#include "umod4_WP.h"
#include "WP_log.h"

#include "uart_tx.pio.h"
const uint32_t dbg = 1;
extern void pico_set_led(bool on);

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


#if 1
// ----------------------------------------------------------------------------------
bool Logger::openNewLog()
{
const char* fname = "next_logId";
uint16_t id;
lfs_file_t fp;
lfs_dir_t dir;
struct lfs_info info;
int32_t lfs_err;
int32_t len;
const char* path="/";
const char* prefix = "log.";
uint32_t prefixLen = strlen(prefix);

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
          if (strncmp(prefix, info.name, prefixLen) == 0) {
            // The prefix matched, now make sure that all remaining chars in the filename are decimal digits
            char* p = &info.name[prefixLen];
            uint32_t digitCount = 0;
            uint32_t value = 0;
            while ((*p>='0') && (*p<='9')) {
              digitCount++;
              value = (value*10) + (*p-'0');
              p++;
            }
            if ((digitCount >= 1) && (digitCount<=5) && (*p==0)) {
              // We found a valid logfile: a prefix followed by a numeric specifier between 1 and 5 digits long
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

    snprintf(logName, sizeof(logName), "log.%d", maxValue+1);
    printf("%s: Creating logfile with temporary name \"%s\"\n", __FUNCTION__, logName);
    lfs_err = lfs_file_open(lfs, &logf, logName, LFS_O_CREAT | LFS_O_TRUNC | LFS_O_RDWR);
    if (lfs_err != LFS_ERR_OK) {
      printf("%sw: Unable to open new logfile\"%s\": err=%d\n", __FUNCTION__, logName, lfs_err);
    }
  }

  return (lfs_err == LFS_ERR_OK);
}
#else
// ----------------------------------------------------------------------------------
bool Logger::openNewLog()
{
  const char* fname = "next_logId";
  uint16_t id;
  lfs_file_t fp;
  int32_t err;

  // Open the file containing the next id as RW, creating it if it does not exist, and erasing it if it already exists
  err = lfs_file_open(lfs, &fp, fname, LFS_O_CREAT | LFS_O_RDWR);
  if (err != LFS_ERR_OK) {
      printf("%s: opening \"%s\" returned err %d\n", __FUNCTION__, fname, err);
      return err;
  }

  // Read the short int contained in the file. It's value will be used to create a temp filename.
  int32_t bytesRead = lfs_file_read(lfs, &fp, &id, sizeof(id));
  if (dbg>1) printf("%s: Attempting to read %d bytes from \"%s\" actually read %d\n", __FUNCTION__, sizeof(id), fname, bytesRead);
  if (bytesRead<sizeof(id)) {
      printf("%s: ID file is missing or corrupted: Creating a new one\n", __FUNCTION__);
      err = lfs_file_rewind(lfs, &fp);
      if (err != LFS_ERR_OK) {
          printf("%s: Failure rewinding \"%s\" to recreate id file, returned %d\n", __FUNCTION__, fname, err);
          return err;
      }

      id = 0;
  }

  // Increment the ID for the next time we create a log

  #if 1
      id++;
  #else
    #warning "Logfile name incrementing is disabled!!"
  #endif
  err = lfs_file_rewind(lfs, &fp);
  if (err != LFS_ERR_OK) {
      printf("%s: unable to rewind ID file \"%s\": err=%d", __FUNCTION__, fname, err);
      return err;
  }

  int32_t bytesWritten = lfs_file_write(lfs, &fp, &id, sizeof(id));
  err = lfs_file_close(lfs, &fp);

  if (bytesWritten != sizeof(id)) {
      printf("%s: Unable to write new id data %d to file \"%s\": err=%d", __FUNCTION__, id, fname, bytesWritten);
      return bytesWritten;
  }

  // If we get here, id contains the numeric value we will use in our temp filename.
  // We create the file if needed, and erase its contents if it already existed.
  tempName = true;
  snprintf(logName, sizeof(logName), "T%05hu", id);
  printf("%s: Creating logfile with temporary name \"%s\"\n", __FUNCTION__, logName);
  err = lfs_file_open(lfs, &logf, logName, LFS_O_CREAT | LFS_O_TRUNC | LFS_O_RDWR);
  if (err != LFS_ERR_OK) {
      printf("%sw: Unable to open new logfile\"%s\": err=%d\n", __FUNCTION__, logName, err);
      return err;
  }

  return (err == LFS_ERR_OK);
}
#endif

// ----------------------------------------------------------------------------------
bool __time_critical_func(Logger::logData)(uint8_t logId, uint8_t data, bool fromISR)
{
  bool rVal = true;
  UBaseType_t interruptStatus;

  #if 0
  // Print out what we are being asked to log
  printf("%s: %02X %02X\n", __FUNCTION__, logId, data);
  #endif

  if (fromISR) {
    interruptStatus = taskENTER_CRITICAL_FROM_ISR();
  }
  else {
    taskENTER_CRITICAL();
  }

  //check if there is enough room left in the log buffer for both bytes
  int32_t spaceRemaining = bufferLen - inUse();
  if (spaceRemaining >= 2) {
    *headP++ = logId;
    if (headP > lastBufferP) {
      headP = buffer;
    }
    *headP++ = data;
    if (headP > lastBufferP) {
      headP = buffer;
    }
  }
  else {
    rVal = false;
  }

  if (fromISR) {
    taskEXIT_CRITICAL_FROM_ISR(interruptStatus);  
  }
  else {
    taskEXIT_CRITICAL();
  }

  return rVal;
}

#if 0
// ----------------------------------------------------------------------------------
bool __time_critical_func(Logger::logData)(uint8_t logId, uint16_t data)
{
  bool rVal = true;

  #if 1
  // Print out what we are being asked to log
  printf("%s: %02X %02X %02X\n", __FUNCTION__, logId, data & 0xFF, (data>>8)&0xFF);
  #endif

  taskENTER_CRITICAL();

  //check if there is enough room left in the log buffer for both bytes
  int32_t spaceRemaining = bufferLen - inUse();
  if (spaceRemaining >= 3) {
    *headP++ = logId;
    if (headP > lastBufferP) {
      headP = buffer;
    }
    *headP++ = data & 0xFF;
    if (headP > lastBufferP) {
      headP = buffer;
    }
    *headP++ = (data>>8) & 0xFF;
    if (headP > lastBufferP) {
      headP = buffer;
    }
  }
  taskEXIT_CRITICAL();
  return rVal;
}
#endif


// ----------------------------------------------------------------------------------
bool __time_critical_func(Logger::logData)(uint8_t logId, int8_t len, uint8_t* data)
{
  bool rVal = true;

  #if 0
  // Print out what we are being asked to log
  printf("%s: %02X ", __FUNCTION__, logId);
  uint8_t* p = data;
  int8_t l = len;
  while (l-- > 0) {
    printf("%02X ", *p++);
  }
  printf("[%d bytes]\n", len+1);
  #endif

  taskENTER_CRITICAL();

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
    rVal = false;
  }

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
  printf("%s: %d: Write time: %lld uSec\n", __FUNCTION__, writeCount, elapsed);

  totalWriteEvents += 1;
  totalTimeWriting += elapsed;
  if (elapsed < minTimeWriting) {
    minTimeWriting = elapsed;
  }
  if (elapsed > maxTimeWriting) {
    maxTimeWriting = elapsed;
    if (maxTimeWriting > 0) {
      printf("%s: New max write time: %lld uSec\n", __FUNCTION__, maxTimeWriting);
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

  totalSyncEvents += 1;
  totalTimeSyncing += elapsed;
  if (elapsed < minTimeSyncing) {
    minTimeSyncing = elapsed;
  }
  if (elapsed > maxTimeSyncing) {
    maxTimeSyncing = elapsed;
    if (maxTimeSyncing > 0) {
      printf("%s: New max sync time: %lld uSec\n", __FUNCTION__, maxTimeSyncing);
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

  #if defined DBG_UART_TX_PIN
  uart_tx_program_puts(PIO_UART, PIO_UART_SM, decodeState[state]);
  uart_tx_program_puts(PIO_UART, PIO_UART_SM, "\r\n");
  #endif
  while (1) {
    if (!lfs) {
      // There are no files to close or anything like that because the filesystem disappeared on us and is already gone
      state = UNMOUNTED;
    }

    if (state != state_prev) {
      #if defined DBG_UART_TX_PIN
        // printf("%s: Moving from state %s to state %s\n", __FUNCTION__, decodeState[state_prev], decodeState[state]);
        //uart_tx_program_puts(pio1, 0, __FUNCTION__": ");
        uart_tx_program_puts(pio1, 0, decodeState[state_prev]);
        uart_tx_program_puts(pio1, 0, "->");
        uart_tx_program_puts(pio1, 0, decodeState[state]);
        uart_tx_program_puts(pio1, 0, "\r\n");
      #endif
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
          printf("%s: %d bytes in buffer\n", __FUNCTION__, logLength);
        #endif
        if (logLength >= bytesToWriteBeforeSyncing) {
          state = WRITE_DATA;
        }
        else {
          vTaskDelay(pdMS_TO_TICKS(1000));
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
          tailP = tP;
          // Now that the write[s] are done, we sync to commit them to flash
          syncLog();

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