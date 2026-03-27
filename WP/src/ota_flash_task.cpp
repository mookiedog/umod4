/**
 * OTA Flash Task Implementation
 *
 * Dedicated task for OTA firmware updates with proper shutdown sequencing,
 * upgrade logging, and comprehensive pre-reboot cleanup.
 */

#include "ota_flash_task.h"
#include "FlashConfig.h"
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

#include "lfsMgr.h"
#include "fs_custom.h"

#include <stdio.h>
#include <string.h>

// Logger - declared as extern in Logger.h
extern void* logger;

// Helper functions from main.cpp
extern void ota_shutdown_logger(void);
extern bool ota_logger_valid(void);

// Flash config (owned by main.cpp) — saved here after WiFi is down, so flash ops are safe
extern flash_config_t g_flash_config;

// Task configuration
#define OTA_TASK_STACK_SIZE   1024

// OTA request structure
typedef struct {
    char uf2_path[80];
    bool valid;
    bool reboot_only;       // If true, skip flashing and just do a clean reboot
    bool save_flash_config; // If true, save g_flash_config to flash before rebooting
                            // (done AFTER shutdown_wifi so flash ops are safe)
} ota_request_t;

// Task state
static QueueHandle_t ota_queue = NULL;
static TaskHandle_t ota_task_handle = NULL;
static volatile bool ota_in_progress = false;

// Forward declarations
static void ota_flash_task(void* params);
static void shutdown_logger(void);
static void shutdown_wifi(void);
static void prepare_for_reboot(uint32_t target_addr, bool flash_success);

// External function from FlashWp.cpp
extern void unpause_watchdog_tick(void);

void ota_flash_task_init(void)
{
    // Create queue for OTA requests (only 1 deep - no concurrent OTA)
    static uint8_t       s_queue_storage[1 * sizeof(ota_request_t)];
    static StaticQueue_t s_queue_buf;
    ota_queue = xQueueCreateStatic(1, sizeof(ota_request_t), s_queue_storage, &s_queue_buf);
    configASSERT(ota_queue != NULL);

    // Create task pinned to core 0
    static StackType_t  s_stack[OTA_TASK_STACK_SIZE];
    static StaticTask_t s_tcb;
    ota_task_handle = xTaskCreateStaticAffinitySet(
        ota_flash_task,
        "OTA_Flash",
        OTA_TASK_STACK_SIZE,
        NULL,
        TASK_NORMAL_PRIORITY,
        s_stack, &s_tcb,
        (1<<0)
    );
    configASSERT(ota_task_handle != NULL);

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

    ota_request_t request = {};
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

bool ota_reboot_request(void)
{
    if (ota_in_progress) {
        printf("OTA: Reboot rejected - OTA already in progress\n");
        return false;
    }

    ota_request_t request = {};
    request.valid = true;
    request.reboot_only = true;

    if (xQueueSend(ota_queue, &request, 0) != pdTRUE) {
        printf("OTA: Reboot queue send failed\n");
        return false;
    }

    printf("OTA: Clean reboot queued\n");
    return true;
}

bool ota_reboot_with_config_save_request(void)
{
    if (ota_in_progress) {
        printf("OTA: Reboot+config-save rejected - OTA already in progress\n");
        return false;
    }

    ota_request_t request = {};
    request.valid = true;
    request.reboot_only = true;
    request.save_flash_config = true;

    if (xQueueSend(ota_queue, &request, 0) != pdTRUE) {
        printf("OTA: Reboot+config-save queue send failed\n");
        return false;
    }

    printf("OTA: Clean reboot+config-save queued\n");
    return true;
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

            // Close any persistent LFS handle held by the chunk download path.
            // Each open LFS file consumes 16KB of heap; releasing this before
            // opening the UF2 file prevents OOM during flash programming.
            fs_custom_close_persistent_handle();

            if (request.reboot_only) {
                printf("OTA: Starting clean reboot (no flash)\n");
                printf("OTA: Waiting for HTTP response to flush\n");
                vTaskDelay(pdMS_TO_TICKS(300));

                printf("OTA: Shutting off WiFi\n");
                shutdown_wifi();

                if (request.save_flash_config) {
                    printf("OTA: Saving flash config\n");
                    flash_config_save(&g_flash_config);
                }

                printf("OTA: Rebooting via watchdog\n");
                vTaskDelay(pdMS_TO_TICKS(1000));
                prepare_for_reboot(0, false);
            }

            printf("OTA: Starting OTA flash process\n");
            printf("OTA: File: %s\n", request.uf2_path);

            printf("OTA: Waiting for HTTP response to complete\n");
            vTaskDelay(pdMS_TO_TICKS(200));

            printf("OTA: Shutting down data logger\n");
            shutdown_logger();
            printf("OTA: Logger shutdown complete\n");

            printf("OTA: Shutting off WiFi\n");
            shutdown_wifi();
            printf("OTA: WiFi shutdown complete\n");

            printf("OTA: Starting flash programming\n");
            uint32_t target_addr = 0;
            int32_t flash_result = flash_wp_uf2(request.uf2_path, true, &target_addr);

            bool flash_success = (flash_result == 0);
            if (flash_success) {
                printf("OTA: Flash programming successful, target=0x%08lX\n",
                       (unsigned long)target_addr);
            } else {
                printf("OTA: FLASH PROGRAMMING FAILED, error=%ld\n", (long)flash_result);
            }

            prepare_for_reboot(target_addr, flash_success);

            // Should NEVER reach here - prepare_for_reboot doesn't return
            printf("OTA: CRITICAL ERROR - prepare_for_reboot returned!\n");
            while (1) {
                __wfi();
            }
        }
    }
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
    printf("OTA: Preparing for reboot\n");

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
