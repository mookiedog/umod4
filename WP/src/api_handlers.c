#include "api_handlers.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/sha256.h"
#include "FreeRTOS.h"
#include "task.h"
#include "lfs.h"
#include "file_io_task.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

// Forward declaration for C++ class (opaque pointer in C)
#ifdef __cplusplus
class SdCardBase;
#else
typedef struct SdCardBase SdCardBase;
#endif

// External references
extern const char* get_wp_version(void);
extern bool wifi_is_connected(void);
extern const char* wifi_get_ssid(void);
extern lfs_t lfs;
extern bool lfs_mounted;
extern uint16_t ecuLiveLog[256];
extern SdCardBase* sdCard;

// Function to check if card is inserted (implemented in C++ code)
extern bool sdcard_is_inserted(SdCardBase* card);

// Access to SHA-256 cache from fs_custom.c
typedef struct {
    char filename[64];
    sha256_result_t hash;
    bool valid;
} file_hash_cache_t;

extern file_hash_cache_t g_file_hash_cache;

// Buffer for JSON responses (reused across requests to save stack space)
static char json_response_buffer[512];

// Larger buffer for file listing (can hold ~100 log files at ~60 bytes each)
static char file_list_buffer[8192];

/**
 * Build JSON response for /api/info
 * Called from fs_open_custom()
 */
void generate_api_info_json(char* buffer, size_t size)
{
    // Get device MAC address
    char mac_str[18] = "unknown";
    uint8_t mac[6];
    cyw43_hal_get_mac(CYW43_HAL_MAC_WLAN0, mac);
    snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // Get uptime in seconds
    uint32_t uptime_seconds = xTaskGetTickCount() / configTICK_RATE_HZ;

    // Get WiFi status
    const char* wifi_status = wifi_is_connected() ? "true" : "false";
    const char* wifi_ssid = wifi_get_ssid();
    if (!wifi_ssid) wifi_ssid = "";

    // Determine filesystem status
    const char* fs_status;
    const char* fs_message;
    if (!sdCard) {
        fs_status = "no_card";
        fs_message = "SD card not detected or initialization failed";
    } else if (!sdcard_is_inserted(sdCard)) {
        fs_status = "no_card";
        fs_message = "SD card is not inserted";
    } else if (!lfs_mounted) {
        fs_status = "mount_failed";
        fs_message = "SD card present but filesystem failed to mount";
    } else {
        fs_status = "ok";
        fs_message = "Filesystem mounted and ready";
    }

    // Build JSON response
    snprintf(buffer, size,
             "{\n"
             "  \"device_mac\": \"%s\",\n"
             "  \"wp_version\": \"%s\",\n"
             "  \"uptime_seconds\": %lu,\n"
             "  \"wifi_connected\": %s,\n"
             "  \"wifi_ssid\": \"%s\",\n"
             "  \"fs_status\": \"%s\",\n"
             "  \"fs_message\": \"%s\",\n"
             "  \"ecu_0x54\": %u,\n"
             "  \"ecu_0x55\": %u,\n"
             "  \"ecu_0x56\": %u,\n"
             "  \"ecu_0x57\": %u,\n"
             "  \"ecu_0x58\": %u,\n"
             "  \"ecu_0x59\": %u,\n"
             "  \"ecu_0x5a\": %u\n"
             "}",
             mac_str,
             get_wp_version(),
             (unsigned long)uptime_seconds,
             wifi_status,
             wifi_ssid,
             fs_status,
             fs_message,
             ecuLiveLog[0x54],
             ecuLiveLog[0x55],
             ecuLiveLog[0x56],
             ecuLiveLog[0x57],
             ecuLiveLog[0x58],
             ecuLiveLog[0x59],
             ecuLiveLog[0x5a]);
}

/**
 * Build JSON response for /api/list
 * Called from fs_open_custom()
 */
void generate_api_list_json(char* buffer, size_t size)
{
    // Check if filesystem is mounted
    if (!lfs_mounted) {
        snprintf(buffer, size,
                 "{\"error\": \"Filesystem not mounted\", \"files\": []}");
        return;
    }

    // Start JSON array
    char* ptr = buffer;
    size_t remaining = size;
    int len = snprintf(ptr, remaining, "{\"files\": [");
    ptr += len;
    remaining -= len;

    // Scan SD card root directory for all files
    lfs_dir_t dir;
    int err = lfs_dir_open(&lfs, &dir, "/");
    if (err == 0) {
        struct lfs_info info;
        bool first = true;
        int file_count = 0;

        while (lfs_dir_read(&lfs, &dir, &info) > 0) {
            // Skip directories and non-regular files
            if (info.type != LFS_TYPE_REG) {
                continue;
            }

            // Add comma separator (except for first entry)
            if (!first) {
                if (remaining < 2) break;  // Not enough space
                *ptr++ = ',';
                remaining--;
            }
            first = false;
            file_count++;

            // Add file entry: {"filename": "...", "size": ...}
            // Reserve 5 bytes for closing "\n]}" to ensure valid JSON
            if (remaining < 100) {
                // Not enough space for another entry + closing, stop here
                printf("api_list: Buffer nearly full after %d files, stopping\n", file_count);
                break;
            }

            len = snprintf(ptr, remaining,
                          "\n  {\"filename\": \"%s\", \"size\": %lu}",
                          info.name, (unsigned long)info.size);

            if (len >= (int)remaining) {
                // snprintf truncated - should not happen due to check above
                printf("api_list: Entry truncated after %d files\n", file_count);
                break;
            }

            ptr += len;
            remaining -= len;
        }

        lfs_dir_close(&lfs, &dir);
    } else {
        printf("api_list: Failed to open root directory: %d\n", err);
    }

    // Close JSON array - we reserved space for this above
    len = snprintf(ptr, remaining, "\n]}");
    if (len >= (int)remaining) {
        // Should never happen due to our reservation, but handle it anyway
        printf("api_list: WARNING - Not enough space for closing bracket\n");
        // Ensure valid JSON by overwriting end of buffer
        if (size >= 4) {
            strcpy(buffer + size - 4, "\n]}");
        }
    }
}

/**
 * Build JSON response for /api/delete/<filename>
 * Called from fs_open_custom()
 */
void generate_api_delete_json(char* buffer, size_t size, const char* filename)
{
    // Check if filesystem is mounted
    if (!lfs_mounted) {
        snprintf(buffer, size,
                 "{\"success\": false, \"error\": \"Filesystem not mounted\"}");
        printf("api_delete: Filesystem not mounted\n");
        return;
    }

    // Validate filename (prevent path traversal attacks)
    if (strchr(filename, '/') || strchr(filename, '\\') ||
        strcmp(filename, ".") == 0 || strcmp(filename, "..") == 0) {
        snprintf(buffer, size,
                 "{\"success\": false, \"error\": \"Invalid filename\"}");
        printf("api_delete: Invalid filename '%s'\n", filename);
        return;
    }

    // Request deletion via async task (with 5 second timeout)
    // This ensures deletion happens in task context, not HTTP callback context
    file_delete_result_t result;
    bool success = file_delete_request_async(filename, 5000, &result);

    if (!success) {
        snprintf(buffer, size,
                 "{\"success\": false, \"error\": \"Delete request timed out\"}");
        printf("api_delete: Delete request timed out for '%s'\n", filename);
        return;
    }

    // Return result from async task
    if (result.success) {
        snprintf(buffer, size,
                 "{\"success\": true, \"filename\": \"%s\"}",
                 result.filename);
    } else {
        snprintf(buffer, size,
                 "{\"success\": false, \"error\": \"%s\"}",
                 result.error_message);
    }
}

/**
 * Build JSON response for /api/sha256/<filename>
 * Called from fs_open_custom()
 */
void generate_api_sha256_json(char* buffer, size_t size, const char* filename)
{
    // Check if we have a cached hash for this file
    if (!g_file_hash_cache.valid || strcmp(g_file_hash_cache.filename, filename) != 0) {
        snprintf(buffer, size,
                 "{\"error\": \"No hash available for '%s' (file must be downloaded first)\"}",
                 filename);
        printf("api_sha256: No cached hash for '%s'\n", filename);
        return;
    }

    // Build SHA-256 hex string
    char sha256_hex[SHA256_RESULT_BYTES * 2 + 1];
    for (int i = 0; i < SHA256_RESULT_BYTES; i++) {
        snprintf(sha256_hex + (i * 2), 3, "%02x", g_file_hash_cache.hash.bytes[i]);
    }
    sha256_hex[SHA256_RESULT_BYTES * 2] = '\0';

    // Build JSON response
    snprintf(buffer, size,
             "{\n"
             "  \"filename\": \"%s\",\n"
             "  \"sha256\": \"%s\"\n"
             "}",
             filename,
             sha256_hex);

    printf("api_sha256: Returned hash for '%s': %.16s...\n", filename, sha256_hex);
}

// No CGI handlers needed - APIs are served as virtual files via fs_open_custom()
void api_handlers_register(void)
{
    // Nothing to register - APIs handled by custom filesystem
}
