#include "FlashConfig.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/bootrom.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

// RP2350: The bootrom configures QMI ATRANS so that XIP addresses 0x10000000+
// are translated to the active firmware partition in physical flash.
// Reads beyond the partition size return a bus error (PRECISERR).
// The untranslated alias (0x1C000000) bypasses ATRANS and maps directly
// to the physical flash address — required for reading outside the active partition.
#define XIP_NOTRANSLATE_BASE  0x1C000000U

// ---------------------------------------------------------------------------
// CRC32 (polynomial 0xEDB88320, same as zlib/Ethernet)
// ---------------------------------------------------------------------------
static uint32_t crc32_compute(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320U & -(crc & 1));
        }
    }
    return ~crc;
}

// Offset within the struct at which crc32 lives (everything before it is covered)
#define CRC_DATA_LEN  (sizeof(flash_config_t) - sizeof(uint32_t))

// ---------------------------------------------------------------------------
// Field sanity helpers
// ---------------------------------------------------------------------------

// Returns true if s is a non-empty, printable (excluding control chars) string
// that is properly null-terminated within maxlen bytes.
static bool is_valid_string(const char *s, size_t maxlen)
{
    if (s[0] == '\0') return false; // empty
    for (size_t i = 0; i < maxlen; i++) {
        if (s[i] == '\0') return true;
        if ((unsigned char)s[i] < 0x20 || (unsigned char)s[i] > 0x7E) return false;
    }
    return false; // no null terminator found
}

static bool is_valid_port(uint16_t port)
{
    // Exclude 0xFFFF (erased flash sentinel) and port 0
    return port >= 1 && port != 0xFFFF;
}

// ---------------------------------------------------------------------------
// flash_config_defaults
// ---------------------------------------------------------------------------
void flash_config_defaults(flash_config_t *out)
{
    memset(out, 0, sizeof(*out));
    out->magic   = FLASH_CONFIG_MAGIC;
    out->version = FLASH_CONFIG_VERSION;

#ifdef DEFAULT_SERVER_HOST
    strncpy(out->server_host, DEFAULT_SERVER_HOST, sizeof(out->server_host) - 1);
#endif
#ifdef DEFAULT_SERVER_PORT
    out->server_port = DEFAULT_SERVER_PORT;
#else
    out->server_port = 8081;
#endif

    // device_name, ap_ssid, ap_password left empty: WiFiManager fills them in
    // from the CYW43 MAC address after hardware init and saves to flash.
    out->crc32 = crc32_compute(out, CRC_DATA_LEN);
}

// ---------------------------------------------------------------------------
// flash_config_load
// ---------------------------------------------------------------------------
bool flash_config_load(flash_config_t *out)
{
    // Flush the XIP cache before reading to ensure we get fresh flash data.
    // The bootrom may have left the cache in an indeterminate state after OTA commit.
    rom_flash_flush_cache();

    // Must use the untranslated XIP alias (0x1C000000) — not XIP_BASE (0x10000000).
    // The bootrom configures QMI ATRANS for the active partition only; reads via
    // XIP_BASE at offsets beyond the partition size cause a PRECISERR bus fault.
    uintptr_t read_addr = XIP_NOTRANSLATE_BASE + FLASH_CONFIG_OFFSET;
    printf("FlashConfig: Reading config from flash at 0x%08X\n", (unsigned)read_addr);
    memcpy(out, (const void *)read_addr, sizeof(flash_config_t));

    bool valid = (out->magic == FLASH_CONFIG_MAGIC) &&
                 (crc32_compute(out, CRC_DATA_LEN) == out->crc32);

    if (valid) {
        printf("FlashConfig: Loaded valid config from flash\n");
        // Sanitize port even in a valid config: 0xFFFF can be present if the
        // field was saved before is_valid_port() was tightened to reject it.
        if (!is_valid_port(out->server_port)) {
            flash_config_t defaults;
            flash_config_defaults(&defaults);
            printf("FlashConfig: Corrected invalid port 0x%04X -> %u\n",
                   (unsigned)out->server_port, (unsigned)defaults.server_port);
            out->server_port = defaults.server_port;
        }
        return true;
    }

    printf("FlashConfig: Flash config invalid (magic=%08lX, expected=%08lX) — applying defaults\n",
           (unsigned long)out->magic, (unsigned long)FLASH_CONFIG_MAGIC);

    // Best-effort: try to salvage fields that look valid before applying defaults
    flash_config_t defaults;
    flash_config_defaults(&defaults);

    if (!is_valid_string(out->wifi_ssid, sizeof(out->wifi_ssid)))
        memcpy(out->wifi_ssid, defaults.wifi_ssid, sizeof(out->wifi_ssid));

    if (!is_valid_string(out->wifi_password, sizeof(out->wifi_password)))
        memcpy(out->wifi_password, defaults.wifi_password, sizeof(out->wifi_password));

    if (!is_valid_string(out->server_host, sizeof(out->server_host)))
        memcpy(out->server_host, defaults.server_host, sizeof(out->server_host));

    if (!is_valid_port(out->server_port))
        out->server_port = defaults.server_port;

    // device_name, ap_ssid, ap_password: empty is fine — just zero them cleanly
    if (!is_valid_string(out->device_name, sizeof(out->device_name)))
        memset(out->device_name, 0, sizeof(out->device_name));

    if (!is_valid_string(out->ap_ssid, sizeof(out->ap_ssid)))
        memset(out->ap_ssid, 0, sizeof(out->ap_ssid));

    if (!is_valid_string(out->ap_password, sizeof(out->ap_password)))
        memset(out->ap_password, 0, sizeof(out->ap_password));

    out->magic   = FLASH_CONFIG_MAGIC;
    out->version = FLASH_CONFIG_VERSION;

    return false;
}

// ---------------------------------------------------------------------------
// flash_config_save — called from FreeRTOS task context (configNUMBER_OF_CORES=1)
// ---------------------------------------------------------------------------
bool flash_config_save(const flash_config_t *cfg)
{
    // Recalculate CRC on a mutable copy so the stored CRC is always correct
    flash_config_t buf;
    memcpy(&buf, cfg, sizeof(buf));
    buf.magic   = FLASH_CONFIG_MAGIC;
    buf.version = FLASH_CONFIG_VERSION;
    buf.crc32   = crc32_compute(&buf, CRC_DATA_LEN);

    // Pad to full sector with 0xFF (flash erases to 0xFF, so this is a no-op for unwritten bytes)
    static uint8_t sector_buf[FLASH_SECTOR_SIZE] __attribute__((aligned(4)));
    memset(sector_buf, 0xFF, sizeof(sector_buf));
    memcpy(sector_buf, &buf, sizeof(buf));

    // flash_range_erase/program handle XIP disable/re-enable internally.
    // Disable interrupts to prevent any ISR from being interrupted mid-write.
    // Core 1 is not used (configNUMBER_OF_CORES=1), so no multi-core coordination needed.
    uint32_t irq_state = save_and_disable_interrupts();
    flash_range_erase(FLASH_CONFIG_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_CONFIG_OFFSET, sector_buf, FLASH_SECTOR_SIZE);
    restore_interrupts(irq_state);

    // Flush XIP cache so subsequent reads via memcpy see the freshly programmed data
    rom_flash_flush_cache();

    printf("FlashConfig: Config saved to flash\n");
    return true;
}
