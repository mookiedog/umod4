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


private:
    static bool handle_metablock(uint32_t start_addr, uint8_t *buffer, size_t size, bool verbose=false);
    static int32_t process_uf2(lfs_t *lfs, const char *path, bool verbose=false);

    static flashBufferInterface_1_t fbi_1;
};

extern "C" {
#endif

/**
 * C-compatible wrapper for FlashEp::flashUf2().
 * @param pathname Path to UF2 file on SD card (e.g., "/EP.uf2")
 * @param verbose Enable verbose output
 * @return 0 on success, negative error code on failure:
 *         -1: Unable to connect to EP via SWD
 *         -2: Unable to clear FBI struct in EP RAM
 *         -3: Unable to load flasher program to EP RAM
 *         -4: Unable to start flasher program on EP
 *         -5: Unable to read flashBufferInterface from EP
 *         -6: Timeout waiting for flasher program to start
 *         Other negatives: UF2 processing/flashing errors
 */
int32_t flash_ep_uf2(const char* pathname, bool verbose);

#ifdef __cplusplus
}
#endif

#endif // FLASH_EP_H
