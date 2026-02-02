/**
 * OTA Flash Task Implementation
 *
 * Dedicated task for OTA firmware updates with proper shutdown sequencing,
 * upgrade logging, and comprehensive pre-reboot cleanup.
 */

#include "ota_flash_task.h"
#include "FlashWp.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/bootrom.h"
#include "pico/cyw43_arch.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/structs/watchdog.h"
#include "boot/bootrom_constants.h"
#include "boot/picoboot_constants.h"  // For REBOOT2_FLAG_*
#include "RP2350.h"  // For NVIC

#include "lfs.h"

#include <stdio.h>
#include <string.h>

// External references
extern lfs_t lfs;
extern bool lfs_mounted;

// Logger - defined in Logger.h, but we need C-compatible access
// The logger pointer is declared as extern in Logger.h
extern void* logger;  // Opaque pointer for C code

// Helper function to call logger->deinit() - implemented in main.cpp
extern void ota_shutdown_logger(void);

// Helper function to check if logger is valid
extern bool ota_logger_valid(void);

// Task configuration
#define OTA_TASK_STACK_SIZE   8192
#define OTA_TASK_PRIORITY     (configMAX_PRIORITIES - 1)

// OTA request structure
typedef struct {
    char uf2_path[80];
    bool valid;
} ota_request_t;

// Task state
static QueueHandle_t ota_queue = NULL;
static TaskHandle_t ota_task_handle = NULL;
static volatile bool ota_in_progress = false;

// Upgrade log file
static lfs_file_t upgrade_log_file;
static bool upgrade_log_open = false;

// Forward declarations
static void ota_flash_task(void* params);
static void open_upgrade_log(void);
static void upgrade_log_write(const char* msg);
static void upgrade_log_close(void);
static void shutdown_logger(void);
static void shutdown_wifi(void);
static void prepare_for_reboot(uint32_t target_addr, bool flash_success);

// External function from FlashWp.cpp
extern void unpause_watchdog_tick(void);

void ota_flash_task_init(void)
{
    // Create queue for OTA requests (only 1 deep - no concurrent OTA)
    ota_queue = xQueueCreate(1, sizeof(ota_request_t));
    configASSERT(ota_queue != NULL);

    // Create task with high priority, will be pinned to core 0
    BaseType_t err = xTaskCreate(
        ota_flash_task,
        "OTA_Flash",
        OTA_TASK_STACK_SIZE,
        NULL,
        OTA_TASK_PRIORITY,
        &ota_task_handle
    );
    configASSERT(err == pdPASS);

    printf("OTA: Flash task initialized\n");
}

bool ota_flash_request(const char* uf2_path)
{
    if (!uf2_path || uf2_path[0] == '\0') {
        printf("OTA: Invalid path\n");
        return false;
    }

    if (ota_in_progress) {
        printf("OTA: Already in progress\n");
        return false;
    }

    if (!lfs_mounted) {
        printf("OTA: Filesystem not mounted\n");
        return false;
    }

    // Quick check that file exists
    struct lfs_info info;
    int err = lfs_stat(&lfs, uf2_path, &info);
    if (err != 0) {
        printf("OTA: File not found: %s (err=%d)\n", uf2_path, err);
        return false;
    }

    if (info.type != LFS_TYPE_REG) {
        printf("OTA: Not a regular file: %s\n", uf2_path);
        return false;
    }

    printf("OTA: Queuing request for %s (%lu bytes)\n", uf2_path, (unsigned long)info.size);

    ota_request_t request;
    strncpy(request.uf2_path, uf2_path, sizeof(request.uf2_path) - 1);
    request.uf2_path[sizeof(request.uf2_path) - 1] = '\0';
    request.valid = true;

    // Non-blocking send - if queue full, OTA already pending
    if (xQueueSend(ota_queue, &request, 0) != pdTRUE) {
        printf("OTA: Queue send failed\n");
        return false;
    }

    return true;
}

bool ota_flash_in_progress(void)
{
    return ota_in_progress;
}

static void ota_flash_task(void* params)
{
    (void)params;
    ota_request_t request;

    printf("OTA: Task started on core %u\n", get_core_num());

    while (1) {
        // Wait for OTA request
        if (xQueueReceive(ota_queue, &request, portMAX_DELAY) == pdTRUE) {
            if (!request.valid) {
                continue;
            }

            ota_in_progress = true;
            printf("\n");
            printf("OTA: Starting OTA flash process\n");
            printf("OTA: File: %s\n", request.uf2_path);

            open_upgrade_log();
            upgrade_log_write("OTA flash process starting");
            upgrade_log_write(request.uf2_path);

            upgrade_log_write("Waiting for HTTP response to complete");
            vTaskDelay(pdMS_TO_TICKS(200));

            upgrade_log_write("Shutting down data logger");
            shutdown_logger();
            upgrade_log_write("Logger shutdown complete");

            upgrade_log_write("Shutting off WiFi");
            shutdown_wifi();
            upgrade_log_write("WiFi shutdown complete");

            // Perform the actual reflash operation
            upgrade_log_write("Starting flash programming");

            uint32_t target_addr = 0;
            int32_t flash_result = flash_wp_uf2(request.uf2_path, true, &target_addr);

            char msg[80];
            snprintf(msg, sizeof(msg), "Flash result: %ld, target: 0x%08lX",
                     (long)flash_result, (unsigned long)target_addr);
            upgrade_log_write(msg);

            bool flash_success = (flash_result == 0);
            if (flash_success) {
                upgrade_log_write("Flash programming successful");
            } else {
                upgrade_log_write("FLASH PROGRAMMING FAILED");
                snprintf(msg, sizeof(msg), "Error code: %ld", (long)flash_result);
                upgrade_log_write(msg);
                upgrade_log_write("Will perform recovery reboot");
            }

            upgrade_log_write("Starting pre-reboot cleanup");
            prepare_for_reboot(target_addr, flash_success);

            // Should NEVER reach here - prepare_for_reboot doesn't return
            printf("OTA: CRITICAL ERROR - prepare_for_reboot returned!\n");
            while (1) {
                __wfi();
            }
        }
    }
}

static void open_upgrade_log(void)
{
    if (!lfs_mounted) {
        printf("OTA: WARNING - filesystem not mounted, no upgrade log\n");
        upgrade_log_open = false;
        return;
    }

    // Open/create upgrade.log, truncating any previous content
    int err = lfs_file_open(&lfs, &upgrade_log_file, "/upgrade.log",
                            LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if (err < 0) {
        printf("OTA: Failed to open upgrade.log: %d\n", err);
        upgrade_log_open = false;
        return;
    }

    upgrade_log_open = true;
    printf("OTA: Upgrade log opened\n");

    // Write header
    char header[128];
    uint32_t uptime_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    snprintf(header, sizeof(header),
             "=== OTA Upgrade Log ===\nUptime: %lu ms\nCore: %u\n",
             (unsigned long)uptime_ms, get_core_num());

    lfs_file_write(&lfs, &upgrade_log_file, header, strlen(header));
    lfs_file_sync(&lfs, &upgrade_log_file);
}

static void upgrade_log_write(const char* msg)
{
    // Always print to console
    printf("OTA: %s\n", msg);

    if (!upgrade_log_open) {
        return;
    }

    // Format: "[TIMESTAMP] message\n"
    char line[256];
    uint32_t ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    int len = snprintf(line, sizeof(line), "[%lu] %s\n", (unsigned long)ms, msg);

    if (len > 0 && len < (int)sizeof(line)) {
        lfs_file_write(&lfs, &upgrade_log_file, line, len);
        // Sync after each write to ensure data is committed
        lfs_file_sync(&lfs, &upgrade_log_file);
    }
}

static void upgrade_log_close(void)
{
    if (!upgrade_log_open) {
        return;
    }

    upgrade_log_write("Closing upgrade log - about to reboot");

    // Final sync
    lfs_file_sync(&lfs, &upgrade_log_file);

    // Close file
    lfs_file_close(&lfs, &upgrade_log_file);
    upgrade_log_open = false;

    printf("OTA: Upgrade log closed\n");
}

static void shutdown_logger(void)
{
    if (!ota_logger_valid()) {
        printf("OTA: Logger not initialized, skipping shutdown\n");
        return;
    }

    printf("OTA: Calling logger->deinit()\n");
    ota_shutdown_logger();

    // Give logger task time to notice and stop FS operations
    vTaskDelay(pdMS_TO_TICKS(100));
    printf("OTA: Logger shutdown complete\n");
}

static void shutdown_wifi(void)
{
    // I have had problems where it would not respond to shutdown requests.
    // We use a sledgehammer and just power down the module.
    gpio_init(23);
    gpio_set_dir(23, GPIO_OUT);
    gpio_put(23, 0);
}

static void prepare_for_reboot(uint32_t target_addr, bool flash_success)
{
    printf("OTA: Preparing for reboot, closing log\n");

    upgrade_log_close();

    printf("OTA: Suspending FreeRTOS scheduler\n");
    vTaskSuspendAll();

    // From this point forward, NO FreeRTOS calls!

    // Note: With configNUMBER_OF_CORES=1, core 1 was never started,
    // so no need to reset it.

    printf("OTA: Disabling interrupts\n");
    uint32_t saved_interrupts = save_and_disable_interrupts();
    (void)saved_interrupts;  // We won't restore these

    printf("OTA: Clearing NVIC pending interrupts\n");
    for (int i = 0; i < 8; i++) {
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }

    printf("OTA: Disabling PIO state machines\n");
    pio_set_sm_mask_enabled(pio0, 0x0F, false);
    pio_set_sm_mask_enabled(pio1, 0x0F, false);

    printf("OTA: Aborting DMA channels\n");
    for (uint i = 0; i < NUM_DMA_CHANNELS; i++) {
        dma_channel_abort(i);
    }

    printf("OTA: Unpause watchdog tick\n");
    unpause_watchdog_tick();

    if (flash_success && target_addr != 0) {
        // Success: boot to new partition via TBYB
        printf("OTA: Calling rom_reboot() target=0x%08lX\n", (unsigned long)target_addr);

        int rc = rom_reboot(
            REBOOT2_FLAG_REBOOT_TYPE_FLASH_UPDATE | REBOOT2_FLAG_NO_RETURN_ON_SUCCESS,
            100,  // delay_ms
            target_addr,
            0
        );

        // If we get here, rom_reboot failed
        printf("OTA: rom_reboot failed: %d\n", rc);
    } else {
        // Failure: watchdog reset to boot current (working) partition
        printf("OTA: Flash failed, performing watchdog reset\n");
    }

    // Fallback: watchdog reset
    printf("OTA: Enabling watchdog for reset\n");
    watchdog_enable(1, false);  // 1ms timeout

    while (1) {
        __wfi();
    }
}
