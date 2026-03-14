/**
 * Custom flash safety helper for FreeRTOS SMP.
 *
 * The SDK's default flash_safe_execute() implementation creates a dynamic task
 * on the other core each time a flash operation is performed. That task calls
 * vTaskDelete(NULL) when done, but the memory isn't freed until the idle task
 * runs. During rapid flash operations (like OTA), this causes memory exhaustion.
 *
 * This implementation uses a persistent statically-allocated task on core 1 that
 * waits for flash requests. When core 0 needs to do a flash operation:
 *   1. Core 0 signals the core 1 task
 *   2. Core 1 task disables IRQs and signals "ready"
 *   3. Core 0 disables its own IRQs and performs the flash operation
 *   4. Core 0 signals "done"
 *   5. Core 1 re-enables IRQs
 *
 * This avoids all dynamic memory allocation during flash operations.
 */

#include "FreeRTOS.h"
#include "task.h"
#include "hardware/sync.h"
#include "pico/flash.h"
#include "pico/time.h"

// Stack size for the flash helper task (runs on core 1)
#define FLASH_HELPER_STACK_WORDS 256

// Static allocation for the helper task
static StaticTask_t flash_helper_tcb;
static StackType_t  flash_helper_stack[FLASH_HELPER_STACK_WORDS];

// Lockout state machine - using volatile for cross-core visibility
enum {
    LOCKOUT_IDLE = 0,           // No flash operation in progress
    LOCKOUT_CORE0_WAITING,      // Core 0 is waiting for core 1 to be ready
    LOCKOUT_CORE1_READY,        // Core 1 has disabled IRQs and is ready
    LOCKOUT_CORE0_DONE,         // Core 0 has finished the flash operation
};

static volatile uint8_t lockout_state = LOCKOUT_IDLE;

// Per-core IRQ state storage
static uint32_t core0_irq_state;

/**
 * Flash helper task - runs on core 1 at highest priority.
 * Waits for core 0 to request a flash-safe window, then disables interrupts
 * and spins until core 0 signals completion.
 */
static void __not_in_flash_func(flash_helper_task)(void *arg)
{
    (void)arg;

    while (1) {
        // Wait for core 0 to request lockout
        while (lockout_state != LOCKOUT_CORE0_WAITING) {
            __wfe();
        }

        // Disable interrupts on this core
        uint32_t irq_state = save_and_disable_interrupts();

        // Signal that we're ready
        lockout_state = LOCKOUT_CORE1_READY;
        __sev();

        // Wait for core 0 to finish the flash operation
        while (lockout_state == LOCKOUT_CORE1_READY) {
            __wfe();
        }

        // Re-enable interrupts
        restore_interrupts(irq_state);

        // Return to idle state
        lockout_state = LOCKOUT_IDLE;
        __sev();
    }
}

/**
 * Enter the flash-safe zone. Coordinates with core 1 to ensure it's not
 * executing from flash, then disables interrupts on core 0.
 */
static int my_enter_safe_zone_timeout_ms(uint32_t timeout_ms)
{
    // Signal core 1 that we want to enter the safe zone
    lockout_state = LOCKOUT_CORE0_WAITING;
    __sev();

    // Wait for core 1 to acknowledge with timeout
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (lockout_state != LOCKOUT_CORE1_READY) {
        if (time_reached(deadline)) {
            lockout_state = LOCKOUT_IDLE;
            return PICO_ERROR_TIMEOUT;
        }
        __wfe();
    }

    // Core 1 is now safe - disable interrupts on core 0
    core0_irq_state = save_and_disable_interrupts();

    return PICO_OK;
}

/**
 * Exit the flash-safe zone. Signals core 1 to resume and re-enables
 * interrupts on core 0.
 */
static int my_exit_safe_zone_timeout_ms(uint32_t timeout_ms)
{
    // Re-enable interrupts on core 0
    restore_interrupts(core0_irq_state);

    // Signal core 1 that we're done
    lockout_state = LOCKOUT_CORE0_DONE;
    __sev();

    // Wait for core 1 to return to idle (with timeout for safety)
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (lockout_state != LOCKOUT_IDLE) {
        if (time_reached(deadline)) {
            return PICO_ERROR_TIMEOUT;
        }
        __wfe();
    }

    return PICO_OK;
}

/**
 * Core init/deinit callback - not needed since our task is persistent.
 */
static bool my_core_init_deinit(bool init)
{
    (void)init;
    return true;
}

// The flash safety helper structure
static flash_safety_helper_t my_helper = {
    .core_init_deinit = my_core_init_deinit,
    .enter_safe_zone_timeout_ms = my_enter_safe_zone_timeout_ms,
    .exit_safe_zone_timeout_ms = my_exit_safe_zone_timeout_ms,
};

static bool helper_initialized = false;

/**
 * Get the flash safety helper. This overrides the weak SDK default.
 * Creates the helper task on first call.
 */
flash_safety_helper_t *get_flash_safety_helper(void)
{
    if (!helper_initialized) {
        // Create the helper task pinned to core 1 at highest priority
        // Using xTaskCreateStaticAffinitySet for static allocation with core affinity
        xTaskCreateStaticAffinitySet(
            flash_helper_task,
            "flashhlp",
            FLASH_HELPER_STACK_WORDS,
            NULL,
            configMAX_PRIORITIES - 1,   // Highest priority
            flash_helper_stack,
            &flash_helper_tcb,
            (1u << 1)                   // Pin to core 1
        );

        helper_initialized = true;
    }

    return &my_helper;
}
