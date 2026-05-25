#pragma once

// Start the FreeRTOS task that services the WP_VFY RTT channel.
// Call from boot_system() after wp_rtt_init() has run.
void vfy_task_init(void);

// Run the SWD boot-time connectivity check against the already-running EP
// and cache the result for subsequent cmd_swd() / cmd_health() queries.
// Call after swd is created and g_swd_mutex exists.
void swd_boot_check(void);
