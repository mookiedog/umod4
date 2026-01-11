#include "api_handlers.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "FreeRTOS.h"
#include "task.h"
#include "lfs.h"
#include <stdio.h>
#include <string.h>

// External references
extern const char* get_wp_version(void);
extern bool wifi_is_connected(void);
extern const char* wifi_get_ssid(void);
extern lfs_t lfs;
extern bool lfs_mounted;
extern uint16_t ecuLiveLog[256];

// Buffer for JSON responses (reused across requests to save stack space)
static char json_response_buffer[512];

// Larger buffer for file listing (can hold ~20-30 log files)
static char file_list_buffer[2048];

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

    // Build JSON response
    snprintf(buffer, size,
             "{\n"
             "  \"device_mac\": \"%s\",\n"
             "  \"wp_version\": \"%s\",\n"
             "  \"uptime_seconds\": %lu,\n"
             "  \"wifi_connected\": %s,\n"
             "  \"wifi_ssid\": \"%s\",\n"
             "  \"ecu_0x54\": %u,\n"
             "  \"ecu_0x55\": %u,\n"
             "  \"ecu_0x56\": %u,\n"
             "  \"ecu_0x57\": %u\n"
             "}",
             mac_str,
             get_wp_version(),
             (unsigned long)uptime_seconds,
             wifi_status,
             wifi_ssid,
             ecuLiveLog[0x54],
             ecuLiveLog[0x55],
             ecuLiveLog[0x56],
             ecuLiveLog[0x57]);
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

    // Scan SD card root directory for .um4 files
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

            // Only list .um4 files
            size_t name_len = strlen(info.name);
            if (name_len < 5 || strcmp(info.name + name_len - 4, ".um4") != 0) {
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
            len = snprintf(ptr, remaining,
                          "\n  {\"filename\": \"%s\", \"size\": %lu}",
                          info.name, (unsigned long)info.size);

            if (len >= (int)remaining) {
                // Buffer full, truncate
                printf("api_list: Buffer full after %d files\n", file_count);
                break;
            }

            ptr += len;
            remaining -= len;
        }

        lfs_dir_close(&lfs, &dir);
    } else {
        printf("api_list: Failed to open root directory: %d\n", err);
    }

    // Close JSON array
    if (remaining >= 5) {
        len = snprintf(ptr, remaining, "\n]}");
        ptr += len;
    } else {
        // Emergency fallback if buffer is full
        strcpy(buffer + size - 4, "]}");
    }
}

// No CGI handlers needed - APIs are served as virtual files via fs_open_custom()
void api_handlers_register(void)
{
    // Nothing to register - APIs handled by custom filesystem
}
