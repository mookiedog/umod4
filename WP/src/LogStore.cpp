#include "LogStore.h"
#include "lfsMgr.h"
#include "wp_rtt.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

LogStore* logStore = nullptr;

LogStore::LogStore()
{
    lfs = nullptr;
    sd = nullptr;
    chunk_sectors = 0;
    chunk_bytes = 0;
    active_log_number = -1;
    next_log_number = 1;
    active_sector = 0;
    memset(bitmap, 0, sizeof(bitmap));
    memset(&active_log, 0, sizeof(active_log));
}

// -------------------------------------------------------------------------
// Bitmap helpers

void LogStore::markChunk(uint16_t chunk, bool in_use)
{
    if (chunk >= LOGSTORE_NUM_CHUNKS) return;
    if (in_use)
        bitmap[chunk / 8] |= (1 << (chunk % 8));
    else
        bitmap[chunk / 8] &= ~(1 << (chunk % 8));
}

bool LogStore::isChunkFree(uint16_t chunk) const
{
    if (chunk >= LOGSTORE_NUM_CHUNKS) return false;
    return (bitmap[chunk / 8] & (1 << (chunk % 8))) == 0;
}

int32_t LogStore::allocateChunk()
{
    // Chunk 0 is always LFS — start search at 1
    for (uint32_t i = 1; i < LOGSTORE_NUM_CHUNKS; i++) {
        if (isChunkFree(i)) {
            markChunk(i, true);
            return (int32_t)i;
        }
    }
    return -1;
}

uint32_t LogStore::getFreeChunks() const
{
    uint32_t free = 0;
    for (uint32_t i = 1; i < LOGSTORE_NUM_CHUNKS; i++) {
        if (isChunkFree(i)) free++;
    }
    return free;
}

// -------------------------------------------------------------------------
// Metadata filename: log_42 -> "/log_42.meta"

void LogStore::metadataFilename(uint32_t log_number, char* buf, uint32_t bufsize)
{
    snprintf(buf, bufsize, "/%s%lu%s",
             LOGSTORE_META_PREFIX,
             (unsigned long)log_number,
             LOGSTORE_META_SUFFIX);
}

// -------------------------------------------------------------------------
// Write metadata file as JSON to LFS

bool LogStore::writeMetadata(const LogStoreLogInfo* info)
{
    char path[32];
    metadataFilename(info->log_number, path, sizeof(path));

    // Build JSON into a stack buffer. Worst case with 16 chunks:
    // {"log":99999,"active":false,"chunks":[999,999,...,999],"offset":134217728,"total":999999999}
    // Well under 256 bytes.
    char json[256];
    int pos = snprintf(json, sizeof(json),
        "{\"log\":%lu,\"active\":%s,\"chunks\":[",
        (unsigned long)info->log_number,
        info->active ? "true" : "false");

    for (uint32_t i = 0; i < info->num_chunks && pos < (int)sizeof(json) - 40; i++) {
        if (i > 0) json[pos++] = ',';
        pos += snprintf(json + pos, sizeof(json) - pos, "%u", info->chunks[i]);
    }

    pos += snprintf(json + pos, sizeof(json) - pos,
        "],\"offset\":%lu,\"total\":%lu}",
        (unsigned long)info->write_offset,
        (unsigned long)info->total_bytes);

    lfs_file_t f;
    lfs_file_config cfg = {};
    static uint8_t meta_cache[LFS_CACHE_SIZE];
    cfg.buffer = meta_cache;

    int err = lfs_file_opencfg(lfs, &f, path,
                               LFS_O_CREAT | LFS_O_TRUNC | LFS_O_WRONLY, &cfg);
    if (err < 0) {
        printf("LogStore: failed to open %s for write: %d\n", path, err);
        return false;
    }

    lfs_ssize_t written = lfs_file_write(lfs, &f, json, pos);
    lfs_file_close(lfs, &f);

    if (written != pos) {
        printf("LogStore: short write to %s: %d/%d\n", path, (int)written, pos);
        return false;
    }

    return true;
}

// -------------------------------------------------------------------------
// Read metadata file from LFS, parse JSON

bool LogStore::readMetadata(const char* filename, LogStoreLogInfo* info)
{
    char path[32];
    snprintf(path, sizeof(path), "/%s", filename);

    lfs_file_t f;
    lfs_file_config cfg = {};
    static uint8_t meta_cache[LFS_CACHE_SIZE];
    cfg.buffer = meta_cache;

    int err = lfs_file_opencfg(lfs, &f, path, LFS_O_RDONLY, &cfg);
    if (err < 0) return false;

    char json[256];
    lfs_ssize_t n = lfs_file_read(lfs, &f, json, sizeof(json) - 1);
    lfs_file_close(lfs, &f);

    if (n <= 0) return false;
    json[n] = '\0';

    memset(info, 0, sizeof(*info));

    // Parse log number
    const char* p = strstr(json, "\"log\":");
    if (!p) return false;
    info->log_number = strtoul(p + 6, nullptr, 10);

    // Parse active flag
    p = strstr(json, "\"active\":");
    if (p) info->active = (strstr(p, "true") == p + 9);

    // Parse chunks array
    p = strstr(json, "\"chunks\":[");
    if (!p) return false;
    p += 10;    // skip past "chunks":[
    info->num_chunks = 0;
    while (*p && *p != ']' && info->num_chunks < LOGSTORE_MAX_CHUNKS_PER_LOG) {
        while (*p == ' ' || *p == ',') p++;
        if (*p == ']') break;
        info->chunks[info->num_chunks++] = (uint16_t)strtoul(p, (char**)&p, 10);
    }

    // Parse write offset
    p = strstr(json, "\"offset\":");
    if (p) info->write_offset = strtoul(p + 9, nullptr, 10);

    // Parse total bytes
    p = strstr(json, "\"total\":");
    if (p) info->total_bytes = strtoul(p + 8, nullptr, 10);

    return true;
}

// -------------------------------------------------------------------------
// Init: scan LFS for metadata files, build bitmap, close any active log.

bool LogStore::init(lfs_t* _lfs, SdCardBase* _sd)
{
    lfs = _lfs;
    sd = _sd;

    // Derive chunk size from card capacity / fixed chunk count
    uint32_t total_sectors = sd->getSectorCount();
    chunk_sectors = total_sectors / LOGSTORE_NUM_CHUNKS;
    chunk_bytes = chunk_sectors * 512;
    uint32_t waste_sectors = total_sectors - (chunk_sectors * LOGSTORE_NUM_CHUNKS);

    printf("LogStore: SD card %.1f GB (%lu sectors)\n",
           (float)total_sectors * 512 / (1024 * 1024 * 1024),
           (unsigned long)total_sectors);
    printf("LogStore: %u chunks x %.1f MiB each (chunk 0 = LFS, 1-%u = log data)\n",
           LOGSTORE_NUM_CHUNKS,
           (float)chunk_bytes / (1024 * 1024),
           LOGSTORE_NUM_CHUNKS - 1);
    printf("LogStore: %lu sectors (%.1f MiB) unused at end of card\n",
           (unsigned long)waste_sectors,
           (float)waste_sectors * 512 / (1024 * 1024));

    if (chunk_sectors < 1024) {
        printf("LogStore: card too small (%lu sectors/chunk)\n",
               (unsigned long)chunk_sectors);
        return false;
    }

    // Clear bitmap; mark chunk 0 (LFS) as in-use
    memset(bitmap, 0, sizeof(bitmap));
    markChunk(0, true);

    // Scan LFS for log metadata files (strictly log_DIGITS.meta only —
    // test files like log_t900.meta are ignored at boot so they can't
    // pollute log numbering or claim chunks if left behind after a crash).
    lfs_dir_t dir;
    struct lfs_info entry;
    int err = lfs_dir_open(lfs, &dir, "/");
    if (err < 0) {
        printf("LogStore: failed to open root dir: %d\n", err);
        return false;
    }

    uint32_t log_count = 0;
    next_log_number = 1;

    while (lfs_dir_read(lfs, &dir, &entry) > 0) {
        if (entry.type != LFS_TYPE_REG)
            continue;

        // Strictly match log_DIGITS.meta
        if (strncmp(entry.name, "log_", 4) != 0) continue;
        const char* p = entry.name + 4;
        if (*p < '0' || *p > '9') continue;
        while (*p >= '0' && *p <= '9') p++;
        if (strcmp(p, ".meta") != 0) continue;

        LogStoreLogInfo info;
        if (!readMetadata(entry.name, &info))
            continue;

        for (uint32_t i = 0; i < info.num_chunks; i++) {
            if (info.chunks[i] < LOGSTORE_NUM_CHUNKS)
                markChunk(info.chunks[i], true);
        }

        if (info.log_number >= next_log_number)
            next_log_number = info.log_number + 1;

        if (info.active) {
            printf("LogStore: closing stale active log %lu\n",
                   (unsigned long)info.log_number);
            info.active = false;
            writeMetadata(&info);
        }

        log_count++;
    }
    lfs_dir_close(lfs, &dir);

    // Run integrity check
    uint32_t fsck_errors = verify();

    uint32_t free = getFreeChunks();
    printf("LogStore: %lu chunks total, %lu logs found, %lu chunks free (%.0f MiB)\n",
           (unsigned long)LOGSTORE_NUM_CHUNKS,
           (unsigned long)log_count,
           (unsigned long)free,
           (float)free * chunk_bytes / (1024 * 1024));

    return true;
}

// -------------------------------------------------------------------------
// Verify filesystem integrity. Scans ALL .meta files (including test files
// like log_t900.meta) and checks for errors. Does not modify any state.
// Returns number of errors found.

uint32_t LogStore::verify()
{
    if (!lfs) return 1;

    // Build a temporary bitmap from scratch for verification
    uint8_t check_bitmap[LOGSTORE_BITMAP_BYTES];
    memset(check_bitmap, 0, sizeof(check_bitmap));
    check_bitmap[0] |= 1;  // chunk 0 = LFS, always in use

    uint32_t errors = 0;

    lfs_dir_t dir;
    struct lfs_info entry;
    int err = lfs_dir_open(lfs, &dir, "/");
    if (err < 0) {
        printf("LogStore: FSCK: failed to open root dir: %d\n", err);
        return 1;
    }

    while (lfs_dir_read(lfs, &dir, &entry) > 0) {
        if (entry.type != LFS_TYPE_REG)
            continue;

        const char* dot = strrchr(entry.name, '.');
        if (!dot || strcmp(dot, LOGSTORE_META_SUFFIX) != 0)
            continue;

        LogStoreLogInfo info;
        char msg[128];
        if (!readMetadata(entry.name, &info)) {
            snprintf(msg, sizeof(msg), "CORRUPT: unreadable metadata in %s", entry.name);
            printf("LogStore: FSCK ERROR: %s\n", msg);
            vfy_printf("{\"logstore_fsck_error\":{\"msg\":\"%s\"}}\n", msg);
            errors++;
            continue;
        }

        for (uint32_t i = 0; i < info.num_chunks; i++) {
            uint16_t c = info.chunks[i];
            if (c == 0) {
                snprintf(msg, sizeof(msg), "CHUNK0: log %lu references chunk 0 (LFS)",
                         (unsigned long)info.log_number);
                printf("LogStore: FSCK ERROR: %s\n", msg);
                vfy_printf("{\"logstore_fsck_error\":{\"msg\":\"%s\"}}\n", msg);
                errors++;
            } else if (c >= LOGSTORE_NUM_CHUNKS) {
                snprintf(msg, sizeof(msg), "RANGE: log %lu references chunk %u (max %u)",
                         (unsigned long)info.log_number, c, LOGSTORE_NUM_CHUNKS - 1);
                printf("LogStore: FSCK ERROR: %s\n", msg);
                vfy_printf("{\"logstore_fsck_error\":{\"msg\":\"%s\"}}\n", msg);
                errors++;
            } else if (check_bitmap[c / 8] & (1 << (c % 8))) {
                snprintf(msg, sizeof(msg), "DOUBLE: chunk %u claimed by log %lu but already in use",
                         c, (unsigned long)info.log_number);
                printf("LogStore: FSCK ERROR: %s\n", msg);
                vfy_printf("{\"logstore_fsck_error\":{\"msg\":\"%s\"}}\n", msg);
                errors++;
            } else {
                check_bitmap[c / 8] |= (1 << (c % 8));
            }
        }

        if (info.write_offset > chunk_bytes) {
            snprintf(msg, sizeof(msg), "OFFSET: log %lu write_offset %lu exceeds chunk size %lu",
                     (unsigned long)info.log_number,
                     (unsigned long)info.write_offset,
                     (unsigned long)chunk_bytes);
            printf("LogStore: FSCK ERROR: %s\n", msg);
            vfy_printf("{\"logstore_fsck_error\":{\"msg\":\"%s\"}}\n", msg);
            errors++;
        }

        if (info.num_chunks > 0) {
            uint32_t expected_max = (uint32_t)(info.num_chunks - 1) * chunk_bytes + info.write_offset;
            if (info.total_bytes != expected_max) {
                snprintf(msg, sizeof(msg), "SIZE: log %lu total %lu expected %lu (%u chunks + offset %lu)",
                         (unsigned long)info.log_number,
                         (unsigned long)info.total_bytes,
                         (unsigned long)expected_max,
                         info.num_chunks,
                         (unsigned long)info.write_offset);
                printf("LogStore: FSCK ERROR: %s\n", msg);
                vfy_printf("{\"logstore_fsck_error\":{\"msg\":\"%s\"}}\n", msg);
                errors++;
            }
        }
    }
    lfs_dir_close(lfs, &dir);

    printf("LogStore: FSCK %s (%lu errors)\n",
           errors == 0 ? "PASS" : "FAIL", (unsigned long)errors);

    return errors;
}

// -------------------------------------------------------------------------
// Create a new log: allocate a chunk, write metadata, set as active.

int32_t LogStore::createLog()
{
    if (active_log_number >= 0) {
        printf("LogStore: cannot create log — log %ld still active\n",
               (long)active_log_number);
        return -1;
    }

    int32_t chunk = allocateChunk();
    if (chunk < 0) {
        printf("LogStore: no free chunks\n");
        return -1;
    }

    memset(&active_log, 0, sizeof(active_log));
    active_log.log_number = next_log_number++;
    active_log.num_chunks = 1;
    active_log.chunks[0] = (uint16_t)chunk;
    active_log.write_offset = 0;
    active_log.total_bytes = 0;
    active_log.active = true;

    active_sector = chunkStartSector((uint16_t)chunk);
    active_log_number = (int32_t)active_log.log_number;

    if (!writeMetadata(&active_log)) {
        printf("LogStore: failed to write metadata for log %lu\n",
               (unsigned long)active_log.log_number);
        markChunk((uint16_t)chunk, false);
        active_log_number = -1;
        return -1;
    }

    printf("LogStore: created log %lu in chunk %ld\n",
           (unsigned long)active_log.log_number, (long)chunk);
    return active_log_number;
}

// -------------------------------------------------------------------------
// Write data to the active log.

int32_t LogStore::write(const uint8_t* data, uint32_t len)
{
    if (active_log_number < 0 || !sd)
        return -1;

    if (len == 0 || (len % 512) != 0)
        return -1;

    uint32_t total_written = 0;

    while (len > 0) {
        uint32_t chunk_end = chunkStartSector(active_log.chunks[active_log.num_chunks - 1])
                             + chunk_sectors;
        uint32_t remaining_bytes = (chunk_end - active_sector) * 512;

        // Write as much as fits in the current chunk
        uint32_t to_write = (len < remaining_bytes) ? len : remaining_bytes;

        if (to_write > 0) {
            sd_lock();
            SdErr_t err = sd->writeSectors(active_sector, to_write / 512, data);
            sd_unlock();

            if (err != SD_ERR_NOERR) {
                printf("LogStore: writeSectors failed at sector %lu: %d\n",
                       (unsigned long)active_sector, err);
                return total_written > 0 ? (int32_t)total_written : -1;
            }

            active_sector += to_write / 512;
            active_log.write_offset += to_write;
            active_log.total_bytes += to_write;
            data += to_write;
            len -= to_write;
            total_written += to_write;
        }

        // If there's more data, extend into a new chunk
        if (len > 0) {
            if (!extendActiveLog()) {
                printf("LogStore: cannot extend log %ld\n", (long)active_log_number);
                return total_written > 0 ? (int32_t)total_written : -1;
            }
        }
    }

    return (int32_t)total_written;
}

// -------------------------------------------------------------------------
// Seek the active log's write position to an absolute byte offset.

bool LogStore::seek(uint32_t offset)
{
    if (active_log_number < 0)
        return false;

    if (offset % 512 != 0) {
        printf("LogStore: seek offset %lu not sector-aligned\n",
               (unsigned long)offset);
        return false;
    }

    uint32_t chunk_idx = offset / chunk_bytes;

    // Allocate chunks as needed to reach the target
    while (chunk_idx >= active_log.num_chunks) {
        if (!extendActiveLog())
            return false;
    }

    uint32_t chunk_offset = offset % chunk_bytes;
    active_sector = chunkStartSector(active_log.chunks[chunk_idx]) + (chunk_offset / 512);
    active_log.write_offset = chunk_offset;
    active_log.total_bytes = offset;

    return writeMetadata(&active_log);
}

// -------------------------------------------------------------------------
// Sync metadata to LFS (called by Logger after each flush).

bool LogStore::syncMetadata()
{
    if (active_log_number < 0)
        return false;
    return writeMetadata(&active_log);
}

// -------------------------------------------------------------------------
// Close the active log.

bool LogStore::closeActiveLog()
{
    if (active_log_number < 0)
        return false;

    active_log.active = false;
    bool ok = writeMetadata(&active_log);

    printf("LogStore: closed log %ld (%lu bytes)\n",
           (long)active_log_number,
           (unsigned long)active_log.total_bytes);

    active_log_number = -1;
    return ok;
}

// -------------------------------------------------------------------------
// Delete a log: remove metadata file, free chunks.

bool LogStore::deleteLog(uint32_t log_number)
{
    if ((int32_t)log_number == active_log_number) {
        printf("LogStore: cannot delete active log %lu\n",
               (unsigned long)log_number);
        return false;
    }

    char path[32];
    metadataFilename(log_number, path, sizeof(path));

    // Read the metadata to find which chunks to free
    LogStoreLogInfo info;
    char filename[32];
    snprintf(filename, sizeof(filename), "%s%lu%s",
             LOGSTORE_META_PREFIX,
             (unsigned long)log_number,
             LOGSTORE_META_SUFFIX);

    if (!readMetadata(filename, &info)) {
        printf("LogStore: cannot read metadata for log %lu\n",
               (unsigned long)log_number);
        return false;
    }

    // Delete the .meta file first. LFS COW makes this atomic.
    // Only free the bitmap AFTER the delete succeeds — if lfs_remove
    // fails, the chunks must remain allocated to prevent double-use.
    int err = lfs_remove(lfs, path);
    if (err < 0) {
        printf("LogStore: failed to delete %s: %d\n", path, err);
        return false;
    }

    for (uint32_t i = 0; i < info.num_chunks; i++)
        markChunk(info.chunks[i], false);

    printf("LogStore: deleted log %lu (%lu chunks freed)\n",
           (unsigned long)log_number,
           (unsigned long)info.num_chunks);
    return true;
}

// -------------------------------------------------------------------------
// Get info about a specific log.

bool LogStore::getLogInfo(uint32_t log_number, LogStoreLogInfo* info)
{
    if ((int32_t)log_number == active_log_number) {
        *info = active_log;
        return true;
    }

    char filename[32];
    snprintf(filename, sizeof(filename), "%s%lu%s",
             LOGSTORE_META_PREFIX,
             (unsigned long)log_number,
             LOGSTORE_META_SUFFIX);

    return readMetadata(filename, info);
}

// -------------------------------------------------------------------------
// Read log data from raw chunks. Used by HTTP download path.

int32_t LogStore::readLog(uint32_t log_number, uint32_t offset, uint8_t* buffer, uint32_t len)
{
    LogStoreLogInfo info;
    if (!getLogInfo(log_number, &info))
        return -1;

    if (offset >= info.total_bytes)
        return 0;

    if (offset + len > info.total_bytes)
        len = info.total_bytes - offset;

    uint32_t bytes_read = 0;

    while (len > 0) {
        // Which chunk does this offset fall into?
        uint32_t chunk_idx = offset / chunk_bytes;
        uint32_t chunk_offset = offset % chunk_bytes;

        if (chunk_idx >= info.num_chunks)
            break;

        // How many bytes remain in this chunk?
        uint32_t avail = chunk_bytes - chunk_offset;
        uint32_t to_read = (len < avail) ? len : avail;

        // Align to sector boundaries
        uint32_t sector = chunkStartSector(info.chunks[chunk_idx]) + (chunk_offset / 512);
        uint32_t num_sectors = (to_read + 511) / 512;

        sd_lock();
        SdErr_t err = sd->readSectors(sector, num_sectors, buffer);
        sd_unlock();

        if (err != SD_ERR_NOERR) {
            printf("LogStore: readSectors failed at sector %lu: %d\n",
                   (unsigned long)sector, err);
            return -1;
        }

        buffer += to_read;
        offset += to_read;
        len -= to_read;
        bytes_read += to_read;
    }

    return (int32_t)bytes_read;
}

// -------------------------------------------------------------------------
// Enumerate all logs via callback.

int32_t LogStore::enumerate(void (*callback)(uint32_t, uint32_t, bool, void*), void* ctx)
{
    lfs_dir_t dir;
    struct lfs_info entry;
    int err = lfs_dir_open(lfs, &dir, "/");
    if (err < 0) return -1;

    int32_t count = 0;

    while (lfs_dir_read(lfs, &dir, &entry) > 0) {
        if (entry.type != LFS_TYPE_REG)
            continue;

        const char* dot = strrchr(entry.name, '.');
        if (!dot || strcmp(dot, LOGSTORE_META_SUFFIX) != 0)
            continue;

        LogStoreLogInfo info;
        if (!readMetadata(entry.name, &info))
            continue;

        // For the active log, use cached (up-to-date) state
        if ((int32_t)info.log_number == active_log_number)
            info = active_log;

        callback(info.log_number, info.total_bytes, info.active, ctx);
        count++;
    }

    lfs_dir_close(lfs, &dir);
    return count;
}

// -------------------------------------------------------------------------
// Extend the active log into a new chunk.

bool LogStore::extendActiveLog()
{
    if (active_log.num_chunks >= LOGSTORE_MAX_CHUNKS_PER_LOG) {
        printf("LogStore: log %ld at max chunks (%d)\n",
               (long)active_log_number, LOGSTORE_MAX_CHUNKS_PER_LOG);
        return false;
    }

    int32_t chunk = allocateChunk();
    if (chunk < 0) {
        printf("LogStore: no free chunks for extension\n");
        return false;
    }

    active_log.chunks[active_log.num_chunks++] = (uint16_t)chunk;
    active_log.write_offset = 0;
    active_sector = chunkStartSector((uint16_t)chunk);

    if (!writeMetadata(&active_log)) {
        printf("LogStore: failed to write metadata after extend\n");
        return false;
    }

    printf("LogStore: extended log %ld into chunk %ld (%u chunks total)\n",
           (long)active_log_number, (long)chunk, active_log.num_chunks);
    return true;
}
