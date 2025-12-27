/* Configuration for SDIO_RP2350 library - WP Platform */

#pragma once

#include <cstdio>
#include "umod4_WP.h"  // Get pin definitions and PIO assignments

// Debug/error logging macros
// All messages disabled to prevent printf from interrupt context
// #define SDIO_DBGMSG(txt, arg1, arg2) printf(txt " %lu %lu\n", (uint32_t)(arg1), (uint32_t)(arg2))
// #define SDIO_ERRMSG(txt, arg1, arg2) printf(txt " %lu %lu\n", (uint32_t)(arg1), (uint32_t)(arg2))
// #define SDIO_CRITMSG(txt, arg1, arg2) printf(txt " %lu %lu\n", (uint32_t)(arg1), (uint32_t)(arg2))
#define SDIO_ERRMSG(txt, arg1, arg2)   // Disabled
#define SDIO_CRITMSG(txt, arg1, arg2)  // Disabled

// PIO block assignment (WP uses pio2 for SDIO)
#define SDIO_PIO pio2
#define SDIO_SM  0

// GPIO function for RP2350 PIO2
#define SDIO_GPIO_FUNC GPIO_FUNC_PIO2
#define SDIO_GPIO_SLEW GPIO_SLEW_RATE_FAST
#define SDIO_GPIO_DRIVE GPIO_DRIVE_STRENGTH_8MA

// DMA channels
#define SDIO_DMACH_A 4
#define SDIO_DMACH_B 5
#define SDIO_DMAIRQ_IDX 1
#define SDIO_DMAIRQ DMA_IRQ_1

// Default speed: Use MMC mode (20 MHz)
// SDIO_STANDARD (25 MHz) causes CRC errors after ~10 transfers
#define SDIO_DEFAULT_SPEED SDIO_MMC
#define SDIO_MAX_CLOCK_RATE_EXCEED_PERCENT 15

// Disable SdFat integration (we use LittleFS)
#define SDIO_USE_SDFAT 0

// GPIO pin definitions (from umod4_WP.h)
#define SDIO_CLK SD_SCK_PIN   // GPIO 10
#define SDIO_CMD SD_MOSI_PIN  // GPIO 11
#define SDIO_D0  SD_DAT0      // GPIO 12
#define SDIO_D1  SD_DAT1      // GPIO 13
#define SDIO_D2  SD_DAT2      // GPIO 14
#define SDIO_D3  SD_DAT3      // GPIO 15
