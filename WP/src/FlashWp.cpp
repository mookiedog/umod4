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
#include "lfs.h"

#include "pico/cyw43_arch.h"

extern void pico_toggle_led();
extern bool ota_available;

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

int32_t FlashWp::commitOtaUpdate(void) {
    if (!isOtaPending()) {
        // Not a flash update boot, nothing to commit
        return 0;
    }

    // Ensure we're on core 0 - the bootrom expects this
    uint core = get_core_num();
    if (core != 0) {
        printf("FlashWp::commitOtaUpdate: ERROR - Must run on core 0, currently on core %u\n", core);
        return -2;
    }

    // Core 1 was already reset at the very start of main() after the warm reboot.
    // Don't reset it again here - it causes issues (possibly with FreeRTOS SMP
    // initialization even before the scheduler starts).
    int rc;

    #if 0
        // Calling rom_explicit_buy() will cause flash_safe_execute() to get invoked.
        // That is a problem because flash_safe_execute() assumes that FreeRTOS is running
        // so that it can signal the other core to hold itself in a flash-safe state
        // while the flash operation is being processed.
        // Obviously, at this point early in the boot process, core1 will never respond
        // and the operation hangs.
        rc = rom_explicit_buy(explicit_buy_buffer, EXPLICIT_BUY_BUFFER_SIZE);

    #else
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
        printf("FlashWp::commitOtaUpdate: rom_explicit_buy failed: %d\n", rc);
        return -1;
    }

    printf("FlashWp::commitOtaUpdate: OTA update committed successfully\n");
    return 0;
}

// Partition numbers in partition_table.json:
//   0 = boot region (not part of A/B)
//   1 = slot A
//   2 = slot B (linked to A)
#define PARTITION_A_NUM  1
#define PARTITION_B_NUM  2

// Track which partition we're targeting for OTA
static int pending_target_partition = -1;

int32_t FlashWp::getTargetPartition(uint32_t* start_addr, uint32_t* size) {
    // Flush flash cache before querying partition table
    rom_flash_flush_cache();

    // Use rom_get_boot_info to find which partition we ACTUALLY booted from.
    // This is different from rom_pick_ab_partition which tells us which partition
    // the bootloader would pick NOW (which might differ after we flash a new image).
    boot_info_t boot_info;
    bool ok = rom_get_boot_info(&boot_info);
    if (!ok) {
        printf("FlashWp::getTargetPartition: rom_get_boot_info failed\n");
        return -1;
    }

    int picked = boot_info.partition;
    printf("FlashWp::getTargetPartition: rom_get_boot_info says we booted from partition %d\n", picked);

    if (picked < 0) {
        printf("FlashWp::getTargetPartition: boot_info.partition is negative: %d (no partition table?)\n", picked);
        return picked;
    }

    // Determine target: if we're running from A, target B; if from B, target A
    int target_partition;
    if (picked == PARTITION_A_NUM) {
        target_partition = PARTITION_B_NUM;
        printf("FlashWp::getTargetPartition: Running from slot A, targeting slot B\n");
    } else if (picked == PARTITION_B_NUM) {
        target_partition = PARTITION_A_NUM;
        printf("FlashWp::getTargetPartition: Running from slot B, targeting slot A\n");
    } else {
        printf("FlashWp::getTargetPartition: Unexpected partition %d picked\n", picked);
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
    printf("FlashWp::getTargetPartition: rom_get_partition_table_info returned %d\n", rc);

    if (rc < 0) {
        printf("FlashWp::getTargetPartition: Failed to get partition %d info: %d\n",
               target_partition, rc);
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

    printf("FlashWp::getTargetPartition: raw location_and_permissions=0x%08X, flags_and_permissions=0x%08X\n",
           partition_info[1], partition_info[2]);
    printf("FlashWp::getTargetPartition: first_sector=%u (0x%X), last_sector=%u (0x%X)\n",
           first_sector, first_sector, last_sector, last_sector);

    *start_addr = XIP_BASE + (first_sector * FLASH_SECTOR_SIZE_BYTES);
    *size = ((last_sector - first_sector + 1) * FLASH_SECTOR_SIZE_BYTES);

    printf("FlashWp::getTargetPartition: target partition at 0x%08X - 0x%08X, size %u bytes\n",
           *start_addr, *start_addr + *size, *size);

    return 0;
}

int32_t FlashWp::flashSector(uint32_t flash_addr, const uint8_t* data, size_t size) {
    // flash_addr is an XIP address (0x10xxxxxx), convert to flash offset
    uint32_t flash_offset = flash_addr - XIP_BASE;

    // Validate alignment
    if ((flash_offset % FLASH_SECTOR_SIZE_BYTES) != 0) {
        printf("FlashWp::flashSector: address 0x%08X not sector-aligned\n", flash_addr);
        return -1;
    }
    if ((size % FLASH_SECTOR_SIZE_BYTES) != 0) {
        printf("FlashWp::flashSector: size %zu not sector-aligned\n", size);
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
        printf("FlashWp::flashSector: erase failed at 0x%08X: %d\n", flash_addr, rc);
        return -3;
    }

    // Program the sector(s)
    cflash_flags_t program_flags = { .flags = (CFLASH_OP_VALUE_PROGRAM << CFLASH_OP_LSB) |
                                              (CFLASH_ASPACE_VALUE_STORAGE << CFLASH_ASPACE_LSB) |
                                              (CFLASH_SECLEVEL_VALUE_NONSECURE << CFLASH_SECLEVEL_LSB) };
    rc = rom_flash_op(program_flags, flash_addr, size, (uint8_t*)data);
    if (rc != BOOTROM_OK) {
        printf("FlashWp::flashSector: program failed at 0x%08X: %d\n", flash_addr, rc);
        return -4;
    }

    // Verify the write
    #if 0
    if (memcmp((void*)flash_addr, data, size) != 0) {
        printf("FlashWp::flashSector: verify failed at 0x%08X\n", flash_addr);
        return -5;
    }
    #endif

    return 0;
}

int32_t FlashWp::processUf2(lfs_t* lfs, const char* path, uint32_t partition_start,
                             uint32_t partition_size, bool verbose) {
    lfs_file_t file;
    UF2_Block block;

    // State for accumulating sector data
    uint32_t sector_start_addr = 0;
    size_t sector_offset = 0;
    bool first_block = true;

    printf("FlashWp::processUf2: Opening %s\n", path);
    printf("FlashWp::processUf2: Target partition: 0x%08X - 0x%08X (%u bytes)\n",
           partition_start, partition_start + partition_size, partition_size);

    int err = lfs_file_open(lfs, &file, path, LFS_O_RDONLY);
    if (err < 0) {
        printf("FlashWp::processUf2: Failed to open %s: %d\n", path, err);
        return -3;
    }

    uint32_t partition_end = partition_start + partition_size;

    // Initialize sector buffer to erased state
    memset(sector_buffer, 0xFF, FLASH_SECTOR_SIZE_BYTES);

    // First pass: scan blocks with RP2350_FAMILY_ID to find the base address
    // We need the minimum address of application blocks (not absolute blocks)
    uint32_t uf2_base_addr = UINT32_MAX;
    uint32_t app_block_count = 0;

    while (lfs_file_read(lfs, &file, &block, sizeof(UF2_Block)) == sizeof(UF2_Block)) {
        if (block.magicStart0 != UF2_MAGIC_START0 ||
            block.magicStart1 != UF2_MAGIC_START1 ||
            block.magicEnd != UF2_MAGIC_END) {
            printf("FlashWp::processUf2: Invalid UF2 block magic\n");
            lfs_file_close(lfs, &file);
            return -4;
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
        printf("FlashWp::processUf2: No application blocks found (familyID 0x%08X)\n", RP2350_FAMILY_ID);
        lfs_file_close(lfs, &file);
        return -4;
    }

    // Sector-align the base address
    uf2_base_addr = uf2_base_addr & ~(FLASH_SECTOR_SIZE_BYTES - 1);
    printf("FlashWp::processUf2: Found %u app blocks, base address: 0x%08X\n", app_block_count, uf2_base_addr);

    // Rewind to start of file for second pass
    lfs_file_seek(lfs, &file, 0, LFS_SEEK_SET);

    // Second pass: process and flash all blocks
    uint32_t abs_block_count = 0;

    while (lfs_file_read(lfs, &file, &block, sizeof(UF2_Block)) == sizeof(UF2_Block)) {
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
                printf("\nFlashWp::processUf2: Skipping absolute block %u -> 0x%08X\n",
                       block.blockNo, block.targetAddr);
            }
            continue;
        } else if (block.familyID == RP2350_FAMILY_ID) {
            // Application blocks get translated to target partition
            uint32_t image_offset = block.targetAddr - uf2_base_addr;
            target_addr = partition_start + image_offset;

            #if 0
            if (verbose && (block.blockNo % 100 == 0)) {
                printf("FlashWp::processUf2: App block %u/%u -> 0x%08X\n",
                       block.blockNo, block.numBlocks, target_addr);
            }
            #endif

            // Bounds check for application blocks
            if (target_addr < partition_start || target_addr + block.payloadSize > partition_end) {
                printf("\nFlashWp::processUf2: Block %u address 0x%08X out of partition bounds\n",
                       block.blockNo, target_addr);
                lfs_file_close(lfs, &file);
                return -5;
            }
        } else {
            // Unknown family ID - skip this block
            if (verbose) {
                printf("\nFlashWp::processUf2: Skipping block with unknown familyID 0x%08X\n",
                       block.familyID);
            }
            continue;
        }

        // Calculate which sector this block belongs to
        uint32_t block_sector_start = target_addr & ~(FLASH_SECTOR_SIZE_BYTES - 1);
        uint32_t block_offset_in_sector = target_addr & (FLASH_SECTOR_SIZE_BYTES - 1);

        // If this is a different sector than we're accumulating, flush the current one
        if (!first_block && block_sector_start != sector_start_addr) {
            // Flush the accumulated sector
            if (verbose) {
                printf("FlashWp::processUf2: Flashing sector at 0x%08X\r", sector_start_addr);
            }
            if (ota_available) {
                int rc = flashSector(sector_start_addr, sector_buffer, FLASH_SECTOR_SIZE_BYTES);
                if (rc != 0) {
                    lfs_file_close(lfs, &file);
                    return rc;
                }
            }
            else {
                sleep_ms(10);
                printf("\n*** OTA not available\n");
            }
            // toggle as each sector finishes
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
            printf("FlashWp::processUf2: Flashing final sector at 0x%08X\n", sector_start_addr);
        }
        if (ota_available) {
             int rc = flashSector(sector_start_addr, sector_buffer, FLASH_SECTOR_SIZE_BYTES);
            if (rc != 0) {
                lfs_file_close(lfs, &file);
                return rc;
            }
        }
    }

    printf("FlashWp::processUf2: Flashed %u app blocks + %u absolute blocks\n",
           app_block_count, abs_block_count);

    lfs_file_close(lfs, &file);
    return 0;
}

int32_t FlashWp::flashUf2(const char* pathname, bool verbose, uint32_t* target_addr_out) {
    extern lfs_t lfs;  // From SdCardSDIO.cpp

    // Check which core we're on - flash operations and rom_reboot() should
    // be called from core 0 for reliable operation
    uint core = get_core_num();
    if (core != 0) {
        printf("FlashWp::flashUf2: WARNING - Running on core %u, should be core 0!\n", core);
        printf("FlashWp::flashUf2: Flash operations may be unreliable.\n");
        // TODO: Consider delegating to core 0 via a queue/flag mechanism
    }

    printf("FlashWp::flashUf2: Flashing WP with \"%s\" (core %u)\n", pathname, core);

    // Step 1: Reset bootrom state to release any held locks (SHA256, etc.)
    // This is necessary because previous operations may have left locks held,
    // especially after crashes or warm reboots.
    rom_bootrom_state_reset(BOOTROM_STATE_RESET_GLOBAL_STATE);

    // Step 2: Load partition table if not already loaded
    int rc = rom_load_partition_table(workarea, WORKAREA_SIZE, false);
    printf("FlashWp::flashUf2: rom_load_partition_table returned %d\n", rc);
    if (rc < 0) {
        if (rc == BOOTROM_ERROR_NOT_FOUND) {
            printf("FlashWp::flashUf2: WARNING - No partition table found in flash!\n");
            printf("FlashWp::flashUf2: You must flash partition_table.uf2 before OTA updates will work.\n");
        } else {
            printf("FlashWp::flashUf2: Failed to load partition table: %d\n", rc);
        }
        return -1;
    }
    printf("FlashWp::flashUf2: Partition table loaded successfully\n");

    // Step 2: Get the target partition (the one we're NOT running from)
    uint32_t target_start, target_size;
    rc = getTargetPartition(&target_start, &target_size);
    if (rc != 0) {
        printf("FlashWp::flashUf2: Failed to get target partition\n");
        return -2;
    }

    // Step 3: Process the UF2 file and flash to target partition
    rc = processUf2(&lfs, pathname, target_start, target_size, verbose);
    if (rc != 0) {
        printf("FlashWp::flashUf2: Failed to process UF2: %d\n", rc);
        return rc;
    }

    printf("FlashWp::flashUf2: Flash complete! Target partition: 0x%08X\n", target_start);

    // Flush the flash cache to ensure any cached reads see the new data
    rom_flash_flush_cache();

    // Return target address if caller wants it
    if (target_addr_out) {
        *target_addr_out = target_start;
    }

    // NOTE: Reboot is NOT triggered here. The caller (OTA task) is responsible
    // for performing proper system shutdown and reboot sequence.
    return 0;
}

void FlashWp::rebootToUpdate(uint32_t target_addr, uint32_t delay_ms) {
    printf("FlashWp::rebootToUpdate: Rebooting to 0x%08lX in %lu ms\n",
           (unsigned long)target_addr, (unsigned long)delay_ms);

    unpause_watchdog_tick();

    // Not sure this is needed anymore FIXME
    printf("Suspending FreeRTOS scheduler\n");
    vTaskSuspendAll();

    // Disable interrupts on this core to prevent any pending interrupts
    // from firing during or after the reboot
    printf("Disabling interrupts\n");
    save_and_disable_interrupts();

    printf("Calling rom_reboot() with FLASH_UPDATE, target window base 0x%08lX\n",
           (unsigned long)target_addr);
    // p0 = flash_update_boot_window_base: the XIP address of the partition to boot for TBYB
    // The bootrom compares this against partition flash_start_offsets to select which one to boot
    int rc = rom_reboot(
        REBOOT2_FLAG_REBOOT_TYPE_FLASH_UPDATE | REBOOT2_FLAG_NO_RETURN_ON_SUCCESS,
        delay_ms,
        target_addr,  // p0 = flash_update_boot_window_base (XIP address of target partition)
        0
    );
    // If we get here, rom_reboot failed
    printf("FlashWp::rebootToUpdate: rom_reboot failed with %d\n", rc);
}

// C-compatible wrappers
extern "C" int32_t flash_wp_uf2(const char* pathname, bool verbose, uint32_t* target_addr_out) {
    return FlashWp::flashUf2(pathname, verbose, target_addr_out);
}

extern "C" void flash_wp_reboot_to_update(uint32_t target_addr, uint32_t delay_ms) {
    FlashWp::rebootToUpdate(target_addr, delay_ms);
}

extern "C" int32_t flash_wp_commit_ota(void) {
    return FlashWp::commitOtaUpdate();
}

extern "C" bool flash_wp_is_ota_pending(void) {
    return FlashWp::isOtaPending();
}
