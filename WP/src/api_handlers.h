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
 * CGI handler for /api/info endpoint.
 * Returns JSON with device status information.
 */
const char* cgi_api_info(int iIndex, int iNumParams, char *pcParam[], char *pcValue[]);

#ifdef __cplusplus
}
#endif

#endif // API_HANDLERS_H
