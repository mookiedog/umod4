#include "HttpClient.h"
#include "pico/stdlib.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include <cstdio>
#include <cstring>

// Server configuration from build-time
#ifndef UMOD4_SERVER_HOST
#error "UMOD4_SERVER_HOST not defined"
#endif

#ifndef UMOD4_SERVER_PORT
#error "UMOD4_SERVER_PORT not defined"
#endif

HttpClient::HttpClient(const char* server_host, uint16_t server_port) :
      server_port_(server_port),
      last_status_code_(0)
{
    strncpy(server_host_, server_host, sizeof(server_host_) - 1);
    server_host_[sizeof(server_host_) - 1] = '\0';
    last_error_[0] = '\0';
}

HttpClient::~HttpClient()
{
}

bool HttpClient::registerDevice(const char* mac_address,
                                const char* wp_version,
                                const char* ep_version,
                                const char* ip_address)
{
    printf("HTTP: Registering device %s with server %s:%u\n",
           mac_address, server_host_, server_port_);

    // Build JSON request body
    char json_body[256];
    snprintf(json_body, sizeof(json_body),
             "{\"mac_address\":\"%s\",\"wp_version\":\"%s\",\"ep_version\":\"%s\",\"ip_address\":\"%s\"}",
             mac_address,
             wp_version ? wp_version : "unknown",
             ep_version ? ep_version : "unknown",
             ip_address);

    char response_buf[1024];
    bool result = sendRequest("POST", "/api/device/register",
                             "application/json",
                             (const uint8_t*)json_body, strlen(json_body),
                             response_buf, sizeof(response_buf));

    if (result && last_status_code_ == 200) {
        printf("HTTP: Device registered successfully\n");
        return true;
    } else {
        printf("HTTP: Device registration failed: %s\n", last_error_);
        return false;
    }
}

bool HttpClient::uploadLogFile(const char* mac_address,
                               const char* filename,
                               const uint8_t* data,
                               size_t size,
                               void (*progress_callback)(size_t, size_t))
{
    printf("HTTP: Uploading %s (%zu bytes) to server\n", filename, size);

    // Build upload path: /logs/upload/{device_mac}
    char path[128];
    snprintf(path, sizeof(path), "/logs/upload/%s", mac_address);

    // Create socket
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", server_port_);

    if (getaddrinfo(server_host_, port_str, &hints, &res) != 0) {
        setError("DNS lookup failed");
        return false;
    }

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        freeaddrinfo(res);
        setError("Socket creation failed");
        return false;
    }

    // Connect
    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        close(sock);
        freeaddrinfo(res);
        setError("Connection failed");
        return false;
    }

    freeaddrinfo(res);

    // Build HTTP request
    char header[512];
    snprintf(header, sizeof(header),
             "POST %s HTTP/1.1\r\n"
             "Host: %s:%u\r\n"
             "X-Filename: %s\r\n"
             "Content-Type: application/octet-stream\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n"
             "\r\n",
             path, server_host_, server_port_, filename, size);

    // Send header
    if (send(sock, header, strlen(header), 0) < 0) {
        close(sock);
        setError("Failed to send header");
        return false;
    }

    // Send body in chunks (64KB at a time)
    const size_t chunk_size = 65536;
    size_t sent = 0;

    while (sent < size) {
        size_t to_send = (size - sent > chunk_size) ? chunk_size : (size - sent);
        ssize_t result = send(sock, data + sent, to_send, 0);

        if (result < 0) {
            close(sock);
            setError("Failed to send data");
            return false;
        }

        sent += result;

        if (progress_callback) {
            progress_callback(sent, size);
        }
    }

    printf("HTTP: Upload sent %zu bytes\n", sent);

    // Read response
    char response_buf[1024];
    ssize_t recv_len = recv(sock, response_buf, sizeof(response_buf) - 1, 0);
    close(sock);

    if (recv_len < 0) {
        setError("Failed to receive response");
        return false;
    }

    response_buf[recv_len] = '\0';

    // Parse response
    int status_code = 0;
    const char* body = nullptr;
    size_t body_len = 0;

    if (!parseHttpResponse(response_buf, recv_len, &status_code, &body, &body_len)) {
        setError("Failed to parse response");
        return false;
    }

    last_status_code_ = status_code;

    if (status_code == 200) {
        printf("HTTP: Upload successful\n");
        return true;
    } else {
        snprintf(last_error_, sizeof(last_error_), "Upload failed with status %d", status_code);
        printf("HTTP: %s\n", last_error_);
        return false;
    }
}

bool HttpClient::sendRequest(const char* method, const char* path,
                             const char* content_type,
                             const uint8_t* body, size_t body_len,
                             char* response_buf, size_t response_buf_len)
{
    // Create socket
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", server_port_);

    printf("HTTP: Resolving %s:%s...\n", server_host_, port_str);
    if (getaddrinfo(server_host_, port_str, &hints, &res) != 0) {
        setError("DNS lookup failed");
        printf("HTTP: DNS lookup failed for %s\n", server_host_);
        return false;
    }

    // Print resolved address
    struct sockaddr_in* addr_in = (struct sockaddr_in*)res->ai_addr;
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(addr_in->sin_addr), ip_str, INET_ADDRSTRLEN);
    printf("HTTP: Resolved to %s:%u\n", ip_str, ntohs(addr_in->sin_port));

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        freeaddrinfo(res);
        setError("Socket creation failed");
        printf("HTTP: Socket creation failed\n");
        return false;
    }

    // Connect
    printf("HTTP: Connecting to %s:%u...\n", ip_str, ntohs(addr_in->sin_port));
    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        close(sock);
        freeaddrinfo(res);
        setError("Connection failed");
        printf("HTTP: Connection failed to %s:%u\n", ip_str, ntohs(addr_in->sin_port));
        return false;
    }
    printf("HTTP: Connected successfully\n");

    freeaddrinfo(res);

    // Build HTTP request
    char request[1024];
    int len = snprintf(request, sizeof(request),
                      "%s %s HTTP/1.1\r\n"
                      "Host: %s:%u\r\n"
                      "Content-Type: %s\r\n"
                      "Content-Length: %zu\r\n"
                      "Connection: close\r\n"
                      "\r\n",
                      method, path, server_host_, server_port_,
                      content_type, body_len);

    // Send request header
    if (send(sock, request, len, 0) < 0) {
        close(sock);
        setError("Failed to send request");
        return false;
    }

    // Send body if present
    if (body && body_len > 0) {
        if (send(sock, body, body_len, 0) < 0) {
            close(sock);
            setError("Failed to send body");
            return false;
        }
    }

    // Read response
    ssize_t recv_len = recv(sock, response_buf, response_buf_len - 1, 0);
    close(sock);

    if (recv_len < 0) {
        setError("Failed to receive response");
        return false;
    }

    response_buf[recv_len] = '\0';

    // Parse response
    int status_code = 0;
    const char* response_body = nullptr;
    size_t response_body_len = 0;

    if (!parseHttpResponse(response_buf, recv_len, &status_code, &response_body, &response_body_len)) {
        setError("Failed to parse response");
        return false;
    }

    last_status_code_ = status_code;

    if (status_code >= 200 && status_code < 300) {
        last_error_[0] = '\0';  // Clear error
        return true;
    } else {
        snprintf(last_error_, sizeof(last_error_), "HTTP %d", status_code);
        return false;
    }
}

bool HttpClient::parseHttpResponse(const char* response, size_t response_len,
                                   int* status_code, const char** body, size_t* body_len)
{
    // Parse status line: "HTTP/1.1 200 OK\r\n"
    const char* status_line_end = strstr(response, "\r\n");
    if (!status_line_end) {
        return false;
    }

    // Extract status code
    const char* status_start = strchr(response, ' ');
    if (!status_start || status_start >= status_line_end) {
        return false;
    }

    *status_code = atoi(status_start + 1);

    // Find body (after "\r\n\r\n")
    const char* body_start = strstr(response, "\r\n\r\n");
    if (body_start) {
        body_start += 4;  // Skip "\r\n\r\n"
        *body = body_start;
        *body_len = response_len - (body_start - response);
    } else {
        *body = nullptr;
        *body_len = 0;
    }

    return true;
}

void HttpClient::setError(const char* error)
{
    strncpy(last_error_, error, sizeof(last_error_) - 1);
    last_error_[sizeof(last_error_) - 1] = '\0';
}

bool HttpClient::uploadLogFileChunk(const char* mac_address,
                                    const char* filename,
                                    const uint8_t* chunk_data,
                                    size_t chunk_size,
                                    size_t chunk_offset,
                                    size_t total_size,
                                    bool is_last_chunk,
                                    uint32_t chunk_crc32,
                                    const char* session_id,
                                    char* out_session_id)
{
    printf("HTTP: Uploading chunk offset=%zu size=%zu (last=%d)\n",
           chunk_offset, chunk_size, is_last_chunk);

    // Build upload path: /logs/upload/{device_mac}/chunk
    char path[128];
    snprintf(path, sizeof(path), "/logs/upload/%s/chunk", mac_address);

    // Create socket
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", server_port_);

    if (getaddrinfo(server_host_, port_str, &hints, &res) != 0) {
        setError("DNS lookup failed");
        return false;
    }

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        freeaddrinfo(res);
        setError("Socket creation failed");
        return false;
    }

    // Connect
    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        close(sock);
        freeaddrinfo(res);
        setError("Connection failed");
        return false;
    }

    freeaddrinfo(res);

    // Build HTTP request with chunk metadata headers
    char header[768];
    int header_len;

    if (session_id) {
        // Resume with session ID
        header_len = snprintf(header, sizeof(header),
                 "POST %s HTTP/1.1\r\n"
                 "Host: %s:%u\r\n"
                 "X-Filename: %s\r\n"
                 "X-Chunk-Offset: %zu\r\n"
                 "X-Chunk-Size: %zu\r\n"
                 "X-Total-Size: %zu\r\n"
                 "X-Is-Last-Chunk: %s\r\n"
                 "X-Chunk-CRC32: %08X\r\n"
                 "X-Session-ID: %s\r\n"
                 "Content-Type: application/octet-stream\r\n"
                 "Content-Length: %zu\r\n"
                 "Connection: close\r\n"
                 "\r\n",
                 path, server_host_, server_port_, filename,
                 chunk_offset, chunk_size, total_size,
                 is_last_chunk ? "true" : "false",
                 chunk_crc32,
                 session_id,
                 chunk_size);
    } else {
        // New upload (no session ID)
        header_len = snprintf(header, sizeof(header),
                 "POST %s HTTP/1.1\r\n"
                 "Host: %s:%u\r\n"
                 "X-Filename: %s\r\n"
                 "X-Chunk-Offset: %zu\r\n"
                 "X-Chunk-Size: %zu\r\n"
                 "X-Total-Size: %zu\r\n"
                 "X-Is-Last-Chunk: %s\r\n"
                 "X-Chunk-CRC32: %08X\r\n"
                 "Content-Type: application/octet-stream\r\n"
                 "Content-Length: %zu\r\n"
                 "Connection: close\r\n"
                 "\r\n",
                 path, server_host_, server_port_, filename,
                 chunk_offset, chunk_size, total_size,
                 is_last_chunk ? "true" : "false",
                 chunk_crc32,
                 chunk_size);
    }

    // Send header
    if (send(sock, header, header_len, 0) < 0) {
        close(sock);
        setError("Failed to send header");
        return false;
    }

    // Send chunk data
    size_t sent = 0;
    while (sent < chunk_size) {
        ssize_t result = send(sock, chunk_data + sent, chunk_size - sent, 0);
        if (result < 0) {
            close(sock);
            setError("Failed to send chunk data");
            return false;
        }
        sent += result;
    }

    // Read response
    char response_buf[1024];
    ssize_t recv_len = recv(sock, response_buf, sizeof(response_buf) - 1, 0);
    close(sock);

    if (recv_len < 0) {
        setError("Failed to receive response");
        return false;
    }

    response_buf[recv_len] = '\0';

    // Parse response
    int status_code = 0;
    const char* body = nullptr;
    size_t body_len = 0;

    if (!parseHttpResponse(response_buf, recv_len, &status_code, &body, &body_len)) {
        setError("Failed to parse response");
        return false;
    }

    last_status_code_ = status_code;

    if (status_code == 200) {
        // Parse JSON response to extract session_id
        if (out_session_id && body) {
            // Simple JSON parsing: look for "session_id":"<uuid>"
            const char* sid_key = strstr(body, "\"session_id\"");
            if (sid_key) {
                const char* sid_value = strchr(sid_key, ':');
                if (sid_value) {
                    sid_value = strchr(sid_value, '"');
                    if (sid_value) {
                        sid_value++;  // Skip opening quote
                        const char* sid_end = strchr(sid_value, '"');
                        if (sid_end && (sid_end - sid_value) <= 36) {
                            strncpy(out_session_id, sid_value, sid_end - sid_value);
                            out_session_id[sid_end - sid_value] = '\0';
                        }
                    }
                }
            }
        }

        printf("HTTP: Chunk upload successful\n");
        return true;
    } else if (status_code == 409) {
        // Offset mismatch - server expects different offset
        snprintf(last_error_, sizeof(last_error_),
                "Offset mismatch (server has different data)");
        printf("HTTP: %s\n", last_error_);
        return false;
    } else {
        snprintf(last_error_, sizeof(last_error_),
                "Chunk upload failed with status %d", status_code);
        printf("HTTP: %s\n", last_error_);
        return false;
    }
}

bool HttpClient::queryUploadSession(const char* mac_address,
                                    const char* filename,
                                    char* out_session_id,
                                    size_t* out_bytes_received,
                                    size_t* out_chunk_size)
{
    // Build query path: /logs/upload/{device_mac}/session?filename={filename}
    char path[256];
    snprintf(path, sizeof(path), "/logs/upload/%s/session?filename=%s",
             mac_address, filename);

    char response_buf[1024];
    bool result = sendRequest("GET", path, "application/json",
                             nullptr, 0,
                             response_buf, sizeof(response_buf));

    if (!result || last_status_code_ != 200) {
        return false;
    }

    // Parse JSON response
    // Look for: "session_found":true, "session_id":"<uuid>", "bytes_received":<num>, "chunk_size":<num>

    // Check session_found
    const char* found_key = strstr(response_buf, "\"session_found\"");
    if (found_key) {
        const char* found_value = strchr(found_key, ':');
        if (found_value && strstr(found_value, "true")) {
            // Session exists - extract fields

            // Extract session_id
            const char* sid_key = strstr(response_buf, "\"session_id\"");
            if (sid_key && out_session_id) {
                const char* sid_value = strchr(sid_key, ':');
                if (sid_value) {
                    sid_value = strchr(sid_value, '"');
                    if (sid_value) {
                        sid_value++;
                        const char* sid_end = strchr(sid_value, '"');
                        if (sid_end && (sid_end - sid_value) <= 36) {
                            strncpy(out_session_id, sid_value, sid_end - sid_value);
                            out_session_id[sid_end - sid_value] = '\0';
                        }
                    }
                }
            }

            // Extract bytes_received
            const char* bytes_key = strstr(response_buf, "\"bytes_received\"");
            if (bytes_key && out_bytes_received) {
                const char* bytes_value = strchr(bytes_key, ':');
                if (bytes_value) {
                    *out_bytes_received = atoi(bytes_value + 1);
                }
            }

            // Extract chunk_size
            const char* chunk_key = strstr(response_buf, "\"chunk_size\"");
            if (chunk_key && out_chunk_size) {
                const char* chunk_value = strchr(chunk_key, ':');
                if (chunk_value) {
                    *out_chunk_size = atoi(chunk_value + 1);
                }
            }

            return true;  // Session found
        }
    }

    return false;  // No session found
}
