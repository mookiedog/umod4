#pragma once
#include <stdarg.h>

// RTT channel assignments for WP
#define WP_RTT_CHANNEL_STDIO   0    // general debug output (printf)
#define WP_RTT_CHANNEL_VFY     1    // structured test verification messages
#define WP_RTT_CHANNEL_SHELL   2    // shell I/O (bidirectional)

#define WP_VFY_UP_BUFFER_SIZE       2048
#define WP_VFY_DOWN_BUFFER_SIZE      512
#define WP_SHELL_UP_BUFFER_SIZE     4096
#define WP_SHELL_DOWN_BUFFER_SIZE    512

// Call once from main() after stdio_init_all()
void wp_rtt_init(void);

// Write a JSON response line to the WP_VFY up-channel.
//
// WARNING: WP_VFY uses BLOCK_IF_FIFO_FULL. Only call vfy_printf() in response
// to an explicit test command — never unconditionally during boot or normal
// operation. If no debugger is draining the buffer, the calling task will
// block permanently. Boot status belongs on WP_STDIO (printf).
void vfy_printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// Read up to maxLen bytes from the WP_VFY down-channel (automation commands in).
// Returns number of bytes read (may be 0 if nothing available).
unsigned vfy_rtt_read(char* buf, unsigned maxLen);

// Write to the shell output channel (WP_SHELL up-buffer).
void shell_printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// Read up to maxLen bytes from the shell input channel (WP_SHELL down-buffer).
// Returns number of bytes read (may be 0 if nothing available).
unsigned shell_rtt_read(char* buf, unsigned maxLen);
