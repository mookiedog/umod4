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

#ifdef __cplusplus
}
#endif

#endif // API_HANDLERS_H
