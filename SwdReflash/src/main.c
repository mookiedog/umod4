/*
 * SwdReflash Phase 0: RAM-resident LED blink test
 *
 * Target:
 *   RP2040 EP processor on umod4 board (Cortex-M0+)
 *
 * Memory Layout:
 *   - Vector table at 0x20000000 (256-byte aligned)
 *   - Code/data in striped RAM (0x20000100+)
 *   - Stack at 0x2003E000-0x20042000 (16KB)
 */

#include <stdint.h>
#include <assert.h>

#include "pico.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/clocks.h"

#include "hardware.h"  // From EP/src/hardware.h for DBG_BSY_LSB

/*
 * NOTE: This program uses the Pico SDK's standard startup code (crt0.S) for initialization.
 * The SDK handles:
 *   - Vector table setup
 *   - .bss zero-initialization
 *   - .data copy (not needed for RAM-only, but harmless)
 *   - Calling main()
 *
 * We don't need a custom _entry_point for this simple LED blink program.
 */

/* LED blink configuration
 *
 * 0.5Hz = 2 seconds per cycle = 1 second ON + 1 second OFF
 */
#define LED_ON_TIME_US  (50000)   // 0.1 second ON
#define LED_OFF_TIME_US (100000)   // 0.1 second OFF

void blink() __attribute__((noreturn));
void blink()
{
    // Initialize DBG_BSY LED (GPIO 29)
    // Note: DBG_BSY is active-low, so:
    //   gpio_put(DBG_BSY_LSB, 0) = LED ON
    //   gpio_put(DBG_BSY_LSB, 1) = LED OFF
    gpio_init(DBG_BSY_LSB);
    gpio_set_dir(DBG_BSY_LSB, GPIO_OUT);
    gpio_put(DBG_BSY_LSB, 1);  // Start with LED OFF

    // Infinite blink loop at 0.1Hz
    while (true) {
        // Turn LED ON (active-low, so write 0)
        gpio_put(DBG_BSY_LSB, 0);
        busy_wait_us_32(LED_ON_TIME_US);

        // Turn LED OFF (active-low, so write 1)
        gpio_put(DBG_BSY_LSB, 1);
        busy_wait_us_32(LED_OFF_TIME_US);
    }
}

/* Main program never returns. It runs until:
 *   - Debugger halts it
 *   - EP is reset via RUN pin
 *   - Power is cycled
 */
int main(void)
{
    // Force RP2040 to a standard 125MHz
    set_sys_clock_khz(125000, true);

    // Leave this LED blinking example in the source for easy testing, if needed later.
    if (true) {
        blink();
    }


    // Never reached
    return 0;
}
