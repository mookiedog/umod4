#pragma once

// WP RTT channel assignments
#define WP_RTT_CH_GENERAL   0   // WP general debug (printf)
#define WP_RTT_CH_VFY       1   // WP verification channel
#define WP_RTT_CH_EP_GEN    2   // EP general debug (forwarded)
#define WP_RTT_CH_EP_VFY    3   // EP verification (forwarded)

// Configure WP RTT channels 1-3 with their ring buffers.
// Call this BEFORE vTaskStartScheduler() so Cortex-Debug sees all channels
// on its initial RTT scan (ch0 is configured by pico_enable_stdio_rtt).
void ep_rtt_channels_init(void);

// Start the FreeRTOS task that polls EP's RTT channels via SWD and
// forwards output to WP RTT channels 2 (EP_GEN) and 3 (EP_VFY).
// Call after g_swd_mutex is created and image_store_init() has run.
void ep_rtt_forwarder_init(void);
