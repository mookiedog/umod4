#include "FlashConfig.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/bootrom.h"
#include "pico/flash.h"
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

// ---------------------------------------------------------------------------
// flash_config_defaults
// ---------------------------------------------------------------------------
void flash_config_defaults(flash_config_t *out)
{
    memset(out, 0, sizeof(*out));
    out->magic   = FLASH_CONFIG_MAGIC;
    out->version = FLASH_CONFIG_VERSION;

    // device_name, ap_ssid, ap_password are left empty intentionally.
    // This function is called before FreeRTOS and before cyw43_arch_init(), so
    // the CYW43 MAC address is not yet available.  WiFiManager's UNINITIALIZED
    // state fills these in from the MAC once hardware is up and saves to flash.
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
        if (out->version == FLASH_CONFIG_VERSION) {
            printf("FlashConfig: Loaded valid config (version %u)\n", (unsigned)out->version);
            return true;
        }

        if (out->version < FLASH_CONFIG_VERSION) {
            // Flash holds an older config version — run the migration chain.
            //
            // HOW TO ADD A MIGRATION (example: v0 -> v1):
            //
            //   1. Freeze the current struct as flash_config_v0_t in FlashConfig.h
            //      (copy it verbatim — never modify it again).
            //   2. Define the new layout as flash_config_v1_t and update the
            //      flash_config_t typedef to point to it.
            //   3. Bump FLASH_CONFIG_VERSION to 1.
            //   4. Write a migration function:
            //        static void migrate_v0_to_v1(const flash_config_v0_t *src,
            //                                     flash_config_v1_t       *dst)
            //      Copy all surviving fields; fill new fields with sensible defaults.
            //   5. Add a case below:
            //        if (out->version == 0) {
            //            flash_config_v0_t v0;
            //            memcpy(&v0, out, sizeof(v0));
            //            migrate_v0_to_v1(&v0, (flash_config_v1_t *)out);
            //        }
            //   6. After all cases, fall through to flash_config_save() below so
            //      the next boot reads a clean vN config without migrating again.
            //
            // Each migration step only knows about its two adjacent versions.
            // The chain handles multi-step upgrades automatically (v0->v1->v2 etc.).

            printf("FlashConfig: Migrating config from version %u to %u\n",
                   (unsigned)out->version, (unsigned)FLASH_CONFIG_VERSION);

            // (no migration steps yet — v0 is the first version)

            out->version = FLASH_CONFIG_VERSION;
            out->crc32   = crc32_compute(out, CRC_DATA_LEN);
            flash_config_save(out);
            return true;
        }

        // out->version > FLASH_CONFIG_VERSION: config was written by newer firmware.
        // We cannot safely interpret fields we don't know about — apply defaults.
        printf("FlashConfig: Config version %u is newer than firmware version %u — applying defaults\n",
               (unsigned)out->version, (unsigned)FLASH_CONFIG_VERSION);
        flash_config_defaults(out);
        return false;
    }

    // Any validation failure (wrong magic or bad CRC) means the config cannot be trusted.
    // Applying clean defaults is the only safe option — a corrupted WiFi password is
    // useless, and a corrupted AP SSID/password leaves the device unrecoverable.
    printf("FlashConfig: Invalid config (magic=%08lX crc_ok=%d) — applying defaults\n",
           (unsigned long)out->magic,
           (int)(crc32_compute(out, CRC_DATA_LEN) == out->crc32));
    flash_config_defaults(out);
    return false;
}

// ---------------------------------------------------------------------------
// flash_config_save — called from FreeRTOS SMP task context
// ---------------------------------------------------------------------------

// Must be in SRAM: flash_range_erase/program disable XIP, so this cannot
// execute from flash.  Placed at file scope so the callback can reach it.
static uint8_t s_config_sector_buf[FLASH_SECTOR_SIZE] __attribute__((aligned(4)));

static void __not_in_flash_func(do_flash_config_save)(void *param)
{
    (void)param;
    flash_range_erase(FLASH_CONFIG_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_CONFIG_OFFSET, s_config_sector_buf, FLASH_SECTOR_SIZE);
}

bool flash_config_save(const flash_config_t *cfg)
{
    // Recalculate CRC on a mutable copy so the stored CRC is always correct
    flash_config_t buf;
    memcpy(&buf, cfg, sizeof(buf));
    buf.magic   = FLASH_CONFIG_MAGIC;
    buf.version = FLASH_CONFIG_VERSION;
    buf.crc32   = crc32_compute(&buf, CRC_DATA_LEN);

    // Pad to full sector with 0xFF (flash erases to 0xFF, so this is a no-op for unwritten bytes)
    memset(s_config_sector_buf, 0xFF, sizeof(s_config_sector_buf));
    memcpy(s_config_sector_buf, &buf, sizeof(buf));

    // flash_safe_execute coordinates with the core-1 flash helper task so that
    // neither core is executing from flash during the erase/program window.
    // Using raw save_and_disable_interrupts here is wrong: it only masks interrupts
    // on core 0, leaving core 1 free to run FreeRTOS tasks that hold the kernel
    // spin lock — causing a deadlock when SysTick fires on core 0 after re-enabling.
    int rc = flash_safe_execute(do_flash_config_save, NULL, 1000 /*ms timeout*/);
    if (rc != PICO_OK) {
        printf("FlashConfig: flash_safe_execute failed (%d)\n", rc);
        return false;
    }

    // Flush XIP cache so subsequent reads via memcpy see the freshly programmed data
    rom_flash_flush_cache();

    printf("FlashConfig: Config saved to flash\n");
    return true;
}
