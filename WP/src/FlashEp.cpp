#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "hardware/gpio.h"

#include "FlashEp.h"
#include "swdreflash_binary.h"
#include "SWDLoader.h"

#include "lfs.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define UF2_MAGIC_START0 0x0A324655
#define UF2_MAGIC_START1 0x9E5D5157
#define UF2_MAGIC_END    0x0AB16F30
#define METABLOCK_SIZE   (64 * 1024)
#define UF2_BLOCK_SIZE   512

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
bool FlashEp::handle_metablock(uint32_t start_addr, uint8_t *buffer, size_t size)
{
    printf("%s: Metablock ready for target [0x%08X..0x%08X], size %zu\n", __FUNCTION__, start_addr, start_addr+size-1, size);
    if (size == 0) {
        return true;
    }
    if (size % 4096 != 0) {
        printf("%s: Metablock size %zu is not multiple of 4096\n", __FUNCTION__,  size);
        return false;
    }
    bool ok = swdLoader->load_ram(0x20010000, (uint32_t*)buffer, size);
    return ok;
}

int32_t FlashEp::process_uf2(lfs_t *lfs, const char *path)
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
    // 1. Stub: Open the file
    int err = lfs_file_open(lfs, &file, path, LFS_O_RDONLY);
    if (err < 0) return err;

    // 2. Loop to process the file
    // 3. Stub: Read a UF2 512-byte block
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
                return -1;
        }

        // Logic: Check for non-contiguous address or full metablock
        bool is_discontinuous = (!first_block && block.targetAddr != next_expected_addr);
        bool is_buffer_full = (metablock_offset + block.payloadSize > METABLOCK_SIZE);

        if (is_discontinuous || is_buffer_full) {
            // 7. Stub: Deal with the 64K block
            if (!handle_metablock(metablock_start_addr, metablock_buffer, metablock_offset)) {
                lfs_file_close(lfs, &file);
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
        if (!handle_metablock(metablock_start_addr, metablock_buffer, metablock_offset)) {
            lfs_file_close(lfs, &file);
            return -3;
        }
    }

    lfs_file_close(lfs, &file);
    return 0;
}