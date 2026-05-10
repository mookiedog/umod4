#pragma once
#include "wp_health.h"

// Boot component: reports boot/target slot and build timestamp.
// State is always COMP_PASS after init — this component reports facts, not
// something that can fail. Lets the test harness confirm it is testing the
// image it just flashed.
extern ComponentHealth g_boot_health;

// Call once from main() after show_partition_info().
void health_boot_init(void);
