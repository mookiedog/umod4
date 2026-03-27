#ifndef LFSMGR_H
#define LFSMGR_H

// Manage the storage subsystem for the WP processor.
//
// This module:
//  - owns the SD card (SdCardSDIO via SDIO 4-bit)
//  - manages the LittleFS lifecycle (mount/format/unmount)
//  - provides the lfs_read/lfs_prog/lfs_erase/lfs_sync callbacks that bridge LittleFS
//    to the SD card's sector interface
//  - provides hotplug support to allow SD cards to be inserted/removed at runtime
//  - mount/unmount callbacks decouple consumers (i.e. the Logger) from this module so they can
//    react to hotplug events without having to know SD card or filesystem management details.

#include "lfs.h"
#include "SdCardBase.h"

// LittleFS geometry — shared constants used by lfsMgr, Logger, and others.
// LFS_CACHE_SIZE must divide LFS_BLOCK_SIZE. Kept smaller than the block size
// to limit per-file heap allocation (each lfs_file_open malloc's one cache buffer).
#define LFS_BLOCK_SIZE  16384

// Experiments show that increasing LFS_CACHE_SIZE to 16384 improved large file downloads
// by a mere 2%.
#define LFS_CACHE_SIZE  4096

// Filesystem state
extern lfs_t lfs;
extern bool lfs_mounted;
extern struct lfs_config lfs_cfg;

// SD card instance (owned by lfsMgr)
extern SdCardBase* sdCard;
bool sdcard_is_inserted(SdCardBase* card);

// SD card performance tracking, for use by shell at the moment
typedef struct {
    uint32_t read_count;
    uint64_t read_bytes;
    uint64_t read_time_us;
    uint32_t read_min_us;
    uint32_t read_max_us;

    uint32_t write_count;
    uint64_t write_bytes;
    uint64_t write_time_us;
    uint32_t write_min_us;
    uint32_t write_max_us;
} sd_perf_stats_t;

extern sd_perf_stats_t sd_perf_stats;

// Lifecycle
void startFileSystem();
void sd_shutdown_for_reboot(void);

// Acquire the LittleFS mutex without releasing it.
// Call before raw SD sector operations that must not race with active LittleFS I/O.
// No unlock is needed if the caller is about to reboot.
void lfs_lock(void);

// Mount/unmount callbacks for decoupled Logger integration
typedef bool (*lfs_mount_cb_t)(lfs_t* lfs);
typedef void (*lfs_unmount_cb_t)(void);
void lfs_register_mount_callbacks(lfs_mount_cb_t on_mount, lfs_unmount_cb_t on_unmount);

#endif
