#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "hardware/gpio.h"

#include "FlashEp.h"
#include "swdreflash_binary.h"
#include "Swd.h"
#include "swd_lock.h"
#include "murmur3.h"

#include "lfsMgr.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Shared 1KB buffer used by flashSlotFromFile() and rewriteSlotHeader().
// Both functions are dispatched through file_io_task's single-item queue and
// are therefore never called concurrently.
static uint8_t s_chunk_buf[1024];

// ---------------------------------------------------------------------------
// Minimal BSON builder — write-only, used to construct slot BSON headers.
// Mirrors the BsonWriter in upload_handler.cpp but is local to FlashEp.cpp.

typedef struct { uint8_t* buf; size_t cap; size_t pos; bool ovf; } FEBsonW;

static void feb_byte(FEBsonW* w, uint8_t b)
    { if (w->pos < w->cap) w->buf[w->pos] = b; else w->ovf = true; w->pos++; }
static void feb_i32(FEBsonW* w, int32_t v) {
    feb_byte(w,(uint8_t)(v&0xFF)); feb_byte(w,(uint8_t)((v>>8)&0xFF));
    feb_byte(w,(uint8_t)((v>>16)&0xFF)); feb_byte(w,(uint8_t)((v>>24)&0xFF));
}
static void feb_cstr(FEBsonW* w, const char* s)
    { while (*s) feb_byte(w,(uint8_t)*s++); feb_byte(w,0); }
static void feb_utf8(FEBsonW* w, const char* key, const char* val)
    { feb_byte(w,0x02); feb_cstr(w,key); feb_i32(w,(int32_t)(strlen(val)+1)); feb_cstr(w,val); }
static void feb_int32(FEBsonW* w, const char* key, int32_t val)
    { feb_byte(w,0x10); feb_cstr(w,key); feb_i32(w,val); }

// Build {name, description, image_m3, [protection if !="N"]}. Returns byte
// count, 0 on overflow.
static size_t feb_build_slot_bson(uint8_t* buf, size_t cap,
                                   const char* name, const char* description,
                                   uint32_t image_m3, const char* protection)
{
    FEBsonW w = { buf, cap, 4, false };  /* reserve 4 bytes for doc length */
    feb_utf8(&w, "name", name);
    feb_utf8(&w, "description", description);
    feb_int32(&w, "image_m3", (int32_t)image_m3);
    if (protection && protection[0] && strcmp(protection, "N") != 0)
        feb_utf8(&w, "protection", protection);
    feb_byte(&w, 0x00);  /* doc terminator */
    int32_t t = (int32_t)w.pos;
    if (!w.ovf && cap >= 4) {
        buf[0]=(uint8_t)(t&0xFF); buf[1]=(uint8_t)((t>>8)&0xFF);
        buf[2]=(uint8_t)((t>>16)&0xFF); buf[3]=(uint8_t)((t>>24)&0xFF);
    }
    return w.ovf ? 0 : (size_t)t;
}

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
    lfs_file_t file;

    UF2_Block block;

    // Metablock state
    static uint8_t metablock_buffer[METABLOCK_SIZE];
    uint32_t metablock_start_addr = 0;
    uint32_t next_expected_addr = 0;
    size_t metablock_offset = 0;
    bool first_block = true;

    if (verbose) printf("%s: Opening UF2 file %s\n", __FUNCTION__, path);
    int err = lfs_file_open(lfs, &file, path, LFS_O_RDONLY);
    if (err < 0) {
        printf("%s: Unable to open file <%s>: lfs err: %d\n", __FUNCTION__, path, err);
        return err;
    }

    // A UF2 file is a sequence of 512-byte blocks
    while (lfs_file_read(lfs, &file, &block, sizeof(UF2_Block)) == sizeof(UF2_Block)) {

        // 4. Parse and verify
        if (false && verbose) {
            printf("%s: Read UF2 block %u/%u to 0x%08X, size %u\n",
            __FUNCTION__, block.blockNo, block.numBlocks, block.targetAddr, block.payloadSize);
        }
        if (block.magicStart0 != UF2_MAGIC_START0 ||
            block.magicStart1 != UF2_MAGIC_START1 ||
            block.magicEnd != UF2_MAGIC_END) {
                // Malformed block!
                return -10;
        }

        // Logic: Check for non-contiguous address or full metablock
        bool is_discontinuous = (!first_block && block.targetAddr != next_expected_addr);
        bool is_buffer_full = (metablock_offset + block.payloadSize > METABLOCK_SIZE);

        if (is_discontinuous || is_buffer_full) {
            // 7. Stub: Deal with the 64K block
            if (!handle_metablock(metablock_start_addr, metablock_buffer, metablock_offset)) {
                lfs_file_close(lfs, &file);
                return -11;
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
        uint32_t padded_size = (metablock_offset + 4095) & ~0xFFFU;
        uint32_t padding_count = padded_size - metablock_offset;
        memset(metablock_buffer + metablock_offset, 0xFF, padding_count);
        if (!handle_metablock(metablock_start_addr, metablock_buffer, padded_size)) {
            lfs_file_close(lfs, &file);
            return -12;
        }
    }

    lfs_file_close(lfs, &file);
    return 0;
}


// -----------------------------------------------------------------------------------
int32_t FlashEp::flashUf2(const char* pathname, bool verbose)
{
    SWDLock lock;
    int32_t res = 0;
    uint32_t magic;
    uint32_t t0 = time_us_32();
    uint32_t timeout;


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
    res = process_uf2(&lfs, pathname, verbose);

    if (res == 0) {
        printf("Flash EP completed successfully!\n");
    }
    else {
        printf("%s: Flash operation failed: %d\n", __FUNCTION__, res);
    }

    abort:
    // Release SWD pins before resetting EP, so an external debug probe can attach
    swd->unload();

    // Hard reset the EP via EP_RUN pin
    epResetAndRun();

    return res;
}

// ---------------------------------------------------------------------------
// flashSlot: write a 64KB image-store slot to EP flash.
// The slot layout: [32KB BSON header, zero-padded to 32768][32KB image binary].
// Uses the same EP flasher helper as flashUf2 but writes arbitrary data directly
// to EP SRAM rather than parsing a UF2 file.

int32_t FlashEp::flashSlot(const uint8_t* bson_hdr, size_t bson_hdr_size,
                            const uint8_t* image_data, uint32_t target_flash_addr,
                            bool erase)
{
    SWDLock lock;
    static const uint8_t zero_buf[1024] = {0};
    static uint8_t ff_buf[1024];
    static bool ff_buf_ready = false;
    if (!ff_buf_ready) {
        memset(ff_buf, 0xFF, sizeof(ff_buf));
        ff_buf_ready = true;
    }

    const uint32_t HEADER_SIZE = 32768;
    const uint32_t IMAGE_SIZE  = 32768;
    const uint32_t SLOT_SIZE   = 65536;
    const uint32_t SWD_CHUNK   = 1024;

    int32_t  res = 0;
    mailbox_t mbox;
    int32_t  flash_status;
    uint32_t t0, timeout;

    // bson_hdr_size must not exceed the header region
    if (bson_hdr_size > HEADER_SIZE) {
        printf("%s: bson_hdr_size %zu > %u\n", __FUNCTION__, bson_hdr_size, HEADER_SIZE);
        return -1;
    }

    printf("%s: Writing 64K slot to 0x%08X\n", __FUNCTION__, target_flash_addr);

    epResetAndRun();

    if (!swd->connect_target(0, true)) {
        printf("%s: connect_target failed\n", __FUNCTION__);
        res = -2;
        goto abort;
    }

    {
        uint32_t clr[64];
        memset(clr, 0, sizeof(clr));
        if (!swd->write_target_mem(FLASH_BUFFER_INTERFACE_ADDR, clr, sizeof(clr))) {
            res = -3; goto abort;
        }
    }

    if (!swd->write_target_mem(0x20000000, swdreflash_data, swdreflash_size)) {
        res = -4; goto abort;
    }

    if (!swd->start_target(0x20000001, 0x20042000)) {
        res = -5; goto abort;
    }

    t0 = time_us_32();
    timeout = 1000000;
    while (1) {
        if (!swd->read_target_mem(FLASH_BUFFER_INTERFACE_ADDR, (uint32_t*)&fbi_1, sizeof(fbi_1))) {
            res = -6; goto abort;
        }
        if (fbi_1.magic == MAGIC_1) break;
        if (time_us_32() - t0 > timeout) {
            printf("%s: Timeout waiting for flasher\n", __FUNCTION__);
            res = -7; goto abort;
        }
        busy_wait_us_32(10000);
    }
    printf("%s: Flasher running [%u ms]\n", __FUNCTION__, (time_us_32() - t0) / 1000);

    // --- Write BSON header (actual bytes) ---
    // SWD requires word-aligned size and address. Round up bson_hdr_size to the
    // next multiple of 4. Caller must zero-initialize their buffer so the extra
    // padding bytes are valid (zeros, which are harmless after the BSON null terminator).
    {
        uint32_t hdr_write_size = ((uint32_t)bson_hdr_size + 3) & ~3u;
        if (hdr_write_size > HEADER_SIZE) hdr_write_size = HEADER_SIZE;
        if (hdr_write_size > 0) {
            if (!swd->write_target_mem(fbi_1.bufferStartAddr,
                                       (const uint32_t*)bson_hdr,
                                       hdr_write_size)) {
                printf("%s: SWD write bson_hdr failed\n", __FUNCTION__);
                res = -8; goto abort;
            }
        }
    }

    // --- Zero-pad (or FF-fill for erase) remainder of header ---
    // Start from the word-aligned end of the header write.
    {
        uint32_t hdr_pad_start = ((uint32_t)bson_hdr_size + 3) & ~3u;
        if (hdr_pad_start > HEADER_SIZE) hdr_pad_start = HEADER_SIZE;
        const uint8_t* fill = erase ? ff_buf : zero_buf;
        for (uint32_t off = hdr_pad_start; off < HEADER_SIZE; off += SWD_CHUNK) {
            uint32_t bytes = HEADER_SIZE - off;
            if (bytes > SWD_CHUNK) bytes = SWD_CHUNK;
            if (!swd->write_target_mem(fbi_1.bufferStartAddr + off,
                                       (const uint32_t*)fill, bytes)) {
                printf("%s: SWD write header-pad failed at off %u\n", __FUNCTION__, off);
                res = -9; goto abort;
            }
        }
    }

    // --- Write image data (or zeros/FF if NULL or erase) ---
    {
        const uint8_t* fill = erase ? ff_buf : zero_buf;
        if (!erase && image_data != nullptr) {
            // Write actual image in 1KB chunks
            for (uint32_t off = 0; off < IMAGE_SIZE; off += SWD_CHUNK) {
                uint32_t bytes = IMAGE_SIZE - off;
                if (bytes > SWD_CHUNK) bytes = SWD_CHUNK;
                if (!swd->write_target_mem(fbi_1.bufferStartAddr + HEADER_SIZE + off,
                                           (const uint32_t*)(image_data + off), bytes)) {
                    printf("%s: SWD write image failed at off %u\n", __FUNCTION__, off);
                    res = -10; goto abort;
                }
            }
        } else {
            // Fill with zeros or 0xFF
            for (uint32_t off = 0; off < IMAGE_SIZE; off += SWD_CHUNK) {
                uint32_t bytes = IMAGE_SIZE - off;
                if (bytes > SWD_CHUNK) bytes = SWD_CHUNK;
                if (!swd->write_target_mem(fbi_1.bufferStartAddr + HEADER_SIZE + off,
                                           (const uint32_t*)fill, bytes)) {
                    printf("%s: SWD write image-fill failed at off %u\n", __FUNCTION__, off);
                    res = -11; goto abort;
                }
            }
        }
    }

    printf("%s: SWD data writes complete, issuing mailbox cmd\n", __FUNCTION__);

    // --- Issue flash program command ---
    mbox.cmd          = MAILBOX_CMD_PGM;
    mbox.buffer_addr  = fbi_1.bufferStartAddr;
    mbox.target_addr  = target_flash_addr;
    mbox.length       = SLOT_SIZE;
    mbox.status       = 0;
    if (!swd->write_target_mem(fbi_1.mailboxAddr, (uint32_t*)&mbox, sizeof(mbox))) {
        printf("%s: Write mailbox failed\n", __FUNCTION__);
        res = -12; goto abort;
    }
    printf("%s: Mailbox written, polling for completion...\n", __FUNCTION__);

    t0 = time_us_32();
    timeout = 10000000;
    while (1) {
        if (!swd->read_target_mem(fbi_1.mailboxAddr, (uint32_t*)&flash_status, sizeof(flash_status))) {
            printf("%s: SWD read mailbox failed\n", __FUNCTION__);
            res = -13; goto abort;
        }
        if (flash_status > MAILBOX_STATUS_BUSY) break;
        if (time_us_32() - t0 > timeout) {
            printf("%s: Timeout waiting for flash completion\n", __FUNCTION__);
            res = -14; goto abort;
        }
        busy_wait_us_32(100000);
    }

    if (flash_status == MAILBOX_STATUS_SUCCESS) {
        printf("%s: Flash slot write success!\n", __FUNCTION__);
    } else {
        printf("%s: Flash error status %d\n", __FUNCTION__, flash_status);
        res = -15;
    }

    abort:
    swd->unload();
    epResetAndRun();
    return res;
}

// eraseSlot: fill a 64KB slot with 0xFF and program it (logical delete).
int32_t FlashEp::eraseSlot(uint32_t target_flash_addr)
{
    return flashSlot(nullptr, 0, nullptr, target_flash_addr, true);
}

// ---------------------------------------------------------------------------
// readSlotBinary: read the 32KB image binary from an image-store slot via SWD.
// Connects without halting EP (non-destructive). No reset.
bool FlashEp::readSlotBinary(uint32_t slot_flash_addr, uint8_t* buf, size_t buf_size)
{
    SWDLock lock;
    const uint32_t IMAGE_OFFSET = 32768;
    const uint32_t IMAGE_SIZE   = 32768;
    const uint32_t SWD_CHUNK    = 1024;

    if (buf_size < IMAGE_SIZE) {
        printf("%s: buf_size %zu < %u\n", __FUNCTION__, buf_size, IMAGE_SIZE);
        return false;
    }

    printf("%s: Reading slot binary from 0x%08X\n", __FUNCTION__, slot_flash_addr);

    if (!swd->connect_target(0, false)) {
        printf("%s: connect_target failed\n", __FUNCTION__);
        return false;
    }

    for (uint32_t off = 0; off < IMAGE_SIZE; off += SWD_CHUNK) {
        uint32_t bytes = IMAGE_SIZE - off;
        if (bytes > SWD_CHUNK) bytes = SWD_CHUNK;
        if (!swd->read_target_mem(slot_flash_addr + IMAGE_OFFSET + off,
                                   (uint32_t*)(buf + off), bytes)) {
            printf("%s: SWD read failed at off %u\n", __FUNCTION__, off);
            swd->unload();
            return false;
        }
    }

    swd->unload();
    printf("%s: OK\n", __FUNCTION__);
    return true;
}

// ---------------------------------------------------------------------------
// Shared flasher-setup helper used by flashSlotFromFile / rewriteSlotHeader.
// Resets EP, halts it via SWD, loads the flasher binary, and waits for it to
// signal readiness by writing MAGIC_1 into fbi_1.
// Returns 0 on success, negative error code on failure (same codes as flashSlot).
int32_t FlashEp::start_flasher()
{
    uint32_t t0, timeout;

    epResetAndRun();

    if (!swd->connect_target(0, true)) return -2;

    {
        uint32_t clr[64];
        memset(clr, 0, sizeof(clr));
        if (!swd->write_target_mem(FLASH_BUFFER_INTERFACE_ADDR, clr, sizeof(clr)))
            return -3;
    }

    if (!swd->write_target_mem(0x20000000, swdreflash_data, swdreflash_size))
        return -4;

    if (!swd->start_target(0x20000001, 0x20042000))
        return -5;

    t0 = time_us_32();
    timeout = 1000000;
    while (1) {
        if (!swd->read_target_mem(FLASH_BUFFER_INTERFACE_ADDR,
                                   (uint32_t*)&fbi_1, sizeof(fbi_1)))
            return -6;
        if (fbi_1.magic == MAGIC_1) break;
        if (time_us_32() - t0 > timeout) return -7;
        busy_wait_us_32(10000);
    }
    printf("EP flasher running [%u ms]\n", (time_us_32() - t0) / 1000);
    return 0;
}

// ---------------------------------------------------------------------------
// flashSlotFromFile: flash a 64KB image-store slot from an LFS file.
// - Starts the EP flasher.
// - Reads img_path in 1KB chunks → writes to EP SRAM image half, computing
//   murmur3 in parallel (single file pass).
// - Builds BSON header on stack (using computed hash).
// - Writes BSON header + zero padding to EP SRAM header half.
// - Issues mailbox flash command, waits, resets EP.
// - Always deletes img_path from LFS (cleanup on both success and failure).

int32_t FlashEp::flashSlotFromFile(lfs_t* lfs_ptr, const char* img_path,
                                    const char* name, const char* description,
                                    const char* protection,
                                    uint32_t target_flash_addr)
{
    SWDLock lock;
    const uint32_t HEADER_SIZE = 32768;
    const uint32_t IMAGE_SIZE  = 32768;
    const uint32_t SLOT_SIZE   = 65536;
    const uint32_t SWD_CHUNK   = 1024;

    int32_t  res = 0;
    mailbox_t mbox;
    int32_t  flash_status;
    uint32_t t0, timeout;

    printf("%s: img='%s' addr=0x%08X\n", __FUNCTION__, img_path, target_flash_addr);

    res = start_flasher();
    if (res != 0) {
        printf("%s: start_flasher failed: %d\n", __FUNCTION__, res);
        goto abort;
    }

    // --- Read image from file in 1KB chunks, write to EP SRAM image half,
    //     computing murmur3 incrementally ---
    {
        lfs_file_t f;
        int err = lfs_file_open(lfs_ptr, &f, img_path, LFS_O_RDONLY);
        if (err < 0) {
            printf("%s: lfs_file_open(%s) failed: %d\n", __FUNCTION__, img_path, err);
            res = -8; goto abort;
        }

        murmur3_state_t m3;
        murmur3_begin(&m3, ~0x0);

        for (uint32_t off = 0; off < IMAGE_SIZE; off += SWD_CHUNK) {
            uint32_t to_read = IMAGE_SIZE - off;
            if (to_read > SWD_CHUNK) to_read = SWD_CHUNK;

            lfs_ssize_t n = lfs_file_read(lfs_ptr, &f, s_chunk_buf, to_read);
            if (n != (lfs_ssize_t)to_read) {
                printf("%s: lfs_file_read failed at off=%u (got %d)\n",
                       __FUNCTION__, off, (int)n);
                lfs_file_close(lfs_ptr, &f);
                res = -9; goto abort;
            }
            murmur3_update(&m3, s_chunk_buf, to_read);
            if (!swd->write_target_mem(fbi_1.bufferStartAddr + HEADER_SIZE + off,
                                       (const uint32_t*)s_chunk_buf, to_read)) {
                printf("%s: SWD write image failed at off=%u\n", __FUNCTION__, off);
                lfs_file_close(lfs_ptr, &f);
                res = -10; goto abort;
            }
        }
        lfs_file_close(lfs_ptr, &f);

        uint32_t image_m3 = murmur3_finish(&m3);
        printf("%s: image m3=0x%08X\n", __FUNCTION__, image_m3);

        // Build BSON header on stack
        uint8_t bson_buf[256];
        memset(bson_buf, 0, sizeof(bson_buf));
        size_t hdr_size = feb_build_slot_bson(bson_buf, sizeof(bson_buf),
                                               name, description, image_m3, protection);
        if (hdr_size == 0) {
            printf("%s: BSON header overflow\n", __FUNCTION__);
            res = -11; goto abort;
        }

        // Write BSON header to EP SRAM[0..hdr_write_size], zero-fill the rest
        uint32_t hdr_write_size = ((uint32_t)hdr_size + 3) & ~3u;
        if (!swd->write_target_mem(fbi_1.bufferStartAddr,
                                   (const uint32_t*)bson_buf, hdr_write_size)) {
            res = -12; goto abort;
        }
        memset(s_chunk_buf, 0, SWD_CHUNK);
        for (uint32_t pad = hdr_write_size; pad < HEADER_SIZE; pad += SWD_CHUNK) {
            uint32_t bytes = HEADER_SIZE - pad;
            if (bytes > SWD_CHUNK) bytes = SWD_CHUNK;
            if (!swd->write_target_mem(fbi_1.bufferStartAddr + pad,
                                       (const uint32_t*)s_chunk_buf, bytes)) {
                res = -13; goto abort;
            }
        }
    }

    // --- Issue flash command ---
    printf("%s: issuing mailbox cmd\n", __FUNCTION__);
    mbox.cmd         = MAILBOX_CMD_PGM;
    mbox.buffer_addr = fbi_1.bufferStartAddr;
    mbox.target_addr = target_flash_addr;
    mbox.length      = SLOT_SIZE;
    mbox.status      = 0;
    if (!swd->write_target_mem(fbi_1.mailboxAddr, (uint32_t*)&mbox, sizeof(mbox))) {
        res = -14; goto abort;
    }

    t0 = time_us_32(); timeout = 10000000;
    while (1) {
        if (!swd->read_target_mem(fbi_1.mailboxAddr,
                                   (uint32_t*)&flash_status, sizeof(flash_status))) {
            res = -15; goto abort;
        }
        if (flash_status > MAILBOX_STATUS_BUSY) break;
        if (time_us_32() - t0 > timeout) {
            printf("%s: flash timeout\n", __FUNCTION__); res = -16; goto abort;
        }
        busy_wait_us_32(100000);
    }
    if (flash_status == MAILBOX_STATUS_SUCCESS) {
        printf("%s: success!\n", __FUNCTION__);
    } else {
        printf("%s: flash error %d\n", __FUNCTION__, flash_status);
        res = -17;
    }

abort:
    swd->unload();
    epResetAndRun();
    // Always clean up temp file regardless of success/failure
    lfs_remove(lfs_ptr, img_path);
    return res;
}

// ---------------------------------------------------------------------------
// bson_find_int32: scan a BSON document for an int32 field by name.
// Returns true and writes value to *out if found. No bounds checking beyond
// doc_len — caller must ensure buf is at least doc_len bytes.
static bool bson_find_int32(const uint8_t* buf, uint32_t doc_len,
                             const char* key, int32_t* out)
{
    if (doc_len < 5) return false;
    uint32_t pos = 4;  // skip document length field
    while (pos + 1 < doc_len) {
        uint8_t type = buf[pos++];
        if (type == 0x00) break;  // end of document
        // read key (c-string)
        uint32_t key_start = pos;
        while (pos < doc_len && buf[pos] != 0) pos++;
        if (pos >= doc_len) break;
        pos++;  // skip null terminator
        if (type == 0x10) {  // int32
            if (pos + 4 > doc_len) break;
            int32_t v = (int32_t)( (uint32_t)buf[pos]
                                 | ((uint32_t)buf[pos+1] << 8)
                                 | ((uint32_t)buf[pos+2] << 16)
                                 | ((uint32_t)buf[pos+3] << 24) );
            if (strcmp((const char*)(buf + key_start), key) == 0) {
                *out = v;
                return true;
            }
            pos += 4;
        } else if (type == 0x02) {  // UTF-8 string
            if (pos + 4 > doc_len) break;
            uint32_t slen = (uint32_t)buf[pos] | ((uint32_t)buf[pos+1] << 8)
                          | ((uint32_t)buf[pos+2] << 16) | ((uint32_t)buf[pos+3] << 24);
            pos += 4 + slen;
        } else {
            break;  // unknown type; stop
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// rewriteSlotHeader: overwrite the BSON header of an existing image-store slot
// while preserving the existing 32KB image binary (untouched in flash).
//
// The slot is two independent 32KB flash regions. Only the header half is
// erased and reprogrammed; the image half is left intact.
//
// The existing image_m3 is read from the current BSON header (a small SWD
// read while EP is running normally, before the flasher starts). This avoids
// reading 32KB of image binary entirely.
//
// Sequence:
// 1. Connect to EP via SWD without halting (XIP valid). Read 512 bytes from
//    slot start. Extract image_m3 from existing BSON. Disconnect.
// 2. Build new BSON header with preserved m3 + updated metadata.
// 3. start_flasher() — resets EP, loads flasher, SPI enters command mode.
// 4. Write new BSON header + zero padding to EP SRAM (32KB).
// 5. Issue mailbox cmd with length=32768 (header half only). Wait.

int32_t FlashEp::rewriteSlotHeader(uint32_t slot_flash_addr,
                                    const char* name, const char* description,
                                    const char* protection)
{
    SWDLock lock;
    const uint32_t HEADER_SIZE  = 32768;
    const uint32_t SWD_CHUNK    = 1024;

    int32_t res = 0;
    mailbox_t mbox;
    int32_t flash_status;
    uint32_t t0, timeout;
    uint8_t bson_buf[512];
    int32_t existing_m3 = 0;
    size_t hdr_size;

    printf("%s: slot=0x%08X name='%s'\n", __FUNCTION__, slot_flash_addr, name);

    // --- Step 1: Read existing BSON header to extract image_m3 ---
    // Connect without halting; EP is running normally, XIP is fully operational.
    if (!swd->connect_target(0, false)) {
        printf("%s: connect_target (pre-read) failed\n", __FUNCTION__);
        return -1;
    }
    memset(bson_buf, 0, sizeof(bson_buf));
    if (!swd->read_target_mem(slot_flash_addr, (uint32_t*)bson_buf, sizeof(bson_buf))) {
        printf("%s: SWD read existing header failed\n", __FUNCTION__);
        swd->unload();
        return -2;
    }
    swd->unload();

    bool need_m3_from_flasher = false;
    if (!bson_find_int32(bson_buf, sizeof(bson_buf), "image_m3", &existing_m3)) {
        // No m3 in existing header (old format). Will ask the flasher to compute it.
        printf("%s: image_m3 not in header, will use MAILBOX_CMD_M3\n", __FUNCTION__);
        need_m3_from_flasher = true;
    } else {
        printf("%s: existing image_m3=0x%08X\n", __FUNCTION__, (uint32_t)existing_m3);
    }

    // --- Step 2: Build new BSON header with preserved m3 ---
    memset(bson_buf, 0, sizeof(bson_buf));
    hdr_size = feb_build_slot_bson(bson_buf, sizeof(bson_buf),
                                    name, description, (uint32_t)existing_m3, protection);
    if (hdr_size == 0) {
        printf("%s: BSON header overflow\n", __FUNCTION__);
        return -4;
    }

    // --- Step 3: Start flasher (EP resets; SPI bus enters command mode) ---
    res = start_flasher();
    if (res != 0) {
        printf("%s: start_flasher failed: %d\n", __FUNCTION__, res);
        goto abort;
    }

    // --- Step 3b: If m3 wasn't in the existing header, ask the flasher to compute it ---
    if (need_m3_from_flasher) {
        const uint32_t IMAGE_OFFSET = 32768;
        const uint32_t IMAGE_SIZE   = 32768;
        mailbox_t m3_mbox;
        m3_mbox.cmd         = MAILBOX_CMD_M3;
        m3_mbox.target_addr = slot_flash_addr + IMAGE_OFFSET;
        m3_mbox.length      = IMAGE_SIZE;
        m3_mbox.buffer_addr = 0;
        m3_mbox.status      = 0;
        if (!swd->write_target_mem(fbi_1.mailboxAddr, (uint32_t*)&m3_mbox, sizeof(m3_mbox))) {
            printf("%s: write M3 mailbox failed\n", __FUNCTION__);
            res = -8; goto abort;
        }
        uint32_t t0m = time_us_32();
        while (1) {
            if (!swd->read_target_mem(fbi_1.mailboxAddr, (uint32_t*)&m3_mbox, sizeof(m3_mbox))) {
                res = -9; goto abort;
            }
            if (m3_mbox.status > MAILBOX_STATUS_BUSY) break;
            if (time_us_32() - t0m > 5000000) {
                printf("%s: M3 command timeout\n", __FUNCTION__); res = -10; goto abort;
            }
            busy_wait_us_32(10000);
        }
        if (m3_mbox.status != MAILBOX_STATUS_SUCCESS) {
            printf("%s: M3 command failed: %d\n", __FUNCTION__, (int)m3_mbox.status);
            res = -11; goto abort;
        }
        existing_m3 = (int32_t)m3_mbox.buffer_addr;
        printf("%s: computed image_m3=0x%08X\n", __FUNCTION__, (uint32_t)existing_m3);

        // Rebuild BSON header with freshly computed m3
        memset(bson_buf, 0, sizeof(bson_buf));
        hdr_size = feb_build_slot_bson(bson_buf, sizeof(bson_buf),
                                        name, description, (uint32_t)existing_m3, protection);
        if (hdr_size == 0) {
            printf("%s: BSON header overflow (rebuild)\n", __FUNCTION__);
            res = -12; goto abort;
        }
    }

    // --- Step 4: Write BSON header to EP SRAM, zero-fill remainder of 32KB ---
    {
        uint32_t hdr_write_size = ((uint32_t)hdr_size + 3) & ~3u;
        if (!swd->write_target_mem(fbi_1.bufferStartAddr,
                                   (const uint32_t*)bson_buf, hdr_write_size)) {
            res = -8; goto abort;
        }
        memset(s_chunk_buf, 0, SWD_CHUNK);
        for (uint32_t pad = hdr_write_size; pad < HEADER_SIZE; pad += SWD_CHUNK) {
            uint32_t bytes = HEADER_SIZE - pad;
            if (bytes > SWD_CHUNK) bytes = SWD_CHUNK;
            if (!swd->write_target_mem(fbi_1.bufferStartAddr + pad,
                                       (const uint32_t*)s_chunk_buf, bytes)) {
                res = -9; goto abort;
            }
        }
    }

    // --- Step 5: Issue flash command for header half only ---
    printf("%s: issuing mailbox cmd (header half only)\n", __FUNCTION__);
    mbox.cmd         = MAILBOX_CMD_PGM;
    mbox.buffer_addr = fbi_1.bufferStartAddr;
    mbox.target_addr = slot_flash_addr;
    mbox.length      = HEADER_SIZE;     // 32KB — header only, image untouched
    mbox.status      = 0;
    if (!swd->write_target_mem(fbi_1.mailboxAddr, (uint32_t*)&mbox, sizeof(mbox))) {
        res = -10; goto abort;
    }

    t0 = time_us_32(); timeout = 10000000;
    while (1) {
        if (!swd->read_target_mem(fbi_1.mailboxAddr,
                                   (uint32_t*)&flash_status, sizeof(flash_status))) {
            res = -11; goto abort;
        }
        if (flash_status > MAILBOX_STATUS_BUSY) break;
        if (time_us_32() - t0 > timeout) {
            printf("%s: flash timeout\n", __FUNCTION__); res = -12; goto abort;
        }
        busy_wait_us_32(100000);
    }
    if (flash_status == MAILBOX_STATUS_SUCCESS) {
        printf("%s: success!\n", __FUNCTION__);
    } else {
        printf("%s: flash error %d\n", __FUNCTION__, flash_status);
        res = -13;
    }

abort:
    swd->unload();
    epResetAndRun();
    return res;
}

// ---------------------------------------------------------------------------
// C-compatible wrapper for use by api_handlers.c
extern "C" int32_t flash_ep_uf2(const char* pathname, bool verbose) {
    return FlashEp::flashUf2(pathname, verbose);
}