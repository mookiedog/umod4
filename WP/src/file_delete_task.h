/**
 * Asynchronous file deletion task.
 *
 * This task handles file deletion requests from the HTTP API.
 * File deletions must happen in a task context (not HTTP callback context)
 * to safely acquire the LittleFS mutex and perform SD card operations.
 */

#ifndef FILE_DELETE_TASK_H
#define FILE_DELETE_TASK_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Delete request structure
typedef struct {
    char filename[64];
} file_delete_request_t;

// Delete result structure
typedef struct {
    char filename[64];
    bool success;
    char error_message[128];
} file_delete_result_t;

/**
 * Initialize the file deletion task.
 * Must be called once during system initialization.
 */
void file_delete_task_init(void);

/**
 * Request file deletion asynchronously.
 *
 * This function sends a delete request to the delete task and waits
 * for the result with a timeout.
 *
 * @param filename Filename to delete (relative to root, e.g., "ride_001.um4")
 * @param timeout_ms Maximum time to wait for deletion to complete (milliseconds)
 * @param result Pointer to result structure (filled on success)
 * @return true if request was processed, false on timeout or error
 */
bool file_delete_request_async(const char* filename, uint32_t timeout_ms, file_delete_result_t* result);

#ifdef __cplusplus
}
#endif

#endif // FILE_DELETE_TASK_H
