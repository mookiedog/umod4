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

#endif

// Operation types
typedef enum {
    FILE_IO_OP_DELETE,
    FILE_IO_OP_UPLOAD_OPEN,
    FILE_IO_OP_UPLOAD_WRITE,
    FILE_IO_OP_UPLOAD_CLOSE,
    FILE_IO_OP_MKDIR,
    FILE_IO_OP_REFLASH_EP,
    FILE_IO_OP_WRITE_ECU_LIVE_CONFIG,
    FILE_IO_OP_FLASH_EP_SLOT,
    FILE_IO_OP_FLASH_EP_SLOT_FROM_FILE,   // Flash slot, image from LFS file
    FILE_IO_OP_REWRITE_EP_SLOT_HEADER     // Rewrite header only, keep existing binary
    // Note: WP reflash is handled by dedicated ota_flash_task, not here
} file_io_op_t;

// Maximum chunk size for upload writes (must match UPLOAD_BUFFER_SIZE)
#define FILE_IO_MAX_CHUNK_SIZE 8192

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

        // For WRITE_ECU_LIVE_CONFIG
        struct {
            int16_t items[10];  // ECU live log IDs (-1 = empty slot, 0-255 = log ID)
        } ecu_live_config_op;

        // For FLASH_EP_SLOT
        struct {
            const uint8_t* bson_hdr;     // BSON header bytes (caller keeps valid)
            uint32_t bson_hdr_size;      // Actual size of bson_hdr (≤ 32768)
            const uint8_t* image_data;   // 32KB image binary, or NULL for zeros/erase
            uint32_t target_flash_addr;  // EP flash address, must be 65536-aligned
            bool erase;                  // If true: fill entire slot with 0xFF (delete)
        } flash_ep_slot_op;

        // For FLASH_EP_SLOT_FROM_FILE: flash slot with image read from LFS file.
        // flashSlotFromFile() computes murmur3 and builds BSON header internally.
        // The LFS file is deleted by the operation after use.
        struct {
            char name[32];
            char description[64];
            char protection[4];
            char lfs_path[80];          // e.g. "/tmp_imgstore.bin"
            uint32_t target_flash_addr; // EP flash address, must be 65536-aligned
        } flash_ep_slot_from_file_op;

        // For REWRITE_EP_SLOT_HEADER: update BSON header, preserve existing binary.
        // rewriteSlotHeader() reads the binary from EP flash via SWD and re-flashes.
        struct {
            char name[32];
            char description[64];
            char protection[4];
            uint32_t slot_flash_addr;   // EP flash address, must be 65536-aligned
        } rewrite_ep_slot_header_op;
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
        // For FLASH_EP_SLOT
        struct {
            int32_t flash_result;  // Result from FlashEp::flashSlot() or eraseSlot()
        } flash_ep_slot_result;
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
 * Non-blocking upload write: Submit write without waiting for completion.
 *
 * This function submits a write request to the file I/O task and returns
 * immediately without waiting for the operation to complete. Use
 * file_io_upload_write_is_complete() to check completion status, or
 * file_io_upload_write_wait() to block until complete.
 *
 * IMPORTANT: The data buffer must remain valid until the write completes!
 *
 * @param data Pointer to data buffer (must remain valid until complete)
 * @param length Length of data to write
 * @param timeout_ms Maximum time to wait for queue submission
 * @return true if request was submitted to queue, false on timeout
 */
bool file_io_upload_write_async(const uint8_t* data, uint32_t length, uint32_t timeout_ms);

/**
 * Check if async upload write has completed.
 *
 * @return true if the previous async write has completed, false if still in progress
 */
bool file_io_upload_write_is_complete(void);

/**
 * Wait for async upload write to complete and retrieve result.
 *
 * @param timeout_ms Maximum time to wait for completion
 * @param result Pointer to result structure (filled on return)
 * @return true if operation completed, false on timeout
 */
bool file_io_upload_write_wait(uint32_t timeout_ms, file_io_result_t* result);

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

/**
 * Convenience function: Write ECU live config to /ecu_live.json.
 *
 * @param items Array of 10 log IDs (-1 = empty slot, 0-255 = log ID)
 * @param timeout_ms Maximum time to wait
 * @param result Pointer to result structure
 * @return true if request was processed (check result.success for actual outcome)
 */
bool file_io_write_ecu_live_config(const int16_t items[10], uint32_t timeout_ms, file_io_result_t* result);

/**
 * Convenience function: Flash a 64KB slot to the EP image store.
 *
 * @param bson_hdr    BSON header bytes (caller keeps valid until function returns)
 * @param bson_hdr_size  Size of bson_hdr in bytes (must be ≤ 32768)
 * @param image_data  32KB raw EPROM binary, or NULL to write zeros (e.g. selector slot)
 * @param target_flash_addr  EP flash address, must be 65536-byte aligned
 * @param timeout_ms  Maximum time to wait (use 60000 for a slot write)
 * @param result      Pointer to result structure
 * @return true if request was processed (check result.success for actual outcome)
 */
bool file_io_flash_ep_slot(const uint8_t* bson_hdr, uint32_t bson_hdr_size,
                           const uint8_t* image_data, uint32_t target_flash_addr,
                           uint32_t timeout_ms, file_io_result_t* result);

/**
 * Convenience function: Erase a 64KB EP image-store slot (fills with 0xFF).
 *
 * @param target_flash_addr  EP flash address, must be 65536-byte aligned
 * @param timeout_ms  Maximum time to wait
 * @param result      Pointer to result structure
 * @return true if request was processed (check result.success for actual outcome)
 */
bool file_io_erase_ep_slot(uint32_t target_flash_addr,
                           uint32_t timeout_ms, file_io_result_t* result);

/**
 * Convenience function: Flash a 64KB EP slot with image data from an LFS file.
 * Computes murmur3 and builds BSON header internally.  No heap allocation.
 * Deletes lfs_path from LFS after use regardless of success/failure.
 *
 * @param name / description / protection  Slot metadata for BSON header
 * @param lfs_path          Path to 32768-byte binary on LittleFS
 * @param target_flash_addr EP flash address, must be 65536-byte aligned
 * @param timeout_ms        Maximum time to wait
 * @param result            Pointer to result structure
 * @return true if request was processed (check result.success for actual outcome)
 */
bool file_io_flash_ep_slot_from_file(const char* name, const char* description,
                                     const char* protection, const char* lfs_path,
                                     uint32_t target_flash_addr,
                                     uint32_t timeout_ms, file_io_result_t* result);

/**
 * Convenience function: Rewrite the BSON header of an existing EP image-store
 * slot while preserving the existing 32KB image binary.  Reads the binary from
 * EP flash via SWD (no LFS), recomputes murmur3.  No heap allocation.
 *
 * @param name / description / protection  New metadata for BSON header
 * @param slot_flash_addr   EP flash address of the slot, must be 65536-byte aligned
 * @param timeout_ms        Maximum time to wait
 * @param result            Pointer to result structure
 * @return true if request was processed (check result.success for actual outcome)
 */
bool file_io_rewrite_ep_slot_header(const char* name, const char* description,
                                    const char* protection, uint32_t slot_flash_addr,
                                    uint32_t timeout_ms, file_io_result_t* result);

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

