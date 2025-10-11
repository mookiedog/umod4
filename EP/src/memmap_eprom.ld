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

/* Define the size of our flash, and how much we are reserving for code.
 * The rest of the space will be used to store eprom image_t structs
 */
__FLASH_SIZE_BYTES                    = 16*1024K;
__CODE_SIZE_BYTES                     =  1*1024K;
__BSON_IMAGE_PARTITION_SIZE_BYTES     =  1*1024K;
__FS_SIZE_BYTES                       = __FLASH_SIZE_BYTES - (__CODE_SIZE_BYTES + __BSON_IMAGE_PARTITION_SIZE_BYTES);

/* These next symbols can also be used by the codebase.
 * Note that the EPROM images and the FS must be spec'd to reside in
 * the non-cacheable, non-allocating EPROM address space 0x13xxxxxx
 *
  * TODO: oops, what about the 0x13000000 mentioned above??
 */
__BSON_IMAGE_PARTITION_START_ADDR = 0x10000000 + __CODE_SIZE_BYTES;
__FS_PARTITION_START_ADDR = __BSON_IMAGE_PARTITION_START_ADDR + __BSON_IMAGE_PARTITION_SIZE_BYTES;

/* We will be using RAM in a non-striped configuration.
 * The banks will be assigned exclusively to a core to avoid RAM contention.
 * This is the general layout for the SRAM:
 *   Striped RAM:           0x20000000..0x20040000  256K
 *   Nonstriped RAM bank0:  0x21000000..0x2100FFFF   64K
 *   Nonstriped RAM bank1:  0x21010000..0x2101FFFF   64K
 *   Nonstriped RAM bank2:  0x21020000..0x2102FFFF   64K
 *   Nonstriped RAM bank3:  0x21030000..0x2103FFFF   64K
 */
MEMORY
{
    FLASH(rx)           : ORIGIN = 0x10000000, LENGTH = __CODE_SIZE_BYTES
    BSON_IMAGES(r)      : ORIGIN = __BSON_IMAGE_PARTITION_START_ADDR,   LENGTH = __BSON_IMAGE_PARTITION_SIZE_BYTES
    FS(rw)              : ORIGIN = __FS_PARTITION_START_ADDR,           LENGTH = __FS_SIZE_BYTES
    RAM(rwx)            : ORIGIN = 0x21000000, LENGTH = 128K    /* Assign non-striped bank0/1 fo use by core0 */
    SCRATCH_X(rwx)      : ORIGIN = 0x20040000, LENGTH = 4K
    SCRATCH_Y(rwx)      : ORIGIN = 0x20041000, LENGTH = 4K      /* Core1: Holds the EPROM task */
    USB_DPORT(rwx)      : ORIGIN = 0x50100000, LENGTH = 4K      /* Would be nice to stick Core1 in here, but I get hardfaults! */
}

ENTRY(_entry_point)

SECTIONS
{
    /* todo: should this be 0x13xxxxxx?? As mentioned above?? */
    .bsonStore (__BSON_IMAGE_PARTITION_START_ADDR) :
    {
      KEEP(*(.bsonStore))
    } > BSON_IMAGES =0xFF


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

    /* This is based on us putting the EPROM code in scratch_y at the start, followed by a stack for the rest of scratch_y.
     * Note that the EPROM code basically uses no stack at all during operation so this should be pretty safe.
     */
    .scratch_y : {
        __scratch_y_start__ = .;
        *(.scratch_y.*)
        . = ALIGN(16);
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


    /* We make a tiny stack for core1 just to get the process booted - it is not used otherwise.
     * We put the stack at the end of our codespace in SCXRATCH_Y.
     */
    __StackOneTop = ORIGIN(SCRATCH_Y) + LENGTH(SCRATCH_Y);
    __StackOneBottom = ORIGIN(SCRATCH_Y) + SIZEOF(.scratch_y);

    /* The core0 stack goes at the end of RAM, where RAM is defined to be for the exclusive use of core0.
     * The rest of the ram address space is for core1.
     * I'll reserve 16K for the stack which is probably way overkill.
     */
    __StackLimit = ORIGIN(RAM) + LENGTH(RAM) - 16K;
    __StackTop = ORIGIN(RAM) + LENGTH(RAM);
    __StackBottom = __StackTop - SIZEOF(.stack_dummy);
    PROVIDE(__stack = __StackTop);

    /* Check if data + heap + stack exceeds RAM limit */
    ASSERT(__StackLimit >= __HeapLimit, "region RAM overflowed")

    ASSERT( __binary_info_header_end - __logical_binary_start <= 256, "Binary info must be in first 256 bytes of the binary")
    /* todo assert on extra code */
}

