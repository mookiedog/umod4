#include "wp_rtt.h"
#include "SEGGER_RTT.h"
#include <stdio.h>

static char s_vfy_up_buf[WP_VFY_UP_BUFFER_SIZE];
static char s_vfy_down_buf[WP_VFY_DOWN_BUFFER_SIZE];
static char s_shell_up_buf[WP_SHELL_UP_BUFFER_SIZE];
static char s_shell_down_buf[WP_SHELL_DOWN_BUFFER_SIZE];

void wp_rtt_init(void)
{
    // VFY and SHELL use BLOCK_IF_FIFO_FULL so test results and shell output are
    // never silently dropped. WP_STDIO (ch0) stays NO_BLOCK_TRIM (configured by
    // the Pico SDK) so debug printf from FreeRTOS tasks never deadlocks.
    SEGGER_RTT_ConfigUpBuffer(WP_RTT_CHANNEL_VFY, "WP_VFY",
                              s_vfy_up_buf, sizeof(s_vfy_up_buf),
                              SEGGER_RTT_MODE_BLOCK_IF_FIFO_FULL);

    SEGGER_RTT_ConfigDownBuffer(WP_RTT_CHANNEL_VFY, "WP_VFY",
                                s_vfy_down_buf, sizeof(s_vfy_down_buf),
                                SEGGER_RTT_MODE_NO_BLOCK_TRIM);

    SEGGER_RTT_ConfigUpBuffer(WP_RTT_CHANNEL_SHELL, "WP_SHELL",
                              s_shell_up_buf, sizeof(s_shell_up_buf),
                              SEGGER_RTT_MODE_BLOCK_IF_FIFO_FULL);

    SEGGER_RTT_ConfigDownBuffer(WP_RTT_CHANNEL_SHELL, "WP_SHELL",
                                s_shell_down_buf, sizeof(s_shell_down_buf),
                                SEGGER_RTT_MODE_NO_BLOCK_TRIM);
}

static void rtt_vprintf(unsigned channel, const char* fmt, va_list args)
{
    char buf[512];
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    if (len > 0) {
        if (len >= (int)sizeof(buf)) {
            len = sizeof(buf) - 1;
        }
        SEGGER_RTT_Write(channel, buf, (unsigned)len);
    }
}

void vfy_printf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    rtt_vprintf(WP_RTT_CHANNEL_VFY, fmt, args);
    va_end(args);
}

unsigned vfy_rtt_read(char* buf, unsigned maxLen)
{
    return SEGGER_RTT_Read(WP_RTT_CHANNEL_VFY, buf, maxLen);
}

void shell_printf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    rtt_vprintf(WP_RTT_CHANNEL_SHELL, fmt, args);
    va_end(args);
}

unsigned shell_rtt_read(char* buf, unsigned maxLen)
{
    return SEGGER_RTT_Read(WP_RTT_CHANNEL_SHELL, buf, maxLen);
}
