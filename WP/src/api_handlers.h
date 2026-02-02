#ifndef API_HANDLERS_H
#define API_HANDLERS_H

#include "lwip/apps/httpd.h"

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif

#endif // API_HANDLERS_H
