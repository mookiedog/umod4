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
extern void generate_api_reformat_filesystem_json(char* buffer, size_t size);
extern void generate_api_sd_info_json(char* buffer, size_t size);
extern void generate_api_image_store_json(char* buffer, size_t size);
extern void generate_api_image_store_selector_json(char* buffer, size_t size);
extern void generate_api_image_store_scan_json(char* buffer, size_t size);
extern void generate_api_wifi_scan_start_json(char* buffer, size_t size);
extern void generate_api_wifi_scan_json(char* buffer, size_t size);

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
    char* data;               // In-memory data (for API responses)
    size_t file_size;         // Total file size
    size_t bytes_read;        // Bytes read so far
    bool is_open;             // Track if file is open
    bool is_api;              // True if this is an API response (in-memory)
    pico_sha256_state_t sha_state;  // SHA-256 state for calculating hash during transfer
    bool sha_enabled;         // True if SHA-256 calculation is in progress
    char sha_filename[64];    // Filename for this transfer (for caching)
    char header_buf[200];     // HTTP response headers (for HEADER_INCLUDED streaming)
    size_t header_len;        // Bytes in header_buf (0 = not used)
    size_t header_sent;       // Bytes of header already sent
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
        // info and sha256 need 512 bytes, list and sd-info need 8KB for ~100 files
        // image-store/scan needs 4KB for up to ~127 slot entries
        // image-store and image-store/selector need 1KB for the selector JSON
        size_t api_buffer_size = (strcmp(api_name, "list") == 0 ||
                                   strcmp(api_name, "sd-info") == 0) ? 8192 :
                                  (strcmp(api_name, "image-store/scan") == 0 ||
                                   strcmp(api_name, "wifi-scan") == 0) ? 4096 :
                                  (strcmp(api_name, "image-store") == 0 ||
                                   strcmp(api_name, "image-store/selector") == 0) ? 1024 : 512;

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

        struct lfs_custom_file* api_file = (struct lfs_custom_file*)malloc(sizeof(struct lfs_custom_file));
        if (!api_file) {
            return 0;
        }

        api_file->data = (char*)malloc(256);
        if (!api_file->data) {
            free(api_file);
            return 0;
        }

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
        if (lfs_file->is_api) {
            // API response - free the data buffer
            free(lfs_file->data);
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
