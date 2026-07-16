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
#include "hardware/resets.h"
#include "hardware/sync.h"
#include "pico/bootrom.h"
#include "FlashBuffer.h"
#include "murmur3.h"

#include "umod4_EP.h"   // For EP flash size
#include "hardware.h"   // From EP/src/hardware.h for DBG_BSY_LSB

// 64KB block erase opcode - defined in SDK flash.c but not exported in hardware/flash.h
#define FLASH_BLOCK_ERASE_CMD 0xd8u


extern uint32_t __end__;
void* endOfRam = (void*)&__end__;

#define LED_ON  0
#define LED_OFF 1

// Set up the flash interface from any starting state.
//
// SwdReflash is loaded by the debugger via connect_target() which HALTS the RP2040 but
// does NOT reset it.  EP may be halted at any point in its boot sequence — including
// mid-SSI-transaction — so the SSI peripheral can be in an unknown or partially-configured
// state.  The ROM connect_internal_flash/flash_exit_xip functions cannot reliably recover
// from an SSI that is stuck mid-transaction.
//
// Fix part 1: explicitly reset the XIP (which includes the SSI on RP2040) and QSPI
// peripherals via the RP2040 reset controller, putting SSI in a guaranteed clean state
// before the ROM calls touch it.
//
// Fix part 2: SDK 2.3.0 added flash_save_hardware_state() at the start of
// flash_range_erase/program, which snapshots xip_ctrl_hw->ctrl, and
// flash_restore_hardware_state() at the end which puts it back.  If XIP was not
// already enabled when the snapshot was taken, restore disables XIP and the verify
// loop reads via XIP_NOCACHE_BASE hang.  Calling enter_cmd_xip here ensures the
// snapshot captures a valid slow-03h XIP state so restore preserves it.
void init_flash_for_reflash() {
    // Deassert all QSPI pins momentarily by resetting the IO and pad blocks.
    // The XIP/SSI peripheral itself has no reset bit on RP2040, but resetting
    // IO_QSPI + PADS_QSPI forces CSn high (pin no longer driven), which aborts
    // any in-progress flash command and returns the chip to standby.  After
    // unreset, connect_internal_flash reconfigures the SSI and pads cleanly.
    reset_block(RESETS_RESET_IO_QSPI_BITS | RESETS_RESET_PADS_QSPI_BITS);
    unreset_block_wait(RESETS_RESET_IO_QSPI_BITS | RESETS_RESET_PADS_QSPI_BITS);
    typedef void (*rom_connect_internal_flash_fn)(void);
    typedef void (*rom_flash_exit_xip_fn)(void);
    typedef void (*rom_flash_flush_cache_fn)(void);
    typedef void (*rom_flash_enter_cmd_xip_fn)(void);

    rom_connect_internal_flash_fn connect_internal_flash =
        (rom_connect_internal_flash_fn)rom_func_lookup_inline(ROM_FUNC_CONNECT_INTERNAL_FLASH);
    rom_flash_exit_xip_fn flash_exit_xip =
        (rom_flash_exit_xip_fn)rom_func_lookup_inline(ROM_FUNC_FLASH_EXIT_XIP);
    rom_flash_flush_cache_fn flash_flush_cache =
        (rom_flash_flush_cache_fn)rom_func_lookup_inline(ROM_FUNC_FLASH_FLUSH_CACHE);
    rom_flash_enter_cmd_xip_fn flash_enter_cmd_xip =
        (rom_flash_enter_cmd_xip_fn)rom_func_lookup_inline(ROM_FUNC_FLASH_ENTER_CMD_XIP);

    if (connect_internal_flash && flash_exit_xip && flash_flush_cache && flash_enter_cmd_xip) {
        connect_internal_flash();  // configure SSI/pads for SPI access
        flash_exit_xip();          // reset flash chip to standard SPI mode
        flash_flush_cache();       // clean cache state, deassert CSn
        flash_enter_cmd_xip();     // leave XIP in a known valid slow-03h state before any flash ops
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

    // One-time flash interface setup required because SwdReflash is debugger-loaded into SRAM
    // after reset-halt; boot2 never ran, so xip_ctrl_hw->ctrl is in post-reset state.
    // SDK 2.3.0+ saves/restores that register around every flash_range_erase/program call.
    // Without this call the snapshot captures a non-XIP value, restore disables XIP, and
    // subsequent XIP_NOCACHE verify reads hang.  If a future SDK version adds more hardware
    // state to flash_save_hardware_state(), check whether that state also needs pre-warming here.
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
                //
                // Use direct ROM calls instead of the SDK flash_range_erase/program wrappers.
                // SDK 2.3.0 added flash_save/restore_hardware_state around those wrappers, which
                // snapshots xip_ctrl_hw->ctrl before the erase/program and restores it after.
                // When boot2 has not run (as here — SwdReflash is debugger-loaded into SRAM),
                // the snapshot captures the post-reset (non-XIP) value, restore disables XIP,
                // and subsequent XIP_NOCACHE verify reads hang.  Calling the ROM functions
                // directly bypasses the save/restore entirely.  Granular status codes between
                // each sub-step let us pinpoint exactly which call hangs if this fails again.
                typedef void (*rom_connect_fn)(void);
                typedef void (*rom_exit_xip_fn)(void);
                typedef void (*rom_erase_fn)(uint32_t, size_t, uint32_t, uint8_t);
                typedef void (*rom_program_fn)(uint32_t, const uint8_t *, size_t);
                typedef void (*rom_flush_fn)(void);
                typedef void (*rom_enter_xip_fn)(void);

                rom_connect_fn   rom_connect   = (rom_connect_fn)  rom_func_lookup_inline(ROM_FUNC_CONNECT_INTERNAL_FLASH);
                rom_exit_xip_fn  rom_exit_xip  = (rom_exit_xip_fn) rom_func_lookup_inline(ROM_FUNC_FLASH_EXIT_XIP);
                rom_erase_fn     rom_erase     = (rom_erase_fn)    rom_func_lookup_inline(ROM_FUNC_FLASH_RANGE_ERASE);
                rom_program_fn   rom_program   = (rom_program_fn)  rom_func_lookup_inline(ROM_FUNC_FLASH_RANGE_PROGRAM);
                rom_flush_fn     rom_flush     = (rom_flush_fn)    rom_func_lookup_inline(ROM_FUNC_FLASH_FLUSH_CACHE);
                rom_enter_xip_fn rom_enter_xip = (rom_enter_xip_fn)rom_func_lookup_inline(ROM_FUNC_FLASH_ENTER_CMD_XIP);

                if (!rom_connect || !rom_exit_xip || !rom_erase || !rom_program || !rom_flush || !rom_enter_xip) {
                    status = MAILBOX_STATUS_ERR_CMD;
                    goto abort;
                }

                // Erase the desired range using 64KB block erase (D8h)
                gpio_put(DBG_BSY_LSB, LED_OFF);          // LED is OFF while erasing
                uint32_t offset = mbox->target_addr - XIP_BASE;
                mbox->status = -1;   // about to connect_internal_flash
                rom_connect();
                mbox->status = -11;  // about to flash_exit_xip
                rom_exit_xip();
                mbox->status = -12;  // about to ROM erase
                rom_erase(offset, flash_length, FLASH_BLOCK_SIZE, FLASH_BLOCK_ERASE_CMD);
                mbox->status = -13;  // about to flush_cache
                rom_flush();
                mbox->status = -14;  // about to enter_cmd_xip
                rom_enter_xip();
                mbox->status = -2;   // erase done

                // Verify erase via XIP_NOCACHE_BASE to bypass cache and read
                // actual flash contents.
                {
                    uint32_t nc = mbox->target_addr - XIP_BASE + XIP_NOCACHE_BASE;
                    uint32_t* ev_sa = (uint32_t*)(nc);
                    uint32_t* ev_ea = (uint32_t*)(nc + mbox->length);
                    for (uint32_t* fp = ev_sa; fp < ev_ea; fp++) {
                        if (*fp != 0xFFFFFFFF) {
                            status = MAILBOX_STATUS_ERR_ERASE;
                            goto abort;
                        }
                    }
                }

                // Program the flash
                gpio_put(DBG_BSY_LSB, LED_ON);           // LED is ON while programming
                mbox->status = -3;   // about to connect_internal_flash
                rom_connect();
                mbox->status = -21;  // about to flash_exit_xip
                rom_exit_xip();
                mbox->status = -22;  // about to ROM program
                rom_program(offset, (const uint8_t *)mbox->buffer_addr, mbox->length);
                mbox->status = -23;  // about to flush_cache
                rom_flush();
                mbox->status = -24;  // about to enter_cmd_xip
                rom_enter_xip();

                // Verify program via XIP_NOCACHE_BASE against the source buffer.
                mbox->status = -4;   // program done, about to verify
                {
                    uint32_t nc = mbox->target_addr - XIP_BASE + XIP_NOCACHE_BASE;
                    uint32_t* sa = (uint32_t*)(nc);
                    uint32_t* ea = (uint32_t*)(nc + mbox->length);
                    uint32_t* rp = (uint32_t*)mbox->buffer_addr;
                    for (uint32_t* fp=sa; fp<ea; rp++,fp++) {
                        if (*fp != *rp) {
                            status  = MAILBOX_STATUS_ERR_VERIFY;
                            goto abort;
                        }
                    }
                }

                // Verification passed!
                status = MAILBOX_STATUS_SUCCESS;
            }
            else if (mbox->cmd == MAILBOX_CMD_M3) {
                // Compute murmur3 hash of a flash region.
                // XIP was disabled at startup by init_flash_for_reflash(). Re-enable
                // it in slow 03h mode (no boot2 needed), read via NOCACHE alias to
                // bypass stale cache, then exit XIP again for subsequent PGM commands.
                typedef void (*rom_flash_enter_cmd_xip_fn)(void);
                typedef void (*rom_flash_exit_xip_fn2)(void);

                rom_flash_enter_cmd_xip_fn enter_xip =
                    (rom_flash_enter_cmd_xip_fn)rom_func_lookup_inline(ROM_FUNC_FLASH_ENTER_CMD_XIP);
                rom_flash_exit_xip_fn2 exit_xip =
                    (rom_flash_exit_xip_fn2)rom_func_lookup_inline(ROM_FUNC_FLASH_EXIT_XIP);

                if (!enter_xip || !exit_xip) {
                    status = MAILBOX_STATUS_ERR_CMD;
                    goto abort;
                }

                uint32_t xip_offset = mbox->target_addr - XIP_BASE;
                uint32_t hash_len   = mbox->length;

                enter_xip();

                uint8_t* data = (uint8_t*)(XIP_NOCACHE_BASE + xip_offset);
                mbox->buffer_addr = murmur3_32(data, hash_len, ~0x0u);

                exit_xip();

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
