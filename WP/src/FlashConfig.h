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

// THE CONFIG IS 512 BYTES, ALWAYS, AND ITS UNUSED BYTES ARE ALWAYS ZERO.
//
// The struct below is a union of two views of the same 508 payload bytes (the
// last 4 bytes are the CRC):
//   - the named fields, and
//   - raw[], which is how the CRC is computed and how the whole payload is copied.
// Whatever the named fields do not use is the "reserved" tail. There is no
// _reserved[] array to resize: to add a field, append it at the END of the named
// struct. The static_asserts at the bottom of this file refuse to compile if the
// named fields ever outgrow the payload or if an existing field moves.
//
// Rules for adding a field (they are what make the config extensible without a
// version bump, and without wiping the saved WiFi credentials on a downgrade):
//   1. A value of 0 MUST mean "old behaviour, this feature is not present".
//      Old configs have zeros there, so they keep working unchanged.
//   2. The CRC covers the whole payload, reserved tail included, so every writer
//      must write zeros into bytes that no field uses. Writers today: the
//      firmware (it zero-fills the defaults, then every save copies the loaded
//      struct) and tests/harness/flash_config_blobs.py (writes zeros).
//      Any new tool that writes a config must do the same.
//   3. Do NOT change FLASH_CONFIG_MAGIC or FLASH_CONFIG_VERSION for an added field.
//      Older firmware ignores bytes it does not know about, and preserves them
//      when it saves.
//
// All string fields are null-terminated; an empty first byte means "not configured".
// All members are sized as a multiple of one word (4 bytes).
// __attribute__((packed)) ensures no compiler padding - flash layout must be deterministic.
#define FLASH_CONFIG_SIZE          512U
#define FLASH_CONFIG_PAYLOAD_SIZE  (FLASH_CONFIG_SIZE - 4U)

typedef struct __attribute__((packed)) {
    __extension__ union {
        struct __attribute__((packed)) {
            uint32_t magic;             // FLASH_CONFIG_MAGIC - detects blank/corrupt flash
            uint32_t version;           // Struct version (FLASH_CONFIG_VERSION)
            char     device_name[64];   // Human-readable device name, e.g. "Robin's Tuono"
            char     wifi_ssid[64];     // Home network SSID
            char     wifi_password[64]; // Home network password
            char     ap_ssid[32];       // AP mode SSID; empty = use "umod4-XXYY" from MAC
            char     ap_password[64];   // AP mode password; empty = use "umod4-XXYY" from MAC
            // New fields go here, at the end. 0 must mean "feature not present".
        };
        uint8_t raw[FLASH_CONFIG_PAYLOAD_SIZE];   // the whole payload: covered by the CRC
    };
    uint32_t crc32;                 // CRC32 of all preceding bytes in this struct
} flash_config_v0_t;

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
#endif

// Compile-time layout checks. These pin the on-flash layout: if one fails, an edit
// has changed what older firmware and the test harness expect to find in flash.
#ifdef __cplusplus
#include <cstddef>
#define FLASH_CONFIG_ASSERT(cond, msg) static_assert(cond, msg)
#else
#include <stddef.h>
#define FLASH_CONFIG_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif

FLASH_CONFIG_ASSERT(sizeof(flash_config_t) == FLASH_CONFIG_SIZE, "flash_config_t must be exactly 512 bytes (named fields outgrew the payload?)");
FLASH_CONFIG_ASSERT(offsetof(flash_config_t, crc32) == FLASH_CONFIG_PAYLOAD_SIZE, "crc32 must be the last word");
FLASH_CONFIG_ASSERT(offsetof(flash_config_t, device_name)   == 8,   "device_name moved");
FLASH_CONFIG_ASSERT(offsetof(flash_config_t, wifi_ssid)     == 72,  "wifi_ssid moved");
FLASH_CONFIG_ASSERT(offsetof(flash_config_t, wifi_password) == 136, "wifi_password moved");
FLASH_CONFIG_ASSERT(offsetof(flash_config_t, ap_ssid)       == 200, "ap_ssid moved");
FLASH_CONFIG_ASSERT(offsetof(flash_config_t, ap_password)   == 232, "ap_password moved");
