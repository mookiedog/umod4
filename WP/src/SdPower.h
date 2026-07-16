#ifndef SDPOWER_H
#define SDPOWER_H

// Controls EN_VDD_SD_PIN, the SY6280 load switch that powers the SD card on 4V2.
// True no-connect on 4V1 (which has no power switch -- the card is always on), so
// these are harmless no-ops there.
//
// sd_power_init()/sd_power_off() also tristate the SDIO bus (SD_SCK/SD_MOSI/SD_DAT0-3),
// releasing them from PIO back to plain GPIO with no pulls -- SdCardSDIO::init() leaves
// internal pullups enabled on all six lines whenever it hands them to PIO, and nothing
// else reverses that, so without this the bus keeps weakly backpowering an unpowered
// card through its DAT-line ESD protection diodes.

// Configures the pin and drives power OFF. Call once at boot, before anything
// (in particular the SD hotplug task) can touch the SD bus.
void sd_power_init(void);

void sd_power_on(void);
void sd_power_off(void);

// Reads back the pin directly (not a cached flag) so it can't drift out of sync
// with reality. Used as a hardware-level backstop guard in SdCardSDIO -- see its
// init()/readSectors()/writeSectors() -- independent of the hotplug state machine's
// own POWER_UP/NO_CARD sequencing.
bool sd_power_is_on(void);

#endif
