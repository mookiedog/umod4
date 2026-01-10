#ifndef FS_CUSTOM_H
#define FS_CUSTOM_H

#include "lwip/apps/fs.h"
#include "lfs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Custom filesystem bridge for lwIP httpd to serve files from LittleFS SD card.
 *
 * This implements the custom filesystem interface required by lwIP when
 * LWIP_HTTPD_CUSTOM_FILES is enabled. It maps HTTP paths to LittleFS files
 * and streams them directly from the SD card without loading into RAM.
 */

/**
 * Initialize the custom filesystem.
 * Must be called before httpd_init().
 *
 * @param lfs_ptr Pointer to the mounted LittleFS filesystem
 */
void fs_custom_init(lfs_t* lfs_ptr);

/**
 * Check if the filesystem is ready to serve files.
 * @return true if LittleFS is mounted and ready
 */
bool fs_custom_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif // FS_CUSTOM_H
