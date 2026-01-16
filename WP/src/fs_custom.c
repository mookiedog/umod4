#include "fs_custom.h"
#include "lwip/apps/fs.h"
#include "lfs.h"
#include "pico/stdlib.h"
#include "pico/sha256.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Forward declarations for API JSON generators
extern void generate_api_info_json(char* buffer, size_t size);
extern void generate_api_list_json(char* buffer, size_t size);
extern void generate_api_sha256_json(char* buffer, size_t size, const char* filename);
extern void generate_api_delete_json(char* buffer, size_t size, const char* filename);
extern void generate_api_upload_session_json(char* buffer, size_t size, const char* session_id);

// Global LittleFS context (set by fs_custom_init)
static lfs_t* g_lfs = NULL;

/**
 * SHA-256 hash cache for most recently served file.
 * This avoids re-reading the file from SD card when server requests hash.
 * Exported to api_handlers.c for /api/sha256/<filename> endpoint.
 */
typedef struct {
    char filename[64];
    sha256_result_t hash;
    bool valid;
} file_hash_cache_t;

file_hash_cache_t g_file_hash_cache = {0};

/**
 * Custom file structure for both LittleFS files and virtual API files.
 */
struct lfs_custom_file {
    lfs_file_t file;          // LittleFS file handle (only for real files)
    char* data;               // In-memory data (for API responses)
    size_t file_size;         // Total file size
    size_t bytes_read;        // Bytes read so far
    bool is_open;             // Track if file is open
    bool is_api;              // True if this is an API response (in-memory)
    pico_sha256_state_t sha_state;  // SHA-256 state for calculating hash during transfer
    bool sha_enabled;         // True if SHA-256 calculation is in progress
    char sha_filename[64];    // Filename for this transfer (for caching)
};

void fs_custom_init(lfs_t* lfs_ptr)
{
    g_lfs = lfs_ptr;
    printf("fs_custom: Initialized with LittleFS context\n");
}

bool fs_custom_is_ready(void)
{
    return (g_lfs != NULL);
}

/**
 * Open a file from the custom filesystem.
 * This is called by lwIP httpd when a file is requested.
 *
 * Supported paths:
 * - /api/*       - API endpoints (served from RAM)
 * - /logs/*.um4  - Log files from SD card
 * - Other paths  - Return 0 to let lwIP check built-in fsdata.c
 */
int fs_open_custom(struct fs_file *file, const char *name)
{
    // Strip leading slash
    const char* path = name;
    if (path[0] == '/') {
        path++;
    }

    printf("fs_custom: Opening '%s'\n", path);

    // Check if this is an API endpoint (/api/*)
    if (strncmp(path, "api/", 4) == 0) {
        const char* api_name = path + 4;  // Skip "api/" prefix

        // Allocate structure for API response
        struct lfs_custom_file* api_file = (struct lfs_custom_file*)malloc(sizeof(struct lfs_custom_file));
        if (!api_file) {
            printf("fs_custom: Failed to allocate API file structure\n");
            return 0;
        }

        // Allocate buffer for API response
        // info and sha256 need 512 bytes, list needs 8KB for ~100 files
        size_t api_buffer_size = strcmp(api_name, "list") == 0 ? 8192 : 512;

        api_file->data = (char*)malloc(api_buffer_size);
        if (!api_file->data) {
            printf("fs_custom: Failed to allocate API buffer (%zu bytes)\n", api_buffer_size);
            free(api_file);
            return 0;
        }

        // Generate JSON based on API endpoint
        if (strcmp(api_name, "info") == 0) {
            generate_api_info_json(api_file->data, api_buffer_size);
        } else if (strcmp(api_name, "list") == 0) {
            generate_api_list_json(api_file->data, api_buffer_size);
        } else if (strncmp(api_name, "sha256/", 7) == 0) {
            // Extract filename from sha256/<filename>
            const char* filename = api_name + 7;
            generate_api_sha256_json(api_file->data, api_buffer_size, filename);
        } else if (strncmp(api_name, "delete/", 7) == 0) {
            // Extract filename from delete/<filename>
            const char* filename = api_name + 7;
            generate_api_delete_json(api_file->data, api_buffer_size, filename);
        } else if (strncmp(api_name, "upload/session?session_id=", 26) == 0) {
            // Extract session_id from upload/session?session_id=<uuid>
            const char* session_id = api_name + 26;
            generate_api_upload_session_json(api_file->data, api_buffer_size, session_id);
        } else {
            printf("fs_custom: Unknown API endpoint: %s\n", api_name);
            free(api_file->data);
            free(api_file);
            return 0;
        }

        api_file->file_size = strlen(api_file->data);
        api_file->bytes_read = 0;
        api_file->is_open = true;
        api_file->is_api = true;

        // Fill in fs_file structure for lwIP
        // For APIs, we serve directly from memory (not streaming)
        file->data = api_file->data;  // Point directly to our buffer
        file->len = (int)api_file->file_size;
        file->index = 0;
        file->pextension = api_file;
        file->flags = FS_FILE_FLAGS_HEADER_PERSISTENT;  // Don't set CUSTOM flag for in-memory

        printf("fs_custom: Serving API '%s', %zu bytes\n",
               api_name, api_file->file_size);
        return 1;  // Success
    }

    // Check if this is a log file request (/logs/*.um4)
    if (strncmp(path, "logs/", 5) == 0) {
        // Log files require LittleFS to be mounted
        if (!g_lfs) {
            printf("fs_custom: LittleFS not initialized for log file access\n");
            return 0;
        }

        const char* filename = path + 5;  // Skip "logs/" prefix

        // Validate filename (only allow .um4 files)
        size_t len = strlen(filename);
        if (len < 5 || strcmp(filename + len - 4, ".um4") != 0) {
            printf("fs_custom: Invalid log file extension: %s\n", filename);
            return 0;
        }

        // Allocate custom file structure
        struct lfs_custom_file* lfs_file = (struct lfs_custom_file*)malloc(sizeof(struct lfs_custom_file));
        if (!lfs_file) {
            printf("fs_custom: Failed to allocate file structure\n");
            return 0;
        }

        // Open file from LittleFS
        int err = lfs_file_open(g_lfs, &lfs_file->file, filename, LFS_O_RDONLY);
        if (err < 0) {
            printf("fs_custom: Failed to open '%s': %d\n", filename, err);
            free(lfs_file);
            return 0;
        }

        // Get file size
        lfs_soff_t size = lfs_file_size(g_lfs, &lfs_file->file);
        if (size < 0) {
            printf("fs_custom: Failed to get file size: %ld\n", (long)size);
            lfs_file_close(g_lfs, &lfs_file->file);
            free(lfs_file);
            return 0;
        }

        lfs_file->file_size = (size_t)size;
        lfs_file->bytes_read = 0;
        lfs_file->is_open = true;
        lfs_file->is_api = false;  // This is a real LittleFS file
        lfs_file->data = NULL;

        // Initialize SHA-256 calculation for this file
        strncpy(lfs_file->sha_filename, filename, sizeof(lfs_file->sha_filename) - 1);
        lfs_file->sha_filename[sizeof(lfs_file->sha_filename) - 1] = '\0';

        if (pico_sha256_try_start(&lfs_file->sha_state, SHA256_BIG_ENDIAN, true) == PICO_OK) {
            lfs_file->sha_enabled = true;
            printf("fs_custom: SHA-256 enabled for '%s'\n", filename);
        } else {
            lfs_file->sha_enabled = false;
            printf("fs_custom: WARNING: SHA-256 hardware busy, serving without hash\n");
        }

        // Fill in fs_file structure for lwIP
        file->data = NULL;  // NULL means streaming mode (use fs_read_custom)
        file->len = (int)lfs_file->file_size;
        file->index = 0;
        file->pextension = lfs_file;  // Store custom file handle
        file->flags = FS_FILE_FLAGS_HEADER_PERSISTENT | FS_FILE_FLAGS_CUSTOM;

        printf("fs_custom: Opened '%s', size=%zu bytes\n", filename, lfs_file->file_size);
        return 1;  // Success
    }

    // Check if this is a virtual upload response file
    if (strcmp(path, "upload_success.json") == 0 ||
        strcmp(path, "upload_error.json") == 0 ||
        strcmp(path, "upload_progress.json") == 0) {

        // Allocate structure for API response
        struct lfs_custom_file* api_file = (struct lfs_custom_file*)malloc(sizeof(struct lfs_custom_file));
        if (!api_file) {
            printf("fs_custom: Failed to allocate file structure\n");
            return 0;
        }

        // Allocate buffer for response
        api_file->data = (char*)malloc(256);
        if (!api_file->data) {
            printf("fs_custom: Failed to allocate response buffer\n");
            free(api_file);
            return 0;
        }

        // Generate appropriate JSON response
        if (strcmp(path, "upload_success.json") == 0) {
            snprintf(api_file->data, 256,
                    "{\"success\": true, \"message\": \"Upload complete\"}");
        } else if (strcmp(path, "upload_error.json") == 0) {
            snprintf(api_file->data, 256,
                    "{\"success\": false, \"error\": \"Upload failed\"}");
        } else {  // upload_progress.json
            snprintf(api_file->data, 256,
                    "{\"success\": true, \"message\": \"Chunk received\"}");
        }

        api_file->file_size = strlen(api_file->data);
        api_file->bytes_read = 0;
        api_file->is_open = true;
        api_file->is_api = true;

        // Fill in fs_file structure for lwIP
        file->data = api_file->data;
        file->len = (int)api_file->file_size;
        file->index = 0;
        file->pextension = api_file;
        file->flags = FS_FILE_FLAGS_HEADER_PERSISTENT;

        printf("fs_custom: Serving upload response '%s'\n", path);
        return 1;  // Success
    }

    // Future: Handle embedded web UI files (index.html, etc.)
    // For now, return 0 (file not found)
    printf("fs_custom: Path not recognized: %s\n", path);
    return 0;
}

/**
 * Read data from a custom file.
 * This is called by lwIP httpd to stream file data over HTTP.
 */
int fs_read_custom(struct fs_file *file, char *buffer, int count)
{
    if (!file || !file->pextension) {
        printf("fs_custom: Invalid file handle in fs_read_custom\n");
        return FS_READ_EOF;  // EOF/error
    }

    struct lfs_custom_file* lfs_file = (struct lfs_custom_file*)file->pextension;

    if (!lfs_file->is_open) {
        printf("fs_custom: Attempting to read from closed file\n");
        return FS_READ_EOF;
    }

    // Check if we've reached EOF
    if (lfs_file->bytes_read >= lfs_file->file_size) {
        return FS_READ_EOF;
    }

    // Calculate how many bytes to read (don't read past EOF)
    size_t to_read = count;
    size_t remaining = lfs_file->file_size - lfs_file->bytes_read;
    if (to_read > remaining) {
        to_read = remaining;
    }

    ssize_t bytes_read;

    if (lfs_file->is_api) {
        // Read from in-memory buffer (API response)
        memcpy(buffer, lfs_file->data + lfs_file->bytes_read, to_read);
        bytes_read = to_read;
    } else {
        // Read from LittleFS
        bytes_read = lfs_file_read(g_lfs, &lfs_file->file, buffer, to_read);
        if (bytes_read < 0) {
            printf("fs_custom: LFS read error: %ld\n", (long)bytes_read);
            return FS_READ_EOF;
        }

        // Update SHA-256 hash with data being sent
        if (lfs_file->sha_enabled && bytes_read > 0) {
            pico_sha256_update_blocking(&lfs_file->sha_state, (const uint8_t*)buffer, bytes_read);
        }
    }

    lfs_file->bytes_read += bytes_read;
    file->index += bytes_read;

    // Return the number of bytes read
    // httpd will call us again, and we'll return FS_READ_EOF on the next call
    return (int)bytes_read;
}

/**
 * Close a custom file and free resources.
 * This is called by lwIP httpd when the HTTP connection is closed.
 */
void fs_close_custom(struct fs_file *file)
{
    if (!file || !file->pextension) {
        return;
    }

    struct lfs_custom_file* lfs_file = (struct lfs_custom_file*)file->pextension;

    if (lfs_file->is_open) {
        if (lfs_file->is_api) {
            // API response - free the data buffer
            free(lfs_file->data);
            printf("fs_custom: Closed API (%zu/%zu bytes transferred)\n",
                   lfs_file->bytes_read, lfs_file->file_size);
        } else {
            // LittleFS file - close the file
            lfs_file_close(g_lfs, &lfs_file->file);

            // Finalize SHA-256 hash and cache it
            if (lfs_file->sha_enabled && lfs_file->bytes_read == lfs_file->file_size) {
                sha256_result_t result;
                pico_sha256_finish(&lfs_file->sha_state, &result);

                // Cache the hash for this file
                strncpy(g_file_hash_cache.filename, lfs_file->sha_filename, sizeof(g_file_hash_cache.filename) - 1);
                g_file_hash_cache.filename[sizeof(g_file_hash_cache.filename) - 1] = '\0';
                memcpy(&g_file_hash_cache.hash, &result, sizeof(sha256_result_t));
                g_file_hash_cache.valid = true;

                printf("fs_custom: Closed file '%s' (%zu/%zu bytes), SHA-256 cached\n",
                       lfs_file->sha_filename, lfs_file->bytes_read, lfs_file->file_size);
            } else {
                printf("fs_custom: Closed file (%zu/%zu bytes transferred)\n",
                       lfs_file->bytes_read, lfs_file->file_size);
            }
        }
        lfs_file->is_open = false;
    }

    free(lfs_file);
    file->pextension = NULL;
}
