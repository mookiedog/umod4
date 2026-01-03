#ifndef LOG_UPLOADER_H
#define LOG_UPLOADER_H

#include "lfs.h"
#include <cstdint>

class HttpClient;

/**
 * Log file upload manager for umod4 WP.
 *
 * Scans SD card for .um4 log files and uploads them to server.
 * Uses stateless approach: queries server for existing files,
 * uploads files that server doesn't have.
 */
class LogUploader {
public:
    LogUploader(HttpClient* http_client, lfs_t* lfs);
    ~LogUploader();

    /**
     * Scan SD card and upload all .um4 files not already on server.
     *
     * @param device_mac Device MAC address (for upload path)
     * @param active_log_name Name of currently active log file to skip (or nullptr)
     * @return Number of files uploaded, or -1 on error
     */
    int uploadAllLogs(const char* device_mac, const char* active_log_name = nullptr);

    /**
     * Get last error message.
     */
    const char* getLastError() const { return last_error_; }

private:
    HttpClient* http_client_;
    lfs_t* lfs_;
    char last_error_[128];

    // Get list of .um4 files already on server
    bool getServerFileList(const char* device_mac, char* file_list_buf, size_t buf_len);

    // Check if filename exists in server list
    bool isFileOnServer(const char* filename, const char* server_list);

    // Upload a single log file
    bool uploadFile(const char* device_mac, const char* filename);

    void setError(const char* error);
};

#endif // LOG_UPLOADER_H
