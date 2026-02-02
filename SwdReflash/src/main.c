/*
 * SwdReflash Phase 0: RAM-resident LED blink test
 *
 * Target:
 *   RP2040 EP processor on umod4 board (Cortex-M0+)
 *
 * Memory Layout:
 *   - Vector table at 0x20000000 (256-byte aligned)
 *   - Code/data in striped RAM (0x20000100+)
 *   - Stack at 0x2003E000-0x20042000 (16KB)
 */

#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/structs/scb.h"
#include "hardware/structs/systick.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/bootrom.h"
#include "FlashBuffer.h"

#include "umod4_EP.h"   // For EP flash size
#include "hardware.h"   // From EP/src/hardware.h for DBG_BSY_LSB


extern uint32_t __end__;
void* endOfRam = (void*)&__end__;

#define LED_ON  0
#define LED_OFF 1

// Define the function pointers
typedef void (*rom_connect_internal_flash_fn)(void);
typedef void (*rom_flash_exit_xip_fn)(void);

// This manual setting up of QSPI flash interface is required in case the flash is completely blank,
// or if swd connected and halted the processor before the bootrom had done this itself.
void init_flash_for_reflash() {
    // 1. Look up the functions in the ROM table
    rom_connect_internal_flash_fn connect_internal_flash =
        (rom_connect_internal_flash_fn)rom_func_lookup_inline(ROM_FUNC_CONNECT_INTERNAL_FLASH);

    rom_flash_exit_xip_fn flash_exit_xip =
        (rom_flash_exit_xip_fn)rom_func_lookup_inline(ROM_FUNC_FLASH_EXIT_XIP);

    // 2. Execute the initialization
    if (connect_internal_flash && flash_exit_xip) {
        // Restore QSPI pad controls and connect the SSI (Synchronous Serial Interface)
        connect_internal_flash();

        // Put the flash into a clean state (standard SPI) so the SDK can manage it
        flash_exit_xip();
    }
}


#define MAILBOX_COUNT 2
mailbox_t mailbox[MAILBOX_COUNT] __attribute__((aligned(4))) = {0};

/* Main program never returns. It runs until:
 *   - Debugger halts it
 *   - EP is reset via RUN pin
 *   - Power is cycled
 */
int main(void)
{
    // Just to be sure
    save_and_disable_interrupts();

    // Initialize DBG_BSY LED (GPIO 29)
    gpio_init(DBG_BSY_LSB);
    gpio_put(DBG_BSY_LSB, LED_OFF);
    gpio_set_dir(DBG_BSY_LSB, GPIO_OUT);

    // Make sure the QSPI interface is set up and ready
    init_flash_for_reflash();

    // Flush all the mailboxes
    memset((void*)mailbox, 0, sizeof(mailbox));

    // Get a pointer to the agreed-upon location where the flashBufferInterface_X object will reside:
    flashBufferInterface_1_t* fbi = (flashBufferInterface_1_t*)FLASH_BUFFER_INTERFACE_ADDR;

    // Initialize the flashBufferInterface_t object at that location.
    // This will tell the WP that we are alive, as well as some specifics about the buffering we can handle:
    fbi->mailboxCount = MAILBOX_COUNT;
    fbi->mailboxAddr = (uint32_t)mailbox;
    fbi->bufferStartAddr = ((uint32_t)(endOfRam) + 4095) & ~4095;                // 4K-aligned start address
    fbi->bufferSizeBytes = (0x20040000 - (uint32_t)(fbi->bufferStartAddr));      // The flasher can use all the RAM up to 0x20040000

    // Write the magic field last - when the host sees this magic number,
    // it knows that the entire struct is populated
    fbi->magic = MAGIC_1;

    // Our mailbox processing loop
    mailbox_t* mbox = &mailbox[0];
    mailbox_t* last_mbox = &mailbox[fbi->mailboxCount];
    while (1) {
        uint32_t status;
        if (mbox->cmd != MAILBOX_CMD_NONE) {
            // Indicate that we are on the job
            mbox->status = status = MAILBOX_STATUS_BUSY;

            if (mbox->cmd == MAILBOX_CMD_PGM) {
                // program some flash!
                uint32_t flash_offset = mbox->target_addr - XIP_BASE;
                uint32_t flash_length = mbox->length;
                uint32_t ram_buffer   = mbox->buffer_addr;

                // Error check things before we start:
                if (mbox->target_addr & (4096-1)) {
                    // Address not on a 4K boundary
                    status = MAILBOX_STATUS_ERR_ADDR_ALGN;
                    goto abort;
                }

                if (mbox->length & (4096-1)) {
                    // Length not on a 4K boundary
                    status = MAILBOX_STATUS_ERR_ADDR_ALGN;
                    goto abort;
                }

                if (mbox->buffer_addr < fbi->bufferStartAddr) {
                    // Address starts below the FBI buffer area
                    status = MAILBOX_STATUS_ERR_RAM_BUF_STRT;
                    goto abort;
                }

                if ((mbox->buffer_addr+ mbox->length) > (mbox->buffer_addr + fbi->bufferSizeBytes)) {
                    // Address is off the end of our FBI buffer area
                    status = MAILBOX_STATUS_ERR_RAM_BUF_LEN;
                    goto abort;
                }

                if ((mbox->target_addr < XIP_BASE)) {
                    // Start address is below the flash address space
                    status = MAILBOX_STATUS_ERR_ADDR;
                    goto abort;
                }

                if ((mbox->target_addr + flash_length) > (XIP_BASE + PICO_FLASH_SIZE_BYTES)) {
                    // End address will be past the end of flash address space
                    status = MAILBOX_STATUS_ERR_LEN;
                    goto abort;
                }

                // No errors so far: Let's do this thing!
                // Specify using the big block erase
                // Erase the desired range
                gpio_put(DBG_BSY_LSB, LED_OFF);          // LED is OFF while erasing
                mbox->status = -1;
                uint32_t offset = mbox->target_addr - XIP_BASE;
                flash_range_erase(offset, flash_length);
                mbox->status = -2;

                // Verify that every byte is now in the erased state (all 1's)
                uint32_t* sa = (uint32_t*)(mbox->target_addr);
                uint32_t* ea = (uint32_t*)(mbox->target_addr + mbox->length);
                for (uint32_t* fp=sa; fp<ea; fp++) {
                    if (*fp != 0xFFFFFFFF) {
                        status  = MAILBOX_STATUS_ERR_ERASE;
                        goto abort;
                    }
                }

                // Program the flash
                mbox->status = -3;
                gpio_put(DBG_BSY_LSB, LED_ON);          // LED is ON while programming
                flash_range_program(offset, (uint8_t*)mbox->buffer_addr, mbox->length);

                // Verify the flash
                mbox->status = -4;
                sa = (uint32_t*)(mbox->target_addr);
                ea = (uint32_t*)(mbox->target_addr + mbox->length);
                uint32_t* rp = (uint32_t*)mbox->target_addr;
                for (uint32_t* fp=sa; fp<ea; rp++,fp++) {
                    if (*fp != *rp) {
                        status  = MAILBOX_STATUS_ERR_ERASE;
                        goto abort;
                    }
                }

                // Verification passed!
                status = MAILBOX_STATUS_SUCCESS;
            }
            else {
                // Oops: unknown command
                status = MAILBOX_STATUS_ERR_CMD;
                goto abort;
            }

            // Mark that we completed this command
            abort:
            mbox->cmd = MAILBOX_CMD_NONE;
            mbox->status = status;
        }
        else {
            if (++mbox == last_mbox) {
                mbox = mailbox;
            }
        }
    }

    // Never reached
    return 0;
}
