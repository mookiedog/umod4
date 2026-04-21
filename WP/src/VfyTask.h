#pragma once

// Start the FreeRTOS task that services the WP_VFY RTT channel.
// Call from boot_system() after wp_rtt_init() has run.
void vfy_task_init(void);
