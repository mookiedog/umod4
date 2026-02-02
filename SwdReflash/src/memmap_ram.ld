/* memmap_ram.ld - RAM-only linker script for SwdReflash
 *
 * This linker script is designed for programs that run entirely from RAM,
 * loaded via SWD debugger. No flash or boot2 bootloader is used.
 *
 * Key differences from flash-based linker scripts:
 * - No FLASH memory region
 * - No boot2 section (debugger loads directly to RAM)
 * - Vector table in RAM at 0x20000000
 * - Code and data both in RAM (no LMA/VMA split)
 * - Entry point is _entry_point function
 *
 * Memory Layout:
 *   Vector Table:  0x20000000-0x200000FF  (256 bytes, must be aligned)
 *   Code (.text):  0x20000100-...         (program code)
 *   Data (.data):  ...                    (initialized data)
 *   BSS  (.bss):   ...                    (zero-initialized data)
 *   Heap:          ...                    (minimal, ~1KB)
 *   Stack:         0x2003E000-0x20042000  (16KB, grows downward)
 *   Stack Top:     0x20042000             (initial SP value)
 *
 * RP2040 has 264KB SRAM total:
 *   Striped RAM:   0x20000000-0x2003FFFF  (256KB) - used for this program
 *   Bank 0:        0x21000000-0x2100FFFF  (64KB)  - not used
 *   Bank 1:        0x21010000-0x2101FFFF  (64KB)  - not used
 *   Bank 2:        0x21020000-0x2102FFFF  (64KB)  - not used
 *   Bank 3:        0x21030000-0x2103FFFF  (64KB)  - not used
 *   SCRATCH_X:     0x20040000-0x20040FFF  (4KB)   - not used
 *   SCRATCH_Y:     0x20041000-0x20041FFF  (4KB)   - not used
 */

MEMORY
{
    /* Use striped RAM for entire program
     * Reserve top 16KB for stack, giving us 240KB for code+data+heap
     */
    RAM(rwx) : ORIGIN = 0x20000000, LENGTH = 240K
}

/* SDK's crt0.S provides _entry_point symbol as the reset handler */
ENTRY(_entry_point)

SECTIONS
{
    /* Vector table MUST be first and 256-byte aligned
     * ARM Cortex-M requirement: VTOR register must point to 256-byte boundary
     */
    .vectors : ALIGN(256)
    {
        __vectors_start = .;
        KEEP(*(.vectors))
        . = ALIGN(256);
        __vectors_end = .;
    } > RAM

    /* Program code and read-only data
     * All in RAM since this is a RAM-only build
     */
    .text : ALIGN(4)
    {
        __text_start = .;

        /* Reset handler section (SDK's crt0.S) */
        KEEP(*(.reset))

        /* Constructor/destructor arrays */
        KEEP(*(.init))
        KEEP(*(.fini))

        /* Exception handling (C++) */
        *(.eh_frame*)
        KEEP(*(.eh_frame))

        /* All executable code */
        *(.text*)

        /* Read-only data (constants, strings) */
        *(.rodata*)

        . = ALIGN(4);
        __text_end = .;
        __etext = __text_end;  /* SDK crt0 expects this symbol */
    } > RAM

    /* Binary info (Pico SDK metadata) */
    .binary_info : ALIGN(4)
    {
        __binary_info_start = .;
        KEEP(*(.binary_info.keep.*))
        *(.binary_info.*)
        __binary_info_end = .;
    } > RAM

    .binary_info_header : ALIGN(4)
    {
        KEEP(*(.binary_info_header))
    } > RAM

    /* ARM exception unwinding info */
    .ARM.extab : ALIGN(4)
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > RAM

    .ARM.exidx : ALIGN(4)
    {
        __exidx_start = .;
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __exidx_end = .;
    } > RAM

    /* Initialized data
     * In flash builds, this would have LMA in flash and VMA in RAM
     * In RAM-only builds, both LMA and VMA are in RAM (no copy needed)
     */
    .data : ALIGN(4)
    {
        __data_start__ = .;

        /* Constructor/destructor function pointers */
        __preinit_array_start = .;
        KEEP(*(.preinit_array))
        __preinit_array_end = .;

        __init_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = .;

        __fini_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
        KEEP(*(.fini_array))
        __fini_array_end = .;

        /* Initialized variables */
        *(.data*)

        . = ALIGN(4);
        __data_end__ = .;
    } > RAM

    /* Uninitialized data (zero-initialized by startup code) */
    .bss : ALIGN(4)
    {
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end__ = .;
    } > RAM

    /* Heap (minimal for LED blink program) */
    .heap (NOLOAD) : ALIGN(4)
    {
        __end__ = .;
        end = __end__;

        /* Reserve 1KB heap (probably not needed for LED blink,
         * but allows for small dynamic allocations if needed later)
         */
        . = . + 1024;

        __HeapLimit = .;
    } > RAM

    /* Stack grows downward from top of RAM
     * Reserve 16KB for stack (0x2003E000 - 0x20042000)
     * This is generous for a simple LED blink program
     */
    __StackLimit = ORIGIN(RAM) + LENGTH(RAM);  /* 0x2003C000 */
    __StackTop = 0x20042000;                    /* Top of SCRATCH_Y region */
    PROVIDE(__stack = __StackTop);

    /* SDK crt0 expects these scratch memory symbols even though we don't use SCRATCH_X/Y
     * For RAM-only builds, we don't need separate scratch regions, so set them to zero-size
     */
    __scratch_x_source__ = __etext;
    __scratch_x_start__ = 0x20040000;
    __scratch_x_end__ = 0x20040000;  /* Zero-size region */

    __scratch_y_source__ = __etext;
    __scratch_y_start__ = 0x20041000;
    __scratch_y_end__ = 0x20041000;  /* Zero-size region */

    /* Ensure stack doesn't collide with heap */
    ASSERT(__StackLimit >= __HeapLimit, "ERROR: Stack collision with heap! Reduce heap size or code size.")

    /* Ensure vector table is properly aligned */
    ASSERT((__vectors_start & 0xFF) == 0, "ERROR: Vector table must be 256-byte aligned!")
}

/* Export useful symbols for debugging */
__flash_binary_start = ORIGIN(RAM);
__binary_end = __HeapLimit;
__total_ram_used = __HeapLimit - ORIGIN(RAM);
