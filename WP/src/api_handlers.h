#ifndef API_HANDLERS_H
#define API_HANDLERS_H

#include "lwip/apps/httpd.h"

/**
 * Register all CGI handlers with lwIP httpd.
 * Call this after httpd_init().
 */
void api_handlers_register(void);

/**
 * Generate JSON for /api/info endpoint.
 * Called by fs_open_custom() when serving the API.
 */
void generate_api_info_json(char* buffer, size_t size);

/**
 * Generate JSON for /api/list endpoint.
 * Called by fs_open_custom() when serving the API.
 */
void generate_api_list_json(char* buffer, size_t size);

/**
 * Generate JSON for /api/sha256/<filename> endpoint.
 * Called by fs_open_custom() when serving the API.
 */
void generate_api_sha256_json(char* buffer, size_t size, const char* filename);

/**
 * Generate JSON for /api/delete/<filename> endpoint.
 * Called by fs_open_custom() when serving the API.
 */
void generate_api_delete_json(char* buffer, size_t size, const char* filename);

/**
 * Handle file upload chunk via POST to /api/upload
 * Called by fs_open_custom() when receiving upload data.
 */
void handle_upload_chunk(char* buffer, size_t size, const char* post_data, size_t post_data_len);

/**
 * Generate JSON for /api/reflash/ep?filename=<name> endpoint.
 * Triggers EP reflash via SWD using specified UF2 file from SD card.
 * WARNING: This operation takes 10-30 seconds and blocks!
 * Called by fs_open_custom() when serving the API.
 */
void generate_api_reflash_ep_json(char* buffer, size_t size, const char* filename);

/**
 * Generate JSON for /api/reflash/wp?filename=<name> endpoint.
 * Triggers WP self-reflash using specified UF2 file from SD card.
 * The new firmware is written to the inactive A/B partition.
 * WARNING: This operation takes 30-120 seconds and blocks!
 * After completion, a reboot is required to activate the new firmware.
 * The reboot uses TBYB (Try Before You Buy) for automatic rollback protection.
 * Called by fs_open_custom() when serving the API.
 */
void generate_api_reflash_wp_json(char* buffer, size_t size, const char* filename);

/**
 * Generate JSON for /api/system endpoint.
 * Returns the SYSTEM_JSON string containing build metadata (Git hash, build time).
 * Called by fs_open_custom() when serving the API.
 */
void generate_api_system_json(char* buffer, size_t size);

/**
 * Generate JSON for /api/eprom-info endpoint.
 * Returns the image_selector contents (compact JSON array string received from EP at boot)
 * plus which slot was actually loaded.
 * Called by fs_open_custom() when serving the API.
 */
void generate_api_eprom_info_json(char* buffer, size_t size);

/**
 * Generate JSON for /api/reboot endpoint.
 * Schedules a clean system reboot via watchdog (fires ~500ms after response is sent).
 * Called by fs_open_custom() when serving the API.
 */
void generate_api_reboot_json(char* buffer, size_t size);

/**
 * Generate JSON for GET /api/config endpoint.
 * Returns current device config with passwords redacted.
 * Called by fs_open_custom() when serving the API.
 */
void generate_api_config_json(char* buffer, size_t size);

/**
 * Generate JSON for GET /api/wifi-reset endpoint.
 * Clears wifi_ssid and wifi_password from flash config, saves, and reboots.
 * Device will boot into AP mode on next start.
 * Response includes ap_ssid so the client can show reconnect instructions.
 * Called by fs_open_custom() when serving the API.
 */
void generate_api_wifi_reset_json(char* buffer, size_t size);

/**
 * Generate JSON for GET /api/ping endpoint.
 * Returns {"ok":true}. Used by browser JS to poll for device-back-online
 * after a reboot-triggering action (config save, OTA, etc.).
 */
void generate_api_ping_json(char* buffer, size_t size);

/**
 * Generate JSON for GET /api/sd-info endpoint.
 * Returns filesystem capacity, used space, and file listing with open-file status.
 * Called by fs_open_custom() when serving the API.
 */
void generate_api_sd_info_json(char* buffer, size_t size);

// ---------------------------------------------------------------------------
// ECU Live Stream configuration (ecu_live.html)

#define ECU_LIVE_ITEMS_MAX 10

/**
 * Per-slot log ID selection: 0-255 = ecuLiveLog index to display, -1 = empty slot.
 * Initialized to compile-time defaults; overwritten by ecu_live_config_load().
 */
extern int8_t g_ecu_live_items[ECU_LIVE_ITEMS_MAX];

/**
 * Load ECU live config from /ecu_live.json on LittleFS.
 * Call once after LittleFS is mounted. On any error, compile-time defaults are kept.
 */
void ecu_live_config_load(void);

/**
 * Generate JSON for GET /api/ecu-live-config.
 * Returns {"items":[v0,v1,...,v9]} with current slot assignments.
 */
void generate_api_ecu_live_config_json(char* buffer, size_t size);

/**
 * Generate JSON for GET /api/ecu-live-data.
 * Returns all 10 slots with current ecuLiveLog[] values (null for empty slots).
 */
void generate_api_ecu_live_data_json(char* buffer, size_t size);

/**
 * Generate JSON for GET /api/reformat-filesystem endpoint.
 * Takes the LFS mutex, zeroes the first 64 raw SD sectors (LFS blocks 0 and 1,
 * both superblock copies), syncs, then schedules a reboot via the OTA task.
 * On next boot, mount failure triggers the automatic reformat path.
 */
void generate_api_reformat_filesystem_json(char* buffer, size_t size);

/**
 * Read EP flash IMAGE_STORE slot 0 via SWD and cache the selector as JSON.
 * Call once from main() after the Swd object is created.
 */
void image_store_init(void);

/**
 * Serve the cached /api/image-store and /api/image-store/selector JSON.
 * Safe to call from the lwIP tcpip thread — no SWD access.
 */
void generate_api_image_store_json(char* buffer, size_t size);
void generate_api_image_store_selector_json(char* buffer, size_t size);

/**
 * Invalidate the selector cache (e.g. after writing a new selector to slot 0).
 * The next call to generate_api_image_store_selector_json() will re-read slot 0.
 */
void image_store_invalidate_selector_cache(void);

/**
 * Scan EP flash slots 1–127 via SWD and build a JSON list of populated slots.
 * The result is cached; call image_store_invalidate_scan_cache() after any write.
 * Safe to call from the lwIP tcpip thread (short, non-destructive SWD read).
 */
void generate_api_image_store_scan_json(char* buffer, size_t size);
void generate_api_wifi_scan_start_json(char* buffer, size_t size);
void generate_api_wifi_scan_json(char* buffer, size_t size);

/**
 * Invalidate the scan cache (e.g. after uploading or deleting a slot).
 */
void image_store_invalidate_scan_cache(void);

#endif // API_HANDLERS_H
