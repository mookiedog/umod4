/**
 * Stub implementations for lwIP httpd functions.
 * These are required by httpd but not yet fully implemented in Phase 1.
 */

#include "lwip/apps/httpd.h"
#include "lwip/apps/fs.h"

/**
 * Custom filesystem open - Phase 1 stub.
 * Returns false to use default fsdata.c embedded files.
 * Will be replaced in Phase 2 with SD card filesystem bridge.
 */
int fs_open_custom(struct fs_file *file, const char *name)
{
    (void)file;
    (void)name;
    // Return 0 to indicate "not found", httpd will use embedded fsdata
    return 0;
}

/**
 * Custom filesystem close - Phase 1 stub.
 */
void fs_close_custom(struct fs_file *file)
{
    (void)file;
    // Nothing to do in Phase 1
}

/**
 * POST begin handler - Phase 1 stub.
 * POST support deferred to later phases.
 */
err_t httpd_post_begin(void *connection, const char *uri, const char *http_request,
                      u16_t http_request_len, int content_len, char *response_uri,
                      u16_t response_uri_len, u8_t *post_auto_wnd)
{
    (void)connection;
    (void)uri;
    (void)http_request;
    (void)http_request_len;
    (void)content_len;
    (void)response_uri;
    (void)response_uri_len;
    (void)post_auto_wnd;
    return ERR_VAL;  // Not supported in Phase 1
}

/**
 * POST receive data handler - Phase 1 stub.
 */
err_t httpd_post_receive_data(void *connection, struct pbuf *p)
{
    (void)connection;
    (void)p;
    return ERR_VAL;  // Not supported in Phase 1
}

/**
 * POST finished handler - Phase 1 stub.
 */
void httpd_post_finished(void *connection, char *response_uri, u16_t response_uri_len)
{
    (void)connection;
    (void)response_uri;
    (void)response_uri_len;
    // Nothing to do in Phase 1
}
