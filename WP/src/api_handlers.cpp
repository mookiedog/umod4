#include "api_handlers.h"
#include "log_meta.h"
#include "WiFiManager.h"
#include "bsonlib.h"
#include "Swd.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/sha256.h"
#include "FreeRTOS.h"
#include "task.h"
#include "lfsMgr.h"
#include "file_io_task.h"
#include "ota_flash_task.h"
#include "FlashWp.h"
#include "FlashConfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// External references from main.cpp
extern const char* get_wp_version(void);
extern bool wifi_is_connected(void);
extern bool wifi_is_ap_mode(void);
extern const char* wifi_get_ssid(void);
extern const char* wifi_get_ap_ssid(void);
extern int32_t wifi_get_rssi(void);
extern uint16_t ecuLiveLog[256];
extern const char* get_device_name(void);
extern flash_config_t g_flash_config;
extern const char* get_current_log_name(void);
extern uint32_t get_last_ecu_data_us(void);
extern uint32_t get_last_crank_event_us(void);

// Access to SHA-256 cache from fs_custom.c
typedef struct {
    char filename[64];
    sha256_result_t hash;
    bool valid;
} file_hash_cache_t;

extern file_hash_cache_t g_file_hash_cache;

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
    bool is_connected = wifi_is_connected();
    bool is_ap = wifi_is_ap_mode();
    const char* wifi_mode = is_connected ? "sta" : (is_ap ? "ap" : "disconnected");
    const char* wifi_ssid = wifi_get_ssid();
    if (!wifi_ssid) wifi_ssid = "";
    const char* ap_ssid = wifi_get_ap_ssid();
    if (!ap_ssid) ap_ssid = "";
    int32_t wifi_rssi = wifi_get_rssi();

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
    // Note: wp_version is embedded as a JSON object, not a string
    snprintf(buffer, size,
             "{\n"
             "  \"device_name\": \"%s\",\n"
             "  \"device_mac\": \"%s\",\n"
             "  \"wp_version\": %s,\n"
             "  \"uptime_seconds\": %lu,\n"
             "  \"wifi_connected\": %s,\n"
             "  \"wifi_mode\": \"%s\",\n"
             "  \"wifi_rssi\": %ld,\n"
             "  \"wifi_ssid\": \"%s\",\n"
             "  \"ap_ssid\": \"%s\",\n"
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
             get_device_name(),
             mac_str,
             get_wp_version(),
             (unsigned long)uptime_seconds,
             is_connected ? "true" : "false",
             wifi_mode,
             (long)wifi_rssi,
             wifi_ssid,
             ap_ssid,
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

    uint32_t remaining_words = uxTaskGetStackHighWaterMark(NULL);
    if (remaining_words < 256) {
        printf("*** WARNING: tcpip stack only has %lu words remaining\n", remaining_words);
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

/**
 * Build JSON response for /api/reflash/ep/<filename>
 * Called from fs_open_custom()
 *
 * NOTE: This is a long-running operation (10-30 seconds).
 * The actual flashing is performed in the FileIO task context to avoid
 * stack/context issues in the HTTP callback.
 */
void generate_api_reflash_ep_json(char* buffer, size_t size, const char* filename)
{
    // Validate filename
    if (!filename || filename[0] == '\0') {
        snprintf(buffer, size,
                 "{\"success\": false, \"error\": \"No filename specified\"}");
        printf("api_reflash_ep: No filename specified\n");
        return;
    }

    // Check for path traversal attempts
    if (strchr(filename, '/') || strchr(filename, '\\') ||
        strcmp(filename, ".") == 0 || strcmp(filename, "..") == 0) {
        snprintf(buffer, size,
                 "{\"success\": false, \"error\": \"Invalid filename\"}");
        printf("api_reflash_ep: Invalid filename '%s'\n", filename);
        return;
    }

    // Check if filesystem is mounted
    if (!lfs_mounted) {
        snprintf(buffer, size,
                 "{\"success\": false, \"error\": \"Filesystem not mounted\"}");
        printf("api_reflash_ep: Filesystem not mounted\n");
        return;
    }

    // Build full path (files are in root directory)
    char filepath[80];
    snprintf(filepath, sizeof(filepath), "/%s", filename);

    printf("api_reflash_ep: Requesting EP reflash with '%s'\n", filepath);

    // Execute reflash via FileIO task (runs in proper FreeRTOS task context)
    // Use 120 second timeout for long-running SWD operations
    file_io_result_t result;
    bool ok = file_io_reflash_ep(filepath, true, 120000, &result);

    if (!ok) {
        snprintf(buffer, size,
                 "{\"success\": false, \"error\": \"Reflash request timed out or failed to queue\"}");
        printf("api_reflash_ep: Request failed to execute\n");
        return;
    }

    if (result.success) {
        snprintf(buffer, size,
                 "{\"success\": true, \"message\": \"EP reflash completed successfully\"}");
        printf("api_reflash_ep: Success!\n");
    } else {
        snprintf(buffer, size,
                 "{\"success\": false, \"error\": \"%s\"}",
                 result.error_message);
        printf("api_reflash_ep: Failed: %s\n", result.error_message);
    }
}

/**
 * Build JSON response for /api/reflash/wp/<filename>
 * Called from fs_open_custom()
 *
 * This function validates the UF2 file and queues the OTA request to the
 * dedicated OTA flash task. It returns immediately with an acknowledgment -
 * the server should NOT wait for more feedback.
 *
 * The OTA task will:
 * 1. Shut down the logger and WiFi
 * 2. Flash the UF2 to the inactive partition
 * 3. Reboot to the new firmware (TBYB)
 *
 * The new firmware must call rom_explicit_buy() within 16.7 seconds of boot
 * to commit the update, otherwise it will revert to the previous partition.
 */
void generate_api_reflash_wp_json(char* buffer, size_t size, const char* filename)
{
    // Validate filename
    if (!filename || filename[0] == '\0') {
        snprintf(buffer, size,
                 "{\"success\": false, \"error\": \"No filename specified\"}");
        printf("api_reflash_wp: No filename specified\n");
        return;
    }

    // Check for path traversal attempts
    if (strchr(filename, '/') || strchr(filename, '\\') ||
        strcmp(filename, ".") == 0 || strcmp(filename, "..") == 0) {
        snprintf(buffer, size,
                 "{\"success\": false, \"error\": \"Invalid filename\"}");
        printf("api_reflash_wp: Invalid filename '%s'\n", filename);
        return;
    }

    // Check if OTA is already in progress
    if (ota_flash_in_progress()) {
        snprintf(buffer, size,
                 "{\"success\": false, \"error\": \"OTA update already in progress\"}");
        printf("api_reflash_wp: OTA already in progress\n");
        return;
    }

    // Build full path (files are in root directory)
    char filepath[80];
    snprintf(filepath, sizeof(filepath), "/%s", filename);

    printf("api_reflash_wp: Requesting WP self-reflash with '%s'\n", filepath);

    // Queue the OTA request - this validates the file exists and queues the request
    // It returns immediately, it does NOT wait for the flash to complete
    bool queued = ota_flash_request(filepath);

    if (queued) {
        // Success - OTA task will take over from here
        // Server should return to normal operation and wait for device to check in
        snprintf(buffer, size,
                 "{\"success\": true, \"message\": \"OTA update starting. Device will reboot.\"}");
        printf("api_reflash_wp: OTA request queued successfully\n");
    } else {
        // Failed to queue - could be file not found, filesystem not mounted, etc.
        snprintf(buffer, size,
                 "{\"success\": false, \"error\": \"Failed to start OTA update (file not found or filesystem error)\"}");
        printf("api_reflash_wp: Failed to queue OTA request\n");
    }
}

/**
 * Build JSON response for /api/system
 * Returns the SYSTEM_JSON string containing build metadata.
 * Called from fs_open_custom()
 */
extern const char* SYSTEM_JSON;

void generate_api_system_json(char* buffer, size_t size)
{
    // SYSTEM_JSON is an escaped JSON string like: {\"GH\":\"abc123\",\"BT\":\"2024-01-21 10:30:00\"}
    // We need to unescape it for the response
    snprintf(buffer, size, "%s", SYSTEM_JSON);
}

/**
 * Build JSON response for /api/eprom-info
 * Returns the image_selector contents captured from the EP log stream at boot.
 */

// Getters defined in main.cpp
extern const char* get_ep_imgsel_str(void);
extern bool        get_ep_imgsel_complete(void);

void generate_api_eprom_info_json(char* buffer, size_t size)
{
    const char* imgsel  = get_ep_imgsel_str();
    bool        ready   = get_ep_imgsel_complete();

    uint32_t age_ms = (time_us_32() - get_last_ecu_data_us()) / 1000;
    bool ecu_alive  = (age_ms < 200);

    // Engine is considered running if a crank event was seen within the last second.
    // g_last_crank_event_us == 0 at startup means never seen → not running.
    uint32_t last_crank = get_last_crank_event_us();
    bool engine_running = (last_crank != 0) &&
                          ((time_us_32() - last_crank) < 1000000u);

    const char* status;
    if (!ready) {
        status = "waiting";
    } else if (strstr(imgsel, "\"code\":\"limp-mode\"")) {
        status = "limp_mode";
    } else {
        status = "ok";
    }

    // image_selector is already a valid JSON array string from EP, embed it directly
    snprintf(buffer, size,
             "{\n"
             "  \"image_selector\": %s,\n"
             "  \"status\": \"%s\",\n"
             "  \"ecu_alive\": %s,\n"
             "  \"engine_running\": %s\n"
             "}",
             ready ? imgsel : "[]",
             status,
             ecu_alive ? "true" : "false",
             engine_running ? "true" : "false");
}

/**
 * Build JSON response for /api/reboot
 * Queues a clean system reboot through the OTA task shutdown sequence
 * (logger shutdown, WiFi shutdown, watchdog reset). Returns immediately;
 * the reboot happens ~200ms later after the HTTP response is flushed.
 */
void generate_api_reboot_json(char* buffer, size_t size)
{
    if (ota_flash_in_progress()) {
        snprintf(buffer, size,
                 "{\"success\": false, \"error\": \"OTA update in progress, cannot reboot\"}");
        printf("api_reboot: Rejected - OTA in progress\n");
        return;
    }

    bool queued = ota_reboot_request();
    if (queued) {
        snprintf(buffer, size, "{\"success\": true, \"message\": \"Rebooting...\"}");
        printf("api_reboot: Reboot queued\n");
    } else {
        snprintf(buffer, size,
                 "{\"success\": false, \"error\": \"Failed to queue reboot\"}");
        printf("api_reboot: Failed to queue reboot\n");
    }
}

/**
 * Build JSON response for GET /api/reformat-filesystem
 * Takes the LFS mutex, zeroes the first 64 raw SD sectors (LFS blocks 0 and 1,
 * both superblock copies), syncs, then schedules a reboot. On next boot,
 * mount failure triggers the automatic reformat path.
 */
void generate_api_reformat_filesystem_json(char* buffer, size_t size)
{
    if (!sdCard || !sdCard->operational()) {
        snprintf(buffer, size, "{\"success\": false, \"error\": \"SD card not available\"}");
        return;
    }

    // Take the LFS mutex so no LittleFS operation can be in progress while we
    // destroy the superblock. Never released - we're rebooting.
    lfs_lock();

    // Destroy LittleFS superblock by zeroing the first 64 raw sectors.
    // LFS block size = 16 KB = 32 sectors; blocks 0 and 1 hold both superblock copies.
    static const uint8_t zeros[512] = {0};
    printf("reformat_filesystem: destroying LittleFS superblock (sectors 0-63)\n");
    for (uint32_t sector = 0; sector < 64; sector++) {
        SdErr_t err = sdCard->writeSectors(sector, 1, zeros);
        if (err != SD_ERR_NOERR) {
            snprintf(buffer, size,
                     "{\"success\": false, \"error\": \"SD write failed at sector %lu\"}", sector);
            printf("reformat_filesystem: writeSectors(%lu) failed: %ld\n", sector, err);
            return;
        }
    }
    sdCard->sync();
    printf("reformat_filesystem: superblock destroyed, requesting reboot\n");

    bool queued = ota_reboot_request();
    if (queued) {
        snprintf(buffer, size, "{\"success\": true}");
    } else {
        snprintf(buffer, size, "{\"success\": false, \"error\": \"Failed to queue reboot\"}");
    }
}

/**
 * Build JSON response for GET /api/config
 * Returns current config. Passwords are redacted as "***".
 */
void generate_api_config_json(char* buffer, size_t size)
{
    const flash_config_t* cfg = &g_flash_config;
    bool is_ap = wifi_is_ap_mode();

    snprintf(buffer, size,
             "{\n"
             "  \"device_name\": \"%s\",\n"
             "  \"wifi_ssid\": \"%s\",\n"
             "  \"wifi_password\": \"***\",\n"
             "  \"server_host\": \"%s\",\n"
             "  \"server_port\": %u,\n"
             "  \"ap_ssid\": \"%s\",\n"
             "  \"ap_password\": \"***\",\n"
             "  \"wifi_mode\": \"%s\"\n"
             "}",
             cfg->device_name,
             cfg->wifi_ssid,
             cfg->server_host,
             (unsigned)cfg->server_port,
             cfg->ap_ssid,
             is_ap ? "ap" : "sta");
}

/**
 * Build JSON response for GET /api/wifi-reset
 * Clears wifi_ssid and wifi_password from flash config, saves, and reboots.
 * Device will boot into AP mode on next start.
 */
void generate_api_wifi_reset_json(char* buffer, size_t size)
{
    if (ota_flash_in_progress()) {
        snprintf(buffer, size,
                 "{\"success\": false, \"error\": \"OTA update in progress, cannot reset WiFi\"}");
        printf("api_wifi_reset: Rejected - OTA in progress\n");
        return;
    }

    // Clear credentials in RAM; let OTA task save to flash after WiFi is down.
    // (Same reason as /api/config POST: flash ops from the lwIP thread corrupt CYW43 state.)
    g_flash_config.wifi_ssid[0] = '\0';
    g_flash_config.wifi_password[0] = '\0';

    // Include ap_ssid so the browser can show specific reconnect instructions
    const char* ap_ssid = wifi_get_ap_ssid();
    if (!ap_ssid) ap_ssid = "";

    bool queued = ota_reboot_with_config_save_request();
    if (queued) {
        snprintf(buffer, size,
                 "{\"success\": true, \"ap_ssid\": \"%s\"}", ap_ssid);
        printf("api_wifi_reset: WiFi credentials cleared in RAM, reboot+save queued\n");
    } else {
        snprintf(buffer, size,
                 "{\"success\": false, \"error\": \"Failed to queue reboot\"}");
        printf("api_wifi_reset: Reboot queue failed\n");
    }
}

/**
 * Build JSON response for GET /api/ping.
 * Used by browser JS to poll for device-back-online after a reboot.
 */
void generate_api_ping_json(char* buffer, size_t size)
{
    snprintf(buffer, size, "{\"ok\":true}");
}

/**
 * Build JSON response for /api/sd-info
 * Returns filesystem capacity, space used by files, and a full file listing
 * indicating which file is currently open for logging.
 * Called from fs_open_custom()
 */
extern char g_device_name[64];

void generate_api_sd_info_json(char* buffer, size_t size)
{
    if (!lfs_mounted) {
        snprintf(buffer, size,
                 "{\"error\": \"Filesystem not mounted\", \"device_name\": \"%s\", \"files\": []}",
                 g_device_name);
        return;
    }

    // Get filesystem geometry via lfs_fs_stat (fast, cached by LittleFS)
    struct lfs_fsinfo fsinfo;
    uint32_t total_mb = 0;
    if (lfs_fs_stat(&lfs, &fsinfo) == 0) {
        uint64_t total_bytes = (uint64_t)fsinfo.block_count * fsinfo.block_size;
        total_mb = (uint32_t)(total_bytes / (1024 * 1024));
    }

    // Pass 1: sum file sizes to compute used space
    uint64_t used_bytes = 0;
    lfs_dir_t dir;
    if (lfs_dir_open(&lfs, &dir, "/") == 0) {
        struct lfs_info info;
        while (lfs_dir_read(&lfs, &dir, &info) > 0) {
            if (info.type == LFS_TYPE_REG) used_bytes += info.size;
        }
        lfs_dir_close(&lfs, &dir);
    }
    uint32_t used_mb = (uint32_t)(used_bytes / (1024 * 1024));

    // Get name of the currently-open log file (empty string if none)
    const char* open_name = get_current_log_name();

    // Build JSON header
    char* ptr = buffer;
    size_t remaining = size;
    int len = snprintf(ptr, remaining,
                       "{\n"
                       "  \"device_name\": \"%s\",\n"
                       "  \"total_mb\": %lu,\n"
                       "  \"used_mb\": %lu,\n"
                       "  \"open_file\": \"%s\",\n"
                       "  \"files\": [",
                       g_device_name,
                       (unsigned long)total_mb,
                       (unsigned long)used_mb,
                       open_name);
    ptr += len;
    remaining -= len;

    // Pass 2: scan directory to build file entries
    if (lfs_dir_open(&lfs, &dir, "/") == 0) {
        struct lfs_info info;
        bool first = true;
        while (lfs_dir_read(&lfs, &dir, &info) > 0) {
            if (info.type != LFS_TYPE_REG) continue;
            if (remaining < 120) {
                printf("api_sd_info: Buffer nearly full, truncating file list\n");
                break;
            }
            if (!first) {
                *ptr++ = ',';
                remaining--;
            }
            first = false;
            bool is_open = (open_name[0] != '\0' &&
                            strcmp(info.name, open_name) == 0);
            len = snprintf(ptr, remaining,
                           "\n    {\"name\": \"%s\", \"size\": %lu, \"open\": %s}",
                           info.name,
                           (unsigned long)info.size,
                           is_open ? "true" : "false");
            ptr += len;
            remaining -= len;
        }
        lfs_dir_close(&lfs, &dir);
    }

    snprintf(ptr, remaining, "\n  ]\n}");
}

// No CGI handlers needed - APIs are served as virtual files via fs_open_custom()
void api_handlers_register(void)
{
    // Nothing to register - APIs handled by custom filesystem
}

// ---------------------------------------------------------------------------
// ECU Live Stream configuration

int16_t g_ecu_live_items[ECU_LIVE_ITEMS_MAX] = {
    // Battery Voltage, Manifold Pressure, Coolant Temp, Air Temp, Trim Pot 1, Trim Pot 2
    0x66, 0x62, 0x64, 0x65, 0x56, 0x57, -1, -1, -1, -1
};

void ecu_live_config_load(void)
{
    if (!lfs_mounted) {
        printf("ecu_live_config_load: LittleFS not mounted, keeping defaults\n");
        return;
    }

    lfs_file_t file;
    int err = lfs_file_open(&lfs, &file, "/ecu_live.json", LFS_O_RDONLY);
    if (err != 0) {
        printf("ecu_live_config_load: /ecu_live.json not found, keeping defaults\n");
        return;
    }

    char buf[128];
    lfs_ssize_t n = lfs_file_read(&lfs, &file, buf, (lfs_size_t)(sizeof(buf) - 1));
    lfs_file_close(&lfs, &file);

    if (n <= 0) {
        printf("ecu_live_config_load: empty file, keeping defaults\n");
        return;
    }
    buf[n] = '\0';

    // Parse {"items":[v0,v1,...,v9]}
    char* p = strstr(buf, "\"items\"");
    if (!p) {
        printf("ecu_live_config_load: no 'items' key, keeping defaults\n");
        return;
    }
    p = strchr(p, '[');
    if (!p) {
        printf("ecu_live_config_load: no '[' found, keeping defaults\n");
        return;
    }
    p++;

    int16_t items[ECU_LIVE_ITEMS_MAX];
    for (int i = 0; i < ECU_LIVE_ITEMS_MAX; i++) {
        items[i] = -1;
    }

    for (int i = 0; i < ECU_LIVE_ITEMS_MAX; i++) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == ']' || *p == '\0') break;
        char* endp;
        long val = strtol(p, &endp, 10);
        if (endp == p) break;
        if (val >= -1 && val <= 255) {
            items[i] = (int8_t)val;
        }
        p = endp;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == ',') p++;
    }

    memcpy(g_ecu_live_items, items, sizeof(g_ecu_live_items));
    printf("ecu_live_config_load: loaded from /ecu_live.json\n");
}

void generate_api_ecu_live_config_json(char* buffer, size_t size)
{
    int len = snprintf(buffer, size, "{\"items\":[");
    for (int i = 0; i < ECU_LIVE_ITEMS_MAX; i++) {
        if (i > 0) len += snprintf(buffer + len, size - len, ",");
        len += snprintf(buffer + len, size - len, "%d", (int)g_ecu_live_items[i]);
    }
    snprintf(buffer + len, size - len, "]}");
}

void generate_api_ecu_live_data_json(char* buffer, size_t size)
{
    // ECU is considered alive if data arrived within the last 200ms.
    // At >100 events/second when running, any gap longer than that means ECU is off.
    uint32_t age_us = time_us_32() - get_last_ecu_data_us();
    uint32_t age_ms = age_us / 1000;
    bool ecu_alive = (age_ms < 200);

    int len = snprintf(buffer, size,
                       "{\"ecu_alive\":%s,\"data_age_ms\":%lu,\"items\":[",
                       ecu_alive ? "true" : "false",
                       (unsigned long)age_ms);
    for (int i = 0; i < ECU_LIVE_ITEMS_MAX; i++) {
        if (i > 0) len += snprintf(buffer + len, size - len, ",");
        int16_t logid = g_ecu_live_items[i];
        if (logid < 0) {
            len += snprintf(buffer + len, size - len,
                            "{\"slot\":%d,\"logid\":-1,\"name\":null,\"units\":null,\"raw\":null}",
                            i);
        } else {
            uint8_t id = (uint8_t)logid;
            const log_id_meta_t* m = &g_log_id_meta[id];
            const char* name  = m->name  ? m->name  : "";
            const char* units = m->units ? m->units : "";
            len += snprintf(buffer + len, size - len,
                            "{\"slot\":%d,\"logid\":%d,\"name\":\"%s\",\"units\":\"%s\",\"raw\":%u}",
                            i, (int)id, name, units,
                            (unsigned)ecuLiveLog[id]);
        }
    }
    snprintf(buffer + len, size - len, "]}");
}

void generate_api_ecu_live_meta_json(char* buffer, size_t size)
{
    int len = snprintf(buffer, size, "{\"channels\":[");
    bool first = true;
    for (int i = 0; i < 256; i++) {
        const log_id_meta_t* m = &g_log_id_meta[i];
        if (!m->display || !m->name) continue;
        const char* units = m->units ? m->units : "";
        if (!first) len += snprintf(buffer + len, size - len, ",");
        first = false;
        len += snprintf(buffer + len, size - len,
                        "{\"id\":%d,\"name\":\"%s\",\"units\":\"%s\"}",
                        i, m->name, units);
    }
    snprintf(buffer + len, size - len, "]}");
}

// Cached selector JSON, built by image_store_init() and re-built after selector writes.
static char g_image_store_json[1024];
static bool g_selector_cache_valid = false;

// Cached scan JSON, built on-demand and invalidated after any slot write.
static char g_imgstore_scan_json[4096];
static bool g_scan_cache_valid = false;

/**
 * Read the image_selector BSON doc from EP flash slot 0 via SWD and cache the result.
 * Must be called from main() after the Swd object is created, before the scheduler
 * hands control to the tcpip thread.  EP flash is static (written only during reflash),
 * so reading it once is sufficient.
 */
void image_store_init(void)
{
    g_selector_cache_valid = false;
    if (!swd) {
        snprintf(g_image_store_json, sizeof(g_image_store_json),
                 "{\"found\":false,\"error\":\"SWD not available\"}");
        return;
    }

    if (!swd->connect_target(0, false)) {
        snprintf(g_image_store_json, sizeof(g_image_store_json),
                 "{\"found\":false,\"error\":\"SWD connect failed\"}");
        return;
    }

    // Slot 0 of the IMAGE_STORE partition holds the image_selector BSON doc.
    const uint32_t IMAGE_STORE_ADDR = 0x10200000;
    const size_t   READ_BUF_SIZE    = 4096;
    const uint32_t CHUNK            = 1024;  // max per read_target_mem call

    uint8_t* doc = (uint8_t*)malloc(READ_BUF_SIZE);
    if (!doc) {
        snprintf(g_image_store_json, sizeof(g_image_store_json),
                 "{\"found\":false,\"error\":\"OOM\"}");
        return;
    }
    // Guard: ensure bsonlib's strlen() cannot walk past this buffer even on malformed data
    doc[READ_BUF_SIZE - 1] = 0;

    uint32_t header = 0;
    if (!swd->read_target_mem(IMAGE_STORE_ADDR, &header, 4)) {
        free(doc);
        snprintf(g_image_store_json, sizeof(g_image_store_json),
                 "{\"found\":false,\"error\":\"SWD read failed\"}");
        return;
    }
    uint32_t docSize = Bson::read_unaligned_uint32((const uint8_t*)&header);
    if (docSize < 5 || docSize >= READ_BUF_SIZE) {
        free(doc);
        snprintf(g_image_store_json, sizeof(g_image_store_json),
                 "{\"found\":false,\"error\":\"invalid image_selector size\"}");
        return;
    }

    uint32_t docSizeAligned = (docSize + 3) & ~3u;
    bool read_ok = true;
    for (uint32_t off = 0; off < docSizeAligned; off += CHUNK) {
        uint32_t bytes = docSizeAligned - off;
        if (bytes > CHUNK) bytes = CHUNK;
        if (!swd->read_target_mem(IMAGE_STORE_ADDR + off, (uint32_t*)(doc + off), bytes)) {
            read_ok = false;
            break;
        }
    }
    if (!read_ok) {
        free(doc);
        snprintf(g_image_store_json, sizeof(g_image_store_json),
                 "{\"found\":false,\"error\":\"SWD read failed\"}");
        return;
    }

    element_t images_elem;
    if (!Bson::findElement(doc, "images", images_elem) ||
        images_elem.elementType != BSON_TYPE_ARRAY) {
        free(doc);
        snprintf(g_image_store_json, sizeof(g_image_store_json),
                 "{\"found\":false,\"error\":\"no images array in slot 0\"}");
        return;
    }

    int len = snprintf(g_image_store_json, sizeof(g_image_store_json),
                       "{\"found\":true,\"addr\":\"0x%08X\",\"images\":[", IMAGE_STORE_ADDR);

    bool first = true;
    char indexStr[16];
    for (int32_t index = 0; ; index++) {
        snprintf(indexStr, sizeof(indexStr), "%d", index);
        element_t entry;
        if (!Bson::findElement(images_elem.data, indexStr, entry) ||
            entry.elementType != BSON_TYPE_EMBEDDED_DOC) {
            break;
        }

        element_t codeElem;
        const char* code = "?";
        if (Bson::findElement(entry.data, "code", codeElem) &&
            codeElem.elementType == BSON_TYPE_UTF8) {
            code = (const char*)codeElem.data + 4;
        }

        element_t mapblobElem;
        const char* mapblob = nullptr;
        if (Bson::findElement(entry.data, "mapblob", mapblobElem) &&
            mapblobElem.elementType == BSON_TYPE_UTF8) {
            mapblob = (const char*)mapblobElem.data + 4;
        }

        if (mapblob) {
            len += snprintf(g_image_store_json + len, sizeof(g_image_store_json) - len,
                            "%s{\"code\":\"%s\",\"mapblob\":\"%s\"}",
                            first ? "" : ",", code, mapblob);
        } else {
            len += snprintf(g_image_store_json + len, sizeof(g_image_store_json) - len,
                            "%s{\"code\":\"%s\"}",
                            first ? "" : ",", code);
        }
        first = false;
    }

    snprintf(g_image_store_json + len, sizeof(g_image_store_json) - len, "]}");
    free(doc);
    g_selector_cache_valid = true;
}

/**
 * Serve cached /api/image-store (selector) JSON. Backward-compat alias.
 */
void generate_api_image_store_json(char* buffer, size_t size)
{
    snprintf(buffer, size, "%s", g_image_store_json);
}

void generate_api_image_store_selector_json(char* buffer, size_t size)
{
    if (!g_selector_cache_valid) {
        image_store_init();
    }
    snprintf(buffer, size, "%s", g_image_store_json);
}

void image_store_invalidate_selector_cache(void)
{
    g_selector_cache_valid = false;
}

void image_store_invalidate_scan_cache(void)
{
    g_scan_cache_valid = false;
}

// ---------------------------------------------------------------------------
// Slot scan: reads slots 1–127 from EP flash and builds a JSON list.

static void do_image_store_scan(void)
{
    const uint32_t IMAGE_STORE_BASE = 0x10200000;
    const uint32_t SLOT_SIZE        = 65536;
    const uint32_t HEADER_PROBE     = 512;   // bytes read per slot (enough for all fields)
    const uint32_t CHUNK            = 512;   // max per read_target_mem call (keep ≤ 1024)

    if (!swd || !swd->connect_target(0, false)) {
        snprintf(g_imgstore_scan_json, sizeof(g_imgstore_scan_json),
                 "{\"error\":\"SWD connect failed\",\"slots\":[]}");
        return;
    }

    static uint8_t hdr[HEADER_PROBE + 4];   // +4 for alignment guard

    int len = snprintf(g_imgstore_scan_json, sizeof(g_imgstore_scan_json), "{\"slots\":[");
    bool first = true;

    for (int slot = 1; slot < 128; slot++) {
        uint32_t slot_addr = IMAGE_STORE_BASE + (uint32_t)slot * SLOT_SIZE;

        // Read first 4 bytes to check if empty (all 0xFF)
        uint32_t first_word = 0;
        if (!swd->read_target_mem(slot_addr, &first_word, 4)) continue;
        if (first_word == 0xFFFFFFFF) continue;

        // Validate as BSON document size
        uint32_t doc_size = Bson::read_unaligned_uint32((const uint8_t*)&first_word);
        if (doc_size < 5 || doc_size > 32768) {
            len += snprintf(g_imgstore_scan_json + len, sizeof(g_imgstore_scan_json) - len,
                            "%s{\"index\":%d,\"error\":\"bad_size\"}",
                            first ? "" : ",", slot);
            first = false;
            if (len >= (int)sizeof(g_imgstore_scan_json) - 64) break;
            continue;
        }

        // Read enough of the header to parse all fields
        uint32_t read_bytes = (doc_size < HEADER_PROBE) ? ((doc_size + 3) & ~3u) : HEADER_PROBE;
        bool ok = true;
        for (uint32_t off = 0; off < read_bytes; off += CHUNK) {
            uint32_t n = read_bytes - off;
            if (n > CHUNK) n = CHUNK;
            if (!swd->read_target_mem(slot_addr + off, (uint32_t*)(hdr + off), n)) {
                ok = false; break;
            }
        }
        if (!ok) {
            len += snprintf(g_imgstore_scan_json + len, sizeof(g_imgstore_scan_json) - len,
                            "%s{\"index\":%d,\"error\":\"swd_error\"}",
                            first ? "" : ",", slot);
            first = false;
            if (len >= (int)sizeof(g_imgstore_scan_json) - 64) break;
            continue;
        }
        hdr[HEADER_PROBE - 1] = 0;  // guard against strlen overrun in bsonlib

        element_t e;
        const char* name        = nullptr;
        const char* description = nullptr;
        const char* protection  = "N";
        uint32_t    image_m3    = 0;

        if (Bson::findElement(hdr, "name", e) && e.elementType == BSON_TYPE_UTF8)
            name = (const char*)e.data + 4;
        if (Bson::findElement(hdr, "description", e) && e.elementType == BSON_TYPE_UTF8)
            description = (const char*)e.data + 4;
        if (Bson::findElement(hdr, "image_m3", e) && e.elementType == BSON_TYPE_INT32)
            image_m3 = Bson::read_unaligned_uint32(e.data);
        if (Bson::findElement(hdr, "protection", e) && e.elementType == BSON_TYPE_UTF8)
            protection = (const char*)e.data + 4;

        if (!name) {
            len += snprintf(g_imgstore_scan_json + len, sizeof(g_imgstore_scan_json) - len,
                            "%s{\"index\":%d,\"error\":\"no_name\"}",
                            first ? "" : ",", slot);
            first = false;
            if (len >= (int)sizeof(g_imgstore_scan_json) - 64) break;
            continue;
        }

        char hash_str[12];
        snprintf(hash_str, sizeof(hash_str), "0x%08X", image_m3);

        len += snprintf(g_imgstore_scan_json + len, sizeof(g_imgstore_scan_json) - len,
                        "%s{\"index\":%d,\"name\":\"%s\",\"description\":\"%s\","
                        "\"hash\":\"%s\",\"protection\":\"%s\"}",
                        first ? "" : ",",
                        slot,
                        name,
                        description ? description : "",
                        hash_str,
                        protection);
        first = false;

        if (len >= (int)sizeof(g_imgstore_scan_json) - 64) break;  // avoid overrun
    }

    snprintf(g_imgstore_scan_json + len, sizeof(g_imgstore_scan_json) - len, "]}");
    g_scan_cache_valid = true;
}

void generate_api_image_store_scan_json(char* buffer, size_t size)
{
    if (!g_scan_cache_valid) {
        do_image_store_scan();
    }
    snprintf(buffer, size, "%s", g_imgstore_scan_json);
}

// ----------------------------------------------------------------------------------
// Trigger a new WiFi scan: called by /api/wifi-scan-start.
// Sets scan_requested and scanning so the WiFiManager task will run a scan,
// and the browser immediately sees scanning:true and starts polling.
void generate_api_wifi_scan_start_json(char* buffer, size_t size)
{
    if (!g_wifi_scan_results.scanning) {
        g_wifi_scan_results.count = 0;
        g_wifi_scan_results.scan_requested = true;
        g_wifi_scan_results.scanning = true;
    }
    snprintf(buffer, size, "{\"scanning\":true}");
}

// ----------------------------------------------------------------------------------
// Build JSON response for /api/wifi-scan.
// Pure read-only status — never triggers a scan. The JS calls /api/wifi-scan-start
// once to kick off the scan, then polls this endpoint until scanning:false.
// JSON format:
//   { "scanning": bool, "home_ssid": "...",
//     "home": { "ssid":"...", "rssi":-65, "channel":6, "found":true } or {"ssid":"...","found":false},
//     "networks": [ {"ssid":"...","rssi":-78,"channel":11}, ... ] }
void generate_api_wifi_scan_json(char* buffer, size_t size)
{

    const char* home_ssid = g_flash_config.wifi_ssid;
    WifiScanResults& r = g_wifi_scan_results;

    // Find home network in results
    const WifiScanEntry* home_entry = nullptr;
    for (int i = 0; i < r.count; i++) {
        if (strcmp(r.entries[i].ssid, home_ssid) == 0) {
            home_entry = &r.entries[i];
            break;
        }
    }

    // Start building JSON
    int pos = 0;
    pos += snprintf(buffer + pos, size - pos,
        "{\"scanning\":%s,\"home_ssid\":\"%s\",\"home\":",
        r.scanning ? "true" : "false", home_ssid);

    if (home_entry) {
        pos += snprintf(buffer + pos, size - pos,
            "{\"ssid\":\"%s\",\"rssi\":%d,\"channel\":%d,\"found\":true}",
            home_entry->ssid, home_entry->rssi, home_entry->channel);
    } else {
        pos += snprintf(buffer + pos, size - pos,
            "{\"ssid\":\"%s\",\"found\":false}", home_ssid);
    }

    pos += snprintf(buffer + pos, size - pos, ",\"networks\":[");
    bool first = true;
    for (int i = 0; i < r.count; i++) {
        const WifiScanEntry& e = r.entries[i];
        if (strcmp(e.ssid, home_ssid) == 0) continue; // home is already in "home" field
        if (!first) pos += snprintf(buffer + pos, size - pos, ",");
        pos += snprintf(buffer + pos, size - pos,
            "{\"ssid\":\"%s\",\"rssi\":%d,\"channel\":%d}",
            e.ssid, e.rssi, e.channel);
        first = false;
    }
    snprintf(buffer + pos, size - pos, "]}");;
}
