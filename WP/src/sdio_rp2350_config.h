#pragma once

// Configuration file for SDIO_RP2350 library
// This file defines hardware-specific settings for the umod4 WP board

#include "umod4_WP.h"  // For SD card pin definitions
#include <cstdio>      // For printf in error macros
#include <hardware/gpio.h>

// Pin assignments come from umod4_WP.h
#define SDIO_CLK SD_SCK_PIN
#define SDIO_CMD SD_MOSI_PIN
#define SDIO_D0  SD_DAT0
#define SDIO_D1  SD_DAT1
#define SDIO_D2  SD_DAT2
#define SDIO_D3  SD_DAT3

// PIO configuration.
// This SDIO driver gets exclusive access to this PIO unit
#define SDIO_PIO PIO_SDIO
#define SDIO_SM  0
#define SDIO_GPIO_FUNC SD_GPIO_FUNC
#define SDIO_GPIO_SLEW GPIO_SLEW_RATE_FAST
#define SDIO_GPIO_DRIVE GPIO_DRIVE_STRENGTH_8MA

// DMA channels (use channels 4 and 5, IRQ 1)
#define SDIO_DMACH_A 4
#define SDIO_DMACH_B 5
#define SDIO_DMAIRQ_IDX 1
#define SDIO_DMAIRQ DMA_IRQ_1

// Performance settings
#define SDIO_DEFAULT_SPEED SDIO_HIGHSPEED  // 50 MHz
#define SDIO_BLOCK_SIZE 512
#define SDIO_MAX_BLOCKS_PER_REQ 128

// Timeouts
#define SDIO_CMD_TIMEOUT_US 50
#define SDIO_TRANSFER_TIMEOUT_US (1000 * 1000)
#define SDIO_INIT_TIMEOUT_US (1000 * 1000)

// Debug output (CRITICAL: printf from DMA interrupt causes false timeouts!)
// Only enable for finding where init hangs, then IMMEDIATELY disable
#define SDIO_DBGMSG(txt, arg1, arg2)   // Disabled - causes false timeouts
#define SDIO_ERRMSG(txt, arg1, arg2)   printf(txt " %lu %lu\n", (unsigned long)(arg1), (unsigned long)(arg2))
#define SDIO_CRITMSG(txt, arg1, arg2)  printf(txt " %lu %lu\n", (unsigned long)(arg1), (unsigned long)(arg2))

// Timing
#include <pico/time.h>
#define SDIO_TIME_US() time_us_32()
#define SDIO_ELAPSED_US(start) (time_us_32() - (start))
#define SDIO_WAIT_US(x) busy_wait_us_32(x)

// Retry and fallback
#define SDIO_MAX_RETRYCOUNT 1
#define SDIO_FALLBACK_CRC_ERROR_COUNT 3
#define SDIO_FALLBACK_MODE SDIO_STANDARD
