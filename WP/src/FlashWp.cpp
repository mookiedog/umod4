#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "pico/bootrom.h"
#include "pico/flash.h"
#include "pico/multicore.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "boot/bootrom_constants.h"
#include "hardware/watchdog.h"

#include "FreeRTOS.h"
#include "task.h"

#include "FlashWp.h"
#include "hardware/structs/watchdog.h"
#include "boot/picoboot_constants.h"
#include "lfsMgr.h"

#include "pico/cyw43_arch.h"

extern void pico_toggle_led();

// UF2 magic numbers
#define UF2_MAGIC_START0 0x0A324655
#define UF2_MAGIC_START1 0x9E5D5157
#define UF2_MAGIC_END    0x0AB16F30

// UF2 family IDs
#define RP2350_FAMILY_ID   0xe48bff59  // rp2350-arm-s (application code)
#define ABSOLUTE_FAMILY_ID 0xe48bff57  // absolute (no address translation, e.g. partition table)

// Flash constants
#define FLASH_SECTOR_SIZE_BYTES 4096
#define FLASH_PAGE_SIZE_BYTES   256

// Work area for bootrom functions (4K recommended, matching working example)
#define WORKAREA_SIZE 4096
static uint8_t workarea[WORKAREA_SIZE] __attribute__((aligned(4)));

// Scratch buffer for rom_explicit_buy (4K required)
#define EXPLICIT_BUY_BUFFER_SIZE 4096
static uint8_t explicit_buy_buffer[EXPLICIT_BUY_BUFFER_SIZE] __attribute__((aligned(4)));

// Sector buffer for accumulating data before flashing
static uint8_t sector_buffer[FLASH_SECTOR_SIZE_BYTES] __attribute__((aligned(4)));

// Class statics
boot_info_t FlashWp::boot_info;
bool FlashWp::boot_info_valid;
int32_t FlashWp::boot_slot;
int32_t FlashWp::target_slot;

/**
 * Unpause the watchdog tick so rom_reboot() works even with debugger attached.
 */
extern "C" void unpause_watchdog_tick(void)
{
    watchdog_hw->ctrl &= ~((1<<24) | (1<<25) | (1<<26));
}

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

bool FlashWp::isOtaPending(void) {
    int boot_type = rom_get_last_boot_type();
    return (boot_type == BOOT_TYPE_FLASH_UPDATE);
}


// Currently, this routine expects to be called before FreeRTOS is running.
int32_t FlashWp::commitOtaUpdate(void)
{
    if (!isOtaPending()) {
        // Not a flash update boot, nothing to commit
        return 0;
    }

    // Ensure we're on core 0 - the bootrom expects this
    uint core = get_core_num();
    if (core != 0) {
        printf("%s: ERROR - Must run on core 0, currently on core %u\n", __FUNCTION__, core);
        return -2;
    }

    int rc;

    #if 0
        // FreeRTOS is running:
        // Calling rom_explicit_buy() will cause flash_safe_execute() to get invoked.
        // That is a problem because flash_safe_execute() assumes that FreeRTOS is running
        // so that it can signal the other core to hold itself in a flash-safe state
        // while the flash operation is being processed.
        // Obviously, at this point early in the boot process, core1 will never respond
        // and the operation hangs.
        rc = rom_explicit_buy(explicit_buy_buffer, EXPLICIT_BUY_BUFFER_SIZE);

    #else
        // FreeRTOS is NOT running:
        // Call the lower-level rom_helper_explicit_buy() routine because it will not
        // invoke flash_safe_execute() like the higher-level rom_explicit_buy() routine would.
        // We know core1 is not running yet at this early point in the boot process.
        rom_helper_explicit_buy_params_t params;
        params.buffer = explicit_buy_buffer;
        params.buffer_size = EXPLICIT_BUY_BUFFER_SIZE;
        params.res = &rc;
        rom_helper_explicit_buy(&params);
    #endif

    if (rc != BOOTROM_OK) {
        printf("%s: rom_explicit_buy failed: %d\n", __FUNCTION__, rc);
        return -1;
    }

    printf("%s: OTA update committed successfully\n", __FUNCTION__);
    return 0;
}

// Partition numbers in partition_table.json:
//   0 = boot region (not part of A/B)
//   1 = slot A
//   2 = slot B (linked to A)
#define PARTITION_A_NUM  1
#define PARTITION_B_NUM  2

// --------------------------------------------------------------------------------
void FlashWp::get_boot_info()
{
    // We only read the boot_info once because it can only change as the result of a system reboot
    if (!boot_info_valid) {
        // Assume failure
        boot_slot = -1;
        target_slot = -1;

        printf("%s: Reading boot_info\n", __FUNCTION__);
        // Flush flash cache before querying partition table
        rom_flash_flush_cache();

        boot_info_valid = rom_get_boot_info(&boot_info);
        if (!boot_info_valid) {
            printf("%s: rom_get_boot_info failed\n", __FUNCTION__);
        }
        else {
            int32_t bs = (int32_t)boot_info.partition;
            if ((bs == PARTITION_A_NUM) || (bs == PARTITION_B_NUM)) {
                // All is good: assign the slot numbers
                boot_slot = bs;
                target_slot = (boot_slot == PARTITION_A_NUM) ? PARTITION_B_NUM : PARTITION_A_NUM;
            }
        }
    }
}

// --------------------------------------------------------------------------------
int32_t FlashWp::get_boot_slot()
{
    get_boot_info();
    return boot_slot;
}

// --------------------------------------------------------------------------------
int32_t FlashWp::get_target_slot()
{
    get_boot_info();
    return target_slot;
}

// --------------------------------------------------------------------------------
bool FlashWp::get_ota_availability()
{
    get_boot_info();
    return (target_slot >= PARTITION_A_NUM);
}

// --------------------------------------------------------------------------------
// Get the Flash start address and size of the target partition.
// Returns 0 if info is good, negative if there is no target partition.
int32_t FlashWp::getTargetPartitionInfo(uint32_t* start_addr, uint32_t* size)
{

    int32_t target_partition = get_target_slot();
    if (target_partition < PARTITION_A_NUM) {
        printf("%s: Invalid target partition: %d\n", __FUNCTION__, target_partition);
        return -1;
    }

    // Get the partition info for the target partition
    // Return format for PT_INFO_PARTITION_LOCATION_AND_FLAGS | PT_INFO_SINGLE_PARTITION:
    //   buffer[0] = flags echo (the flags we requested)
    //   buffer[1] = location_and_permissions
    //   buffer[2] = flags_and_permissions
    // Returns 3 (number of words written)
    uint32_t partition_info[3];
    int rc = rom_get_partition_table_info(partition_info, 3,
                                          PT_INFO_PARTITION_LOCATION_AND_FLAGS |
                                          PT_INFO_SINGLE_PARTITION | (target_partition << 24));

    if (rc < 0) {
        printf("%s: Failed to get partition %d info: %d\n", __FUNCTION__, target_partition, rc);
        return rc;
    }

    // partition_info[0] = flags echo (should match our request)
    // partition_info[1] = location_and_permissions
    // partition_info[2] = flags_and_permissions
    // Extract the start and end from location_and_permissions (partition_info[1])
    // From boot/picobin.h:
    //   PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_LSB = 0   (bits [12:0])
    //   PICOBIN_PARTITION_LOCATION_LAST_SECTOR_LSB = 13   (bits [25:13])
    uint32_t first_sector = partition_info[1] & 0x1FFF;
    uint32_t last_sector = (partition_info[1] >> 13) & 0x1FFF;

    printf("%s: raw location_and_permissions=0x%08X, flags_and_permissions=0x%08X\n",
           __FUNCTION__, partition_info[1], partition_info[2]);
    printf("%s: first_sector=%u (0x%X), last_sector=%u (0x%X)\n",
           __FUNCTION__, first_sector, first_sector, last_sector, last_sector);

    *start_addr = XIP_BASE + (first_sector * FLASH_SECTOR_SIZE_BYTES);
    *size = ((last_sector - first_sector + 1) * FLASH_SECTOR_SIZE_BYTES);

    printf("%s: target partition at 0x%08X - 0x%08X, size %u bytes\n",
           __FUNCTION__, *start_addr, *start_addr + *size, *size);

    return 0;
}

int32_t FlashWp::flashSector(uint32_t flash_addr, const uint8_t* data, size_t size) {
    // flash_addr is an XIP address (0x10xxxxxx), convert to flash offset
    uint32_t flash_offset = flash_addr - XIP_BASE;

    // Validate alignment
    if ((flash_offset % FLASH_SECTOR_SIZE_BYTES) != 0) {
        printf("%s: address 0x%08X not sector-aligned\n", __FUNCTION__, flash_addr);
        return -1;
    }
    if ((size % FLASH_SECTOR_SIZE_BYTES) != 0) {
        printf("%s: size %zu not sector-aligned\n", __FUNCTION__, size);
        return -2;
    }

    // Erase the sector(s)
    // Note: flash_range_erase requires interrupts disabled and is not safe
    // while executing from flash. We use flash_safe_execute via rom_flash_op
    // which handles this properly.

    // Use the higher-level rom_flash_op which handles safety and permission checking
    // Flags: operation type, address space, and security level (NONSECURE for non-TrustZone builds)
    cflash_flags_t erase_flags = { .flags = (CFLASH_OP_VALUE_ERASE << CFLASH_OP_LSB) |
                                            (CFLASH_ASPACE_VALUE_STORAGE << CFLASH_ASPACE_LSB) |
                                            (CFLASH_SECLEVEL_VALUE_NONSECURE << CFLASH_SECLEVEL_LSB) };
    int rc = rom_flash_op(erase_flags, flash_addr, size, NULL);
    if (rc != BOOTROM_OK) {
        printf("%s: erase failed at 0x%08X: %d\n", __FUNCTION__, flash_addr, rc);
        return -3;
    }

    // Program the sector(s)
    cflash_flags_t program_flags = { .flags = (CFLASH_OP_VALUE_PROGRAM << CFLASH_OP_LSB) |
                                              (CFLASH_ASPACE_VALUE_STORAGE << CFLASH_ASPACE_LSB) |
                                              (CFLASH_SECLEVEL_VALUE_NONSECURE << CFLASH_SECLEVEL_LSB) };
    rc = rom_flash_op(program_flags, flash_addr, size, (uint8_t*)data);
    if (rc != BOOTROM_OK) {
        printf("%s: program failed at 0x%08X: %d\n", __FUNCTION__, flash_addr, rc);
        return -4;
    }

    // Verify the write
    #if 0
    if (memcmp((void*)flash_addr, data, size) != 0) {
        printf("%s: verify failed at 0x%08X\n", __FUNCTION__, flash_addr);
        return -5;
    }
    #endif

    return 0;
}

// --------------------------------------------------------------------------------
// Flash the target slot partition with the contents of the specified UF2 file
int32_t FlashWp::flashUf2(
    const char* path,
    bool verbose,
    uint32_t* partition_start_addr)
{
    lfs_file_t file;
    UF2_Block block;

    if (!get_ota_availability()) {
        printf("%s: OTA is unavailable! Aborting.\n", __FUNCTION__);
        return -1;
    }

    // Check which core we're on - flash operations and rom_reboot() should
    // be called from core 0 for reliable operation
    uint core = get_core_num();
    if (core != 0) {
        printf("%s: ERROR: This function must run on core 0!\n", __FUNCTION__);
        return -2;
    }

    printf("%s: Flashing WP with \"%s\"\n", __FUNCTION__, path);

    // FIXME: IS THIS REALLY NECESSARY?
    // Reset bootrom state to release any held locks (SHA256, etc.)
    rom_bootrom_state_reset(BOOTROM_STATE_RESET_GLOBAL_STATE);

    // Load partition table if not already loaded
    int rc = rom_load_partition_table(workarea, WORKAREA_SIZE, false);
    if (rc < 0) {
        if (rc == BOOTROM_ERROR_NOT_FOUND) {
            printf("%s: ERROR - No partition table found!\n", __FUNCTION__);
        } else {
            printf("%s: Failed to load partition table: %d\n", __FUNCTION__, rc);
        }
        return -3;
    }

    uint32_t target_partition_start, target_partition_size;
    rc = getTargetPartitionInfo(&target_partition_start, &target_partition_size);
    if (rc != 0) {
        printf("%s: Failed to get target partition\n", __FUNCTION__);
        return -4;
    }

    uint32_t target_partition_end = target_partition_start + target_partition_size - 1;
    printf("%s: Target partition: [0x%08X..0x%08X]\n",
           __FUNCTION__, target_partition_start, target_partition_end);

    printf("%s: Opening file \"%s\"\n", __FUNCTION__, path);
    int err = lfs_file_open(&lfs, &file, path, LFS_O_RDONLY);
    if (err < 0) {
        printf("%s: Failed to open %s: %d\n", __FUNCTION__, path, err);
        return -5;
    }

    // First pass: scan blocks with RP2350_FAMILY_ID to find the base address
    // We need the minimum address of application blocks (not absolute blocks)
    printf("%s: Scanning \"%s\" to find the base address of the application blocks\n", __FUNCTION__, path);
    uint32_t uf2_base_addr = UINT32_MAX;
    uint32_t app_block_count = 0;

    while (lfs_file_read(&lfs, &file, &block, sizeof(UF2_Block)) == sizeof(UF2_Block)) {
        if (block.magicStart0 != UF2_MAGIC_START0 ||
            block.magicStart1 != UF2_MAGIC_START1 ||
            block.magicEnd != UF2_MAGIC_END) {
            printf("%s: Invalid UF2 block magic\n", __FUNCTION__);
            lfs_file_close(&lfs, &file);
            return -6;
        }

        // Only consider application blocks for base address calculation
        if (block.familyID == RP2350_FAMILY_ID) {
            if (block.targetAddr < uf2_base_addr) {
                uf2_base_addr = block.targetAddr;
            }
            app_block_count++;
        }
    }

    if (app_block_count == 0) {
        printf("%s: No application blocks found (familyID 0x%08X)\n", __FUNCTION__, RP2350_FAMILY_ID);
        lfs_file_close(&lfs, &file);
        return -7;
    }

    // Sector-align the base address
    uf2_base_addr = uf2_base_addr & ~(FLASH_SECTOR_SIZE_BYTES - 1);
    printf("%s: Found %u app blocks, base address: 0x%08X\n", __FUNCTION__, app_block_count, uf2_base_addr);

    // Rewind to start of file for second pass
    lfs_file_seek(&lfs, &file, 0, LFS_SEEK_SET);

    // Second pass: process and flash all blocks
    uint32_t abs_block_count = 0;

    // Initialize state for accumulating UF2 blocks into sector data
    uint32_t sector_start_addr = 0;
    size_t sector_offset = 0;
    bool first_block = true;
    memset(sector_buffer, 0xFF, FLASH_SECTOR_SIZE_BYTES);

    // Process all the UF2 blocks in the file one at a time
    printf("%s: Flashing the target partition\n", __FUNCTION__);
    while (lfs_file_read(&lfs, &file, &block, sizeof(UF2_Block)) == sizeof(UF2_Block)) {
        // Magic already validated in first pass

        uint32_t target_addr;

        if (block.familyID == ABSOLUTE_FAMILY_ID) {
            // Absolute blocks (like partition table) are skipped during OTA.
            // The partition table was flashed with the initial firmware and
            // should not be modified during runtime OTA updates.
            // (The bootrom's rom_flash_op won't allow writing to unpartitioned
            // space at runtime anyway.)
            abs_block_count++;
            if (verbose || abs_block_count <= 2) {
                printf("\n%s: Skipping absolute block %u -> 0x%08X\n", __FUNCTION__, block.blockNo, block.targetAddr);
            }
            continue;
        }
        else if (block.familyID == RP2350_FAMILY_ID) {
            // Application blocks get translated to target partition
            uint32_t image_offset = block.targetAddr - uf2_base_addr;
            target_addr = target_partition_start + image_offset;

            // Bounds check for application blocks
            if (target_addr < target_partition_start || target_addr + block.payloadSize > target_partition_end) {
                printf("\n%s: Block %u address 0x%08X out of partition bounds\n", __FUNCTION__, block.blockNo, target_addr);
                lfs_file_close(&lfs, &file);
                return -8;
            }
        }
        else {
            // Unknown family ID - skip this block
            if (verbose) {
                printf("\n%s: Skipping block with unknown familyID 0x%08X\n", __FUNCTION__ , block.familyID);
            }
            continue;
        }

        // Calculate which sector this block belongs to
        uint32_t block_sector_start = target_addr & ~(FLASH_SECTOR_SIZE_BYTES - 1);
        uint32_t block_offset_in_sector = target_addr & (FLASH_SECTOR_SIZE_BYTES - 1);

        // If it belongs to a different sector than we're accumulating, flush the current one
        if (!first_block && block_sector_start != sector_start_addr) {
            if (verbose) {
                printf("%s: Flashing sector at 0x%08X\r", __FUNCTION__, sector_start_addr);
            }
            int rc = flashSector(sector_start_addr, sector_buffer, FLASH_SECTOR_SIZE_BYTES);
            if (rc != 0) {
                lfs_file_close(&lfs, &file);
                return -9;
            }

            // Give a visual indication of progress: toggle the LED as each sector finishes
            pico_toggle_led();

            // Flush buffer for new sector in case it is only partially filled before it gets written
            memset(sector_buffer, 0xFF, FLASH_SECTOR_SIZE_BYTES);
            sector_offset = 0;
        }

        // Start new sector if needed
        if (first_block || block_sector_start != sector_start_addr) {
            sector_start_addr = block_sector_start;
            first_block = false;
        }

        // Copy block data into sector buffer
        memcpy(sector_buffer + block_offset_in_sector, block.data, block.payloadSize);
        sector_offset = block_offset_in_sector + block.payloadSize;
    }

    // Flush the final sector if we have pending data
    if (!first_block && sector_offset > 0) {
        if (verbose) {
            printf("%s: Flashing final sector at 0x%08X\n", __FUNCTION__, sector_start_addr);
        }
        int rc = flashSector(sector_start_addr, sector_buffer, FLASH_SECTOR_SIZE_BYTES);
        if (rc != 0) {
            lfs_file_close(&lfs, &file);
            return -10;
        }
    }

    printf("%s: Flashed %u app blocks + %u absolute blocks\n", __FUNCTION__, app_block_count, abs_block_count);

    lfs_file_close(&lfs, &file);

    // Flush the flash cache to ensure any cached reads see the new data
    rom_flash_flush_cache();

    // Return the partition's start address if caller wants it
    if (partition_start_addr) {
        *partition_start_addr = target_partition_start;
    }
    return 0;
}

// C-compatible wrappers
extern "C" int32_t flash_wp_uf2(const char* pathname, bool verbose, uint32_t* target_addr_out) {
    return FlashWp::flashUf2(pathname, verbose, target_addr_out);
}
