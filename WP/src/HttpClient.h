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
