// hc11SyncSetup.cpp
//
// Initializes PIO1 SM0 as the HC11 E-clock synchronizer and Core 1 wakeup source.
//
// SM0 watches GPIO 15 (HC11_E_LSB) for a falling edge and fires PIO1 IRQ flag 0,
// which routes to CPU IRQ 9 (PIO1_IRQ_0) to wake Core 1 from WFI.
//
// The SM asserts IRQ flag 0 for 2 cycles then clears it itself, so Core 1's
// post-WFI ack only needs to clear NVIC_ICPR — it never writes PIO1->irq.
//
// PIO1 is otherwise unused; busFailsafe uses PIO0 SM1.
//
// After this function returns:
//   - PIO1 SM0 is running, watching for E-fall
//   - PIO1 IRQ flag 0 routes to CPU IRQ 9

#include "hardware/pio.h"
#include "hc11Sync.pio.h"
#include "hardware.h"

void hc11SyncSetup()
{
    PIO pio = pio1;
    const uint sm = 0;

    uint offset = pio_add_program(pio, &hc11_sync_program);
    pio_sm_config c = hc11_sync_program_get_default_config(offset);

    // No IN/OUT/side-set pins needed — SM only watches GPIO via 'wait' instruction
    // and communicates via PIO IRQ flag only.

    pio_sm_init(pio, sm, offset + hc11_sync_wrap_target, &c);

    // Route PIO1 IRQ flag 0 → CPU IRQ 9 (PIO1_IRQ_0) to wake Core 1 from WFI.
    // Bit 8 of inte = PIO IRQ flag 0. (Bit 0 would be SM0 RX FIFO not-empty — wrong.)
    pio1->irq_ctrl[0].inte = 0x100u;

    // Start SM — begins watching for E-fall immediately.
    // Core 1 must flush stale IRQ state before its sync WFI (see epromEmulator.S).
    pio_sm_set_enabled(pio, sm, true);
}
