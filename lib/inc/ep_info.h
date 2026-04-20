#pragma once
#include <stdint.h>

// EpInfo is written to USB DPSRAM (EP_INFO_ADDR) early in EP's main().
// EP never uses USB, so this 4KB region is available as a fixed-address
// mailbox readable by WP via SWD without any linker script coordination.
//
// WP polls EP_INFO_ADDR via SWD, waits for magic == EP_INFO_MAGIC,
// then follows rtt_cb_addr to locate EP's RTT control block.

#define EP_INFO_MAGIC   0xEE119001u     // "EP INFO v1" sentinel
#define EP_INFO_ADDR    0x50100000u     // USB DPSRAM base address (RP2040)

struct EpInfo {
    uint32_t magic;         // set to EP_INFO_MAGIC last, acts as ready flag
    uint32_t rtt_cb_addr;   // address of _SEGGER_RTT control block
};
