#ifndef FLASHEP_H
#define FLASHEP_H

#include <stdint.h>

#include "SWDDriver.h"
#include "kc1fsz-tools/SWDUtils.h"
#include "lfs.h"

/**
 * Makes a 16-bit RP2040 ROM table code from the two 
 * ASCII characters. 
 */
static uint16_t rom_table_code(char c1, char c2) {
    return (c2 << 8) | c1;
}      

// Region tracking for two-phase UF2 flashing
struct FlashRegion {
    uint32_t start_addr;      // Flash offset (without 0x10000000 base)
    uint32_t length_bytes;    // Total length in bytes
    bool valid;               // True if this entry is used
};

class FlashEp
{
    public:
        FlashEp(int32_t swd_clk_pin, int32_t swd_dat_pin, int32_t ep_run_pin);

        int32_t takeControl();
        void releaseControl();

        int32_t readBlk(uint32_t startAddr, uint32_t* readBuffer);
        int32_t writeBlk(uint32_t targetStartAddr, uint32_t* writeBuffer);
        int32_t writeChunk(uint32_t flash_offset, uint32_t* writeBuffer, uint32_t length_bytes);

        int32_t flashUF2(const char* fileName, bool skipProgramming=false);

    private:
        int32_t swd_clk_pin;
        int32_t swd_dat_pin;
        int32_t ep_run_pin;

        kc1fsz::SWDDriver* swd;

        bool function_table_inited;
        int32_t init_function_table();

        // ROM function table addresses
        uint16_t rom_connect_internal_flash_func;
        uint16_t rom_flash_exit_xip_func;
        uint16_t rom_flash_range_erase_func;
        uint16_t rom_flash_range_program_func;
        uint16_t rom_flash_flush_cache_func;
        uint16_t rom_flash_enter_cmd_xip_func;
        uint16_t rom_debug_trampoline_func;
        uint16_t rom_debug_trampoline_end_func;

        // UF2 region tracking for two-phase flashing
        static const uint32_t MAX_UF2_REGIONS = 32;
        FlashRegion flash_regions[MAX_UF2_REGIONS];
        uint32_t num_regions;

        // Helper methods for two-phase UF2 flashing
        int32_t scanUF2Regions(lfs_file_t* file);
        bool addRegion(uint32_t start_addr, uint32_t length);
        int32_t validateRegions();
        int32_t eraseAllRegions();
        int32_t programAllBlocks(lfs_file_t* file);
        int32_t performFlashOperation(lfs_file_t* file);

        // Constants for flash operations
        static const uint32_t page_size_bytes = 4096;
        static const uint32_t page_size_words = page_size_bytes / 4;

        // Flash programming chunk size (must be multiple of 256)
        static const uint32_t PROGRAM_CHUNK_SIZE = 16384;

        // Declare the chunk_buffer in the class (as opposed to on the stack)
        // so that its large size doesn't cause stack issues.
        uint8_t chunk_buffer[PROGRAM_CHUNK_SIZE];
};

#endif
