#include "pico/stdlib.h"

#include "SdPower.h"
#include "umod4_WP.h"

// Releases the SDIO bus back to plain, tristated GPIO. SdCardSDIO::init() explicitly re-enables
// internal pullups on all six lines after handing them to PIO (see its own comment there), and
// nothing else ever reverses that. Left alone, those pullups could weakly backpower an
// unpowered card through its DAT-line ESD protection diodes even after power is supposedly off.
// init() unconditionally redoes the full pin/PIO setup on every power-on cycle, so it's always
// safe to fully release these pins here.
static void tristate_sdio_bus(void)
{
    const uint sdio_pins[] = { SD_SCK_PIN, SD_MOSI_PIN, SD_DAT0, SD_DAT1, SD_DAT2, SD_DAT3 };
    for (uint i = 0; i < count_of(sdio_pins); i++) {
        gpio_init(sdio_pins[i]);
        gpio_set_dir(sdio_pins[i], GPIO_IN);
        gpio_set_pulls(sdio_pins[i], false, false);
    }
}

void sd_power_init(void)
{
    gpio_init(EN_VDD_SD_PIN);
    gpio_put(EN_VDD_SD_PIN, 0);
    gpio_set_dir(EN_VDD_SD_PIN, GPIO_OUT);
    tristate_sdio_bus();
}

void sd_power_on(void)
{
    gpio_put(EN_VDD_SD_PIN, 1);
}

void sd_power_off(void)
{
    gpio_put(EN_VDD_SD_PIN, 0);
    tristate_sdio_bus();
}

bool sd_power_is_on(void)
{
    return gpio_get(EN_VDD_SD_PIN);
}
