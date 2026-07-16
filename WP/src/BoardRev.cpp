#include <stdio.h>

#include "pico/stdlib.h"

#include "BoardRev.h"
#include "umod4_WP.h"

static PcbRev s_pcb_rev = PCB_REV_UNKNOWN;

PcbRev boardrev_detect(void)
{
    // To tell 4V1 from 4V2 PCBs, the DISK_BSY_PIN (GP26) gets a deliberate bodge strap each of
    // the small number of existing 4V1 boards gets a 3.3K resistor to GND, paralleled with
    // with the existing active-high LED and its current-limit resistor.
    // Since GP26 is driven as a low-impedance push-pull output when the LED is ON,
    // the parallel bodge resistor doesn't perceptibly dim it.
    //
    // Detection Mechanism:
    // Enable the internal pullup (RP2350 datasheet: 32-86K) and read GP26.
    // If the 3.3K bodge resistor is present (4V1 only), it overpowers the weak internal pullup
    // and the input will read as '0'.
    // Since 4V2 boards do not have this pulldown bodge, they will read as '1' due to
    // the weak internal pullup.
    gpio_init(DISK_BSY_PIN);
    gpio_set_dir(DISK_BSY_PIN, GPIO_IN);
    gpio_set_pulls(DISK_BSY_PIN, true, false);
    sleep_us(10);
    bool bodge_present = !gpio_get(DISK_BSY_PIN);
    gpio_set_pulls(DISK_BSY_PIN, false, false);

    s_pcb_rev = bodge_present ? PCB_REV_4V1 : PCB_REV_4V2;
    return s_pcb_rev;
}

PcbRev boardrev_get(void)
{
    return s_pcb_rev;
}

// For consistency: provide a routine that returns the PCB version in the form of a string
const char* boardrev_str(void)
{
    switch (s_pcb_rev) {
        case PCB_REV_4V1: return "4V1";
        case PCB_REV_4V2: return "4V2";
        default:          return "Unknown";
    }
}
