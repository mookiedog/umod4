#pragma once
#include <stdarg.h>
#include <stdio.h>
#include "SEGGER_RTT.h"

// RTT channel assignments for EP
#define EP_RTT_CH_GENERAL   0   // general debug output; printf() also writes here
#define EP_RTT_CH_VFY       1   // verification / test harness channel

// printf-style write to EP RTT channel 1 (verification).
// Usage: vfy_printf("cycle %d: value=0x%04x\n", cycle, val);
static inline void vfy_printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static inline void vfy_printf(const char* fmt, ...)
{
    char buf[128];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n > 0)
        SEGGER_RTT_Write(EP_RTT_CH_VFY, buf, (unsigned)n < sizeof(buf) ? (unsigned)n : sizeof(buf) - 1);
}
