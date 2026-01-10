#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <cstdint>
#include <cstddef>

/**
 * Simple HTTP client for umod4 server communication.
 *
 * Provides device registration and file upload functionality.
 * Uses lwIP sockets for HTTP communication.
 */
class HttpClient {
public:
    HttpClient(const char* server_host, uint16_t server_port);
    ~HttpClient();

    /**
     * Register device with server (POST /api/device/register).
     *
     * Sends device MAC address, WP version, and EP version to server.
     * Server responds with device configuration.
     *
     * @param mac_address Device MAC address (format: "XX:XX:XX:XX:XX:XX")
     * @param wp_version WP firmware version string
     * @param ep_version EP firmware version string (or nullptr if unknown)
     * @param ip_address Device IP address
     * @return true on success, false on failure
     */
    bool registerDevice(const char* mac_address,
                       const char* wp_version,
                       const char* ep_version,
                       const char* ip_address);

    /**
     * Upload log file to server (POST /logs/upload/{device_mac}).
     *
     * @param mac_address Device MAC address
     * @param filename Log filename (e.g., "log_7.um4")
     * @param data File data buffer
     * @param size File size in bytes
     * @param progress_callback Optional callback for upload progress (bytes_sent, total_bytes)
     * @return true on success, false on failure
     */
    bool uploadLogFile(const char* mac_address,
                      const char* filename,
                      const uint8_t* data,
                      size_t size,
                      void (*progress_callback)(size_t, size_t) = nullptr);

    /**
     * Upload a single chunk of a log file (chunked upload with resumption).
     *
     * @param mac_address Device MAC address
     * @param filename Log filename (e.g., "log_7.um4")
     * @param chunk_data Chunk data buffer
     * @param chunk_size Size of this chunk in bytes
     * @param chunk_offset Offset of this chunk in the file (bytes)
     * @param total_size Total file size in bytes
     * @param is_last_chunk True if this is the final chunk
     * @param chunk_crc32 CRC32 checksum of chunk data
     * @param session_id Optional session ID for resuming (nullptr for new upload)
     * @param out_session_id Output buffer for session ID (min 37 bytes for UUID + null)
     * @return true on success, false on failure
     */
    bool uploadLogFileChunk(const char* mac_address,
                           const char* filename,
                           const uint8_t* chunk_data,
                           size_t chunk_size,
                           size_t chunk_offset,
                           size_t total_size,
                           bool is_last_chunk,
                           uint32_t chunk_crc32,
                           const char* session_id = nullptr,
                           char* out_session_id = nullptr);

    /**
     * Query upload session status for resuming interrupted upload.
     *
     * @param mac_address Device MAC address
     * @param filename Filename being uploaded
     * @param out_session_id Output buffer for session ID (min 37 bytes)
     * @param out_bytes_received Output: bytes already received by server
     * @param out_chunk_size Output: server's negotiated chunk size
     * @return true if session found, false if no session exists
     */
    bool queryUploadSession(const char* mac_address,
                           const char* filename,
                           char* out_session_id,
                           size_t* out_bytes_received,
                           size_t* out_chunk_size);

    /**
     * Get last HTTP status code from most recent request.
     */
    int getLastStatusCode() const { return last_status_code_; }

    /**
     * Get last error message.
     */
    const char* getLastError() const { return last_error_; }

private:
    char server_host_[128];
    uint16_t server_port_;
    int last_status_code_;
    char last_error_[128];

    // HTTP request helpers
    bool sendRequest(const char* method, const char* path,
                    const char* content_type,
                    const uint8_t* body, size_t body_len,
                    char* response_buf, size_t response_buf_len);

    bool parseHttpResponse(const char* response, size_t response_len,
                          int* status_code, const char** body, size_t* body_len);

    void setError(const char* error);
};

#endif // HTTP_CLIENT_H
