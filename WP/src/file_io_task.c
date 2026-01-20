/**
 * Asynchronous file I/O task.
 *
 * This task handles file operations from the HTTP API.
 * File operations must happen in a task context (not HTTP callback context)
 * to safely acquire the LittleFS mutex and perform SD card operations.
 */

#include "file_io_task.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "umod4_WP.h"  // For TASK_NORMAL_PRIORITY
#include "FlashEp.h"   // For flash_ep_uf2()
#include <stdio.h>
#include <string.h>

extern lfs_t lfs;
extern bool lfs_mounted;

// Queue for I/O requests
static QueueHandle_t io_queue = NULL;
static TaskHandle_t io_task_handle = NULL;

// Result tracking
static file_io_result_t last_result = {0};
static SemaphoreHandle_t result_ready_sem = NULL;

// Currently open upload file (only one at a time)
static lfs_file_t upload_file;
static bool upload_file_open = false;

// Helper to set error result
static void set_error(file_io_result_t* result, int err, const char* msg)
{
    result->success = false;
    result->error_code = err;
    strncpy(result->error_message, msg, sizeof(result->error_message) - 1);
    result->error_message[sizeof(result->error_message) - 1] = '\0';
}

// Helper to get LFS error string
static const char* lfs_error_string(int err)
{
    switch (err) {
        case LFS_ERR_OK:        return "OK";
        case LFS_ERR_IO:        return "I/O error";
        case LFS_ERR_CORRUPT:   return "Filesystem corruption";
        case LFS_ERR_NOENT:     return "No such file or directory";
        case LFS_ERR_EXIST:     return "File exists";
        case LFS_ERR_NOTDIR:    return "Not a directory";
        case LFS_ERR_ISDIR:     return "Is a directory";
        case LFS_ERR_NOTEMPTY:  return "Directory not empty";
        case LFS_ERR_BADF:      return "Bad file descriptor";
        case LFS_ERR_FBIG:      return "File too large";
        case LFS_ERR_INVAL:     return "Invalid parameter";
        case LFS_ERR_NOSPC:     return "No space left";
        case LFS_ERR_NOMEM:     return "Out of memory";
        case LFS_ERR_NOATTR:    return "No attribute";
        case LFS_ERR_NAMETOOLONG: return "Name too long";
        default:                return "Unknown error";
    }
}

// Process DELETE operation
static void process_delete(const file_io_request_t* req, file_io_result_t* result)
{
    printf("FileIO: DELETE '%s'\n", req->path_op.path);

    if (!lfs_mounted) {
        set_error(result, -1, "Filesystem not mounted");
        return;
    }

    // Check if file exists
    struct lfs_info info;
    int err = lfs_stat(&lfs, req->path_op.path, &info);
    if (err != 0) {
        set_error(result, err, "File not found");
        return;
    }

    if (info.type != LFS_TYPE_REG) {
        set_error(result, LFS_ERR_ISDIR, "Not a regular file");
        return;
    }

    // Delete the file
    err = lfs_remove(&lfs, req->path_op.path);
    if (err == 0) {
        result->success = true;
        result->error_code = 0;
        result->error_message[0] = '\0';
        printf("FileIO: Deleted '%s'\n", req->path_op.path);
    } else {
        set_error(result, err, lfs_error_string(err));
        printf("FileIO: Delete failed: %s\n", result->error_message);
    }
}

// Process MKDIR operation
static void process_mkdir(const file_io_request_t* req, file_io_result_t* result)
{
    printf("FileIO: MKDIR '%s'\n", req->path_op.path);

    if (!lfs_mounted) {
        set_error(result, -1, "Filesystem not mounted");
        return;
    }

    int err = lfs_mkdir(&lfs, req->path_op.path);
    if (err == 0 || err == LFS_ERR_EXIST) {
        result->success = true;
        result->error_code = 0;
        result->error_message[0] = '\0';
        printf("FileIO: Directory '%s' %s\n", req->path_op.path,
               err == LFS_ERR_EXIST ? "already exists" : "created");
    } else {
        set_error(result, err, lfs_error_string(err));
        printf("FileIO: Mkdir failed: %s\n", result->error_message);
    }
}

// Process UPLOAD_OPEN operation
static void process_upload_open(const file_io_request_t* req, file_io_result_t* result)
{
    printf("FileIO: UPLOAD_OPEN '%s' (truncate=%d)\n",
           req->open_op.path, req->open_op.truncate);

    if (!lfs_mounted) {
        set_error(result, -1, "Filesystem not mounted");
        return;
    }

    // Close any existing open file
    if (upload_file_open) {
        printf("FileIO: Closing previously open upload file\n");
        lfs_file_close(&lfs, &upload_file);
        upload_file_open = false;
    }

    // Open new file
    int flags = LFS_O_WRONLY | LFS_O_CREAT;
    if (req->open_op.truncate) {
        flags |= LFS_O_TRUNC;
    } else {
        flags |= LFS_O_APPEND;
    }

    int err = lfs_file_open(&lfs, &upload_file, req->open_op.path, flags);
    if (err == 0) {
        upload_file_open = true;
        result->success = true;
        result->error_code = 0;
        result->error_message[0] = '\0';
        printf("FileIO: Opened '%s' for upload\n", req->open_op.path);
    } else {
        set_error(result, err, lfs_error_string(err));
        printf("FileIO: Open failed: %s\n", result->error_message);
    }
}

// Process UPLOAD_WRITE operation
static void process_upload_write(const file_io_request_t* req, file_io_result_t* result)
{
    if (!upload_file_open) {
        set_error(result, LFS_ERR_BADF, "No file open for upload");
        return;
    }

    if (!req->write_op.data || req->write_op.length == 0) {
        set_error(result, LFS_ERR_INVAL, "Invalid write parameters");
        return;
    }

    lfs_ssize_t written = lfs_file_write(&lfs, &upload_file,
                                          req->write_op.data, req->write_op.length);
    if (written >= 0) {
        result->success = true;
        result->error_code = 0;
        result->error_message[0] = '\0';
        result->write_result.bytes_written = (uint32_t)written;
    } else {
        set_error(result, (int)written, lfs_error_string((int)written));
        result->write_result.bytes_written = 0;
        printf("FileIO: Write failed: %s\n", result->error_message);
    }
}

// Process UPLOAD_CLOSE operation
static void process_upload_close(const file_io_request_t* req, file_io_result_t* result)
{
    printf("FileIO: UPLOAD_CLOSE (sync=%d)\n", req->close_op.sync);

    if (!upload_file_open) {
        // Not an error - file might already be closed
        result->success = true;
        result->error_code = 0;
        result->error_message[0] = '\0';
        return;
    }

    // Sync if requested
    if (req->close_op.sync) {
        int err = lfs_file_sync(&lfs, &upload_file);
        if (err != 0) {
            printf("FileIO: Sync failed: %s\n", lfs_error_string(err));
            // Continue to close anyway
        }
    }

    // Close file
    int err = lfs_file_close(&lfs, &upload_file);
    upload_file_open = false;

    if (err == 0) {
        result->success = true;
        result->error_code = 0;
        result->error_message[0] = '\0';
        printf("FileIO: Upload file closed\n");
    } else {
        set_error(result, err, lfs_error_string(err));
        printf("FileIO: Close failed: %s\n", result->error_message);
    }
}

// Helper to get FlashEp error string
static const char* flash_ep_error_string(int32_t err)
{
    switch (err) {
        case 0:  return "Success";
        case -1: return "Unable to connect to EP via SWD";
        case -2: return "Unable to clear FBI struct in EP RAM";
        case -3: return "Unable to load flasher program to EP RAM";
        case -4: return "Unable to start flasher program on EP";
        case -5: return "Unable to read flashBufferInterface from EP";
        case -6: return "Timeout waiting for flasher program to start";
        default: return "UF2 processing/flashing error";
    }
}

// Process REFLASH_EP operation
static void process_reflash_ep(const file_io_request_t* req, file_io_result_t* result)
{
    printf("FileIO: REFLASH_EP '%s' (verbose=%d)\n",
           req->reflash_ep_op.path, req->reflash_ep_op.verbose);

    if (!lfs_mounted) {
        set_error(result, -1, "Filesystem not mounted");
        result->reflash_result.flash_result = -1;
        return;
    }

    // Check if file exists
    struct lfs_info info;
    int err = lfs_stat(&lfs, req->reflash_ep_op.path, &info);
    if (err != 0) {
        char msg[64];
        snprintf(msg, sizeof(msg), "File not found: %s", req->reflash_ep_op.path);
        set_error(result, err, msg);
        result->reflash_result.flash_result = err;
        return;
    }

    printf("FileIO: Starting EP reflash with '%s' (%lu bytes)\n",
           req->reflash_ep_op.path, (unsigned long)info.size);

    // Call flash_ep_uf2() - this is synchronous and takes 10-30 seconds
    int32_t flash_result = flash_ep_uf2(req->reflash_ep_op.path, req->reflash_ep_op.verbose);

    result->reflash_result.flash_result = flash_result;

    if (flash_result == 0) {
        result->success = true;
        result->error_code = 0;
        result->error_message[0] = '\0';
        printf("FileIO: EP reflash completed successfully\n");
    } else {
        result->success = false;
        result->error_code = flash_result;
        snprintf(result->error_message, sizeof(result->error_message),
                 "%s (code: %ld)", flash_ep_error_string(flash_result), (long)flash_result);
        printf("FileIO: EP reflash failed: %s\n", result->error_message);
    }
}

// Main task function
static void file_io_task(void* params)
{
    file_io_request_t request;

    printf("FileIO: Task started\n");

    while (1) {
        // Wait for request
        if (xQueueReceive(io_queue, &request, portMAX_DELAY) == pdTRUE) {
            file_io_result_t result = {0};

            switch (request.op) {
                case FILE_IO_OP_DELETE:
                    process_delete(&request, &result);
                    break;
                case FILE_IO_OP_MKDIR:
                    process_mkdir(&request, &result);
                    break;
                case FILE_IO_OP_UPLOAD_OPEN:
                    process_upload_open(&request, &result);
                    break;
                case FILE_IO_OP_UPLOAD_WRITE:
                    process_upload_write(&request, &result);
                    break;
                case FILE_IO_OP_UPLOAD_CLOSE:
                    process_upload_close(&request, &result);
                    break;
                case FILE_IO_OP_REFLASH_EP:
                    process_reflash_ep(&request, &result);
                    break;
                default:
                    set_error(&result, -1, "Unknown operation");
                    break;
            }

            // Store result
            last_result = result;

            // Signal that result is ready
            xSemaphoreGive(result_ready_sem);
        }
    }
}

void file_io_task_init(void)
{
    // Create queue (holds 1 request at a time - operations are serialized)
    io_queue = xQueueCreate(1, sizeof(file_io_request_t));
    configASSERT(io_queue != NULL);

    // Create result semaphore
    result_ready_sem = xSemaphoreCreateBinary();
    configASSERT(result_ready_sem != NULL);

    // Create task with larger stack for LittleFS operations
    BaseType_t err = xTaskCreate(
        file_io_task,
        "FileIO",
        4096,  // Stack size in words (larger for LFS operations)
        NULL,
        TASK_NORMAL_PRIORITY,
        &io_task_handle
    );
    configASSERT(err == pdPASS);

    // Pin to core 0 (same core as Logger and HTTP server for safety)
    vTaskCoreAffinitySet(io_task_handle, (1 << 0));

    printf("FileIO: Task initialized\n");
}

bool file_io_execute(const file_io_request_t* request, uint32_t timeout_ms, file_io_result_t* result)
{
    if (!io_queue || !result_ready_sem || !request || !result) {
        return false;
    }

    // Clear any previous result semaphore
    xSemaphoreTake(result_ready_sem, 0);

    // Send request to task
    if (xQueueSend(io_queue, request, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return false;  // Queue full or timeout
    }

    // Wait for result
    if (xSemaphoreTake(result_ready_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return false;  // Timeout waiting for result
    }

    // Copy result
    *result = last_result;
    return true;
}

// Convenience functions

bool file_io_delete(const char* path, uint32_t timeout_ms, file_io_result_t* result)
{
    file_io_request_t req = {0};
    req.op = FILE_IO_OP_DELETE;
    strncpy(req.path_op.path, path, sizeof(req.path_op.path) - 1);
    return file_io_execute(&req, timeout_ms, result);
}

bool file_io_mkdir(const char* path, uint32_t timeout_ms, file_io_result_t* result)
{
    file_io_request_t req = {0};
    req.op = FILE_IO_OP_MKDIR;
    strncpy(req.path_op.path, path, sizeof(req.path_op.path) - 1);
    return file_io_execute(&req, timeout_ms, result);
}

bool file_io_upload_open(const char* path, bool truncate, uint32_t timeout_ms, file_io_result_t* result)
{
    file_io_request_t req = {0};
    req.op = FILE_IO_OP_UPLOAD_OPEN;
    strncpy(req.open_op.path, path, sizeof(req.open_op.path) - 1);
    req.open_op.truncate = truncate;
    return file_io_execute(&req, timeout_ms, result);
}

bool file_io_upload_write(const uint8_t* data, uint32_t length, uint32_t timeout_ms, file_io_result_t* result)
{
    file_io_request_t req = {0};
    req.op = FILE_IO_OP_UPLOAD_WRITE;
    req.write_op.data = (uint8_t*)data;  // Cast away const - task won't modify
    req.write_op.length = length;
    return file_io_execute(&req, timeout_ms, result);
}

bool file_io_upload_close(bool sync, uint32_t timeout_ms, file_io_result_t* result)
{
    file_io_request_t req = {0};
    req.op = FILE_IO_OP_UPLOAD_CLOSE;
    req.close_op.sync = sync;
    return file_io_execute(&req, timeout_ms, result);
}

bool file_io_reflash_ep(const char* path, bool verbose, uint32_t timeout_ms, file_io_result_t* result)
{
    file_io_request_t req = {0};
    req.op = FILE_IO_OP_REFLASH_EP;
    strncpy(req.reflash_ep_op.path, path, sizeof(req.reflash_ep_op.path) - 1);
    req.reflash_ep_op.verbose = verbose;
    return file_io_execute(&req, timeout_ms, result);
}

// Legacy compatibility functions

void file_delete_task_init(void)
{
    // Now handled by file_io_task_init()
    // This is a no-op if called after file_io_task_init()
    if (!io_queue) {
        file_io_task_init();
    }
}

bool file_delete_request_async(const char* filename, uint32_t timeout_ms, file_delete_result_t* result)
{
    if (!result) return false;

    // Build full path
    char path[80];
    snprintf(path, sizeof(path), "/%s", filename);

    file_io_result_t io_result;
    bool ok = file_io_delete(path, timeout_ms, &io_result);

    // Convert to legacy format
    strncpy(result->filename, filename, sizeof(result->filename) - 1);
    result->filename[sizeof(result->filename) - 1] = '\0';
    result->success = ok && io_result.success;
    if (!io_result.success) {
        strncpy(result->error_message, io_result.error_message, sizeof(result->error_message) - 1);
        result->error_message[sizeof(result->error_message) - 1] = '\0';
    } else {
        result->error_message[0] = '\0';
    }

    return ok;
}
