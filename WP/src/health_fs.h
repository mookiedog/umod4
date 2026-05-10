#pragma once
#include "wp_health.h"
#include <stdint.h>

extern ComponentHealth g_fs_health;

// Called from comingOnline() after a successful mount.
// reformatted: true if the filesystem had to be formatted before mounting.
// mount_time_ms: time taken for the successful lfs_mount() call.
void health_fs_set_mounted(bool reformatted, uint32_t mount_time_ms);

// Called from goingOffline() when the card is removed or shut down.
void health_fs_set_unmounted(void);
