// busFailsafeSetup.cpp
//
// Initializes one PIO SM (PIO0 SM1) to own the HC11 data bus GPIOs and act as
// the output driver and a dead-man failsafe for read cycles.
//
// Core 1 pushes a data byte to the SM's TX FIFO when it has the read result
// ready (~cycle 32 from E-fall). The SM drives the byte onto GPIO 16-23,
// enables OE, counts down a calibrated delay, then tristates unconditionally.
// This tristate fires whether or not E ever falls. This prevents the RP2040 from
// back-driving an unpowered 74LVC4245 bus transceiver after ECU power loss.
// This was a real issue with the original firmware where the bus drivers were
// enable after seeing the next falling E: power fail meant that there might not be
// another falling E event, so the RP2040 would continue to drive data into the
// unpowered transceiver.
//
// PIO0 SM0 is used by the UART (uart_tx32.pio). SM1 is used here.
//
// After this function returns:
//   - PIO0 SM1 is running, blocked at 'pull block' waiting for the first byte
//   - GPIO 16-23 are muxed to PIO0, start as inputs (tri-stated)
//
// If enabled, DBG_BSY (GPIO29) gets driven low while SM1 is driving the data bus.
// This enables a scope to verify the data hold timing after the start of the next E cycle.

#include "hardware/pio.h"
#include "busFailsafe.pio.h"
#include "hardware.h"

// PIO and SM index — accessible to epromEmulator.S via these externs
// (setup code reads g_failsafe_pio_txf_addr to get the FIFO address to str into)
extern "C" {
    volatile uint32_t g_failsafe_txf_addr = 0;
}

void busFailsafeSetup()
{
    PIO pio = pio0;
    const uint sm = 1;      // SM0 = UART; SM1 = bus failsafe

    uint offset = pio_add_program(pio, &bus_failsafe_program);
    pio_sm_config c = bus_failsafe_program_get_default_config(offset);

    // OUT pins: data bus GPIO 16-23
    sm_config_set_out_pins(&c, HC11_DB_LSB, 8);

    // OSR shift right, no autopull: byte in bits [7:0], OE mask (0xFF) also in [7:0]
    sm_config_set_out_shift(&c, true, false, 32);

    // Side-set: DBG_BSY (GPIO29) for scope visibility
    sm_config_set_sideset_pins(&c, DBG_BSY_LSB);

    // Initialise SM (resets PC, shift regs, etc.)
    pio_sm_init(pio, sm, offset, &c);

    // Switch GPIO 16-23 from SIO to PIO0, starting as inputs (tri-stated)
    for (uint i = HC11_DB_LSB; i <= HC11_DB_MSB; i++) {
        pio_gpio_init(pio, i);
    }
    pio_sm_set_consecutive_pindirs(pio, sm, HC11_DB_LSB, 8, false);

    #if 0
    // Enable this code to allow the PIO to override Core1 from driving DBG_BSY.
    // This allows us to verify the timing of the PIO engine driving the data bus
    // using a scope. DBG_BSY should get deasserted 32-40 nS after +5_E falls.
    //
    // Pre-set DBG_BSY PIO output latch = 1 (inactive/high) BEFORE switching mux,
    // so the pin is driven high immediately when pio_gpio_init() switches it.
    pio_sm_set_pins_with_mask(pio, sm, 1u << DBG_BSY_LSB, 1u << DBG_BSY_LSB);
    pio_sm_set_pindirs_with_mask(pio, sm, 1u << DBG_BSY_LSB, 1u << DBG_BSY_LSB);
    pio_gpio_init(pio, DBG_BSY_LSB);    // mux switches here; pin immediately driven high
    #endif

    // Preoad Y = 0xFF (OE enable mask) via exec
    pio_sm_put_blocking(pio, sm, 0xFFu);
    pio_sm_exec(pio, sm, pio_encode_pull(false, false));
    pio_sm_exec(pio, sm, pio_encode_mov(pio_y, pio_osr));
    pio_sm_clear_fifos(pio, sm);

    // Record TX FIFO address for use by Core 1 assembly code
    g_failsafe_txf_addr = (uint32_t)&pio->txf[sm];

    // Start SM — it immediately blocks at 'pull block' waiting for Core 1
    pio_sm_set_enabled(pio, sm, true);
}
