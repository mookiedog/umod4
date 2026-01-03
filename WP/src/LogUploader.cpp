#include "LogUploader.h"
#include "HttpClient.h"
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

    // Use static buffer for chunked reading
    const size_t chunk_size = 65536;  // 64KB chunks
    static uint8_t buffer[chunk_size];

    printf("LogUploader: Uploading %s (%u bytes) in %u-byte chunks\n",
           filename, (unsigned)total_size, (unsigned)chunk_size);

    // Read and upload file in chunks
    size_t total_uploaded = 0;
    while (total_uploaded < (size_t)total_size) {
        // Read next chunk
        size_t to_read = ((size_t)total_size - total_uploaded > chunk_size)
                         ? chunk_size
                         : ((size_t)total_size - total_uploaded);

        lfs_ssize_t bytes_read = lfs_file_read(lfs_, &file, buffer, to_read);
        if (bytes_read < 0 || (size_t)bytes_read != to_read) {
            lfs_file_close(lfs_, &file);
            setError("Failed to read file chunk");
            return false;
        }

        // Upload chunk via HTTP client
        bool result = http_client_->uploadLogFile(
            device_mac,
            filename,
            buffer,
            bytes_read,
            nullptr  // No progress callback for now
        );

        if (!result) {
            lfs_file_close(lfs_, &file);
            snprintf(last_error_, sizeof(last_error_),
                    "HTTP upload failed: %s", http_client_->getLastError());
            return false;
        }

        total_uploaded += bytes_read;
        printf("LogUploader: Uploaded %u/%u bytes\n",
               (unsigned)total_uploaded, (unsigned)total_size);
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
