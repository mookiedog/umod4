/**
 * OTA Flash Task
 *
 * Dedicated FreeRTOS task for OTA firmware updates. This task handles the
 * entire OTA process from file validation through flash programming to reboot.
 *
 * Once an OTA request is queued, the only exit path is a system reboot -
 * either to the new firmware (on success) or back to the current firmware
 * (on failure via watchdog reset).
 *
 * The task is pinned to core 0 because rom_reboot() expects to be called
 * from core 0.
 */

#ifndef OTA_FLASH_TASK_H
#define OTA_FLASH_TASK_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the OTA flash task.
 * Creates the task and its queue. Should be called once during system startup.
 */
void ota_flash_task_init(void);

/**
 * Request an OTA flash operation.
 *
 * This function validates that the UF2 file exists and queues the request
 * to the OTA task. It returns immediately - it does NOT wait for the flash
 * to complete.
 *
 * Once queued, the OTA task will:
 * 1. Open upgrade.log for debugging
 * 2. Shut down the logger (stops filesystem writes)
 * 3. Shut down WiFi
 * 4. Flash the UF2 to the inactive partition
 * 5. Perform system cleanup
 * 6. Reboot to the new firmware (or watchdog reset on failure)
 *
 * @param uf2_path Path to the UF2 file on SD card (e.g., "/WP.uf2")
 * @return true if request was queued successfully
 *         false if OTA already in progress, file doesn't exist, or queue error
 */
bool ota_flash_request(const char* uf2_path);

/**
 * Check if an OTA operation is currently in progress.
 *
 * @return true if OTA is in progress (task is processing a request)
 */
bool ota_flash_in_progress(void);

#ifdef __cplusplus
}
#endif

#endif // OTA_FLASH_TASK_H
