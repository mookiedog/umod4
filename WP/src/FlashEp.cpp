#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "hardware/gpio.h"

#include "FlashEp.h"
#include "swdreflash_binary.h"
#include "Swd.h"

#include "lfs.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define UF2_MAGIC_START0 0x0A324655
#define UF2_MAGIC_START1 0x9E5D5157
#define UF2_MAGIC_END    0x0AB16F30
#define METABLOCK_SIZE   (64 * 1024)
#define UF2_BLOCK_SIZE   512

extern void epResetAndRun();

flashBufferInterface_1_t FlashEp::fbi_1;

typedef struct {
    uint32_t magicStart0;
    uint32_t magicStart1;
    uint32_t flags;
    uint32_t targetAddr;
    uint32_t payloadSize;
    uint32_t blockNo;
    uint32_t numBlocks;
    uint32_t familyID;
    uint8_t  data[476];
    uint32_t magicEnd;
} UF2_Block;

bool verbose = true;

// Stub: Deal with a consolidated [up to] 64K metablock
// For beginners, we are not going to pin-pong, but just use a single block

bool FlashEp::handle_metablock(uint32_t target_start_addr, uint8_t *buffer, size_t size, bool verbose)
{
    bool ok = false;
    mailbox_t mbox;
    int32_t status;

    if (verbose) printf("%s: Metablock ready for target [0x%08X..0x%08X], size %zu\n",
           __FUNCTION__, target_start_addr, target_start_addr+size-1, size);
    if (size == 0) {
        return true;
    }
    if (size % 4096 != 0) {
        printf("%s: Metablock size %zu is not multiple of 4096\n", __FUNCTION__,  size);
        return false;
    }
    if (fbi_1.magic == MAGIC_1) {
        // Write the data from the WP buffer to the EP RAM buffer
        ok = swd->write_target_mem(fbi_1.bufferStartAddr, (uint32_t*)buffer, size);
        if (!ok) {
            return false;
        }
        // Now, construct the mailbox to tell the flasher program what to do
        // The order of construction here is immaterial: what matters is the order that write_target_mem() writes the structure.
        mbox.cmd = MAILBOX_CMD_PGM;                 // We want to program flash
        mbox.buffer_addr = fbi_1.bufferStartAddr;   // Target RAM address where the data to flash can be found
        mbox.target_addr = target_start_addr;       // Target Flash address where the data needs to be flashed
        mbox.length = size;
        mbox.status = 0;                            // Harmless, but useful in case we read the status before the flasher zeroes it

        // All ready: tell the flasher app to get to work
        if (verbose) printf("%s: Writing mailbox\n", __FUNCTION__);
        ok = swd->write_target_mem(fbi_1.mailboxAddr, (uint32_t*)&mbox, sizeof(mbox));
        if (!ok) {
            printf("%s: Write mbox failed\n", __FUNCTION__);
            return false;
        }

        // Now, we wait until the flasher finishes or we time out.
        // The flasher will write a positive, non-zero value in status when it completes
        uint32_t timeout = 10000000;
        uint32_t t0 = time_us_32();
        uint32_t elapsed;
        while (1) {
            uint32_t tNow = time_us_32();
            elapsed = tNow - t0;

            if (elapsed > timeout) {
                printf("Metablock write operation timed out\n");
                return false;
                break;
            }

            // Check the status byte and see if the EP is done flashing yet
            if (!swd->read_target_mem(fbi_1.mailboxAddr, (uint32_t*)&status, sizeof(status))) {
                printf("%s: Read Status failed\n", __FUNCTION__);
                return false;
            }
            if (verbose) printf("%s: Read Status returned %d\n", __FUNCTION__, status);
            if (status > MAILBOX_STATUS_BUSY) {
                break;
            }
            busy_wait_us_32(100000);
        }

        // Flasher has reported being done in some fashion:
        if (status == MAILBOX_STATUS_SUCCESS) {
            // As good as it gets!!
            if (verbose) printf("%s: Flash Metablock write success!\n", __FUNCTION__);
            ok = true;
        }
        else if (status == MAILBOX_STATUS_ERR_ERASE) {
            printf("%s: Flash Erase Error\n", __FUNCTION__);
            return false;
        }
        else if (status == MAILBOX_STATUS_ERR_VERIFY) {
            printf("%s: Flash Verify error\n", __FUNCTION__);
            return false;
        }
        else {
            printf("%s: Flash Error: 0x%X\n", __FUNCTION__, status);
            return false;
        }
    }
    else {
        printf("%s: Unknown magic number: 0x%08X\n", __FUNCTION__, fbi_1.magic);
        return false;
    }

    return true;
}

int32_t FlashEp::process_uf2(lfs_t *lfs, const char *path, bool verbose)
{
    // NOT COOL
    extern lfs_t lfs;
    lfs_file_t file;

    UF2_Block block;

    // Metablock state
    static uint8_t metablock_buffer[METABLOCK_SIZE];
    uint32_t metablock_start_addr = 0;
    uint32_t next_expected_addr = 0;
    size_t metablock_offset = 0;
    bool first_block = true;

    if (verbose) printf("%s: Opening UF2 file %s\n", __FUNCTION__, path);
    int err = lfs_file_open(&lfs, &file, path, LFS_O_RDONLY);
    if (err < 0) {
        printf("%s: Unable to open file <%s>: lfs err: %d\n", __FUNCTION__, path, err);
        return err;
    }

    // A UF2 file is a sequence of 512-byte blocks
    while (lfs_file_read(&lfs, &file, &block, sizeof(UF2_Block)) == sizeof(UF2_Block)) {

        // 4. Parse and verify
        if (false && verbose) {
            printf("%s: Read UF2 block %u/%u to 0x%08X, size %u\n",
            __FUNCTION__, block.blockNo, block.numBlocks, block.targetAddr, block.payloadSize);
        }
        if (block.magicStart0 != UF2_MAGIC_START0 ||
            block.magicStart1 != UF2_MAGIC_START1 ||
            block.magicEnd != UF2_MAGIC_END) {
                // Malformed block!
                return -1;
        }

        // Logic: Check for non-contiguous address or full metablock
        bool is_discontinuous = (!first_block && block.targetAddr != next_expected_addr);
        bool is_buffer_full = (metablock_offset + block.payloadSize > METABLOCK_SIZE);

        if (is_discontinuous || is_buffer_full) {
            // 7. Stub: Deal with the 64K block
            if (!handle_metablock(metablock_start_addr, metablock_buffer, metablock_offset)) {
                lfs_file_close(&lfs, &file);
                return -2;
            }

            metablock_offset = 0;
            first_block = true;
        }

        if (first_block) {
            metablock_start_addr = block.targetAddr;
            first_block = false;
        }

        // 5. Copy data area (usually 256 bytes) to metablock
        memcpy(metablock_buffer + metablock_offset, block.data, block.payloadSize);
        metablock_offset += block.payloadSize;
        next_expected_addr = block.targetAddr + block.payloadSize;
    }

    // 8. Handle last metablock at EOF
    if (metablock_offset > 0) {
        // UF2 files may not fill the last metablock completely, so pad it out to a full 4K page
        uint32_t padding_count = ((metablock_offset + 4095) & 0xFFF)+1;
        memset(metablock_buffer + metablock_offset, 0xFF, padding_count);
        if (!handle_metablock(metablock_start_addr, metablock_buffer, metablock_offset + padding_count)) {
            lfs_file_close(&lfs, &file);
            return -3;
        }
    }

    lfs_file_close(&lfs, &file);
    return 0;
}


// -----------------------------------------------------------------------------------
int32_t FlashEp::flashUf2(const char* pathname, bool verbose)
{
    int32_t res = 0;
    uint32_t magic;
    uint32_t t0 = time_us_32();
    uint32_t timeout;

    lfs_t* lfs = nullptr; // Assume this is set up elsewhere

    printf("Flashing EP with \"%s\"\n", pathname);

    if (verbose) printf("  - Resetting the EP\n");
    epResetAndRun();

    if (verbose) printf("  - Loading SWD Reflash Helper\n");

    // We need to halt the target core to load the flasher program
    const uint32_t core0 = 0;
    const bool halt = true;
    if (!swd->connect_target(core0, halt)) {
        printf("%s: Unable to connect to target\n", __FUNCTION__);
        res = -1;
        goto abort;
    }

    // First thing: clear out the flashBufferInterface_t structure in target RAM
    // When the flasher program starts, it will re-initialize this structure.
    // We will watch for that to prove that it is alive and running.
    uint32_t buff[64];
    memset(&buff, 0, sizeof(buff));
    if (!swd->write_target_mem(FLASH_BUFFER_INTERFACE_ADDR, buff, sizeof buff)) {
        printf("%s: Unable to flush FBI struct in target's RAM\n", __FUNCTION__);
        res = -2;
        goto abort;
    }

    // Load the flasher program into target RAM
    if (!swd->write_target_mem(0x20000000, swdreflash_data, swdreflash_size)) {
        printf("%s: Unable to load flasher program to target's RAM\n", __FUNCTION__);
        res = -3;
        goto abort;
    }

    // Start the flasher program on the target
    if (!swd->start_target(0x20000001, 0x20042000)) {
        printf("%s: Unable to start program on target\n", __FUNCTION__);
        res = -4;
        goto abort;
    }

    // We wait for a period of time to see the flashBufferInterface_t structure get initialized
    // This should happen in less than 100 mSec
    t0 = time_us_32();
    timeout = 1000000;      // 1 second
    while (1) {
        uint32_t tNow = time_us_32();
        uint32_t elapsed = tNow - t0;

        // Read the first word of the flashBufferInterface_t structure
        if (!swd->read_target_mem(FLASH_BUFFER_INTERFACE_ADDR, (uint32_t*)&fbi_1, sizeof(fbi_1))) {
            printf("%s: Unable to read flashBufferInterface_t from target RAM\n", __FUNCTION__);
            res = -5;
            goto abort;
        }

        if (fbi_1.magic == MAGIC_1) {
            // The flasher program is alive and running!
            // Display its FBI info
            printf("%s: Flasher program is running [%u mSec]\n", __FUNCTION__, (time_us_32() - t0)/1000);
            if (verbose) {
                printf("%s: FBI\n", __FUNCTION__);
                printf("%s:   magic:           %08x\n", __FUNCTION__, fbi_1.magic);
                printf("%s:   mailboxCount:    %d\n", __FUNCTION__, fbi_1.mailboxCount);
                printf("%s:   mailboxAddr:     %08x (target addr space)\n", __FUNCTION__, fbi_1.mailboxAddr);
                printf("%s:   bufferStartAddr: %08x (target addr space)\n", __FUNCTION__, fbi_1.bufferStartAddr);
                printf("%s:   bufferSizeBytes: %08x\n", __FUNCTION__, fbi_1.bufferSizeBytes);
            }
            break;
        }

        if (elapsed > timeout) {
            break;
        }

        // check in 10 mSec increments until we time out
        busy_wait_us_32(10000);
    }
    if (fbi_1.magic != MAGIC_1) {
        printf("%s: Timeout waiting for flasher program to start\n", __FUNCTION__);
        res = -6;
        goto abort;
    }

    printf("  - Flashing \"%s\"\n", pathname);
    res = process_uf2(lfs, pathname, verbose);

    if (res == 0) {
        printf("Flash EP completed successfully!\n");
    }
    else {
        printf("%s: Flash operation failed: %d\n", __FUNCTION__, res);
    }

    abort:
    // Hard reset the EP via EP_RUN pin
    epResetAndRun();

    return res;
}

// C-compatible wrapper for use by api_handlers.c
extern "C" int32_t flash_ep_uf2(const char* pathname, bool verbose) {
    return FlashEp::flashUf2(pathname, verbose);
}