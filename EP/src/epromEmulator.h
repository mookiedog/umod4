#if !defined EPROMEMULATOR_H
#define EPROMEMULATOR_H


// Define where the code image will be served from.
// This address corresponds to the first 32K of the 64K SRAM bank 3
#define IMAGE_BASE 0x21030000

// Serve EPROM memory requests to the HC11.
// The routine uses standard C linkage, not that it matters since there are no params.
extern "C" void epromTask(void) __attribute__((noreturn));

#endif
