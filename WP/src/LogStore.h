#ifndef LOGSTORE_H
#define LOGSTORE_H

#include <stdint.h>
#include <stdbool.h>
#include "SdCardBase.h"
#include "lfs.h"

// SD card is divided into fixed-size chunks. Chunk 0 is LittleFS (config,
// metadata). Chunks 1..N are raw log storage written sequentially with no
// filesystem overhead. See doc/chunked-log-storage-design.md.

// Fixed number of chunks regardless of card size. Chunk size adapts:
// 64 GB card → 64 MiB chunks, 128 GB → 128 MiB, 2 TB → 2 GiB, etc.
#define LOGSTORE_NUM_CHUNKS         1024
#define LOGSTORE_BITMAP_BYTES       (LOGSTORE_NUM_CHUNKS / 8)   // 128 bytes

// Maximum number of chunks a single log can span
#define LOGSTORE_MAX_CHUNKS_PER_LOG 16

// Metadata file naming: /log_42.meta in LFS, presented as log_42.um4 via API
#define LOGSTORE_META_PREFIX        "log_"
#define LOGSTORE_META_SUFFIX        ".meta"

struct LogStoreLogInfo {
    uint32_t log_number;
    uint32_t num_chunks;
    uint16_t chunks[LOGSTORE_MAX_CHUNKS_PER_LOG];
    uint32_t write_offset;          // bytes written in current chunk
    uint32_t total_bytes;           // total bytes across all chunks
    bool     active;                // true if this log is currently being written
};

class LogStore {
public:
    LogStore();

    // Initialize from mounted LFS + SD card. Scans metadata files,
    // builds free-chunk bitmap, closes any previously-active log.
    // Returns true on success.
    bool init(lfs_t* lfs, SdCardBase* sd);

    // Create a new log. Allocates a chunk, creates the metadata file,
    // returns the log number. Returns -1 on failure (no free chunks).
    int32_t createLog();

    // Write data to the active log. Writes raw sectors, updates metadata.
    // Returns bytes written, or -1 on error.
    int32_t write(const uint8_t* data, uint32_t len);

    // Sync the metadata file to LFS (commits the current write offset).
    // Called periodically by the Logger after each flush.
    bool syncMetadata();

    // Close the active log (mark as not active, final metadata sync).
    bool closeActiveLog();

    // Delete a log by number. Removes metadata file, frees chunks.
    bool deleteLog(uint32_t log_number);

    // Get info about a specific log by number.
    bool getLogInfo(uint32_t log_number, LogStoreLogInfo* info);

    // Read data from a log. Used by HTTP download path.
    // Reads up to len bytes starting at byte offset into the provided buffer.
    // Returns bytes read, or -1 on error.
    int32_t readLog(uint32_t log_number, uint32_t offset, uint8_t* buffer, uint32_t len);

    // Enumerate all logs. Calls the callback for each log found.
    // Callback receives log_number, total_bytes, active flag.
    // Returns number of logs found.
    int32_t enumerate(void (*callback)(uint32_t log_number, uint32_t total_bytes, bool active, void* ctx), void* ctx);

    // Get the active log number, or -1 if none.
    int32_t getActiveLogNumber() const { return active_log_number; }

    // Verify filesystem integrity. Scans all .meta files (including test
    // files) and checks for errors. Returns number of errors found.
    uint32_t verify();

    // Get total chunk count and free chunk count for status reporting.
    uint32_t getTotalChunks() const { return LOGSTORE_NUM_CHUNKS; }
    uint32_t getFreeChunks() const;

private:
    lfs_t*      lfs;
    SdCardBase* sd;
    uint32_t    chunk_sectors;      // sectors per chunk, derived from card size / NUM_CHUNKS
    uint32_t    chunk_bytes;        // bytes per chunk (chunk_sectors * 512)
    int32_t     active_log_number;  // -1 if no active log
    uint32_t    next_log_number;    // next log number to assign

    // Free-chunk bitmap: bit set = chunk in use
    uint8_t     bitmap[LOGSTORE_BITMAP_BYTES];

    // Cached state for the active log (avoids re-reading metadata on every write)
    LogStoreLogInfo active_log;
    uint32_t    active_sector;      // next sector to write within current chunk

    // Bitmap helpers
    void        markChunk(uint16_t chunk, bool in_use);
    bool        isChunkFree(uint16_t chunk) const;
    int32_t     allocateChunk();     // returns chunk number, or -1 if full

    // Chunk-to-sector arithmetic
    uint32_t    chunkStartSector(uint16_t chunk) const { return (uint32_t)chunk * chunk_sectors; }

    // Metadata file I/O
    bool        writeMetadata(const LogStoreLogInfo* info);
    bool        readMetadata(const char* filename, LogStoreLogInfo* info);
    void        metadataFilename(uint32_t log_number, char* buf, uint32_t bufsize);

    // Extend the active log into a new chunk when the current one fills up
    bool        extendActiveLog();
};

extern LogStore* logStore;

#endif
