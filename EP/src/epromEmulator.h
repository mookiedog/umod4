#if !defined EPROMEMULATOR_H
#define EPROMEMULATOR_H

#include <stdint.h>

// Start of EPROM image buffer (lower 32K of bank0), defined by the linker script
extern "C" uint8_t __ram_core1_eprom_start__[];
#define EPROM_IMAGE_BASE ((uintptr_t)__ram_core1_eprom_start__)

#define EPROM_IMAGE_SIZE_BYTES 32768


// Serve EPROM memory requests to the HC11.
// The routine uses standard C linkage, not that it matters since there are no params.
extern "C" void epromTask(void) __attribute__((noreturn));

#endif
