#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Physical flash offset of the config partition (see partition_table.json)
// Boot(64K) + A(2012K) + B(2012K) = 0x3FE000; config partition starts here.
#define FLASH_CONFIG_OFFSET  0x3FE000U

// WARNING: changing FLASH_CONFIG_MAGIC invalidates all existing device configs.
// Devices will boot into AP mode and require physical access (BOOTSEL) to reflash
// if the resulting AP SSID/password is unusable. Always bench-test magic/layout
// changes before deploying to a remote device (e.g. the motorcycle).
#define FLASH_CONFIG_MAGIC   0x55CF0000U
#define FLASH_CONFIG_VERSION 0

// All string fields are null-terminated; an empty first byte means "not configured".
// All members are sized as a multiple of one word (4 bytes).
// __attribute__((packed)) ensures no compiler padding — flash layout must be deterministic.
typedef struct __attribute__((packed)) {
    uint32_t magic;             // FLASH_CONFIG_MAGIC — detects blank/corrupt flash
    uint32_t version;           // Struct version (FLASH_CONFIG_VERSION)
    char     device_name[64];   // Human-readable device name, e.g. "Robin's Tuono"
    char     wifi_ssid[64];     // Home network SSID
    char     wifi_password[64]; // Home network password
    char     ap_ssid[32];       // AP mode SSID; empty = use "umod4-XXYY" from MAC
    char     ap_password[64];   // AP mode password; empty = use "umod4-XXYY" from MAC
    uint32_t _reserved[53];     // Pad to 512 bytes
    uint32_t crc32;             // CRC32 of all preceding bytes in this struct
} flash_config_v0_t;            // Total: 4+4+64+64+64+32+64+212+4=512

typedef flash_config_v0_t flash_config_t;

/**
 * Load config from flash into *out.
 *
 * Validates magic and CRC32. On validation failure, still does best-effort
 * field-by-field sanity checks and fills any invalid fields from compile-time
 * defaults (see flash_config_defaults).
 *
 * @return true  if magic matched, CRC32 was valid, and version matched
 *         false if blank/corrupt flash or unknown version (defaults were applied)
 */
bool flash_config_load(flash_config_t *out);

/**
 * Save config to flash.
 * Erases the first 4KB sector of the config partition and writes the struct.
 * Must be called from a FreeRTOS task context (uses flash_safe_execute).
 *
 * @return true on success
 */
bool flash_config_save(const flash_config_t *cfg);

/**
 * Fill *out with compile-time defaults.
 * Falls back to empty strings for any field whose CMake default is not defined.
 */
void flash_config_defaults(flash_config_t *out);

#ifdef __cplusplus
}
// Compile-time check that the struct is exactly 512 bytes (C++ form)
static_assert(sizeof(flash_config_t) == 512, "flash_config_t must be exactly 512 bytes");
#else
_Static_assert(sizeof(flash_config_t) == 512, "flash_config_t must be exactly 512 bytes");
#endif
