#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "hardware/structs/watchdog.h"
#include "hardware/structs/psm.h"
#include "hardware/structs/ticks.h"
#include "hardware/regs/watchdog.h"
#include "hardware/regs/psm.h"

#define SPARE1_LED_PIN 26

// This whole program turned out uglier than I would like. But it works, so there's that.

// Stub out pico_stdlib runtime init functions to prevent SDK from running
// heavy initialization before main(). Without these, the SDK init sequence
// executes bkpt instructions that halt the CPU when running under a debugger.
void runtime_init_post_clock_resets(void) {}
void runtime_init_default_alarm_pool(void) {}
void runtime_init_bootrom_reset(void) {}
void runtime_init_per_core_bootrom_reset(void) {}
void runtime_init_spin_locks_reset(void) {}
void runtime_init_per_core_irq_priorities(void) {}


static void busy_wait_ms(uint32_t ms, uint32_t clk_sys_khz)
{
    uint32_t iterations = (clk_sys_khz * ms) / 4;
    for (volatile uint32_t i = 0; i < iterations; i++) {}
}

int main(void)
{
    uint32_t clk_sys_khz = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS);

    gpio_init(SPARE1_LED_PIN);
    gpio_set_dir(SPARE1_LED_PIN, GPIO_OUT);

    for (int i = 0; i < 6; i++) {
        gpio_put(SPARE1_LED_PIN, 1);
        busy_wait_ms(20, clk_sys_khz);
        gpio_put(SPARE1_LED_PIN, 0);
        busy_wait_ms(7, clk_sys_khz);
    }

    // Write USB BOOTSEL boot parameters directly to watchdog scratch registers.
    // This replicates what the RP2350 bootrom's s_varm_api_reboot() does for
    // REBOOT2_FLAG_REBOOT_TYPE_BOOTSEL, derived from pico-bootrom-rp2350 varm_apis.c.
    //
    // On the subsequent watchdog reset, the bootrom reads scratch[4] for
    // VECTORED_BOOT_MAGIC, validates scratch[5] = scratch[7] ^ -scratch[4],
    // and reads the boot type from scratch[6].
    watchdog_hw->scratch[2] = 0;            // bootsel_flags: no interface disable
    watchdog_hw->scratch[3] = 0;            // gpio_pin: no activity LED
    watchdog_hw->scratch[4] = 0xb007c0d3u;  // VECTORED_BOOT_MAGIC
    watchdog_hw->scratch[5] = 0xfffffffe;   // REBOOT_TO_MAGIC_PC ^ -REBOOT_TO_MAGIC_PC
    watchdog_hw->scratch[6] = 2;            // BOOT_TYPE_BOOTSEL
    watchdog_hw->scratch[7] = 0xb007c0d3u;  // REBOOT_TO_MAGIC_PC

    // Divide clk_sys down to 1 MHz for the watchdog tick
    ticks_hw->ticks[TICK_WATCHDOG].cycles = clk_sys_khz / 1000;
    ticks_hw->ticks[TICK_WATCHDOG].ctrl   = 1; // ENABLE

    // Reset everything on watchdog fire, then arm with 10ms
    psm_hw->wdsel = PSM_WDSEL_BITS;
    watchdog_hw->ctrl = 0;                          // disable and clear pause-on-debug
    watchdog_hw->load = 10000;                      // 10 ms at 1 MHz tick
    watchdog_hw->ctrl = WATCHDOG_CTRL_ENABLE_BITS;  // start countdown

    while (1) {}
}
