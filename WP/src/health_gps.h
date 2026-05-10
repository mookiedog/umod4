#pragma once
#include "wp_health.h"
#include "Gps.h"

extern ComponentHealth g_gps_health;

// Call once after startGps() in boot_system().
void health_gps_init(Gps* gps);
