#ifndef FLASH_EP_H
#define FLASH_EP_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus

#include "lfs.h"
#include "FlashBuffer.h"

class FlashEp {
public:
    static int32_t flashUf2(const char* args, bool verbose=false);

    /**
     * Flash a complete 64KB image-store slot to EP flash.
     * The slot layout is: [32KB BSON header, zero-padded][32KB raw image binary].
     * bson_hdr / bson_hdr_size: the actual BSON header bytes; rest is zero-padded.
     * image_data: 32768-byte raw EPROM binary, or NULL to write zeros (e.g. selector slot).
     * target_flash_addr: EP flash address, must be 65536-byte aligned (e.g. 0x10200000 + N*65536).
     * Halts EP core0 via SWD, writes slot, then reboots EP (same as flashUf2).
     * Returns 0 on success, negative on error.
     */
    static int32_t flashSlot(const uint8_t* bson_hdr, size_t bson_hdr_size,
                             const uint8_t* image_data, uint32_t target_flash_addr,
                             bool erase = false);

    /**
     * Erase a 64KB image-store slot by filling it with 0xFF and programming it.
     * target_flash_addr: EP flash address, must be 65536-byte aligned.
     * Returns 0 on success, negative on error.
     */
    static int32_t eraseSlot(uint32_t target_flash_addr);

    /**
     * Read the 32KB image binary from an image-store slot via SWD.
     * Does not halt or reset the EP (non-destructive read).
     * slot_flash_addr: EP flash address of the slot (65536-byte aligned).
     * buf: output buffer, must be at least 32768 bytes.
     * Returns true on success.
     */
    static bool readSlotBinary(uint32_t slot_flash_addr, uint8_t* buf, size_t buf_size);

    /**
     * Flash a 64KB image-store slot where the image comes from an LFS file.
     * Reads img_path in 1KB chunks, computing murmur3 incrementally, then
     * builds the BSON header internally.  Uses a 1KB static buffer — no heap.
     * Deletes img_path from LFS after programming (success or failure).
     * Resets EP after programming (same as flashSlot).
     * Returns 0 on success, negative on error.
     */
    static int32_t flashSlotFromFile(lfs_t* lfs_ptr, const char* img_path,
                                     const char* name, const char* description,
                                     const char* protection,
                                     uint32_t target_flash_addr);

    /**
     * Rewrite only the BSON header of an existing image-store slot.
     * Reads the existing 32KB image from EP flash via SWD in 1KB chunks,
     * computes murmur3 incrementally, then re-flashes the full 64KB slot with
     * the new header + original image.  Uses a 1KB static buffer — no heap.
     * Resets EP after programming.
     * Returns 0 on success, negative on error.
     */
    static int32_t rewriteSlotHeader(uint32_t slot_flash_addr,
                                     const char* name, const char* description,
                                     const char* protection);

private:
    static bool handle_metablock(uint32_t start_addr, uint8_t *buffer, size_t size, bool verbose=false);
    static int32_t process_uf2(lfs_t *lfs, const char *path, bool verbose=false);
    static int32_t start_flasher();

    static flashBufferInterface_1_t fbi_1;
};

extern "C" {
#endif

/**
 * C-compatible wrapper for FlashEp::flashUf2().
 * @param pathname Path to UF2 file on SD card (e.g., "/EP.uf2")
 * @param verbose Enable verbose output
 * @return 0 on success, negative error code on failure:
 *         -1:  Unable to connect to EP via SWD
 *         -2:  Unable to clear FBI struct in EP RAM
 *         -3:  Unable to load flasher program to EP RAM
 *         -4:  Unable to start flasher program on EP
 *         -5:  Unable to read flashBufferInterface from EP
 *         -6:  Timeout waiting for flasher program to start
 *         -10: Malformed UF2 block
 *         -11: Metablock flash failed
 *         -12: Final metablock flash failed
 */
int32_t flash_ep_uf2(const char* pathname, bool verbose);

#ifdef __cplusplus
}
#endif

#endif // FLASH_EP_H
