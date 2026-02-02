/**
 * Upload handler for HTTP file uploads.
 *
 * File I/O operations are delegated to file_io_task to avoid calling
 * LittleFS functions directly from the lwIP HTTP callback context
 * (which has insufficient stack space for LFS operations).
 */

#include "upload_handler.h"
#include "file_io_task.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Timeout for file I/O operations (milliseconds)
#define FILE_IO_TIMEOUT_MS 5000

// Global upload session table
static upload_session_t upload_sessions[MAX_UPLOAD_SESSIONS];

// Forward declarations for helper functions
static upload_session_t* find_session_by_connection(void* connection);
static upload_session_t* find_session_by_id(const char* session_id);
static upload_session_t* allocate_session(void* connection);
static void free_session(upload_session_t* session);
static const char* extract_header_value(const char* headers, const char* header_name);
static bool validate_filename(const char* filename);

void upload_handler_init(void)
{
    memset(upload_sessions, 0, sizeof(upload_sessions));
    printf("upload_handler: Initialized (%d max sessions)\n", MAX_UPLOAD_SESSIONS);
}

err_t upload_post_begin(void *connection, const char *uri,
                        const char *http_request, u16_t http_request_len,
                        int content_len, char *response_uri,
                        u16_t response_uri_len, u8_t *post_auto_wnd)
{
    printf("upload_post_begin: URI=%s, content_len=%d\n", uri, content_len);

    // Only handle /api/upload endpoint
    if (strncmp(uri, "/api/upload", 11) != 0) {
        return ERR_VAL;  // Not our endpoint
    }

    // Parse HTTP headers to extract upload metadata
    // Headers we expect:
    // X-Session-ID: <uuid> (optional for new uploads)
    // X-Filename: <filename>
    // X-Total-Size: <bytes>
    // X-Chunk-Size: <bytes>
    // X-Chunk-Offset: <offset>
    // X-Chunk-CRC32: <hex> (optional for verification)

    // Note: extract_header_value uses a static buffer, so we must copy each value
    // immediately before calling it again
    char session_id[64] = {0};
    char filename[64] = {0};
    uint32_t total_size = 0;
    uint32_t chunk_size = 0;
    uint32_t chunk_offset = 0;

    const char* tmp;

    tmp = extract_header_value(http_request, "X-Session-ID:");
    if (tmp) {
        strncpy(session_id, tmp, sizeof(session_id) - 1);
    }

    tmp = extract_header_value(http_request, "X-Filename:");
    if (tmp) {
        strncpy(filename, tmp, sizeof(filename) - 1);
    }

    tmp = extract_header_value(http_request, "X-Total-Size:");
    if (tmp) {
        total_size = (uint32_t)atoi(tmp);
    }

    tmp = extract_header_value(http_request, "X-Chunk-Size:");
    if (tmp) {
        chunk_size = (uint32_t)atoi(tmp);
    }

    tmp = extract_header_value(http_request, "X-Chunk-Offset:");
    if (tmp) {
        chunk_offset = (uint32_t)atoi(tmp);
    }

    // Validate required headers
    if (filename[0] == '\0' || total_size == 0 || chunk_size == 0) {
        printf("upload_post_begin: Missing required headers (filename='%s', total=%lu, chunk=%lu)\n",
               filename, (unsigned long)total_size, (unsigned long)chunk_size);
        snprintf(response_uri, response_uri_len, "/upload_error.json");
        return ERR_OK;
    }

    // Validate filename (security: prevent path traversal)
    if (!validate_filename(filename)) {
        printf("upload_post_begin: Invalid filename '%s'\n", filename);
        snprintf(response_uri, response_uri_len, "/upload_error.json");
        return ERR_OK;
    }

    // Validate chunk size
    if (chunk_size > 65536) {
        printf("upload_post_begin: Chunk size too large (%lu)\n", (unsigned long)chunk_size);
        snprintf(response_uri, response_uri_len, "/upload_error.json");
        return ERR_OK;
    }

    printf("upload_post_begin: filename='%s', total=%lu, chunk=%lu, offset=%lu\n",
           filename, (unsigned long)total_size, (unsigned long)chunk_size, (unsigned long)chunk_offset);

    // Find or create session
    upload_session_t* session = NULL;

    if (session_id[0] != '\0') {
        // Resuming existing session
        session = find_session_by_id(session_id);
        if (session) {
            printf("upload_post_begin: Resuming session %s at offset %lu\n",
                   session_id, (unsigned long)chunk_offset);

            // Verify parameters match
            if (strcmp(session->filename, filename) != 0 ||
                session->total_size != total_size) {
                printf("upload_post_begin: Session parameters mismatch\n");
                free_session(session);
                snprintf(response_uri, response_uri_len, "/upload_error.json");
                return ERR_OK;
            }

            // Verify offset matches current position
            if (chunk_offset != session->bytes_received) {
                printf("upload_post_begin: Offset mismatch (expected=%lu, got=%lu)\n",
                       (unsigned long)session->bytes_received, (unsigned long)chunk_offset);
                snprintf(response_uri, response_uri_len, "/upload_error.json");
                return ERR_OK;
            }
        } else {
            printf("upload_post_begin: Session %s not found, creating new\n", session_id);
        }
    }

    if (!session) {
        // Create new session
        session = allocate_session(connection);
        if (!session) {
            printf("upload_post_begin: Failed to allocate session (all slots busy)\n");
            snprintf(response_uri, response_uri_len, "/upload_error.json");
            return ERR_OK;
        }

        // Initialize session
        if (session_id[0] != '\0') {
            strncpy(session->session_id, session_id, sizeof(session->session_id) - 1);
        } else {
            // Generate UUID (simplified - use connection pointer + timestamp + file size)
            uint32_t t = time_us_32();
            snprintf(session->session_id, sizeof(session->session_id),
                     "%08lx-%04lx-%04lx-%04x-%012lx",
                     (unsigned long)t,
                     (unsigned long)((uintptr_t)connection >> 16) & 0xFFFF,
                     (unsigned long)((uintptr_t)connection) & 0xFFFF,
                     (uint16_t)(total_size & 0xFFFF),
                     (unsigned long)((uint64_t)t * 1000000ULL + total_size) & 0xFFFFFFFFFFFFULL);
        }
        session->session_id[sizeof(session->session_id) - 1] = '\0';

        strncpy(session->filename, filename, sizeof(session->filename) - 1);
        session->filename[sizeof(session->filename) - 1] = '\0';
        session->total_size = total_size;
        session->chunk_size = chunk_size;
        session->bytes_received = 0;
        session->file_open = false;

        // Build full file path (store in root directory)
        file_io_result_t result;
        char filepath[80];
        snprintf(filepath, sizeof(filepath), "/%s", session->filename);

        // Open file for writing (via file_io_task)
        bool truncate = (chunk_offset == 0);  // New upload - truncate; Resume - append
        if (!file_io_upload_open(filepath, truncate, FILE_IO_TIMEOUT_MS, &result)) {
            printf("upload_post_begin: open timeout or error\n");
            free_session(session);
            snprintf(response_uri, response_uri_len, "/upload_error.json");
            return ERR_OK;
        }

        if (!result.success) {
            printf("upload_post_begin: Failed to open '%s': %s\n", filepath, result.error_message);
            free_session(session);
            snprintf(response_uri, response_uri_len, "/upload_error.json");
            return ERR_OK;
        }

        session->file_open = true;

        // Initialize SHA-256
        if (pico_sha256_try_start(&session->sha_state, SHA256_BIG_ENDIAN, true) == PICO_OK) {
            session->sha_enabled = true;
            printf("upload_post_begin: SHA-256 enabled\n");
        } else {
            session->sha_enabled = false;
            printf("upload_post_begin: WARNING: SHA-256 hardware busy\n");
        }

        printf("upload_post_begin: Created session %s for '%s' (%lu bytes)\n",
               session->session_id, filename, (unsigned long)total_size);
    }

    // Update connection pointer (might have changed on resume)
    session->connection = connection;

    // Enable automatic window updates for faster transfers
    *post_auto_wnd = 1;

    // Response will be generated in upload_post_finished
    return ERR_OK;
}

err_t upload_post_receive_data(void *connection, struct pbuf *p)
{
    upload_session_t* session = find_session_by_connection(connection);
    if (!session) {
        printf("upload_post_receive_data: No session found for connection\n");
        pbuf_free(p);
        return ERR_VAL;
    }

    if (!session->file_open) {
        printf("upload_post_receive_data: File not open\n");
        pbuf_free(p);
        return ERR_VAL;
    }

    // Write data to file via file_io_task
    struct pbuf* q = p;
    while (q != NULL) {
        file_io_result_t result;
        if (!file_io_upload_write((const uint8_t*)q->payload, q->len, FILE_IO_TIMEOUT_MS, &result)) {
            printf("upload_post_receive_data: Write timeout\n");
            pbuf_free(p);
            return ERR_VAL;
        }

        if (!result.success) {
            printf("upload_post_receive_data: Write error: %s\n", result.error_message);
            pbuf_free(p);
            return ERR_VAL;
        }

        // Update SHA-256
        if (session->sha_enabled) {
            pico_sha256_update_blocking(&session->sha_state, (const uint8_t*)q->payload, q->len);
        }

        session->bytes_received += result.write_result.bytes_written;
        q = q->next;
    }

    pbuf_free(p);

    if (false) printf("upload_post_receive_data: Received %u bytes (total=%lu/%lu)\n",
           p->tot_len, (unsigned long)session->bytes_received, (unsigned long)session->total_size);

    return ERR_OK;
}

void upload_post_finished(void *connection, char *response_uri, u16_t response_uri_len)
{
    upload_session_t* session = find_session_by_connection(connection);
    if (!session) {
        printf("upload_post_finished: No session found\n");
        snprintf(response_uri, response_uri_len, "/upload_error.json");
        return;
    }

    bool success = false;
    char sha256_hex[SHA256_RESULT_BYTES * 2 + 1] = "none";

    // Check if upload is complete
    if (session->bytes_received >= session->total_size) {
        // Finalize SHA-256
        if (session->sha_enabled) {
            sha256_result_t sha_result;
            pico_sha256_finish(&session->sha_state, &sha_result);

            // Convert to hex string
            for (int i = 0; i < SHA256_RESULT_BYTES; i++) {
                snprintf(sha256_hex + (i * 2), 3, "%02x", sha_result.bytes[i]);
            }
            sha256_hex[SHA256_RESULT_BYTES * 2] = '\0';
        }

        // Close file (with sync) via file_io_task
        if (session->file_open) {
            file_io_result_t result;
            file_io_upload_close(true, FILE_IO_TIMEOUT_MS, &result);
            session->file_open = false;

            if (!result.success) {
                printf("upload_post_finished: Close error: %s\n", result.error_message);
            }
        }

        printf("upload_post_finished: Upload complete for '%s' (%lu bytes, SHA-256: %.16s...)\n",
               session->filename, (unsigned long)session->bytes_received, sha256_hex);

        success = true;

        // Free session
        free_session(session);
    } else {
        printf("upload_post_finished: Chunk complete (%lu/%lu bytes)\n",
               (unsigned long)session->bytes_received, (unsigned long)session->total_size);

        // Don't close file yet - more chunks coming
        // Don't free session - keep it for next chunk
    }

    // Generate response
    if (success) {
        snprintf(response_uri, response_uri_len, "/upload_success.json");
    } else {
        snprintf(response_uri, response_uri_len, "/upload_progress.json");
    }
}

void generate_api_upload_session_json(char* buffer, size_t size, const char* session_id)
{
    upload_session_t* session = find_session_by_id(session_id);

    if (!session) {
        snprintf(buffer, size,
                 "{\"error\": \"Session not found\"}");
        return;
    }

    snprintf(buffer, size,
             "{\n"
             "  \"session_id\": \"%s\",\n"
             "  \"filename\": \"%s\",\n"
             "  \"total_size\": %lu,\n"
             "  \"bytes_received\": %lu,\n"
             "  \"next_offset\": %lu\n"
             "}",
             session->session_id,
             session->filename,
             (unsigned long)session->total_size,
             (unsigned long)session->bytes_received,
             (unsigned long)session->bytes_received);
}

// Helper functions

static upload_session_t* find_session_by_connection(void* connection)
{
    for (int i = 0; i < MAX_UPLOAD_SESSIONS; i++) {
        if (upload_sessions[i].in_use && upload_sessions[i].connection == connection) {
            return &upload_sessions[i];
        }
    }
    return NULL;
}

static upload_session_t* find_session_by_id(const char* session_id)
{
    for (int i = 0; i < MAX_UPLOAD_SESSIONS; i++) {
        if (upload_sessions[i].in_use &&
            strcmp(upload_sessions[i].session_id, session_id) == 0) {
            return &upload_sessions[i];
        }
    }
    return NULL;
}

static upload_session_t* allocate_session(void* connection)
{
    for (int i = 0; i < MAX_UPLOAD_SESSIONS; i++) {
        if (!upload_sessions[i].in_use) {
            memset(&upload_sessions[i], 0, sizeof(upload_session_t));
            upload_sessions[i].in_use = true;
            upload_sessions[i].connection = connection;
            return &upload_sessions[i];
        }
    }
    return NULL;
}

static void free_session(upload_session_t* session)
{
    if (!session) return;

    // Close file if still open
    if (session->file_open) {
        file_io_result_t result;
        file_io_upload_close(false, FILE_IO_TIMEOUT_MS, &result);
        session->file_open = false;
    }

    // Mark session as free
    session->in_use = false;
}

static const char* extract_header_value(const char* headers, const char* header_name)
{
    // Simple header parser - finds "Header-Name: value"
    // Note: This is a simplified implementation. Returns pointer to static buffer.
    static char value_buffer[256];

    const char* pos = strstr(headers, header_name);
    if (!pos) return NULL;

    // Skip header name
    pos += strlen(header_name);

    // Skip whitespace
    while (*pos == ' ' || *pos == '\t') pos++;

    // Copy value until CRLF or end of string
    size_t len = 0;
    while (pos[len] != '\r' && pos[len] != '\n' && pos[len] != '\0' && len < sizeof(value_buffer) - 1) {
        value_buffer[len] = pos[len];
        len++;
    }
    value_buffer[len] = '\0';

    return value_buffer;
}

static bool validate_filename(const char* filename)
{
    if (!filename || filename[0] == '\0') {
        return false;
    }

    // Check for path traversal attempts
    if (strchr(filename, '/') || strchr(filename, '\\') ||
        strcmp(filename, ".") == 0 || strcmp(filename, "..") == 0) {
        return false;
    }

    // Check length
    if (strlen(filename) >= 64) {
        return false;
    }

    return true;
}
