#ifndef FLASH_WP_H
#define FLASH_WP_H

#include <stdint.h>
#include <stdbool.h>

#include "pico/bootrom.h"

#ifdef __cplusplus

#include "lfs.h"

class FlashWp {
public:
    /**
     * Flash a UF2 file to the inactive partition (self-reflash).
     * @param pathname Path to UF2 file on SD card (e.g., "/WP.uf2")
     * @param verbose Enable verbose output
     * @param target_addr_out If non-null, receives the target partition address (for reboot)
     * @return 0 on success, negative error code on failure
     */
    static int32_t flashUf2(const char* pathname, bool verbose = false, uint32_t* target_addr_out = nullptr);

    /**
     * Commit the current OTA image (call rom_explicit_buy).
     * Should be called after successful self-test to make the current
     * partition permanent. If not called within 16.7 seconds of boot,
     * the device will reboot and revert to the previous partition.
     * @return 0 on success, negative error code on failure
     */
    static int32_t commitOtaUpdate(void);

    /**
     * Check if this boot was a "flash update" boot (i.e., OTA pending commit).
     * @return true if OTA commit is pending, false otherwise
     */
    static bool isOtaPending(void);

    /**
     * Returns the partition slot number that the system booted from.
     * Negative number is a failure.
     */
    static int32_t get_boot_slot();

    /**
     * Returns the reflash target partition slot number, the slot that is available to be reflashed.
     * Negative number is a failure.
     */
    static int32_t get_target_slot();

    /**
     * Based on the existing partition information, returns a bool indicating if an OTA reflash is possible
     */
    static bool get_ota_availability();

private:
    static int32_t getTargetPartitionInfo(uint32_t* start_addr, uint32_t* size);
    static int32_t flashSector(uint32_t flash_addr, const uint8_t* data, size_t size);

    static boot_info_t boot_info;
    static bool boot_info_valid;
    static void get_boot_info();
    static int32_t boot_slot;
    static int32_t target_slot;
};

extern "C" {
#endif

/**
 * C-compatible wrapper for FlashWp::flashUf2().
 * @param pathname Path to UF2 file on SD card (e.g., "/WP.uf2")
 * @param verbose Enable verbose output
 * @param target_addr_out If non-null, receives the target partition address (for reboot)
 * @return 0 on success, negative error code on failure
 */
int32_t flash_wp_uf2(const char* pathname, bool verbose, uint32_t* target_addr_out);

/**
 * Unpause the watchdog tick so rom_reboot() works even with debugger attached.
 * This function clears the watchdog pause bits in the control register.
 */
void unpause_watchdog_tick(void);

#ifdef __cplusplus
}
#endif

#endif // FLASH_WP_H
