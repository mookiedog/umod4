/* Configuration for SDIO_RP2350 library - WP Platform */

#pragma once

#include <cstdio>
#include "umod4_WP.h"  // Get pin definitions and PIO assignments

// Debug/error logging macros
// IMPORTANT: These call printf from DMA interrupt context!
// Only enable for debugging - can cause hangs if used during normal operation
// DISABLED: Causes false timeouts due to printf blocking in interrupt context
#define SDIO_DBGMSG(txt, arg1, arg2)
#define SDIO_ERRMSG(txt, arg1, arg2)
#define SDIO_CRITMSG(txt, arg1, arg2)

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

// Default speed: Use STANDARD mode (25 MHz)
// Previously SDIO_MMC (20 MHz) was used due to CRC errors, but retesting at 25 MHz
#define SDIO_DEFAULT_SPEED SDIO_STANDARD
#define SDIO_MAX_CLOCK_RATE_EXCEED_PERCENT 15

// Increase command timeout for slow init speed (300 kHz)
// At 300 kHz, a full command/response cycle can take 500+ μs
#define SDIO_CMD_TIMEOUT_US 1000

// Disable SdFat integration (we use LittleFS)
#define SDIO_USE_SDFAT 0

// GPIO pin definitions (from umod4_WP.h)
#define SDIO_CLK SD_SCK_PIN   // GPIO 10
#define SDIO_CMD SD_MOSI_PIN  // GPIO 11
#define SDIO_D0  SD_DAT0      // GPIO 12
#define SDIO_D1  SD_DAT1      // GPIO 13
#define SDIO_D2  SD_DAT2      // GPIO 14
#define SDIO_D3  SD_DAT3      // GPIO 15
