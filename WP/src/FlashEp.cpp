#include "FlashEp.h"
#include "pico/stdlib.h"
#include "pico/mutex.h"
#include "stdio.h"
#include "lfs.h"

// UF2 format constants
#define UF2_MAGIC_START0   0x0A324655
#define UF2_MAGIC_START1   0x9E5D5157
#define UF2_MAGIC_END      0x0AB16F30
#define UF2_BLOCK_SIZE     512
#define UF2_PAYLOAD_SIZE   256

// Access to global LittleFS instance from main.cpp
extern lfs_t lfs;
extern mutex_t lfs_mutex;

static const bool dbg = true;

// The RAM address in the EP where we will be buffering pages as they
// either get flashed or get read from the flash.
// We will use SRAM0_BASE offset by 64K:
static const unsigned int target_pagebuffer_ram = 0x20010000;


// --------------------------------------------------------------------------------
FlashEp::FlashEp(int32_t _swd_clk_pin, int32_t _swd_dat_pin, int32_t _ep_run_pin)
{
    swd = new kc1fsz::SWDDriver(_swd_clk_pin, _swd_dat_pin);

    swd_clk_pin = _swd_clk_pin;
    swd_dat_pin = _swd_dat_pin;
    ep_run_pin = _ep_run_pin;

    // Register the GPIOs we will be using to reflash the EP via its SWD unit
    // but don't activate the debug unit until we are ready to flash the EP.
    gpio_init(swd_clk_pin);
    gpio_init(swd_dat_pin);
    swd->init();

    function_table_inited = false;

    // Initialize region tracking
    num_regions = 0;
    memset(flash_regions, 0, sizeof(flash_regions));
}


// --------------------------------------------------------------------------------
// This call takes control of the EP in preparation of reflashing it.
// It will:
//      - RESET the EP to get it into a known state
//      - Activate the EP's debug unit
//          - Stop both cores from running
//          - Use the debug unit to assert HC11 RESET signal to stop the ECU
int32_t FlashEp::takeControl()
{
    gpio_init(swd_clk_pin);
    gpio_init(swd_dat_pin);
    swd->init();

    int32_t result = swd->connect();
    if (result != 0) {
        if (dbg) printf("swd->connect() failed: err %d\n", result);
        return result;
    }

    result = kc1fsz::reset_into_debug(*swd);
    if (result != 0) {
        if (dbg) printf("reset_into_debug() failed: err %d\n", result);
        return result;
    }

    result = init_function_table();

    return result;
}

// --------------------------------------------------------------------------------
// This call releases control of the EP by resetting it.
// As the EP boots, it will release the HC11 RESET signal.
void FlashEp::releaseControl()
{
    if (ep_run_pin >= 0) {
        // We have the ability to assert the EP's RESET (RUN) pin
        gpio_init(ep_run_pin);
        gpio_set_dir(ep_run_pin, GPIO_OUT);
        gpio_put(ep_run_pin, 0);
        sleep_us(100);
        gpio_put(ep_run_pin, 1);
    }
    else {
        // Use the debug unit to generate a reset inside the EP
        // Todo: this does not seem to reset the EP though...
        int32_t result = kc1fsz::reset(*swd);
    }
}

// --------------------------------------------------------------------------------
int32_t FlashEp::init_function_table()
{
    // Move VTOR to SRAM in preparation
    if (const auto r = swd->writeWordViaAP(0xe000ed08, 0x20000000); r != 0)
        return -12;

    // ----- Get the ROM function locations -----------------------------------

    // Observation from an RP2040: table pointer is a 0x7a. This is interesting
    // because it is not a multiple of 4, so we need to be careful when searching
    // the lookup table - use half-word accesses.

    // Get the start of the ROM function table
    uint16_t tab_ptr;
    if (const auto r = swd->readHalfWordViaAP(0x00000014); !r.has_value()) {
        return -3;
    } else {
        tab_ptr = *r;
    }

    // Initialize ROM function table addresses to 0
    rom_connect_internal_flash_func = 0;
    rom_flash_exit_xip_func = 0;
    rom_flash_range_erase_func = 0;
    rom_flash_range_program_func = 0;
    rom_flash_flush_cache_func = 0;
    rom_flash_enter_cmd_xip_func = 0;
    rom_debug_trampoline_func = 0;
    rom_debug_trampoline_end_func = 0;

    // Iterate through the table until we find a null function code
    // Each entry word is two 8-bit codes and a 16-bit
    // address.

    while (true) {

        uint16_t func_code;
        if (const auto r = swd->readHalfWordViaAP(tab_ptr); !r.has_value()) {
            return -4;
        } else {
            func_code = *r;
        }

        uint16_t func_addr;
        if (const auto r = swd->readHalfWordViaAP(tab_ptr + 2); !r.has_value()) {
            return -5;
        } else {
            func_addr = *r;
        }

        if (func_code == 0)
            break;

        if (func_code == rom_table_code('I', 'F'))
            rom_connect_internal_flash_func = func_addr;
        else if (func_code == rom_table_code('E', 'X'))
            rom_flash_exit_xip_func = func_addr;
        else if (func_code == rom_table_code('R', 'E'))
            rom_flash_range_erase_func = func_addr;
        else if (func_code == rom_table_code('R', 'P'))
            rom_flash_range_program_func = func_addr;
        else if (func_code == rom_table_code('F', 'C'))
            rom_flash_flush_cache_func = func_addr;
        else if (func_code == rom_table_code('C', 'X'))
            rom_flash_enter_cmd_xip_func = func_addr;
        else if (func_code == rom_table_code('D', 'T'))
            rom_debug_trampoline_func = func_addr;
        else if (func_code == rom_table_code('D', 'E'))
            rom_debug_trampoline_end_func = func_addr;

        tab_ptr += 4;
    }

    if (!rom_connect_internal_flash_func) {
        return -10;
    }
    if (!rom_flash_exit_xip_func) {
        return -11;
    }
    if (!rom_flash_range_erase_func) {
        return -12;
    }
    if (!rom_flash_range_program_func) {
        return -13;
    }
    if (!rom_flash_flush_cache_func) {
        return -14;
    }
    if (!rom_flash_enter_cmd_xip_func) {
        return -15;
    }
    if (!rom_debug_trampoline_func) {
        return -16;
    }
    if (!rom_debug_trampoline_end_func) {
        return -17;
    }
    return 0;
}


#if 0
// --------------------------------------------------------------------------------
int32_t FlashEp::readBlk(uint32_t flashStartAddr, uint32_t* readBuffer)
{
    // Get back into normal flash reading mode
    if (const auto r = kc1fsz::call_function(*swd, rom_debug_trampoline_func, rom_flash_flush_cache_func, 0, 0, 0, 0); !r.has_value()) {
        return -20;
    }

    if (const auto r = kc1fsz::call_function(*swd, rom_debug_trampoline_func, rom_flash_enter_cmd_xip_func, 0, 0, 0, 0); !r.has_value()) {
        return -21;
    }

    sleep_us(7);

    // Read each word from the debug target's flash into the readBuffer on this processor
    for (unsigned int i = 0; i < page_size_bytes / 4; i++) {
        const auto r = swd->readWordViaAP(flashStartAddr + i);
        if (!r.has_value()) {
            return -22;
        } else {
            *readBuffer++ = *r;
        }
    }

    return 0;
}

#else
int32_t FlashEp::readBlk(uint32_t flash_addr, uint32_t* readBuffer)
{
    const uint32_t BLOCK_SIZE_WORDS = 1024;  // 1K words = 4KB

    // First, ensure flash is in XIP (execute-in-place) mode so we can read it
    // Flush the cache to ensure we're reading current flash contents
    if (const auto r = kc1fsz::call_function(*swd, rom_debug_trampoline_func,
        rom_flash_flush_cache_func, 0, 0, 0, 0); !r.has_value())
        return -1;

    // Enter XIP mode for normal flash reading
    if (const auto r = kc1fsz::call_function(*swd, rom_debug_trampoline_func,
        rom_flash_enter_cmd_xip_func, 0, 0, 0, 0); !r.has_value())
        return -2;

    // Read each word from the target's flash memory into our local buffer
    for (uint32_t i = 0; i < BLOCK_SIZE_WORDS; i++) {
        // Read one 32-bit word from the target processor's flash via SWD
        if (const auto r = swd->readWordViaAP(flash_addr); !r.has_value()) {
            return -3;
        } else {
            // Store the read word in our local buffer
            readBuffer[i] = *r;
        }

        // Move to the next word (4 bytes)
        flash_addr += 4;
    }

    return 0;  // Success
}
#endif

#if 0
// --------------------------------------------------------------------------------
// Writes a 4KB block of data to the target EP's flash memory
// targetStartAddr: Flash offset (not including 0x10000000 base)
// writeBuffer: Pointer to 1024 words (4KB) of data to write
int32_t FlashEp::writeBlk(uint32_t targetStartAddr, uint32_t* writeBuffer)
{
    if (dbg) printf("%s: Writing 4KB block to flash offset 0x%08X\n", __FUNCTION__, targetStartAddr);

    // Step 1: Prepare flash for programming
    // Connect to internal flash
    if (const auto r = kc1fsz::call_function(*swd, rom_debug_trampoline_func,
        rom_connect_internal_flash_func, 0, 0, 0, 0); !r.has_value()) {
        if (dbg) printf("%s: rom_connect_internal_flash_func failed\n", __FUNCTION__);
        return -30;
    }

    // Exit XIP mode (required before programming)
    if (const auto r = kc1fsz::call_function(*swd, rom_debug_trampoline_func,
        rom_flash_exit_xip_func, 0, 0, 0, 0); !r.has_value()) {
        if (dbg) printf("%s: rom_flash_exit_xip_func failed\n", __FUNCTION__);
        return -31;
    }

    // Step 2: Erase the target flash sector (4KB)
    // Parameters: offset, length, block_size, block_cmd
    // Use 4KB erase (block_size=4096, cmd=0x20 for sector erase)
    if (const auto r = kc1fsz::call_function(*swd, rom_debug_trampoline_func,
        rom_flash_range_erase_func, targetStartAddr, page_size_bytes, 4096, 0x20); !r.has_value()) {
        if (dbg) printf("%s: rom_flash_range_erase_func failed\n", __FUNCTION__);
        return -32;
    }

    // Flush cache after erase
    if (const auto r = kc1fsz::call_function(*swd, rom_debug_trampoline_func,
        rom_flash_flush_cache_func, 0, 0, 0, 0); !r.has_value()) {
        if (dbg) printf("%s: rom_flash_flush_cache_func (after erase) failed\n", __FUNCTION__);
        return -33;
    }

    // Step 3: Write data to EP's RAM buffer
    if (const auto r = swd->writeMultiWordViaAP(target_pagebuffer_ram, writeBuffer, page_size_words); r != 0) {
        if (dbg) printf("%s: writeMultiWordViaAP failed: %d\n", __FUNCTION__, r);
        return -34;
    }

    // Step 4: Program flash from RAM buffer
    // Parameters: flash_offset, ram_address, count, 0
    if (const auto r = kc1fsz::call_function(*swd, rom_debug_trampoline_func,
        rom_flash_range_program_func, targetStartAddr, target_pagebuffer_ram, page_size_bytes, 0); !r.has_value()) {
        if (dbg) printf("%s: rom_flash_range_program_func failed\n", __FUNCTION__);
        return -35;
    }

    // Step 5: Flush cache and return to XIP mode
    if (const auto r = kc1fsz::call_function(*swd, rom_debug_trampoline_func,
        rom_flash_flush_cache_func, 0, 0, 0, 0); !r.has_value()) {
        if (dbg) printf("%s: rom_flash_flush_cache_func (after program) failed\n", __FUNCTION__);
        return -36;
    }

    if (const auto r = kc1fsz::call_function(*swd, rom_debug_trampoline_func,
        rom_flash_enter_cmd_xip_func, 0, 0, 0, 0); !r.has_value()) {
        if (dbg) printf("%s: rom_flash_enter_cmd_xip_func failed\n", __FUNCTION__);
        return -37;
    }

    if (dbg) printf("%s: Successfully wrote 4KB block\n", __FUNCTION__);
    return 0;  // Success
}
#endif

// --------------------------------------------------------------------------------
// Writes a variable-length block to flash (assumes flash is already erased)
// flash_offset: Flash offset (not including 0x10000000 base)
// writeBuffer: Pointer to data to write
// length_bytes: Number of bytes to write (must be multiple of 256, ≤ PROGRAM_CHUNK_SIZE)
int32_t FlashEp::writeChunk(uint32_t flash_offset, uint32_t* writeBuffer, uint32_t length_bytes)
{
    // Validate length_bytes parameter
    if (length_bytes == 0 || length_bytes > PROGRAM_CHUNK_SIZE) {
        if (dbg) printf("%s: Invalid length %d (must be > 0 and ≤ %d)\n", __FUNCTION__, length_bytes, PROGRAM_CHUNK_SIZE);
        return -120;  // Invalid length
    }

    if ((length_bytes & 0xFF) != 0) {
        if (dbg) printf("%s: Invalid length %d (must be multiple of 256)\n", __FUNCTION__, length_bytes);
        return -120;  // Not multiple of 256
    }

    // Calculate number of words to write
    uint32_t num_words = length_bytes / 4;

    // Step 1: Write data to EP's RAM buffer
    if (const auto r = swd->writeMultiWordViaAP(target_pagebuffer_ram, writeBuffer, num_words); r != 0) {
        if (dbg) printf("%s: writeMultiWordViaAP failed: %d\n", __FUNCTION__, r);
        return -120;  // RAM write failed
    }

    // Step 2: Program flash from RAM buffer
    if (const auto r = kc1fsz::call_function(*swd, rom_debug_trampoline_func,
        rom_flash_range_program_func, flash_offset, target_pagebuffer_ram, length_bytes, 0);
        !r.has_value()) {
        if (dbg) printf("%s: rom_flash_range_program_func failed\n", __FUNCTION__);
        return -121;  // Flash program failed
    }

    return 0;  // Success
}

// --------------------------------------------------------------------------------
// Add a region to the tracking array
// Returns true on success, false if array is full
bool FlashEp::addRegion(uint32_t start_addr, uint32_t length)
{
    if (num_regions >= MAX_UF2_REGIONS) {
        if (dbg) printf("%s: Too many regions (max %d)\n", __FUNCTION__, MAX_UF2_REGIONS);
        return false;  // Too many regions
    }

    flash_regions[num_regions].start_addr = start_addr;
    flash_regions[num_regions].length_bytes = length;
    flash_regions[num_regions].valid = true;
    num_regions++;

    if (dbg) printf("%s: Added region %d: 0x%08X - 0x%08X (%d bytes)\n",
                    __FUNCTION__, num_regions - 1, start_addr, start_addr + length, length);

    return true;
}

// --------------------------------------------------------------------------------
// Scan UF2 file to identify all contiguous regions
int32_t FlashEp::scanUF2Regions(lfs_file_t* file)
{
    uint8_t uf2_block[UF2_BLOCK_SIZE];
    uint32_t current_region_start = 0xFFFFFFFF;
    uint32_t current_region_end = 0;

    if (dbg) printf("%s: Scanning UF2 file for contiguous regions...\n", __FUNCTION__);

    while (true) {
        // Read UF2 block
        lfs_ssize_t bytes_read = lfs_file_read(&lfs, file, uf2_block, UF2_BLOCK_SIZE);

        if (bytes_read == 0) break;  // EOF
        if (bytes_read != UF2_BLOCK_SIZE) {
            if (dbg) printf("%s: Short read: %d bytes (expected %d)\n",
                            __FUNCTION__, bytes_read, UF2_BLOCK_SIZE);
            return -101;  // Short read
        }

        // Validate UF2 magic numbers
        uint32_t* words = (uint32_t*)uf2_block;
        if (words[0] != UF2_MAGIC_START0 ||
            words[1] != UF2_MAGIC_START1 ||
            words[127] != UF2_MAGIC_END) {
            if (dbg) printf("%s: Invalid UF2 magic numbers\n", __FUNCTION__);
            return -102;  // Invalid UF2 block
        }

        // Extract fields
        uint32_t target_addr = words[3];
        uint32_t payload_size = words[4];
        uint32_t flash_offset = target_addr - 0x10000000;

        // Track regions
        if (current_region_start == 0xFFFFFFFF) {
            // Start new region
            current_region_start = flash_offset;
            current_region_end = flash_offset + payload_size;
        } else {
            // Check if contiguous with current region
            if (flash_offset == current_region_end) {
                // Extend current region
                current_region_end += payload_size;
            } else {
                // Gap detected - save current region, start new one
                if (!addRegion(current_region_start,
                              current_region_end - current_region_start)) {
                    return -103;  // Too many regions (>32)
                }
                current_region_start = flash_offset;
                current_region_end = flash_offset + payload_size;
            }
        }
    }

    // Save final region
    if (current_region_start != 0xFFFFFFFF) {
        if (!addRegion(current_region_start,
                      current_region_end - current_region_start)) {
            return -103;  // Too many regions
        }
    }

    if (dbg) printf("%s: Found %d region(s)\n", __FUNCTION__, num_regions);
    return 0;  // Success
}

// --------------------------------------------------------------------------------
// Validate all regions (4KB alignment, 256-byte multiples, no overlaps)
int32_t FlashEp::validateRegions()
{
    if (num_regions == 0) {
        if (dbg) printf("%s: No regions found\n", __FUNCTION__);
        return -110;  // No regions found
    }

    if (dbg) printf("%s: Validating %d region(s)...\n", __FUNCTION__, num_regions);

    for (uint32_t i = 0; i < num_regions; i++) {
        FlashRegion& region = flash_regions[i];

        // Check 4KB boundary alignment
        if ((region.start_addr & 0xFFF) != 0) {
            if (dbg) printf("%s: Region %d: start 0x%08X not on 4KB boundary\n",
                           __FUNCTION__, i, region.start_addr);
            return -111;  // Not 4KB aligned
        }

        // Check 256-byte multiple
        if ((region.length_bytes & 0xFF) != 0) {
            if (dbg) printf("%s: Region %d: length %d not multiple of 256\n",
                           __FUNCTION__, i, region.length_bytes);
            return -112;  // Not 256-byte multiple
        }

        // Check for overlaps with other regions
        for (uint32_t j = i + 1; j < num_regions; j++) {
            FlashRegion& other = flash_regions[j];
            uint32_t region_end = region.start_addr + region.length_bytes;
            uint32_t other_end = other.start_addr + other.length_bytes;

            // Check if regions overlap
            if (!(region_end <= other.start_addr ||
                  other_end <= region.start_addr)) {
                if (dbg) printf("%s: Regions %d and %d overlap\n", __FUNCTION__, i, j);
                return -113;  // Overlapping regions
            }
        }
    }

    if (dbg) printf("%s: All regions validated successfully\n", __FUNCTION__);
    return 0;  // All regions valid
}

// --------------------------------------------------------------------------------
// Erase all regions in the flash_regions array
int32_t FlashEp::eraseAllRegions()
{
    if (dbg) printf("%s: Erasing %d region(s)...\n", __FUNCTION__, num_regions);

    for (uint32_t i = 0; i < num_regions; i++) {
        FlashRegion& region = flash_regions[i];

        if (dbg) printf("%s: Erasing region %d: 0x%08X - 0x%08X (%d bytes)\n",
                       __FUNCTION__, i, region.start_addr,
                       region.start_addr + region.length_bytes,
                       region.length_bytes);

        // Erase Using max possible size chunk (64KB) cmd=0x20rom_flash_range_erase_func, offset, buf_len, 1 << 16, 0xd8
        if (const auto r = kc1fsz::call_function(*swd, rom_debug_trampoline_func,
            rom_flash_range_erase_func,
            region.start_addr,           // Flash offset
            region.length_bytes,         // Length to erase
            65536,                       // Erase Block size (64KB sectors)
            0xD8);                       // 64K Erase command
            !r.has_value()) {
            if (dbg) printf("%s: Erase failed for region %d\n", __FUNCTION__, i);
            return -123;  // Erase failed
        }

        // Flush cache after erase
        if (const auto r = kc1fsz::call_function(*swd, rom_debug_trampoline_func,
            rom_flash_flush_cache_func, 0, 0, 0, 0); !r.has_value()) {
            if (dbg) printf("%s: Cache flush failed for region %d\n", __FUNCTION__, i);
            return -123;
        }
    }

    if (dbg) printf("%s: All regions erased successfully\n", __FUNCTION__);
    return 0;  // Success
}

// --------------------------------------------------------------------------------
// Program all UF2 blocks from file to flash using 4KB chunking
// 'skipProgramming' does everything except erasing and programming the flash.
int32_t FlashEp::programAllBlocks(lfs_file_t* file)
{
    uint8_t uf2_block[UF2_BLOCK_SIZE];

    uint32_t chunk_start_offset = 0;
    uint32_t chunk_bytes = 0;
    uint32_t total_bytes_written = 0;

    if (dbg) printf("%s: Programming UF2 blocks in %d-byte chunks...\n",
                    __FUNCTION__, PROGRAM_CHUNK_SIZE);

    while (true) {
        // Read UF2 block
        lfs_ssize_t bytes_read = lfs_file_read(&lfs, file, uf2_block, UF2_BLOCK_SIZE);

        if (bytes_read == 0) break;  // EOF
        if (bytes_read != UF2_BLOCK_SIZE) {
            if (dbg) printf("%s: Short read: %d bytes\n", __FUNCTION__, bytes_read);
            return -124;  // Short read
        }

        // Extract fields
        uint32_t* words = (uint32_t*)uf2_block;
        uint32_t target_addr = words[3];
        uint32_t payload_size = words[4];
        uint32_t flash_offset = target_addr - 0x10000000;

        // Get payload (256 bytes at offset 0x20)
        uint8_t* payload = &uf2_block[0x20];

        // Check if payload is contiguous with current chunk
        if (chunk_bytes == 0) {
            // Start new chunk
            chunk_start_offset = flash_offset;
        } else if (flash_offset != (chunk_start_offset + chunk_bytes)) {
            // Gap detected! Flush current chunk before starting new one
            if (dbg) printf("%s: Gap detected at 0x%08X, flushing %d bytes\n",
                           __FUNCTION__, flash_offset, chunk_bytes);

            int32_t result = writeChunk(chunk_start_offset,
                                          (uint32_t*)chunk_buffer,
                                          chunk_bytes);
            if (result != 0) {
                if (dbg) printf("%s: writeChunk failed at offset 0x%08X: %d\n",
                               __FUNCTION__, chunk_start_offset, result);
                return -125;  // Program failed
            }

            total_bytes_written += chunk_bytes;
            printf("Programmed %d bytes (total: %d bytes)\n", chunk_bytes, total_bytes_written);

            // Start new chunk at this flash_offset
            chunk_start_offset = flash_offset;
            chunk_bytes = 0;
        }

        // Copy 256-byte payload to chunk buffer
        memcpy(&chunk_buffer[chunk_bytes], payload, 256);
        chunk_bytes += 256;

        // If chunk is full, flush it
        if (chunk_bytes == PROGRAM_CHUNK_SIZE) {
            int32_t result = writeChunk(chunk_start_offset,
                                        (uint32_t*)chunk_buffer,
                                        chunk_bytes);
            if (result != 0) {
                if (dbg) printf("%s: writeChunk failed at offset 0x%08X: %d\n",
                               __FUNCTION__, chunk_start_offset, result);
                return -125;  // Program failed
            }

            total_bytes_written += chunk_bytes;
            printf("Programmed %d bytes (total: %d bytes)\n", chunk_bytes, total_bytes_written);

            chunk_bytes = 0;  // Reset for next chunk
        }
    }

    // Flush any remaining partial chunk
    if (chunk_bytes > 0) {
        if (dbg) printf("%s: Flushing final partial chunk: %d bytes\n",
                       __FUNCTION__, chunk_bytes);

        int32_t result = writeChunk(chunk_start_offset,
                                      (uint32_t*)chunk_buffer,
                                      chunk_bytes);
        if (result != 0) {
            if (dbg) printf("%s: writeChunk failed at offset 0x%08X: %d\n",
                           __FUNCTION__, chunk_start_offset, result);
            return -125;  // Program failed
        }

        total_bytes_written += chunk_bytes;
        printf("Programmed %d bytes (total: %d bytes)\n", chunk_bytes, total_bytes_written);
    }

    if (dbg) printf("%s: Successfully programmed %d bytes total\n",
                    __FUNCTION__, total_bytes_written);
    return 0;  // Success
}

// --------------------------------------------------------------------------------
// Perform the complete flash operation (take control, erase, program, release)
int32_t FlashEp::performFlashOperation(lfs_file_t* file)
{
    // 1. Rewind file to start
    lfs_file_rewind(&lfs, file);

    // 2. Take control of target processor
    int32_t result = takeControl();
    if (result != 0) {
        if (dbg) printf("%s: takeControl failed: %d\n", __FUNCTION__, result);
        return -120;  // takeControl failed
    }

    // NOTE: HC11 reset signal is already asserted in takeControl()
    // via the debug unit after stopping both cores

    // 3. Exit XIP mode (required before any flash operations)
    if (const auto r = kc1fsz::call_function(*swd, rom_debug_trampoline_func,
        rom_connect_internal_flash_func, 0, 0, 0, 0); !r.has_value()) {
        if (dbg) printf("%s: rom_connect_internal_flash_func failed\n", __FUNCTION__);
        releaseControl();
        return -121;
    }

    if (const auto r = kc1fsz::call_function(*swd, rom_debug_trampoline_func,
        rom_flash_exit_xip_func, 0, 0, 0, 0); !r.has_value()) {
        if (dbg) printf("%s: rom_flash_exit_xip_func failed\n", __FUNCTION__);
        releaseControl();
        return -122;
    }

    // 4. Bulk erase all regions WITH TIMING
    absolute_time_t erase_start = get_absolute_time();
    result = eraseAllRegions();
    int64_t erase_us = absolute_time_diff_us(erase_start, get_absolute_time());
    if (result != 0) {
        if (dbg) printf("%s: eraseAllRegions failed: %d\n", __FUNCTION__, result);
        releaseControl();
        return result;  // Erase failed: -123
    }
    if (dbg) printf("Erase took %.2f ms\n", erase_us / 1000.0);

    // 5. Program all UF2 blocks WITH TIMING
    absolute_time_t program_start = get_absolute_time();
    result = programAllBlocks(file);
    int64_t program_us = absolute_time_diff_us(program_start, get_absolute_time());
    if (result != 0) {
        if (dbg) printf("%s: programAllBlocks failed: %d\n", __FUNCTION__, result);
        releaseControl();
        return result;  // Program failed: -124 to -125
    }
    if (dbg) printf("Programming took %.2f ms\n", program_us / 1000.0);

    // 6. Return to XIP mode
    if (const auto r = kc1fsz::call_function(*swd, rom_debug_trampoline_func,
        rom_flash_flush_cache_func, 0, 0, 0, 0); !r.has_value()) {
        if (dbg) printf("%s: rom_flash_flush_cache_func failed\n", __FUNCTION__);
        releaseControl();
        return -126;
    }

    if (const auto r = kc1fsz::call_function(*swd, rom_debug_trampoline_func,
        rom_flash_enter_cmd_xip_func, 0, 0, 0, 0); !r.has_value()) {
        if (dbg) printf("%s: rom_flash_enter_cmd_xip_func failed\n", __FUNCTION__);
        releaseControl();
        return -126;
    }

    // 7. Release control of target processor
    releaseControl();

    if (dbg) printf("%s: Flash operation completed successfully\n", __FUNCTION__);
    return 0;  // Success
}

// --------------------------------------------------------------------------------
// Reads a UF2 file from LittleFS and programs it to the EP's flash
// Uses two-phase approach: (1) scan and validate, (2) erase and program
// fileName: Path to UF2 file on the filesystem (e.g., "/EP.uf2")
int32_t FlashEp::flashUF2(const char* fileName, bool skipProgramming)
{
    lfs_file_t file;
    int32_t result = 0;

    if (dbg) printf("%s: Opening UF2 file: %s\n", __FUNCTION__, fileName);

    // 1. Initialize region tracking
    num_regions = 0;
    memset(flash_regions, 0, sizeof(flash_regions));

    // 2. Open UF2 file
    int err = lfs_file_open(&lfs, &file, fileName, LFS_O_RDONLY);
    if (err < 0) {
        if (dbg) printf("%s: Failed to open file: %d\n", __FUNCTION__, err);
        return -100;  // File open failed
    }

    // 3. Scan entire file to identify regions WITH TIMING
    absolute_time_t scan_start = get_absolute_time();
    result = scanUF2Regions(&file);
    int64_t scan_us = absolute_time_diff_us(scan_start, get_absolute_time());
    if (result != 0) {
        if (dbg) printf("%s: scanUF2Regions failed: %d\n", __FUNCTION__, result);
        lfs_file_close(&lfs, &file);
        return result;  // Scan failed: -101 to -103
    }
    if (dbg) printf("Region scan took %.2f ms\n", scan_us / 1000.0);

    // 4. Validate all regions
    result = validateRegions();
    if (result != 0) {
        if (dbg) printf("%s: validateRegions failed: %d\n", __FUNCTION__, result);
        lfs_file_close(&lfs, &file);
        return result;  // Validation failed: -110 to -113
    }

    if (skipProgramming) {
        return 0;
    }

    // 5. Perform flash operation with retry
    const int MAX_RETRIES = 3;
    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
        if (dbg) printf("%s: Flash attempt %d of %d\n", __FUNCTION__, attempt, MAX_RETRIES);

        result = performFlashOperation(&file);

        if (result == 0) {
            // Success!
            if (dbg) printf("%s: Flash operation succeeded on attempt %d\n", __FUNCTION__, attempt);
            lfs_file_close(&lfs, &file);
            return 0;
        }

        // Flash operation failed
        if (attempt < MAX_RETRIES) {
            printf("%s: Flash attempt %d failed (error %d), retrying...\n",
                   __FUNCTION__, attempt, result);
            sleep_ms(100);  // Brief delay before retry
        }
    }

    // All retries exhausted
    if (dbg) printf("%s: Flash failed after %d attempts (error %d)\n",
                   __FUNCTION__, MAX_RETRIES, result);
    lfs_file_close(&lfs, &file);
    return -127;  // Flash failed after max retries
}
