/**
 * Asynchronous file deletion task.
 *
 * This task handles file deletion requests from the HTTP API.
 * File deletions must happen in a task context (not HTTP callback context)
 * to safely acquire the LittleFS mutex and perform SD card operations.
 */

#include "file_delete_task.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "lfs.h"
#include "umod4_WP.h"  // For TASK_NORMAL_PRIORITY
#include <stdio.h>
#include <string.h>

extern lfs_t lfs;
extern bool lfs_mounted;

// Queue for delete requests
static QueueHandle_t delete_queue = NULL;
static TaskHandle_t delete_task_handle = NULL;

// Result tracking
static file_delete_result_t last_result = {0};
static SemaphoreHandle_t result_ready_sem = NULL;

void file_delete_task(void* params)
{
    file_delete_request_t request;

    while (1) {
        // Wait for delete request
        if (xQueueReceive(delete_queue, &request, portMAX_DELAY) == pdTRUE) {
            printf("DeleteTask: Processing delete request for '%s'\n", request.filename);

            // Perform the deletion
            file_delete_result_t result;
            strncpy(result.filename, request.filename, sizeof(result.filename) - 1);
            result.filename[sizeof(result.filename) - 1] = '\0';

            if (!lfs_mounted) {
                result.success = false;
                strncpy(result.error_message, "Filesystem not mounted", sizeof(result.error_message) - 1);
                printf("DeleteTask: Filesystem not mounted\n");
            } else {
                // Build full path
                char filepath[128];
                snprintf(filepath, sizeof(filepath), "/%s", request.filename);

                // Check if file exists
                struct lfs_info info;
                int err = lfs_stat(&lfs, filepath, &info);
                if (err != 0) {
                    result.success = false;
                    snprintf(result.error_message, sizeof(result.error_message),
                            "File not found (err=%d)", err);
                    printf("DeleteTask: File not found: err=%d\n", err);
                } else if (info.type != LFS_TYPE_REG) {
                    result.success = false;
                    strncpy(result.error_message, "Not a regular file", sizeof(result.error_message) - 1);
                    printf("DeleteTask: Not a regular file\n");
                } else {
                    // Delete the file
                    err = lfs_remove(&lfs, filepath);
                    if (err == 0) {
                        result.success = true;
                        result.error_message[0] = '\0';
                        printf("DeleteTask: Successfully deleted '%s'\n", request.filename);
                    } else {
                        result.success = false;

                        const char* error_msg = "Unknown error";
                        switch (err) {
                            case LFS_ERR_NOENT:
                                error_msg = "File not found";
                                break;
                            case LFS_ERR_BADF:
                                error_msg = "File is currently open";
                                break;
                            case LFS_ERR_ISDIR:
                                error_msg = "Cannot delete directory";
                                break;
                            case LFS_ERR_NOTEMPTY:
                                error_msg = "Directory not empty";
                                break;
                            case LFS_ERR_IO:
                                error_msg = "I/O error";
                                break;
                            case LFS_ERR_CORRUPT:
                                error_msg = "Filesystem corruption";
                                break;
                            case LFS_ERR_INVAL:
                                error_msg = "Invalid parameter";
                                break;
                        }

                        snprintf(result.error_message, sizeof(result.error_message),
                                "%s (err=%d)", error_msg, err);
                        printf("DeleteTask: Failed: %s\n", result.error_message);
                    }
                }
            }

            // Store result
            last_result = result;

            // Signal that result is ready
            xSemaphoreGive(result_ready_sem);
        }
    }
}

void file_delete_task_init(void)
{
    // Create queue (holds 1 request at a time)
    delete_queue = xQueueCreate(1, sizeof(file_delete_request_t));
    configASSERT(delete_queue != NULL);

    // Create result semaphore
    result_ready_sem = xSemaphoreCreateBinary();
    configASSERT(result_ready_sem != NULL);

    // Create task
    BaseType_t err = xTaskCreate(
        file_delete_task,
        "FileDel",
        2048,  // Stack size in words
        NULL,
        TASK_NORMAL_PRIORITY,
        &delete_task_handle
    );
    configASSERT(err == pdPASS);

    // Pin to core 0 (same core as Logger and HTTP server for safety)
    vTaskCoreAffinitySet(delete_task_handle, (1 << 0));

    printf("DeleteTask: Initialized\n");
}

bool file_delete_request_async(const char* filename, uint32_t timeout_ms, file_delete_result_t* result)
{
    if (!delete_queue || !result_ready_sem) {
        return false;  // Task not initialized
    }

    // Build request
    file_delete_request_t request;
    strncpy(request.filename, filename, sizeof(request.filename) - 1);
    request.filename[sizeof(request.filename) - 1] = '\0';

    // Clear any previous result semaphore
    xSemaphoreTake(result_ready_sem, 0);

    // Send request to task
    if (xQueueSend(delete_queue, &request, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
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
