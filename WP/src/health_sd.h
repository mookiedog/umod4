#pragma once
#include "wp_health.h"
#include "SdCardBase.h"

extern ComponentHealth g_sd_health;

// Call once in startFileSystem() immediately after the SdCardSDIO object is
// created. Gives health_sd a live pointer so to_json() always reflects the
// current hotplug hardware state without any additional callbacks.
void health_sd_init(SdCardBase *card);
