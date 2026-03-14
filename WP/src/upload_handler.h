#ifndef UPLOAD_HANDLER_H
#define UPLOAD_HANDLER_H

#include "lwip/apps/httpd.h"
#include "lwip/apps/fs.h"
#include "pico/sha256.h"
#include <stdint.h>
#include <stdbool.h>

#endif

/**
 * Maximum number of concurrent upload sessions.
 * Each session consumes ~200 bytes of RAM for metadata + 16KB for ping-pong buffers.
 */
#define MAX_UPLOAD_SESSIONS 2

/**
 * Ping-pong buffer size (8KB each).
 * Two buffers allow receiving network data while writing previous buffer to SD.
 */
#define UPLOAD_BUFFER_SIZE 8192

/**
 * Upload session state.
 */
typedef struct {
    char session_id[37];          // UUID string (36 chars + null terminator)
    char filename[64];            // Target filename (without /uploads/ prefix)
    uint32_t total_size;          // Expected total file size
    uint32_t bytes_received;      // Bytes written so far
    uint32_t chunk_size;          // Expected chunk size
    pico_sha256_state_t sha_state; // Running SHA-256 hash
    bool sha_enabled;             // True if SHA-256 calculation active
    bool file_open;               // True if file is currently open (managed by file_io_task)
    bool in_use;                  // True if this slot is allocated
    void* connection;             // lwIP connection handle
    uint32_t last_activity_ms;    // Timestamp of last activity (ms since boot)

    // Ping-pong buffers for non-blocking SD writes (statically allocated)
    uint8_t write_buffer_a[UPLOAD_BUFFER_SIZE];  // Buffer A (8KB, static)
    uint8_t write_buffer_b[UPLOAD_BUFFER_SIZE];  // Buffer B (8KB, static)
    uint8_t active_buffer;        // 0=A, 1=B (which buffer is receiving data)
    uint32_t buffer_used[2];      // Bytes used in each buffer [A, B]
    bool write_in_progress;       // True if async SD write is pending
} upload_session_t;

/**
 * Initialize upload handler subsystem.
 * Call this during startup before httpd_init().
 */
void upload_handler_init(void);

/**
 * Handle POST begin for upload endpoint.
 * Parses headers to extract session info.
 */
err_t upload_post_begin(void *connection, const char *uri,
						const char *http_request, u16_t http_request_len,
						int content_len, char *response_uri,
						u16_t response_uri_len, u8_t *post_auto_wnd);

/**
 * Handle POST data reception for upload.
 * Writes chunks to file and updates SHA-256.
 */
err_t upload_post_receive_data(void *connection, struct pbuf *p);

/**
 * Handle POST completion for upload.
 * Finalizes file, verifies SHA-256, and generates response.
 */
void upload_post_finished(void *connection, char *response_uri, u16_t response_uri_len);

/**
 * Generate JSON response for /api/upload/session?session_id=xxx
 * Returns session status for resumption.
 */
void generate_api_upload_session_json(char* buffer, size_t size, const char* session_id);

