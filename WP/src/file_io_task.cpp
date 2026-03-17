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
#include "pico/bootrom.h"
#include "pico/time.h"  // For timing functions
// Note: WP reflash is handled by ota_flash_task, not here
#include "boot/picoboot_constants.h"
#include "lfsMgr.h"
#include <stdio.h>
#include <string.h>

// Queue for I/O requests
static QueueHandle_t io_queue = NULL;
static TaskHandle_t io_task_handle = NULL;

// Result tracking
static file_io_result_t last_result = {};
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
// Experiments show that completely skipping the LFS write operation below increases the upload
// data rate from ~200K bytes/sec to ~230K bytes/sec.
// This suggests that WiFi is the bottleneck, not LFS or SD card operations.
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

    uint32_t write_start = to_ms_since_boot(get_absolute_time());

    lfs_ssize_t written = lfs_file_write(&lfs, &upload_file,
                                          req->write_op.data, req->write_op.length);
    uint32_t write_duration = to_ms_since_boot(get_absolute_time()) - write_start;

    if (written >= 0) {
        result->success = true;
        result->error_code = 0;
        result->error_message[0] = '\0';
        result->write_result.bytes_written = (uint32_t)written;

        // Only log slow writes (>50ms) or failures
        if (write_duration > 50) {
            printf("[FileIO] Slow write: %lu ms for %lu bytes (%.1f KB/s)\n",
                   (unsigned long)write_duration,
                   (unsigned long)written,
                   write_duration > 0 ? (written / 1024.0) / (write_duration / 1000.0) : 0.0);
        }
    } else {
        set_error(result, (int)written, lfs_error_string((int)written));
        result->write_result.bytes_written = 0;
        printf("[FileIO] Write FAILED after %lu ms: %s\n",
               (unsigned long)write_duration, result->error_message);
    }
}

// Process UPLOAD_CLOSE operation
static void process_upload_close(const file_io_request_t* req, file_io_result_t* result)
{
    printf("\nFileIO: UPLOAD_CLOSE (sync=%d)\n", req->close_op.sync);

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
        case  0:  return "Success";
        case -1:  return "Unable to connect to EP via SWD";
        case -2:  return "Unable to clear FBI struct in EP RAM";
        case -3:  return "Unable to load flasher program to EP RAM";
        case -4:  return "Unable to start flasher program on EP";
        case -5:  return "Unable to read flashBufferInterface from EP";
        case -6:  return "Timeout waiting for flasher program to start";
        case -10: return "Malformed UF2 block";
        case -11: return "Metablock flash failed";
        case -12: return "Final metablock flash failed";
        default:  return "UF2 processing/flashing error";
    }
}

// Process REFLASH_EP operation
static void process_reflash_ep(const file_io_request_t* req, file_io_result_t* result)
{
    printf("FileIO: REFLASH_EP '%s' (verbose=%d)\n",
           req->reflash_ep_op.path, req->reflash_ep_op.verbose);

    if (!lfs_mounted) {
        set_error(result, -1, "Filesystem not mounted");
        result->reflash_ep_result.flash_result = -1;
        return;
    }

    // Check if file exists
    struct lfs_info info;
    int err = lfs_stat(&lfs, req->reflash_ep_op.path, &info);
    if (err != 0) {
        char msg[64];
        snprintf(msg, sizeof(msg), "File not found: %s", req->reflash_ep_op.path);
        set_error(result, err, msg);
        result->reflash_ep_result.flash_result = err;
        return;
    }

    printf("FileIO: Starting EP reflash with '%s' (%lu bytes)\n",
           req->reflash_ep_op.path, (unsigned long)info.size);

    // Call flash_ep_uf2() - this is synchronous and takes 10-30 seconds
    int32_t flash_result = flash_ep_uf2(req->reflash_ep_op.path, req->reflash_ep_op.verbose);

    result->reflash_ep_result.flash_result = flash_result;

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

// Process WRITE_ECU_LIVE_CONFIG operation
static void process_write_ecu_live_config(const file_io_request_t* req, file_io_result_t* result)
{
    if (!lfs_mounted) {
        set_error(result, -1, "Filesystem not mounted");
        return;
    }

    // Build JSON content
    char json[128];
    int len = snprintf(json, sizeof(json), "{\"items\":[");
    for (int i = 0; i < 10; i++) {
        if (i > 0) len += snprintf(json + len, sizeof(json) - len, ",");
        len += snprintf(json + len, sizeof(json) - len, "%d",
                        (int)req->ecu_live_config_op.items[i]);
    }
    snprintf(json + len, sizeof(json) - len, "]}");

    lfs_file_t file;
    int err = lfs_file_open(&lfs, &file, "/ecu_live.json",
                            LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if (err != 0) {
        set_error(result, err, lfs_error_string(err));
        printf("FileIO: WRITE_ECU_LIVE_CONFIG open failed: %s\n", result->error_message);
        return;
    }

    size_t json_len = strlen(json);
    lfs_ssize_t written = lfs_file_write(&lfs, &file, json, json_len);
    lfs_file_close(&lfs, &file);

    if (written == (lfs_ssize_t)json_len) {
        result->success = true;
        result->error_code = 0;
        result->error_message[0] = '\0';
        printf("FileIO: Wrote /ecu_live.json (%zu bytes)\n", json_len);
    } else {
        set_error(result, (int)written, "Write incomplete");
        printf("FileIO: WRITE_ECU_LIVE_CONFIG write failed\n");
    }
}

// Note: WP self-reflash (process_reflash_wp) has been moved to ota_flash_task
// which provides proper subsystem shutdown and upgrade logging.

// Process FLASH_EP_SLOT_FROM_FILE operation
static void process_flash_ep_slot_from_file(const file_io_request_t* req, file_io_result_t* result)
{
    const auto& op = req->flash_ep_slot_from_file_op;
    printf("FileIO: FLASH_EP_SLOT_FROM_FILE '%s' -> 0x%08X\n",
           op.lfs_path, op.target_flash_addr);

    int32_t flash_result = FlashEp::flashSlotFromFile(&lfs, op.lfs_path,
                                                       op.name, op.description,
                                                       op.protection,
                                                       op.target_flash_addr);
    result->flash_ep_slot_result.flash_result = flash_result;
    if (flash_result == 0) {
        result->success = true;
        result->error_code = 0;
        result->error_message[0] = '\0';
        printf("FileIO: FLASH_EP_SLOT_FROM_FILE success\n");
    } else {
        char msg[64];
        snprintf(msg, sizeof(msg), "flashSlotFromFile() returned %ld", (long)flash_result);
        set_error(result, (int)flash_result, msg);
    }
}

// Process REWRITE_EP_SLOT_HEADER operation
static void process_rewrite_ep_slot_header(const file_io_request_t* req, file_io_result_t* result)
{
    const auto& op = req->rewrite_ep_slot_header_op;
    printf("FileIO: REWRITE_EP_SLOT_HEADER slot=0x%08X name='%s'\n",
           op.slot_flash_addr, op.name);

    int32_t flash_result = FlashEp::rewriteSlotHeader(op.slot_flash_addr,
                                                       op.name, op.description,
                                                       op.protection);
    result->flash_ep_slot_result.flash_result = flash_result;
    if (flash_result == 0) {
        result->success = true;
        result->error_code = 0;
        result->error_message[0] = '\0';
        printf("FileIO: REWRITE_EP_SLOT_HEADER success\n");
    } else {
        char msg[64];
        snprintf(msg, sizeof(msg), "rewriteSlotHeader() returned %ld", (long)flash_result);
        set_error(result, (int)flash_result, msg);
    }
}

// Process FLASH_EP_SLOT operation
static void process_flash_ep_slot(const file_io_request_t* req, file_io_result_t* result)
{
    const auto& op = req->flash_ep_slot_op;
    printf("FileIO: FLASH_EP_SLOT addr=0x%08X erase=%d bson_hdr_size=%lu\n",
           op.target_flash_addr, (int)op.erase, (unsigned long)op.bson_hdr_size);

    int32_t flash_result = FlashEp::flashSlot(op.bson_hdr, op.bson_hdr_size,
                                              op.image_data, op.target_flash_addr,
                                              op.erase);
    result->flash_ep_slot_result.flash_result = flash_result;
    if (flash_result == 0) {
        result->success = true;
        result->error_code = 0;
        result->error_message[0] = '\0';
        printf("FileIO: FLASH_EP_SLOT success\n");
    } else {
        char msg[64];
        snprintf(msg, sizeof(msg), "flashSlot() returned %ld", (long)flash_result);
        set_error(result, (int)flash_result, msg);
    }
}

// Main task function
static void file_io_task(void* params)
{
    file_io_request_t request;

    printf("FileIO: Task started\n");

    while (1) {
        // Wait for request
        uint32_t queue_wait_start = to_ms_since_boot(get_absolute_time());
        if (xQueueReceive(io_queue, &request, portMAX_DELAY) == pdTRUE) {
            uint32_t queue_wait_duration = to_ms_since_boot(get_absolute_time()) - queue_wait_start;

            // Always log significant delays (>1s indicates task starvation)
            if (queue_wait_duration > 1000) {
                printf("[FileIO] Task was idle for %lu ms before receiving request\n",
                       (unsigned long)queue_wait_duration);
            }

            file_io_result_t result = {};

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
                case FILE_IO_OP_WRITE_ECU_LIVE_CONFIG:
                    process_write_ecu_live_config(&request, &result);
                    break;
                case FILE_IO_OP_FLASH_EP_SLOT:
                    process_flash_ep_slot(&request, &result);
                    break;
                case FILE_IO_OP_FLASH_EP_SLOT_FROM_FILE:
                    process_flash_ep_slot_from_file(&request, &result);
                    break;
                case FILE_IO_OP_REWRITE_EP_SLOT_HEADER:
                    process_rewrite_ep_slot_header(&request, &result);
                    break;
                // Note: FILE_IO_OP_REFLASH_WP removed - handled by ota_flash_task
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
    // Priority 3 (TASK_HIGH_PRIORITY), below CYW43 (priority 4) but high enough
    // to prevent starvation by lower-priority tasks
    BaseType_t err = xTaskCreate(
        file_io_task,
        "FileIO",
        1024,
        NULL,
        TASK_HIGH_PRIORITY,  // Priority 3, below CYW43 but above normal tasks
        &io_task_handle
    );
    configASSERT(err == pdPASS);

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
    file_io_request_t req = {};
    req.op = FILE_IO_OP_DELETE;
    strncpy(req.path_op.path, path, sizeof(req.path_op.path) - 1);
    return file_io_execute(&req, timeout_ms, result);
}

bool file_io_mkdir(const char* path, uint32_t timeout_ms, file_io_result_t* result)
{
    file_io_request_t req = {};
    req.op = FILE_IO_OP_MKDIR;
    strncpy(req.path_op.path, path, sizeof(req.path_op.path) - 1);
    return file_io_execute(&req, timeout_ms, result);
}

bool file_io_upload_open(const char* path, bool truncate, uint32_t timeout_ms, file_io_result_t* result)
{
    file_io_request_t req = {};
    req.op = FILE_IO_OP_UPLOAD_OPEN;
    strncpy(req.open_op.path, path, sizeof(req.open_op.path) - 1);
    req.open_op.truncate = truncate;
    return file_io_execute(&req, timeout_ms, result);
}

bool file_io_upload_write(const uint8_t* data, uint32_t length, uint32_t timeout_ms, file_io_result_t* result)
{
    file_io_request_t req = {};
    req.op = FILE_IO_OP_UPLOAD_WRITE;
    req.write_op.data = (uint8_t*)data;  // Cast away const - task won't modify
    req.write_op.length = length;
    return file_io_execute(&req, timeout_ms, result);
}

bool file_io_upload_close(bool sync, uint32_t timeout_ms, file_io_result_t* result)
{
    file_io_request_t req = {};
    req.op = FILE_IO_OP_UPLOAD_CLOSE;
    req.close_op.sync = sync;
    return file_io_execute(&req, timeout_ms, result);
}

// Async upload write functions for ping-pong buffer pattern

bool file_io_upload_write_async(const uint8_t* data, uint32_t length, uint32_t timeout_ms)
{
    if (!data || length == 0 || length > FILE_IO_MAX_CHUNK_SIZE) {
        return false;
    }

    // Clear any previous result semaphore (should already be cleared, but be safe)
    xSemaphoreTake(result_ready_sem, 0);

    // Build request and send to queue without waiting for completion
    file_io_request_t req = {};
    req.op = FILE_IO_OP_UPLOAD_WRITE;
    req.write_op.data = (uint8_t*)data;  // Caller must keep data valid until write completes!
    req.write_op.length = length;

    if (xQueueSend(io_queue, &req, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        printf("[FileIO] Queue send TIMEOUT\n");
        return false;  // Queue send timeout
    }

    return true;  // Request submitted, operation will complete asynchronously
}

bool file_io_upload_write_is_complete(void)
{
    // Check if result semaphore is available (operation completed)
    // Use zero timeout to poll without blocking
    if (xSemaphoreTake(result_ready_sem, 0) == pdTRUE) {
        // Operation completed, but we just consumed the semaphore
        // Give it back so file_io_upload_write_wait can retrieve the result
        xSemaphoreGive(result_ready_sem);
        return true;
    }
    return false;  // Still in progress
}

bool file_io_upload_write_wait(uint32_t timeout_ms, file_io_result_t* result)
{
    if (!result) {
        return false;
    }

    // Wait for operation to complete
    if (xSemaphoreTake(result_ready_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        printf("[FileIO] Semaphore wait TIMEOUT\n");
        return false;  // Timeout waiting for completion
    }

    // Copy result
    *result = last_result;

    return true;  // Operation completed (check result.success for actual outcome)
}

bool file_io_reflash_ep(const char* path, bool verbose, uint32_t timeout_ms, file_io_result_t* result)
{
    file_io_request_t req = {};
    req.op = FILE_IO_OP_REFLASH_EP;
    strncpy(req.reflash_ep_op.path, path, sizeof(req.reflash_ep_op.path) - 1);
    req.reflash_ep_op.verbose = verbose;
    return file_io_execute(&req, timeout_ms, result);
}

bool file_io_write_ecu_live_config(const int16_t items[10], uint32_t timeout_ms, file_io_result_t* result)
{
    file_io_request_t req = {};
    req.op = FILE_IO_OP_WRITE_ECU_LIVE_CONFIG;
    memcpy(req.ecu_live_config_op.items, items, 10 * sizeof(int16_t));
    return file_io_execute(&req, timeout_ms, result);
}

bool file_io_flash_ep_slot(const uint8_t* bson_hdr, uint32_t bson_hdr_size,
                           const uint8_t* image_data, uint32_t target_flash_addr,
                           uint32_t timeout_ms, file_io_result_t* result)
{
    file_io_request_t req = {};
    req.op = FILE_IO_OP_FLASH_EP_SLOT;
    req.flash_ep_slot_op.bson_hdr = bson_hdr;
    req.flash_ep_slot_op.bson_hdr_size = bson_hdr_size;
    req.flash_ep_slot_op.image_data = image_data;
    req.flash_ep_slot_op.target_flash_addr = target_flash_addr;
    req.flash_ep_slot_op.erase = false;
    return file_io_execute(&req, timeout_ms, result);
}

bool file_io_erase_ep_slot(uint32_t target_flash_addr,
                           uint32_t timeout_ms, file_io_result_t* result)
{
    file_io_request_t req = {};
    req.op = FILE_IO_OP_FLASH_EP_SLOT;
    req.flash_ep_slot_op.bson_hdr = nullptr;
    req.flash_ep_slot_op.bson_hdr_size = 0;
    req.flash_ep_slot_op.image_data = nullptr;
    req.flash_ep_slot_op.target_flash_addr = target_flash_addr;
    req.flash_ep_slot_op.erase = true;
    return file_io_execute(&req, timeout_ms, result);
}

bool file_io_flash_ep_slot_from_file(const char* name, const char* description,
                                     const char* protection, const char* lfs_path,
                                     uint32_t target_flash_addr,
                                     uint32_t timeout_ms, file_io_result_t* result)
{
    file_io_request_t req = {};
    req.op = FILE_IO_OP_FLASH_EP_SLOT_FROM_FILE;
    strncpy(req.flash_ep_slot_from_file_op.name, name,
            sizeof(req.flash_ep_slot_from_file_op.name) - 1);
    strncpy(req.flash_ep_slot_from_file_op.description, description,
            sizeof(req.flash_ep_slot_from_file_op.description) - 1);
    strncpy(req.flash_ep_slot_from_file_op.protection, protection,
            sizeof(req.flash_ep_slot_from_file_op.protection) - 1);
    strncpy(req.flash_ep_slot_from_file_op.lfs_path, lfs_path,
            sizeof(req.flash_ep_slot_from_file_op.lfs_path) - 1);
    req.flash_ep_slot_from_file_op.target_flash_addr = target_flash_addr;
    return file_io_execute(&req, timeout_ms, result);
}

bool file_io_rewrite_ep_slot_header(const char* name, const char* description,
                                    const char* protection, uint32_t slot_flash_addr,
                                    uint32_t timeout_ms, file_io_result_t* result)
{
    file_io_request_t req = {};
    req.op = FILE_IO_OP_REWRITE_EP_SLOT_HEADER;
    strncpy(req.rewrite_ep_slot_header_op.name, name,
            sizeof(req.rewrite_ep_slot_header_op.name) - 1);
    strncpy(req.rewrite_ep_slot_header_op.description, description,
            sizeof(req.rewrite_ep_slot_header_op.description) - 1);
    strncpy(req.rewrite_ep_slot_header_op.protection, protection,
            sizeof(req.rewrite_ep_slot_header_op.protection) - 1);
    req.rewrite_ep_slot_header_op.slot_flash_addr = slot_flash_addr;
    return file_io_execute(&req, timeout_ms, result);
}

// Note: file_io_reflash_wp() removed - WP reflash is now handled by ota_flash_task

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
