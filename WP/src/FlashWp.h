#ifndef FLASH_WP_H
#define FLASH_WP_H

#include <stdint.h>
#include <stdbool.h>

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
     * Schedule a reboot to the newly-flashed partition.
     * Uses a FreeRTOS timer to allow the caller to complete (e.g., send HTTP response)
     * before the reboot occurs.
     * @param target_addr The partition start address (from flashUf2)
     * @param delay_ms Delay before reboot (e.g., 500ms to let HTTP response transmit)
     */
    static void rebootToUpdate(uint32_t target_addr, uint32_t delay_ms);

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

private:
    static int32_t getTargetPartition(uint32_t* start_addr, uint32_t* size);
    static int32_t processUf2(lfs_t* lfs, const char* path, uint32_t partition_start,
                               uint32_t partition_size, bool verbose);
    static int32_t flashSector(uint32_t flash_addr, const uint8_t* data, size_t size);
};

extern "C" {
#endif

/**
 * C-compatible wrapper for FlashWp::flashUf2().
 * @param pathname Path to UF2 file on SD card (e.g., "/WP.uf2")
 * @param verbose Enable verbose output
 * @param target_addr_out If non-null, receives the target partition address (for reboot)
 * @return 0 on success, negative error code on failure:
 *         -1: Partition table not loaded
 *         -2: Could not determine target partition
 *         -3: Failed to open UF2 file
 *         -4: Invalid UF2 block
 *         -5: Address out of partition bounds
 *         -6: Flash erase failed
 *         -7: Flash program failed
 *         -8: Flash verify failed
 */
int32_t flash_wp_uf2(const char* pathname, bool verbose, uint32_t* target_addr_out);

/**
 * C-compatible wrapper for FlashWp::rebootToUpdate().
 * Schedule a reboot to the newly-flashed partition after a delay.
 * @param target_addr The partition start address (from flash_wp_uf2)
 * @param delay_ms Delay before reboot (e.g., 500ms to let HTTP response transmit)
 */
void flash_wp_reboot_to_update(uint32_t target_addr, uint32_t delay_ms);

/**
 * C-compatible wrapper for FlashWp::commitOtaUpdate().
 * @return 0 on success, negative error code on failure
 */
int32_t flash_wp_commit_ota(void);

/**
 * C-compatible wrapper for FlashWp::isOtaPending().
 * @return true if OTA commit is pending, false otherwise
 */
bool flash_wp_is_ota_pending(void);

/**
 * Unpause the watchdog tick so rom_reboot() works even with debugger attached.
 * This function clears the watchdog pause bits in the control register.
 */
void unpause_watchdog_tick(void);

#ifdef __cplusplus
}
#endif

#endif // FLASH_WP_H
