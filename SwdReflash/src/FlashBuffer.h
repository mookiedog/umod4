#ifndef FLASHBUFFER_H
#define FLASHBUFFER_H

#include <stdint.h>

// Define the memory interface shared between the EP and this SWD Reflash app.
// We need at least 1 hard address for the WP and this SWD Reflash app to agree on.
// That address will be just after the main RAM in the RP2040, at 0x20040000.
// This app sets its stack pointer to the end of RAM at 0x20042000, so we have tons of room.
//
// The memory interface data structure will be created by this app at 0x20040000.
// The WP will read the structure to find out what buffers exist and where they are located.

typedef struct {
    uint32_t magic;                 // Magic number to identify the remainder of this structure
                                    // It must always come first in case what follows ever changes.
    uint32_t mailboxCount;
    uint32_t mailboxAddr;           // Address of first mailbox in RP2040 space
    uint32_t bufferSizeBytes;
    uint32_t bufferStartAddr;
} flashBufferInterface_1_t;

const uint32_t MAGIC_1 = 0x17583653;    // defines that we are using flashBufferInterface_1_t

// The flash buffer interface object is located at a fixed address in the EP address space:
#define FLASH_BUFFER_INTERFACE_ADDR   0x20040000

// Flashing process
//
// This flasher app will create one or more mailboxes in RAM for the WP to use.
// The buffers on this side can be large.
// The buffers on the WP side can be small. The WP can fill a large buffer on this side
// by doing multiple small writes, then invoking a command to process the entire buffer.
// While the flash programming routine has page granularity (256 bytes),
// the erase routine has sector granularity (4K).
// To simplify things, we will require that all erase or write operations be aligned to 4K boundaries.

// What does the cmd interface look like?
// Simple approach is to take advantage of the SDK programming model:
//      void flash_range_erase (uint32_t flash_offs, size_t count)
//      void flash_range_program (uint32_t flash_offs, const uint8_t *data, size_t count)
// These routines deal with flash offsets from start of flash, not absolute addresses.
// Lengths must be multiples of 256 bytes for programming, and 4096 bytes for erasing.
// For simplicity, we will require that all operations be aligned to 4K boundaries.
// There is a performance benefit to having 64K buffers because the flash chips can erase
// entire 64K blocks at a time only a bit slower than erasing a single 4K sector.

#define FLASH_BUFFER_LENGTH_BYTES  (64 * 1024)  // 64KB buffer

enum {
    MAILBOX_CMD_NONE            = 0x00,
    MAILBOX_CMD_PGM             = 0x01,   // Do everything: Erase, Program, Verify result afterwards
    MAILBOX_CMD_MAX
};

enum {
    // zero is meaningless: flasher will complete any operation with a non-zero result
    MAILBOX_STATUS_BUSY             = 0x01,     // must be the first label

    MAILBOX_STATUS_SUCCESS          = 0x02,
    MAILBOX_STATUS_ERR_ADDR         = 0x03,     // Address param is outside of Flash address space
    MAILBOX_STATUS_ERR_LEN          = 0x04,     // End address will be outside of Flash address space
    MAILBOX_STATUS_ERR_ADDR_ALGN    = 0x05,     // Flash Addr not aligned to 4K boundary
    MAILBOX_STATUS_ERR_LEN_ALGN     = 0x06,     // Flash Len not aligned to 4K boundary
    MAILBOX_STATUS_ERR_RAM_BUF_STRT = 0x07,     // RAM buffer ptr is below the FBI buffer space
    MAILBOX_STATUS_ERR_RAM_BUF_LEN  = 0x08,     // RAM buffer ptr goes past end of FBI buffer space
    MAILBOX_STATUS_ERR_ERASE        = 0x09,     // Data verification failure after erasure
    MAILBOX_STATUS_ERR_VERIFY       = 0x0A,     // Data verification failure after programming
    MAILBOX_STATUS_ERR_CMD          = 0x0B,     // Bad mailbox command
    MAILBOX_STATUS_MAX
};

typedef volatile struct {
    // Must come first so that we can read the status without needing to read the whole struct
    int32_t status;             // Status from EP to Flasher

    // This order is not important
    uint32_t buffer_addr;       // Address of data buffer in RP2040 space
    uint32_t length;            // Length of data in bytes
    uint32_t target_addr;       // Target flash address (actual address in RP2040 space, not offset)

    // Must come last so that when this data gets written to target RAM, all data ahead of this is present
    uint32_t cmd;               // Command from Flasher to EP
} mailbox_t;



#endif
