#pragma once
#include <stdint.h>
#include <stdbool.h>

// WP RTT channel assignments
#define WP_RTT_CH_GENERAL   0   // WP general debug (printf)
#define WP_RTT_CH_VFY       1   // WP verification channel
#define WP_RTT_CH_EP_GEN    2   // EP stdio (forwarded from EP ch0)
#define WP_RTT_CH_EP_VFY    3   // EP verification (forwarded from EP ch1)

// Configure WP RTT channels 1-3 with their ring buffers.
// Call this BEFORE vTaskStartScheduler() so Cortex-Debug sees all channels
// on its initial RTT scan (ch0 is configured by pico_enable_stdio_rtt).
void ep_rtt_channels_init(void);

// Start the FreeRTOS task that polls EP's RTT channels via SWD and
// forwards output to WP RTT channels 2 (EP_GEN) and 3 (EP_VFY).
// Call after g_swd_mutex is created and image_store_init() has run.
void ep_rtt_forwarder_init(void);

// Read from the EP stdio circular buffer (1KB, tees EP ch0 output).
// client_offset: 'next' value from the previous call, or 0 on first call.
// out/max_bytes: destination buffer and its capacity.
// out_next: set to the offset the client should send on its next call.
// out_overflow: set true if data was lost since client_offset.
uint32_t ep_stdio_read(uint32_t client_offset, uint8_t* out, uint32_t max_bytes,
                       uint32_t* out_next, bool* out_overflow);
