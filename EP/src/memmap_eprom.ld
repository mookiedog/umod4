/* Based on GCC ARM embedded samples.
   Defines the following symbols for use by code:
    __exidx_start
    __exidx_end
    __etext
    __data_start__
    __preinit_array_start
    __preinit_array_end
    __init_array_start
    __init_array_end
    __fini_array_start
    __fini_array_end
    __data_end__
    __bss_start__
    __bss_end__
    __end__
    end
    __HeapLimit
    __StackLimit
    __StackTop
    __stack (== StackTop)
*/

/* Define how the flash gets allocated */
__FLASH_SIZE_BYTES                    =  16*1024K;      /* Total size of flash */
__CODE_SIZE_BYTES                     =   2*1024K;      /* 2 megabytes for code */
__IMAGE_STORE_PARTITION_SIZE_BYTES    = 128*64K;        /* 8 megabytes for image store (max 128 slots, each slot is 64K (32K BSON header + 32K image)) */
                                                        /* 6 megabytes remains unused */

__IMAGE_STORE_PARTITION_START_ADDR = 0x10000000 + __CODE_SIZE_BYTES;

/* We will be using RAM in a non-striped configuration.
 * The banks will be assigned exclusively to a core to avoid RAM contention.
 * Non-striped SRAM layout:
 *   Bank0:  0x21000000..0x2100FFFF   64K  (core1: EPROM image + bus log)
 *   Bank1:  0x21010000..0x2101FFFF   64K  (core0)
 *   Bank2:  0x21020000..0x2102FFFF   64K  (core0)
 *   Bank3:  0x21030000..0x2103FFFF   64K  (core0)
 * Core1 is in bank0 so that core0 overflow stays in its own banks.
 */
MEMORY
{
    FLASH(rx)               : ORIGIN = 0x10000000, LENGTH = __CODE_SIZE_BYTES
    IMAGE_STORE(rw)         : ORIGIN = __IMAGE_STORE_PARTITION_START_ADDR,  LENGTH = __IMAGE_STORE_PARTITION_SIZE_BYTES
    RAM_CORE1_EPROM(rwx)    : ORIGIN = 0x21000000, LENGTH = 32K     /* Bank0 lower half: EPROM image served by core1 */
    RAM_CORE1_BUSLOG(rwx)   : ORIGIN = 0x21008000, LENGTH = 32K     /* Bank0 upper half: bus log */
    RAM(rwx)                : ORIGIN = 0x21010000, LENGTH = 192K    /* Non-striped banks 1/2/3 for core0 */
    SCRATCH_X(rwx)          : ORIGIN = 0x20040000, LENGTH = 4K      /* Core0 stack */
    SCRATCH_Y(rwx)          : ORIGIN = 0x20041000, LENGTH = 4K      /* epromTask code (copied from flash by SDK crt0) */
    USB_DPORT(rwx)          : ORIGIN = 0x50100000, LENGTH = 4K      /* Would be nice to stick Core1 code in here, but I get hardfaults! */
}

ENTRY(_entry_point)

SECTIONS
{
    .image_store (__IMAGE_STORE_PARTITION_START_ADDR) :
    {
        KEEP(*(.image_store))
    } > IMAGE_STORE

    /* Second stage bootloader is prepended to the image. It must be 256 bytes big
       and checksummed. It is usually built by the boot_stage2 target
       in the Raspberry Pi Pico SDK
    */

    .flash_begin : {
        __flash_binary_start = .;
    } > FLASH

    .boot2 : {
        __boot2_start__ = .;
        KEEP (*(.boot2))
        __boot2_end__ = .;
    } > FLASH

    ASSERT(__boot2_end__ - __boot2_start__ == 256,
        "ERROR: Pico second stage bootloader must be 256 bytes in size")

    /* The second stage will always enter the image at the start of .text.
       The debugger will use the ELF entry point, which is the _entry_point
       symbol if present, otherwise defaults to start of .text.
       This can be used to transfer control back to the bootrom on debugger
       launches only, to perform proper flash setup.
    */

    .text : {
        __logical_binary_start = .;
        KEEP (*(.vectors))
        KEEP (*(.binary_info_header))
        __binary_info_header_end = .;
        KEEP (*(.reset))
        /* TODO revisit this now memset/memcpy/float in ROM */
        /* bit of a hack right now to exclude all floating point and time critical (e.g. memset, memcpy) code from
         * FLASH ... we will include any thing excluded here in .data below by default */
        *(.init)
        *(EXCLUDE_FILE(*libgcc.a: *libc.a:*lib_a-mem*.o *libm.a:) .text*)
        *(.fini)
        /* Pull all c'tors into .text */
        *crtbegin.o(.ctors)
        *crtbegin?.o(.ctors)
        *(EXCLUDE_FILE(*crtend?.o *crtend.o) .ctors)
        *(SORT(.ctors.*))
        *(.ctors)
        /* Followed by destructors */
        *crtbegin.o(.dtors)
        *crtbegin?.o(.dtors)
        *(EXCLUDE_FILE(*crtend?.o *crtend.o) .dtors)
        *(SORT(.dtors.*))
        *(.dtors)

        *(.eh_frame*)
        . = ALIGN(4);
    } > FLASH

    .rodata : {
        *(EXCLUDE_FILE(*libgcc.a: *libc.a:*lib_a-mem*.o *libm.a:) .rodata*)
        . = ALIGN(4);
        *(SORT_BY_ALIGNMENT(SORT_BY_NAME(.flashdata*)))
        . = ALIGN(4);
    } > FLASH

    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > FLASH

    __exidx_start = .;
    .ARM.exidx :
    {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    } > FLASH
    __exidx_end = .;

    /* Machine inspectable binary information */
    . = ALIGN(4);
    __binary_info_start = .;
    .binary_info :
    {
        KEEP(*(.binary_info.keep.*))
        *(.binary_info.*)
    } > FLASH
    __binary_info_end = .;
    . = ALIGN(4);

    /* End of .text-like segments */
    __etext = .;

   .ram_vector_table (COPY): {
        *(.ram_vector_table)
    } > RAM

    .data : {
        __data_start__ = .;
        *(vtable)

        *(.time_critical*)

        /* remaining .text and .rodata; i.e. stuff we exclude above because we want it in RAM */
        *(.text*)
        . = ALIGN(4);
        *(.rodata*)
        . = ALIGN(4);

        *(.data*)

        . = ALIGN(4);
        *(.after_data.*)
        . = ALIGN(4);
        /* preinit data */
        PROVIDE_HIDDEN (__mutex_array_start = .);
        KEEP(*(SORT(.mutex_array.*)))
        KEEP(*(.mutex_array))
        PROVIDE_HIDDEN (__mutex_array_end = .);

        . = ALIGN(4);
        /* preinit data */
        PROVIDE_HIDDEN (__preinit_array_start = .);
        KEEP(*(SORT(.preinit_array.*)))
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN (__preinit_array_end = .);

        . = ALIGN(4);
        /* init data */
        PROVIDE_HIDDEN (__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN (__init_array_end = .);

        . = ALIGN(4);
        /* finit data */
        PROVIDE_HIDDEN (__fini_array_start = .);
        *(SORT(.fini_array.*))
        *(.fini_array)
        PROVIDE_HIDDEN (__fini_array_end = .);

        *(.jcr)
        . = ALIGN(4);
        /* All data end */
        __data_end__ = .;
    } > RAM AT> FLASH

    .uninitialized_data (COPY): {
        . = ALIGN(4);
        *(.uninitialized_data*)
    } > RAM

    /* Start and end symbols must be word-aligned */
    .scratch_x : {
        __scratch_x_start__ = .;
        *(.scratch_x.*)
        . = ALIGN(4);
        __scratch_x_end__ = .;
    } > SCRATCH_X AT > FLASH
    __scratch_x_source__ = LOADADDR(.scratch_x);

    .scratch_y : {
        __scratch_y_start__ = .;
        *(.scratch_y.*)
        . = ALIGN(4);
        __scratch_y_end__ = .;
    } > SCRATCH_Y AT > FLASH
    __scratch_y_source__ = LOADADDR(.scratch_y);

    .usb_dport : {
        __usb_dport_start__ = .;
        *(.usb_dport.*)
        . = ALIGN(16);
        __usb_dport_end__ = .;
    } > USB_DPORT AT > FLASH
    __usb_dport_source__ = LOADADDR(.usb_dport);

    /* EPROM image buffer (32K). Populated at runtime by EpromLoader, not from flash. */
    .ram_core1_eprom (NOLOAD) : {
        __ram_core1_eprom_start__ = .;
        . += LENGTH(RAM_CORE1_EPROM);
        __ram_core1_eprom_end__ = .;
    } > RAM_CORE1_EPROM

    /* ECU bus log (32K). Written by core1's epromTask, read only by debugger.
     * Core1's boot stack (2K at top) overlaps, but is unused once epromTask starts.
     */
    .ram_core1_buslog (NOLOAD) : {
        __ram_core1_buslog_start__ = .;
        . += LENGTH(RAM_CORE1_BUSLOG);
        __ram_core1_buslog_end__ = .;
    } > RAM_CORE1_BUSLOG

    .bss  : {
        . = ALIGN(4);
        __bss_start__ = .;
        *(SORT_BY_ALIGNMENT(SORT_BY_NAME(.bss*)))
        *(COMMON)
        . = ALIGN(4);
        __bss_end__ = .;
    } > RAM

    .heap (COPY):
    {
        __end__ = .;
        end = __end__;
        KEEP(*(.heap*))
        __HeapLimit = .;
    } > RAM

    /* .stack*_dummy section doesn't contains any symbols. It is only
     * used for linker to calculate size of stack sections, and assign
     * values to stack symbols later
     */

    .stack_dummy (COPY):
    {
        KEEP(*(.stack*))
    } > RAM

    .flash_end : {
        __flash_binary_end = .;
    } > FLASH


    /* Core1 boot stack at end of SCRATCH_Y. Overlaps epromTask code growing up from bottom,
     * but the stack is only used briefly to launch core1; once epromTask starts it never returns. */
    __StackOneTop = ORIGIN(SCRATCH_Y) + LENGTH(SCRATCH_Y);
    __StackOneBottom = __StackOneTop - 0x800;

    /* Core0 stack in SCRATCH_X (4K). */
    __StackLimit = ORIGIN(SCRATCH_X);
    __StackTop = ORIGIN(SCRATCH_X) + LENGTH(SCRATCH_X);
    __StackBottom = __StackTop - SIZEOF(.stack_dummy);
    PROVIDE(__stack = __StackTop);

    /* Check if data + heap exceeds RAM limit */
    ASSERT(__HeapLimit <= ORIGIN(RAM) + LENGTH(RAM), "region RAM overflowed")

    ASSERT( __binary_info_header_end - __logical_binary_start <= 256, "Binary info must be in first 256 bytes of the binary")
    /* todo assert on extra code */
}

