/**
 * Asynchronous file I/O task.
 *
 * This task handles file operations from the HTTP API.
 * File operations must happen in a task context (not HTTP callback context)
 * to safely acquire the LittleFS mutex and perform SD card operations.
 *
 * Supported operations:
 * - Delete: Remove a file from the filesystem
 * - Upload Open: Create/open a file for writing uploaded data
 * - Upload Write: Write a chunk of data to an open upload file
 * - Upload Close: Close an upload file (with optional sync)
 */

#ifndef FILE_IO_TASK_H
#define FILE_IO_TASK_H

#include "lfs.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Operation types
typedef enum {
    FILE_IO_OP_DELETE,
    FILE_IO_OP_UPLOAD_OPEN,
    FILE_IO_OP_UPLOAD_WRITE,
    FILE_IO_OP_UPLOAD_CLOSE,
    FILE_IO_OP_MKDIR,
    FILE_IO_OP_REFLASH_EP
    // Note: WP reflash is handled by dedicated ota_flash_task, not here
} file_io_op_t;

// Maximum chunk size for upload writes
#define FILE_IO_MAX_CHUNK_SIZE 4096

// Request structure (union of all operation types)
typedef struct {
    file_io_op_t op;
    union {
        // For DELETE and MKDIR
        struct {
            char path[80];
        } path_op;

        // For UPLOAD_OPEN
        struct {
            char path[80];
            bool truncate;  // true to truncate existing file
        } open_op;

        // For UPLOAD_WRITE
        struct {
            uint8_t* data;      // Pointer to data (caller must keep valid until result)
            uint32_t length;    // Length of data
        } write_op;

        // For UPLOAD_CLOSE
        struct {
            bool sync;  // true to sync before close
        } close_op;

        // For REFLASH_EP
        struct {
            char path[80];  // Path to UF2 file (e.g., "/EP.uf2")
            bool verbose;   // Enable verbose output
        } reflash_ep_op;
    };
} file_io_request_t;

// Result structure
typedef struct {
    bool success;
    int error_code;         // LFS error code on failure (or FlashEp error code for reflash)
    char error_message[64];
    union {
        // For UPLOAD_WRITE
        struct {
            uint32_t bytes_written;
        } write_result;
        // For REFLASH_EP
        struct {
            int32_t flash_result;  // Result from flash_ep_uf2()
        } reflash_ep_result;
    };
} file_io_result_t;

/**
 * Initialize the file I/O task.
 * Must be called once during system initialization.
 */
void file_io_task_init(void);

/**
 * Execute a file I/O operation synchronously.
 *
 * This function sends a request to the file I/O task and waits
 * for the result with a timeout.
 *
 * @param request Pointer to request structure
 * @param timeout_ms Maximum time to wait for operation to complete (milliseconds)
 * @param result Pointer to result structure (filled on return)
 * @return true if request was processed, false on timeout or error
 */
bool file_io_execute(const file_io_request_t* request, uint32_t timeout_ms, file_io_result_t* result);

/**
 * Convenience function: Delete a file.
 *
 * @param path Full path to file (e.g., "/ride_001.um4")
 * @param timeout_ms Maximum time to wait
 * @param result Pointer to result structure
 * @return true if request was processed
 */
bool file_io_delete(const char* path, uint32_t timeout_ms, file_io_result_t* result);

/**
 * Convenience function: Create a directory.
 *
 * @param path Directory path (e.g., "/uploads")
 * @param timeout_ms Maximum time to wait
 * @param result Pointer to result structure
 * @return true if request was processed
 */
bool file_io_mkdir(const char* path, uint32_t timeout_ms, file_io_result_t* result);

/**
 * Convenience function: Open file for upload.
 *
 * @param path Full path to file (e.g., "/uploads/firmware.uf2")
 * @param truncate true to truncate existing file, false to append
 * @param timeout_ms Maximum time to wait
 * @param result Pointer to result structure
 * @return true if request was processed
 */
bool file_io_upload_open(const char* path, bool truncate, uint32_t timeout_ms, file_io_result_t* result);

/**
 * Convenience function: Write data to upload file.
 *
 * @param data Pointer to data buffer
 * @param length Length of data to write
 * @param timeout_ms Maximum time to wait
 * @param result Pointer to result structure (includes bytes_written)
 * @return true if request was processed
 */
bool file_io_upload_write(const uint8_t* data, uint32_t length, uint32_t timeout_ms, file_io_result_t* result);

/**
 * Convenience function: Close upload file.
 *
 * @param sync true to sync file before closing
 * @param timeout_ms Maximum time to wait
 * @param result Pointer to result structure
 * @return true if request was processed
 */
bool file_io_upload_close(bool sync, uint32_t timeout_ms, file_io_result_t* result);

/**
 * Convenience function: Reflash EP processor via SWD.
 *
 * This is a long-running operation (10-30 seconds) that programs the EP
 * flash using a UF2 file on the SD card.
 *
 * @param path Full path to UF2 file (e.g., "/EP.uf2")
 * @param verbose Enable verbose output during flashing
 * @param timeout_ms Maximum time to wait (use 120000 for 2 minutes)
 * @param result Pointer to result structure (includes flash_result code)
 * @return true if request was processed (check result.success for actual outcome)
 */
bool file_io_reflash_ep(const char* path, bool verbose, uint32_t timeout_ms, file_io_result_t* result);

// Note: WP self-reflash is now handled by the dedicated OTA flash task
// (see ota_flash_task.h). This provides proper subsystem shutdown and
// upgrade logging before flashing.

// Legacy compatibility - maps to file_io_delete
typedef struct {
    char filename[64];
} file_delete_request_t;

typedef struct {
    char filename[64];
    bool success;
    char error_message[128];
} file_delete_result_t;

void file_delete_task_init(void);
bool file_delete_request_async(const char* filename, uint32_t timeout_ms, file_delete_result_t* result);

#ifdef __cplusplus
}
#endif

#endif // FILE_IO_TASK_H
