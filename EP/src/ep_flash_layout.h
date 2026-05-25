#pragma once

#include <stdint.h>

// EP SPI flash partition layout — must match memmap_eprom.ld exactly.
//
// EP firmware can use the linker-script symbols directly (extern char __IMAGE_STORE_PARTITION_START_ADDR etc.).
// WP firmware cannot (separate build), so it uses these #defines instead.

#define EP_FLASH_BASE                   0x10000000U
#define EP_FLASH_SIZE                   (16U * 1024U * 1024U)

#define EP_CODE_SIZE                    ( 2U * 1024U * 1024U)

#define EP_IMAGE_STORE_BASE             (EP_FLASH_BASE + EP_CODE_SIZE)   // 0x10200000
#define EP_IMAGE_STORE_SLOT_SIZE        (64U * 1024U)                    // 64K per slot
#define EP_IMAGE_STORE_SLOT_COUNT       128U
#define EP_IMAGE_STORE_SIZE             (EP_IMAGE_STORE_SLOT_COUNT * EP_IMAGE_STORE_SLOT_SIZE)

