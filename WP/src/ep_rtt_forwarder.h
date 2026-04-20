#pragma once
#include <stdint.h>
#include <stdbool.h>

// WP RTT channel assignments (ch0-2 owned by wp_rtt.h, ch3-4 owned here)
#define WP_RTT_CH_GENERAL   0   // WP general debug (printf)
#define WP_RTT_CH_EP_STDIO    3   // EP stdio (forwarded from EP ch0)
#define WP_RTT_CH_EP_VFY    4   // EP verification (forwarded from EP ch1)

// Configure WP RTT channels 3-4 with their ring buffers.
// Call this AFTER wp_rtt_init() and BEFORE vTaskStartScheduler() so
// Cortex-Debug sees all channels on its initial RTT scan.
void ep_rtt_channels_init(void);

// Start the FreeRTOS task that polls EP's RTT channels via SWD and
// forwards output to WP RTT channels 3 (EP_STDIO) and 4 (EP_VFY).
// Call after g_swd_mutex is created and image_store_init() has run.
void ep_rtt_forwarder_init(void);

// Read from the EP stdio circular buffer (1KB, tees EP ch0 output).
// client_offset: 'next' value from the previous call, or 0 on first call.
// out/max_bytes: destination buffer and its capacity.
// out_next: set to the offset the client should send on its next call.
// out_overflow: set true if data was lost since client_offset.
uint32_t ep_stdio_read(uint32_t client_offset, uint8_t* out, uint32_t max_bytes,
                       uint32_t* out_next, bool* out_overflow);
