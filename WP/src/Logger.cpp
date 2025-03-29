#include "Logger.h"

#include "umod4_WP.h"

const uint32_t dbg = 1;

// ----------------------------------------------------------------------------------
extern "C" void start_logger_logTask(void *pvParameters);

void start_logger_logTask(void *pvParameters)
{
  // The task parameter is the specific Log object we should be using in the ISR
  Logger* logger = static_cast<Logger*>(pvParameters);

  // Start the task method on the correct Log instance
  logger->logTask();
  panic(LOCATION("Should never return!"));
}



Logger::Logger()
{
    deinit();

    //xTaskCreate(start_logger_logTask, "Log", 4096 /* words */, this, TASK_NORMAL_PRIORITY, &log_taskHandle);
}

void Logger::deinit()
{
    lfs = nullptr;
    memset(tempName, 0, sizeof(tempName));
    memset(&logf, 0, sizeof(logf));

    if (dbg) printf("%s: Logging is disabled\n", __FUNCTION__);
}


void Logger::init(lfs_t* _lfs)
{
    lfs = _lfs;
    getDiskInfo();

    if (!openNewLog()) {
        // we failed to open a new log
        if (dbg) printf("%s: failed to open a new log\n", __FUNCTION__);
        deinit();
    }
}

void Logger::getDiskInfo()
{
    int32_t err = lfs_fs_stat(lfs, &fsinfo);
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
}

bool Logger::openNewLog()
{
    const char* fname = "next_id";
    uint16_t id;
    lfs_file_t fp;
    int32_t err;

    // Open the file containing the next id as RW, creating it if it does not exist
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

    // If we get here, id contains the numeric value we will use in our temp filename
    snprintf(tempName, sizeof(tempName), "T%05hu", id);
    if (dbg>1) printf("%s: Creating logfile with temporary name \"%s\"\n", __FUNCTION__, tempName);
    err = lfs_file_open(lfs, &logf, fname, LFS_O_CREAT | LFS_O_TRUNC | LFS_O_RDWR);
    if (err != LFS_ERR_OK) {
        printf("%sw: Unable to open logfile\"%s\": err=%d\n", __FUNCTION__, tempName, err);
        return err;
    }

    // Increment the ID for the next time we create a log
    id++;
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

    printf("%s: Creating logfile with temporary name \"%s\"\n", __FUNCTION__, tempName);
    return (err == LFS_ERR_OK);
}


// Actually, it's not clear I need a task. It might be that the hotplug manager does everything we need.
void Logger::logTask()
{
    while (1) {
        vTaskDelay(100);
    }
}