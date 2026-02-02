/**
 * HTTP POST handler implementations for lwIP httpd.
 * Routes POST requests to appropriate handlers (e.g., file upload).
 *
 * NOTE: fs_open_custom, fs_read_custom, and fs_close_custom are implemented
 * in fs_custom.c.
 */

#include "lwip/apps/httpd.h"
#include "lwip/apps/fs.h"
#include "upload_handler.h"
#include <string.h>

/**
 * POST begin handler.
 * Routes POST requests to appropriate handlers based on URI.
 */
err_t httpd_post_begin(void *connection, const char *uri, const char *http_request,
                      u16_t http_request_len, int content_len, char *response_uri,
                      u16_t response_uri_len, u8_t *post_auto_wnd)
{
    // Check if this is an upload request
    if (strncmp(uri, "/api/upload", 11) == 0) {
        return upload_post_begin(connection, uri, http_request, http_request_len,
                                content_len, response_uri, response_uri_len, post_auto_wnd);
    }

    // Unknown POST endpoint
    return ERR_VAL;
}

/**
 * POST receive data handler.
 * Receives data chunks for active POST requests.
 */
err_t httpd_post_receive_data(void *connection, struct pbuf *p)
{
    // All POST data goes to upload handler (it will validate the session)
    return upload_post_receive_data(connection, p);
}

/**
 * POST finished handler.
 * Called when POST request is complete.
 */
void httpd_post_finished(void *connection, char *response_uri, u16_t response_uri_len)
{
    // All POST completions go to upload handler
    upload_post_finished(connection, response_uri, response_uri_len);
}
