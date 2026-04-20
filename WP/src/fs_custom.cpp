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
extern void generate_api_reflash_ep_json(char* buffer, size_t size, const char* filename);
extern void generate_api_reflash_wp_json(char* buffer, size_t size, const char* filename);
extern void generate_api_system_json(char* buffer, size_t size);
extern void generate_api_eprom_info_json(char* buffer, size_t size);
extern void generate_api_reboot_json(char* buffer, size_t size);
extern void generate_api_config_json(char* buffer, size_t size);
extern void generate_api_wifi_reset_json(char* buffer, size_t size);
extern void generate_api_ping_json(char* buffer, size_t size);
extern void generate_api_ecu_live_config_json(char* buffer, size_t size);
extern void generate_api_ecu_live_data_json(char* buffer, size_t size);
extern void generate_api_ecu_live_meta_json(char* buffer, size_t size);
extern void generate_api_reformat_filesystem_json(char* buffer, size_t size);
extern void generate_api_sd_info_json(char* buffer, size_t size);
extern void generate_api_image_store_json(char* buffer, size_t size);
extern void generate_api_image_store_selector_json(char* buffer, size_t size);
extern void generate_api_image_store_scan_json(char* buffer, size_t size);
extern void generate_api_wifi_scan_start_json(char* buffer, size_t size);
extern void generate_api_wifi_scan_json(char* buffer, size_t size);
extern void generate_api_ep_stdio_json(char* buffer, size_t size, uint32_t client_offset);

// Global LittleFS context (set by fs_custom_init)
static lfs_t* g_lfs = NULL;

// Set by WiFiManager when AP mode is active; cleared when returning to STA mode.
// When true, unrecognised URIs are redirected to wifi_config.html to trigger
// the captive portal notification on phones and laptops.
extern bool g_captive_portal_active;
extern char g_device_name[64];

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

file_hash_cache_t g_file_hash_cache = {};

/**
 * Custom file structure for both LittleFS files and virtual API files.
 */
struct lfs_custom_file {
    lfs_file_t file;          // LittleFS file handle (only for real files)
    char* data;               // In-memory data (for API responses or pre-read chunk data)
    size_t file_size;         // Total file size
    size_t bytes_read;        // Bytes read so far
    bool is_open;             // Track if file is open
    bool is_api;              // True if this is an API response or pre-read chunk (in-memory)
    bool is_chunk;            // True if data points to s_chunk_data_buf (do not free; clear busy flag)
    pico_sha256_state_t sha_state;  // SHA-256 state for calculating hash during transfer
    bool sha_enabled;         // True if SHA-256 calculation is in progress
    char sha_filename[64];    // Filename for this transfer (for caching)
    char header_buf[300];     // HTTP response headers (for HEADER_INCLUDED streaming)
    size_t header_len;        // Bytes in header_buf (0 = not used)
    size_t header_sent;       // Bytes of header already sent
};

#define CHUNK_DOWNLOAD_MAX_SIZE (16 * 1024)
#define CHUNK_HEADER_RESERVE    300  // max HTTP header size for chunk responses
// Single static buffer: [HTTP headers][chunk data]. Header is built at the front,
// data read at CHUNK_HEADER_RESERVE offset, then memmoved to close the gap.
static uint8_t s_chunk_buf[CHUNK_HEADER_RESERVE + CHUNK_DOWNLOAD_MAX_SIZE];
// Static file struct for chunk downloads — safe because s_chunk_in_progress serialises access.
static struct lfs_custom_file s_chunk_file;
static bool s_chunk_in_progress = false;

// Timing instrumentation — populated in fs_open_custom, printed in fs_close_custom.
// Produces one "CHK ..." line per chunk on the RTT console.
static uint32_t s_chunk_lfs_us;       // lfs_file_read duration
static uint32_t s_chunk_gap_us;       // gap since previous chunk closed
static uint32_t s_chunk_t_ready;      // absolute time when chunk was handed to lwIP
static uint32_t s_chunk_sz;           // bytes actually read
static uint32_t s_chunk_off;          // byte offset of this chunk
static uint32_t s_chunk_t_prev_close; // absolute time of last fs_close_custom
static bool     s_chunk_has_prev;     // true once first chunk has closed

// Persistent LFS state: file handle kept open across sequential chunks to avoid
// per-chunk open/seek/close overhead.
static lfs_file_t        s_lfs_file;
static char              s_lfs_open_filename[64];  // filename currently open ("" = none)
static lfs_soff_t        s_lfs_next_offset;        // expected file position after last read

void fs_custom_init(lfs_t* lfs_ptr)
{
    g_lfs = lfs_ptr;
    printf("fs_custom: Initialized with LittleFS context\n");
}

void fs_custom_close_persistent_handle(void)
{
    if (s_lfs_open_filename[0] != '\0') {
        lfs_file_close(g_lfs, &s_lfs_file);
        s_lfs_open_filename[0] = '\0';
        printf("fs_custom: Closed persistent LFS handle\n");
    }
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
 * - /api/*             - API endpoints (served from RAM)
 * - /logs/*.um4, *.log - Log files from SD card
 * - Other paths        - Return 0 to let lwIP check built-in fsdata.c
 */
int fs_open_custom(struct fs_file *file, const char *name)
{
    // Strip leading slash
    const char* path = name;
    if (path[0] == '/') {
        path++;
    }

    if (false) printf("fs_custom: Opening '%s'\n", path);

    // Chunked file download: /api/chunks/<filename>/<offset>
    // Returns up to CHUNK_DOWNLOAD_MAX_SIZE bytes of the file starting at <offset>,
    // with X-Chunk-SHA256 and X-File-Size response headers.
    // Each request is independent: LFS is opened, seeked, read, and closed in one shot.
    if (strncmp(path, "api/chunks/", 11) == 0) {
        if (!g_lfs) {
            printf("fs_custom: chunks: LittleFS not initialized\n");
            return 0;
        }
        if (s_chunk_in_progress) {
            printf("fs_custom: chunks: busy (previous chunk still streaming)\n");
            return 0;
        }

        // Parse "<filename>/<offset>" — last '/' separates them
        const char* chunks_arg = path + 11;
        const char* last_slash = strrchr(chunks_arg, '/');
        if (!last_slash || last_slash == chunks_arg) {
            printf("fs_custom: chunks: bad URL '%s'\n", path);
            return 0;
        }

        size_t filename_len = (size_t)(last_slash - chunks_arg);
        if (filename_len == 0 || filename_len >= 64) {
            printf("fs_custom: chunks: filename length invalid\n");
            return 0;
        }

        char filename[64];
        memcpy(filename, chunks_arg, filename_len);
        filename[filename_len] = '\0';

        uint32_t offset = (uint32_t)strtoul(last_slash + 1, NULL, 10);

        uint32_t t0 = time_us_32();
        s_chunk_gap_us = s_chunk_has_prev ? (t0 - s_chunk_t_prev_close) : 0;

        // Open or seek only when necessary. For sequential downloads the handle
        // stays open and lfs_file_read advances the position automatically.
        bool need_open = (strcmp(filename, s_lfs_open_filename) != 0);
        bool need_seek = !need_open && ((lfs_soff_t)offset != s_lfs_next_offset);

        if (need_open) {
            if (s_lfs_open_filename[0] != '\0') {
                lfs_file_close(g_lfs, &s_lfs_file);
                s_lfs_open_filename[0] = '\0';
            }
            int err = lfs_file_open(g_lfs, &s_lfs_file, filename, LFS_O_RDONLY);
            if (err < 0) {
                printf("fs_custom: chunks: open '%s': %d\n", filename, err);
                return 0;
            }
            strncpy(s_lfs_open_filename, filename, sizeof(s_lfs_open_filename) - 1);
            s_lfs_open_filename[sizeof(s_lfs_open_filename) - 1] = '\0';
            s_lfs_next_offset = 0;
        }
        if (need_open || need_seek) {
            if ((lfs_soff_t)offset != s_lfs_next_offset) {
                if (lfs_file_seek(g_lfs, &s_lfs_file,
                                  (lfs_soff_t)offset, LFS_SEEK_SET) < 0) {
                    lfs_file_close(g_lfs, &s_lfs_file);
                    s_lfs_open_filename[0] = '\0';
                    printf("fs_custom: chunks: seek '%s' @%lu failed\n",
                           filename, (unsigned long)offset);
                    return 0;
                }
                s_lfs_next_offset = (lfs_soff_t)offset;
            }
        }

        lfs_soff_t file_size = lfs_file_size(g_lfs, &s_lfs_file);
        if (file_size < 0) {
            lfs_file_close(g_lfs, &s_lfs_file);
            s_lfs_open_filename[0] = '\0';
            return 0;
        }

        // Read data at the header-reservation offset, then memmove to close the gap.
        uint8_t* const data_start = s_chunk_buf + CHUNK_HEADER_RESERVE;
        uint32_t t_read = time_us_32();
        lfs_ssize_t bytes_read = lfs_file_read(
            g_lfs, &s_lfs_file, data_start, CHUNK_DOWNLOAD_MAX_SIZE);
        s_chunk_lfs_us = time_us_32() - t_read;

        if (bytes_read < 0) {
            lfs_file_close(g_lfs, &s_lfs_file);
            s_lfs_open_filename[0] = '\0';
            printf("fs_custom: chunks: read '%s': %ld\n", filename, (long)bytes_read);
            return 0;
        }
        s_lfs_next_offset += bytes_read;

        char sha[65] = "none";
        if (bytes_read > 0) {
            pico_sha256_state_t sha_state;
            if (pico_sha256_try_start(&sha_state, SHA256_BIG_ENDIAN, true) == PICO_OK) {
                sha256_result_t result;
                pico_sha256_update_blocking(&sha_state, data_start, (size_t)bytes_read);
                pico_sha256_finish(&sha_state, &result);
                for (int i = 0; i < SHA256_RESULT_BYTES; i++)
                    snprintf(sha + i * 2, 3, "%02x", result.bytes[i]);
                sha[64] = '\0';
            }
        }

        size_t header_len = (size_t)snprintf(
            (char*)s_chunk_buf, CHUNK_HEADER_RESERVE,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/octet-stream\r\n"
            "Content-Length: %ld\r\n"
            "Connection: keep-alive\r\n"
            "X-Chunk-SHA256: %s\r\n"
            "X-File-Size: %ld\r\n"
            "\r\n",
            (long)bytes_read, sha, (long)file_size);
        // header_len (~215B) < CHUNK_HEADER_RESERVE (300B): source > dest, safe forward move.
        if (header_len < CHUNK_HEADER_RESERVE && bytes_read > 0)
            memmove(s_chunk_buf + header_len, data_start, (size_t)bytes_read);

        size_t total_len = header_len + (size_t)bytes_read;

        s_chunk_sz      = (uint32_t)bytes_read;
        s_chunk_off     = offset;
        s_chunk_t_ready = time_us_32();

        struct lfs_custom_file* chunk_file = &s_chunk_file;
        memset(chunk_file, 0, sizeof(*chunk_file));
        chunk_file->is_open  = true;
        chunk_file->is_chunk = true;

        // In-memory path: file->data != NULL tells httpd to send directly from our buffer.
        // file->index = file->len signals "all data accounted for" so httpd does not
        // call fs_read_custom after the send (avoids double-send, see OOM fix notes).
        file->data       = (char*)s_chunk_buf;
        file->len        = (int)total_len;
        file->index      = (int)total_len;
        file->pextension = chunk_file;
        file->flags      = FS_FILE_FLAGS_HEADER_INCLUDED |
                           FS_FILE_FLAGS_HEADER_PERSISTENT |
                           FS_FILE_FLAGS_CUSTOM;
        s_chunk_in_progress = true;
        return 1;
    }

    // Check if this is an API endpoint (/api/*)
    if (strncmp(path, "api/", 4) == 0) {
        const char* api_name = path + 4;  // Skip "api/" prefix

        // Allocate struct and data buffer in a single block to avoid two separate heap
        // allocations per request, which accumulate fragmentation over many requests.
        // info and sha256 need 512 bytes, list and sd-info need 8KB for ~100 files
        // image-store/scan needs 4KB for up to ~127 slot entries
        // image-store and image-store/selector need 1KB for the selector JSON
        size_t api_buffer_size = (strcmp(api_name, "list") == 0 ||
                                   strcmp(api_name, "sd-info") == 0) ? 8192 :
                                  (strcmp(api_name, "image-store/scan") == 0 ||
                                   strcmp(api_name, "wifi-scan") == 0) ? 4096 :
                                  (strcmp(api_name, "image-store") == 0 ||
                                   strcmp(api_name, "image-store/selector") == 0 ||
                                   strcmp(api_name, "ecu-live-data") == 0) ? 1024 :
                                  (strcmp(api_name, "ecu-live-meta") == 0) ? 2048 :
                                  (strncmp(api_name, "ep-stdio/", 9) == 0) ? 1536 : 512;

        struct lfs_custom_file* api_file =
            (struct lfs_custom_file*)malloc(sizeof(struct lfs_custom_file) + api_buffer_size);
        if (!api_file) {
            printf("fs_custom: Failed to allocate API file+buffer (%zu bytes)\n",
                   sizeof(struct lfs_custom_file) + api_buffer_size);
            return 0;
        }
        memset(api_file, 0, sizeof(*api_file));
        api_file->data = (char*)(api_file + 1);  // data immediately follows struct

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
        } else if (strncmp(api_name, "reflash/ep/", 11) == 0) {
            // Extract filename from reflash/ep/<filename>
            const char* filename = api_name + 11;
            generate_api_reflash_ep_json(api_file->data, api_buffer_size, filename);
        } else if (strncmp(api_name, "reflash/wp/", 11) == 0) {
            // Extract filename from reflash/wp/<filename>
            const char* filename = api_name + 11;
            generate_api_reflash_wp_json(api_file->data, api_buffer_size, filename);
        } else if (strcmp(api_name, "system") == 0) {
            generate_api_system_json(api_file->data, api_buffer_size);
        } else if (strcmp(api_name, "eprom-info") == 0) {
            generate_api_eprom_info_json(api_file->data, api_buffer_size);
        } else if (strcmp(api_name, "reboot") == 0) {
            generate_api_reboot_json(api_file->data, api_buffer_size);
        } else if (strcmp(api_name, "config") == 0) {
            generate_api_config_json(api_file->data, api_buffer_size);
        } else if (strcmp(api_name, "wifi-reset") == 0) {
            generate_api_wifi_reset_json(api_file->data, api_buffer_size);
        } else if (strcmp(api_name, "ping") == 0) {
            generate_api_ping_json(api_file->data, api_buffer_size);
        } else if (strcmp(api_name, "ecu-live-config") == 0) {
            generate_api_ecu_live_config_json(api_file->data, api_buffer_size);
        } else if (strcmp(api_name, "ecu-live-data") == 0) {
            generate_api_ecu_live_data_json(api_file->data, api_buffer_size);
        } else if (strcmp(api_name, "ecu-live-meta") == 0) {
            generate_api_ecu_live_meta_json(api_file->data, api_buffer_size);
        } else if (strcmp(api_name, "sd-info") == 0) {
            generate_api_sd_info_json(api_file->data, api_buffer_size);
        } else if (strcmp(api_name, "reformat-filesystem") == 0) {
            generate_api_reformat_filesystem_json(api_file->data, api_buffer_size);
        } else if (strcmp(api_name, "image-store") == 0) {
            // Backward-compat alias for image-store/selector
            generate_api_image_store_json(api_file->data, api_buffer_size);
        } else if (strcmp(api_name, "image-store/selector") == 0) {
            generate_api_image_store_selector_json(api_file->data, api_buffer_size);
        } else if (strcmp(api_name, "image-store/scan") == 0) {
            generate_api_image_store_scan_json(api_file->data, api_buffer_size);
        } else if (strcmp(api_name, "wifi-scan-start") == 0) {
            generate_api_wifi_scan_start_json(api_file->data, api_buffer_size);
        } else if (strcmp(api_name, "wifi-scan") == 0) {
            generate_api_wifi_scan_json(api_file->data, api_buffer_size);
        } else if (strncmp(api_name, "ep-stdio/", 9) == 0) {
            uint32_t offset = (uint32_t)strtoul(api_name + 9, NULL, 10);
            generate_api_ep_stdio_json(api_file->data, api_buffer_size, offset);
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
        api_file->header_len = 0;
        api_file->header_sent = 0;

        // Fill in fs_file structure for lwIP
        // For APIs, we serve directly from memory (not streaming).
        // IMPORTANT: file->index must equal file->len (not 0) for in-memory files.
        // lwIP's static fsdata convention (fs.c:62) sets index=len to signal
        // "data is already in memory". If index=0, httpd thinks there are still
        // bytes to read via fs_read_custom after the direct write, causing an extra
        // mem_malloc(~512) + re-send on every request (heap fragmentation leak).
        file->data = api_file->data;  // Point directly to our buffer
        file->len = (int)api_file->file_size;
        file->index = (int)api_file->file_size;  // Must equal len for in-memory data
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

        // Validate filename (only allow .um4 and .log files)
        size_t len = strlen(filename);
        bool valid_ext = false;
        if (len >= 4 && strcmp(filename + len - 4, ".um4") == 0) {
            valid_ext = true;
        } else if (len >= 4 && strcmp(filename + len - 4, ".log") == 0) {
            valid_ext = true;
        }
        if (!valid_ext) {
            printf("fs_custom: Invalid log file extension: %s\n", filename);
            return 0;
        }

        // Allocate custom file structure
        struct lfs_custom_file* lfs_file = (struct lfs_custom_file*)malloc(sizeof(struct lfs_custom_file));
        if (!lfs_file) {
            printf("fs_custom: Failed to allocate file structure\n");
            return 0;
        }
        memset(lfs_file, 0, sizeof(*lfs_file));

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
        lfs_file->header_len = 0;
        lfs_file->header_sent = 0;

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

    // Handle upload response files - generate dynamically for POST responses
    // Let lwIP httpd add HTTP headers (including Content-Length and Connection)
    if (strcmp(path, "upload_success.json") == 0 ||
        strcmp(path, "upload_error.json") == 0 ||
        strcmp(path, "upload_progress.json") == 0) {

        struct lfs_custom_file* api_file =
            (struct lfs_custom_file*)malloc(sizeof(struct lfs_custom_file) + 64);
        if (!api_file) {
            return 0;
        }
        memset(api_file, 0, sizeof(*api_file));
        api_file->data = (char*)(api_file + 1);

        // Generate JSON body
        const char* json_body;
        if (strcmp(path, "upload_success.json") == 0) {
            json_body = "{\"success\":true}";
        } else if (strcmp(path, "upload_error.json") == 0) {
            json_body = "{\"success\":false}";
        } else {  // upload_progress.json
            json_body = "{\"success\":true,\"status\":\"in_progress\"}";
        }

        // Simple: Let lwIP build headers. POST responses won't have Content-Length,
        // but that's OK - lwIP httpd closes POST connections anyway (limitation)
        strcpy(api_file->data, json_body);
        api_file->file_size = strlen(api_file->data);
        api_file->bytes_read = 0;
        api_file->is_open = true;
        api_file->is_api = true;
        api_file->header_len = 0;
        api_file->header_sent = 0;

        // Streaming mode - lwIP adds headers, fs_read_custom provides body
        file->data = NULL;
        file->len = (int)api_file->file_size;
        file->index = 0;
        file->pextension = api_file;
        file->flags = FS_FILE_FLAGS_HEADER_PERSISTENT | FS_FILE_FLAGS_CUSTOM;

        return 1;
    }

    // Background image: serve from LittleFS with caching headers if uploaded,
    // else fall back to fsdata.c (which has its own Cache-Control from makefsdata).
    if (strcmp(path, "background.jpg") == 0) {
        if (g_lfs) {
            struct lfs_custom_file* lfs_file =
                (struct lfs_custom_file*)malloc(sizeof(struct lfs_custom_file));
            if (lfs_file) {
                int err = lfs_file_open(g_lfs, &lfs_file->file,
                                        "/background.jpg", LFS_O_RDONLY);
                if (err >= 0) {
                    lfs_soff_t size = lfs_file_size(g_lfs, &lfs_file->file);
                    lfs_file->file_size  = (size_t)size;
                    lfs_file->bytes_read = 0;
                    lfs_file->is_open    = true;
                    lfs_file->is_api     = false;
                    lfs_file->data       = NULL;
                    lfs_file->sha_enabled = false;
                    lfs_file->header_sent = 0;
                    lfs_file->header_len  = (size_t)snprintf(
                        lfs_file->header_buf, sizeof(lfs_file->header_buf),
                        "HTTP/1.0 200 OK\r\n"
                        "Content-Type: image/jpeg\r\n"
                        "Content-Length: %u\r\n"
                        "Cache-Control: max-age=1, public\r\n"
                        "\r\n",
                        (unsigned)size);
                    file->data       = NULL;
                    file->len        = (int)(lfs_file->header_len + lfs_file->file_size);
                    file->index      = 0;
                    file->pextension = lfs_file;
                    file->flags = FS_FILE_FLAGS_HEADER_INCLUDED |
                                  FS_FILE_FLAGS_HEADER_PERSISTENT |
                                  FS_FILE_FLAGS_CUSTOM;
                    printf("fs_custom: Serving background.jpg from LittleFS (%zu bytes)\n", lfs_file->file_size);
                    return 1;
                }
                printf("fs_custom: background.jpg not found in LittleFS (err=%d), trying fsdata.c\n", err);
                free(lfs_file);
            }
        } else {
            printf("fs_custom: background.jpg requested but LittleFS not mounted, trying fsdata.c\n");
        }
        return 0;  // Fall back to fsdata.c default
    }

    // Check if this is a config file download request (/files/background.jpg or /files/ecu_live.json)
    if (strncmp(path, "files/", 6) == 0) {
        const char* filename = path + 6;

        // Whitelist: only allow specific config files
        bool allowed = (strcmp(filename, "background.jpg") == 0 ||
                        strcmp(filename, "ecu_live.json") == 0);
        if (!allowed) {
            printf("fs_custom: Config file download rejected: %s\n", filename);
            return 0;
        }

        if (!g_lfs) {
            printf("fs_custom: LittleFS not mounted for /files/ request\n");
            return 0;
        }

        struct lfs_custom_file* lfs_file =
            (struct lfs_custom_file*)malloc(sizeof(struct lfs_custom_file));
        if (!lfs_file) return 0;
        memset(lfs_file, 0, sizeof(*lfs_file));

        char lfs_path[80];
        snprintf(lfs_path, sizeof(lfs_path), "/%s", filename);
        int err = lfs_file_open(g_lfs, &lfs_file->file, lfs_path, LFS_O_RDONLY);
        if (err < 0) {
            printf("fs_custom: /files/%s not found (err=%d)\n", filename, err);
            free(lfs_file);
            return 0;
        }

        lfs_soff_t size = lfs_file_size(g_lfs, &lfs_file->file);
        if (size < 0) {
            lfs_file_close(g_lfs, &lfs_file->file);
            free(lfs_file);
            return 0;
        }

        lfs_file->file_size  = (size_t)size;
        lfs_file->is_open    = true;
        lfs_file->is_api     = false;
        lfs_file->data       = NULL;
        lfs_file->sha_enabled = false;

        const char* ctype = strstr(filename, ".jpg") ? "image/jpeg" : "application/json";

        // Prefix device name so downloads from multiple devices don't collide in Downloads
        char dl_name[96];
        snprintf(dl_name, sizeof(dl_name), "%s_%s", g_device_name, filename);

        lfs_file->header_len = (size_t)snprintf(
            lfs_file->header_buf, sizeof(lfs_file->header_buf),
            "HTTP/1.0 200 OK\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %u\r\n"
            "Content-Disposition: attachment; filename=\"%s\"\r\n"
            "\r\n",
            ctype, (unsigned)size, dl_name);

        file->data       = NULL;
        file->len        = (int)(lfs_file->header_len + lfs_file->file_size);
        file->index      = 0;
        file->pextension = lfs_file;
        file->flags = FS_FILE_FLAGS_HEADER_INCLUDED |
                      FS_FILE_FLAGS_HEADER_PERSISTENT |
                      FS_FILE_FLAGS_CUSTOM;
        printf("fs_custom: /files/%s -> %zu bytes (dl: %s)\n",
               filename, lfs_file->file_size, dl_name);
        return 1;
    }

    // Captive portal: in AP mode, redirect unrecognised URIs to wifi_config.html.
    // This makes iOS, Android, Windows, and macOS show a "Sign in to network"
    // notification automatically when the user connects to the AP.
    // Exempt: wifi_config.html itself (the redirect target), api/ (form submission),
    // logs/ (file downloads), and POST response JSON files.
    if (g_captive_portal_active &&
        strcmp(path, "wifi_config.html") != 0 &&
        strncmp(path, "api/", 4) != 0 &&
        strncmp(path, "logs/", 5) != 0 &&
        strcmp(path, "upload_success.json") != 0 &&
        strcmp(path, "upload_error.json") != 0 &&
        strcmp(path, "upload_progress.json") != 0) {
        static const char kCaptiveRedirect[] =
            "HTTP/1.0 302 Found\r\n"
            "Location: http://192.168.4.1/wifi_config.html\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n"
            "\r\n";
        file->data = kCaptiveRedirect;
        file->len = (int)(sizeof(kCaptiveRedirect) - 1);
        file->index = (int)(sizeof(kCaptiveRedirect) - 1);  // in-memory: index must equal len
        file->pextension = NULL;
        // HEADER_INCLUDED: data is a complete HTTP response; lwIP must not add headers.
        // HEADER_PERSISTENT: data pointer is static; lwIP must not free it.
        file->flags = FS_FILE_FLAGS_HEADER_INCLUDED | FS_FILE_FLAGS_HEADER_PERSISTENT;
        return 1;
    }

    // Return 0 to let lwIP check built-in fsdata.c (index.html, status.html, etc.)
    return 0;
}

/**
 * Read data from a custom file.
 * This is called by lwIP httpd to stream file data over HTTP.
 */
int fs_read_custom(struct fs_file *file, char *buffer, int count)
{
    if (!file) {
        printf("fs_custom: NULL file handle in fs_read_custom\n");
        return FS_READ_EOF;
    }

    // Static in-memory response (e.g. captive portal redirect): data points
    // directly to the response bytes, pextension is NULL (nothing to free).
    // lwIP always calls fs_read_custom for any file returned from fs_open_custom,
    // so we must handle this case here rather than relying on httpd to read
    // file->data directly.
    if (!file->pextension) {
        if (file->data) {
            int remaining = file->len - file->index;
            if (remaining <= 0) return FS_READ_EOF;
            int to_read = (count < remaining) ? count : remaining;
            memcpy(buffer, file->data + file->index, to_read);
            file->index += to_read;
            return to_read;
        }
        printf("fs_custom: Invalid file handle in fs_read_custom\n");
        return FS_READ_EOF;
    }

    struct lfs_custom_file* lfs_file = (struct lfs_custom_file*)file->pextension;

    if (!lfs_file->is_open) {
        printf("fs_custom: Attempting to read from closed file\n");
        return FS_READ_EOF;
    }

    // Chunk download: with file->data = s_serve_buf, httpd uses the in-memory send path
    // (hs->left = file->len) and should never call fs_read_custom for chunks.
    // This block is a defensive fallback only — should never be reached.
    if (lfs_file->is_chunk) {
        size_t total = (size_t)file->len;
        if ((size_t)count >= total) {
            memcpy(buffer, s_chunk_buf, total);
            file->index = (int)total;
            return (int)total;
        }
        printf("fs_custom: chunk fallback path taken (count=%d total=%u) — unexpected!\n",
               count, (unsigned)total);
        // Fall through to two-phase path.
    }

    // Phase 1: emit HTTP headers before file data (for HEADER_INCLUDED files, e.g. background.jpg)
    if (lfs_file->header_sent < lfs_file->header_len) {
        size_t remaining = lfs_file->header_len - lfs_file->header_sent;
        size_t to_copy = (size_t)count < remaining ? (size_t)count : remaining;
        memcpy(buffer, lfs_file->header_buf + lfs_file->header_sent, to_copy);
        lfs_file->header_sent += to_copy;
        file->index += (int)to_copy;
        return (int)to_copy;
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
        if (lfs_file->is_chunk) {
            // Chunk download — data points to static s_chunk_data_buf, do not free
            uint32_t t_close = time_us_32();
            uint32_t send_us = t_close - s_chunk_t_ready;
            printf("CHK off=%lu gap=%luus lfs=%luus send=%luus sz=%lu\r",
                   (unsigned long)s_chunk_off,
                   (unsigned long)s_chunk_gap_us,
                   (unsigned long)s_chunk_lfs_us,
                   (unsigned long)send_us,
                   (unsigned long)s_chunk_sz);
            s_chunk_t_prev_close = t_close;
            s_chunk_has_prev     = true;

            s_chunk_in_progress  = false;
        } else if (lfs_file->is_api) {
            // data is part of the same allocation as the struct; freed below with free(lfs_file)
        } else {
            // LittleFS file - close the file
            lfs_file_close(g_lfs, &lfs_file->file);

            // Finalize SHA-256 hash - MUST always call finish() to release hardware
            if (lfs_file->sha_enabled) {
                sha256_result_t result;
                pico_sha256_finish(&lfs_file->sha_state, &result);
                lfs_file->sha_enabled = false;

                if (lfs_file->bytes_read == lfs_file->file_size) {
                    // Full file transferred - cache the hash
                    strncpy(g_file_hash_cache.filename, lfs_file->sha_filename, sizeof(g_file_hash_cache.filename) - 1);
                    g_file_hash_cache.filename[sizeof(g_file_hash_cache.filename) - 1] = '\0';
                    memcpy(&g_file_hash_cache.hash, &result, sizeof(sha256_result_t));
                    g_file_hash_cache.valid = true;

                    printf("fs_custom: Closed file '%s' (%zu/%zu bytes), SHA-256 cached\n",
                           lfs_file->sha_filename, lfs_file->bytes_read, lfs_file->file_size);
                } else {
                    printf("fs_custom: Closed file '%s' (%zu/%zu bytes, aborted), SHA-256 released\n",
                           lfs_file->sha_filename, lfs_file->bytes_read, lfs_file->file_size);
                }
            } else {
                printf("fs_custom: Closed file (%zu/%zu bytes transferred)\n",
                       lfs_file->bytes_read, lfs_file->file_size);
            }
        }
        lfs_file->is_open = false;
    }

    // Chunk structs use the static s_chunk_file — never heap-allocated, never freed.
    if (!lfs_file->is_chunk) {
        free(lfs_file);
    }
    file->pextension = NULL;
}
