#include "LogUploader.h"
#include "HttpClient.h"
#include "Crc.h"
#include "pico/stdlib.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <cstdio>
#include <cstring>

LogUploader::LogUploader(HttpClient* http_client, lfs_t* lfs)
    : http_client_(http_client),
      lfs_(lfs)
{
    last_error_[0] = '\0';
}

LogUploader::~LogUploader()
{
}

int LogUploader::uploadAllLogs(const char* device_mac, const char* active_log_name)
{
    if (!lfs_ || !http_client_) {
        setError("LFS or HTTP client not initialized");
        return -1;
    }

    printf("LogUploader: Scanning for .um4 files to upload...\n");
    if (active_log_name) {
        printf("LogUploader: Skipping active log file: %s\n", active_log_name);
    }

    // Get list of files already on server
    char server_list[4096];
    if (!getServerFileList(device_mac, server_list, sizeof(server_list))) {
        printf("LogUploader: Failed to get server file list: %s\n", last_error_);
        printf("LogUploader: Continuing anyway, will attempt to upload all files\n");
        server_list[0] = '\0';  // Empty list - upload everything
    }

    // Scan root directory for .um4 files
    lfs_dir_t dir;
    int err = lfs_dir_open(lfs_, &dir, "/");
    if (err < 0) {
        setError("Failed to open root directory");
        return -1;
    }

    int uploaded_count = 0;
    struct lfs_info info;

    while (true) {
        err = lfs_dir_read(lfs_, &dir, &info);
        if (err < 0) {
            lfs_dir_close(lfs_, &dir);
            setError("Failed to read directory");
            return -1;
        }

        // End of directory
        if (err == 0) {
            break;
        }

        // Skip directories and non-.um4 files
        if (info.type != LFS_TYPE_REG) {
            continue;
        }

        const char* ext = strrchr(info.name, '.');
        if (!ext || strcmp(ext, ".um4") != 0) {
            continue;
        }

        printf("LogUploader: Found %s (%u bytes)\n", info.name, info.size);

        // Skip currently active log file
        if (active_log_name && strcmp(info.name, active_log_name) == 0) {
            printf("LogUploader: %s is currently active, skipping\n", info.name);
            continue;
        }

        // Check if already on server
        if (server_list[0] != '\0' && isFileOnServer(info.name, server_list)) {
            printf("LogUploader: %s already on server, skipping\n", info.name);
            continue;
        }

        // Upload file
        if (uploadFile(device_mac, info.name)) {
            uploaded_count++;
            printf("LogUploader: %s uploaded successfully\n", info.name);
        } else {
            printf("LogUploader: Failed to upload %s: %s\n", info.name, last_error_);
            // Continue with other files
        }
    }

    lfs_dir_close(lfs_, &dir);

    printf("LogUploader: Upload complete. %d files uploaded\n", uploaded_count);
    return uploaded_count;
}

bool LogUploader::getServerFileList(const char* device_mac, char* file_list_buf, size_t buf_len)
{
    // Query server: GET /logs/list/{device_mac}
    // For simplicity, we'll make a raw HTTP request here instead of adding to HttpClient

    // This is a simplified implementation - in a real system you'd want HttpClient to support GET requests
    // For now, just return false and upload everything
    return false;
}

bool LogUploader::isFileOnServer(const char* filename, const char* server_list)
{
    // Simple substring search in JSON array
    // Server returns: ["log_1.um4", "log_2.um4", ...]
    return (strstr(server_list, filename) != nullptr);
}

bool LogUploader::uploadFile(const char* device_mac, const char* filename)
{
    return uploadFileChunked(device_mac, filename);
}

bool LogUploader::uploadFileChunked(const char* device_mac, const char* filename)
{
    // Open file
    lfs_file_t file;
    int err = lfs_file_open(lfs_, &file, filename, LFS_O_RDONLY);
    if (err < 0) {
        setError("Failed to open file");
        return false;
    }

    // Get file size
    lfs_soff_t total_size = lfs_file_size(lfs_, &file);
    if (total_size < 0) {
        lfs_file_close(lfs_, &file);
        setError("Failed to get file size");
        return false;
    }

    printf("LogUploader: Uploading %s (%u bytes) in %u-byte chunks\n",
           filename, (unsigned)total_size, (unsigned)CHUNK_SIZE);

    // Check for existing upload session (for resumption)
    char session_id[37] = {0};
    size_t bytes_already_received = 0;
    size_t server_chunk_size = CHUNK_SIZE;

    bool session_exists = http_client_->queryUploadSession(
        device_mac, filename,
        session_id, &bytes_already_received, &server_chunk_size
    );

    if (session_exists) {
        printf("LogUploader: Resuming upload from offset %u (session: %s)\n",
               (unsigned)bytes_already_received, session_id);

        // Seek to resume position
        err = lfs_file_seek(lfs_, &file, bytes_already_received, LFS_SEEK_SET);
        if (err < 0) {
            lfs_file_close(lfs_, &file);
            setError("Failed to seek to resume position");
            return false;
        }
    } else {
        printf("LogUploader: Starting new upload\n");
        session_id[0] = '\0';  // No session ID for first chunk
    }

    // Allocate chunk buffer (static to avoid stack overflow)
    static uint8_t chunk_buffer[CHUNK_SIZE];

    // Upload file in chunks with retry logic
    size_t current_offset = bytes_already_received;

    while (current_offset < (size_t)total_size) {
        // Calculate chunk size (last chunk may be smaller)
        size_t chunk_size = ((size_t)total_size - current_offset > CHUNK_SIZE)
                            ? CHUNK_SIZE
                            : ((size_t)total_size - current_offset);

        // Read chunk from file
        lfs_ssize_t bytes_read = lfs_file_read(lfs_, &file, chunk_buffer, chunk_size);
        if (bytes_read < 0 || (size_t)bytes_read != chunk_size) {
            lfs_file_close(lfs_, &file);
            setError("Failed to read file chunk");
            return false;
        }

        // Calculate CRC32 of chunk
        uint32_t chunk_crc32 = Crc::crc32(chunk_buffer, chunk_size);

        // Determine if this is the last chunk
        bool is_last_chunk = (current_offset + chunk_size >= (size_t)total_size);

        // Retry logic for this chunk
        bool chunk_uploaded = false;
        int retry_count = 0;

        for (retry_count = 0; retry_count < MAX_CHUNK_RETRIES; retry_count++) {
            if (retry_count > 0) {
                printf("LogUploader: Retry %d/%d for chunk at offset %u\n",
                       retry_count, MAX_CHUNK_RETRIES, (unsigned)current_offset);
            }

            // Upload chunk
            char new_session_id[37] = {0};
            bool result = http_client_->uploadLogFileChunk(
                device_mac,
                filename,
                chunk_buffer,
                chunk_size,
                current_offset,
                total_size,
                is_last_chunk,
                chunk_crc32,
                (session_id[0] != '\0') ? session_id : nullptr,  // Use session ID if we have one
                new_session_id
            );

            if (result) {
                // Chunk uploaded successfully
                chunk_uploaded = true;

                // Update session ID if server returned one
                if (new_session_id[0] != '\0') {
                    strncpy(session_id, new_session_id, sizeof(session_id) - 1);
                    session_id[sizeof(session_id) - 1] = '\0';
                }

                break;  // Success - exit retry loop
            }

            // Check if error is offset mismatch (409) - don't retry, fail immediately
            if (http_client_->getLastStatusCode() == 409) {
                printf("LogUploader: Offset mismatch - upload state corrupted\n");
                break;  // Exit retry loop
            }

            // Other error - will retry unless max retries reached
        }

        if (!chunk_uploaded) {
            // Failed after all retries
            lfs_file_close(lfs_, &file);
            snprintf(last_error_, sizeof(last_error_),
                    "Chunk upload failed after %d retries: %s",
                    MAX_CHUNK_RETRIES, http_client_->getLastError());
            return false;
        }

        // Move to next chunk
        current_offset += chunk_size;

        printf("LogUploader: Progress: %u/%u bytes (%.1f%%)\n",
               (unsigned)current_offset, (unsigned)total_size,
               (current_offset * 100.0f) / total_size);
    }

    lfs_file_close(lfs_, &file);
    printf("LogUploader: Upload complete for %s\n", filename);
    return true;
}

void LogUploader::setError(const char* error)
{
    strncpy(last_error_, error, sizeof(last_error_) - 1);
    last_error_[sizeof(last_error_) - 1] = '\0';
}
