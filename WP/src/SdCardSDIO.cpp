// SDIO 4-bit SD Card Driver for WP (RP2350)
// Minimal implementation following SDIO_RP2350 library pattern
// Target: 20-25 MB/s throughput vs ~3 MB/s SPI

#include "SdCardSDIO.h"
#include "SdCard.h"  // For extract_bits_BE and CSD constants
#include "sdio_rp2350.h"  // From SDIO_RP2350 library
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include "FreeRTOS.h"
#include "task.h"

// SD Commands
static const uint8_t CMD0  = 0;   // GO_IDLE_STATE
static const uint8_t CMD2  = 2;   // ALL_SEND_CID
static const uint8_t CMD3  = 3;   // SEND_RELATIVE_ADDR
static const uint8_t CMD6  = 6;   // SWITCH_FUNC
static const uint8_t CMD7  = 7;   // SELECT_CARD
static const uint8_t CMD8  = 8;   // SEND_IF_COND
static const uint8_t CMD9  = 9;   // SEND_CSD
static const uint8_t CMD12 = 12;  // STOP_TRANSMISSION
static const uint8_t CMD13 = 13;  // SEND_STATUS
static const uint8_t CMD16 = 16;  // SET_BLOCKLEN
static const uint8_t CMD17 = 17;  // READ_SINGLE_BLOCK
static const uint8_t CMD18 = 18;  // READ_MULTIPLE_BLOCK
static const uint8_t CMD24 = 24;  // WRITE_BLOCK
static const uint8_t CMD25 = 25;  // WRITE_MULTIPLE_BLOCK
static const uint8_t CMD55 = 55;  // APP_CMD
static const uint8_t ACMD6 = 6;   // SET_BUS_WIDTH
static const uint8_t ACMD41 = 41; // SD_SEND_OP_COND

// Declare extract_bits_BE from SdCard.cpp
extern uint32_t extract_bits_BE(uint8_t* data, int32_t dataLen_bits, int32_t be_start_bit, int32_t num_bits);

// --------------------------------------------------------------------------------------------
SdCardSDIO::SdCardSDIO(int32_t _cardPresentPad)
{
    cardPresentPad = _cardPresentPad;
    rca = 0;
    blockSize_bytes = 512;
    capacity_blocks = 0;
    capacity_bytes = 0;
    isSDHC = false;
    initTime_max_mS = 0;
    clockFrequency_Hz = 0;  // Will be set during init
    state = NO_CARD;
    memset(regCSD, 0, sizeof(regCSD));

    // Init the card detection signal with a pullup.
    // If a card is present, it will pull this pad to GND.
    gpio_init(cardPresentPad);
    gpio_set_dir(cardPresentPad, GPIO_IN);
    gpio_pull_up(cardPresentPad);
}

// --------------------------------------------------------------------------------------------
bool SdCardSDIO::cardPresent()
{
    bool present = (gpio_get(cardPresentPad) == 0);
    return present;
}

// --------------------------------------------------------------------------------------------
// Removed getBlockSize_bytes() and getCardCapacity_blocks() - now inline in header as getSectorSize() and getSectorCount()
// --------------------------------------------------------------------------------------------
SdErr_t SdCardSDIO::calculateCapacity()
{
    SdErr_t err = SD_ERR_NOERR;

    uint32_t csdStructure = extract_bits_BE(regCSD, REG_CSD_BITLEN, CSD_STRUCTURE_START, CSD_STRUCTURE_LENGTH);
    if (csdStructure > 1) {
        err = SD_ERR_CSD_VERSION;
    }
    else {
        // The read block length is interpreted as 2**N
        uint32_t rdBlkLen = extract_bits_BE(regCSD, REG_CSD_BITLEN, CSD_RD_BLK_LEN_START, CSD_RD_BLK_LEN_LENGTH);
        if ((rdBlkLen < 9) || (rdBlkLen > 11)) {
            rdBlkLen = 9;  // Force to 512 bytes
        }
        blockSize_bytes = (1 << rdBlkLen);

        if (csdStructure == 0) {
            // SDSC card
            uint32_t csize = extract_bits_BE(regCSD, REG_CSD_BITLEN, CSD_V1_CSIZE_START, CSD_V1_CSIZE_LENGTH);
            uint32_t raw_c_size_mult = extract_bits_BE(regCSD, REG_CSD_BITLEN, CSD_V1_CSIZE_MULT_START, CSD_V1_CSIZE_MULT_LENGTH);
            uint32_t c_size_mult = 1 << (raw_c_size_mult + 2);

            capacity_blocks = (csize + 1) * c_size_mult;
            capacity_bytes = capacity_blocks * blockSize_bytes;
        }
        else {
            // SDHC/SDXC card
            uint32_t csize = extract_bits_BE(regCSD, REG_CSD_BITLEN, CSD_V2_CSIZE_START, CSD_V2_CSIZE_LENGTH);

            if (blockSize_bytes != 512) {
                blockSize_bytes = 512;  // SDHC/SDXC always use 512 bytes
            }

            capacity_blocks = (csize + 1) * 1024;
            capacity_bytes = (uint64_t)capacity_blocks * blockSize_bytes;
        }
    }

    return err;
}

// --------------------------------------------------------------------------------------------
SdErr_t SdCardSDIO::resetCard()
{
    // CMD0 - Reset card (no response expected)
    if (rp2350_sdio_command(CMD0, 0, nullptr, 0, 0) != SDIO_OK) {
        return SD_ERR_NO_INIT;
    }

    return SD_ERR_NOERR;
}

// --------------------------------------------------------------------------------------------
SdErr_t SdCardSDIO::checkVoltage()
{
    uint32_t reply;
    int32_t retries = 3;

    // CMD8 - Send interface condition (2.7-3.6V range, test pattern 0xAA)
    uint32_t arg = (0x1 << 8) | 0xAA;

    do {
        if (rp2350_sdio_command_u32(CMD8, arg, &reply, 0) != SDIO_OK) {
            busy_wait_us_32(10);
            continue;
        }

        // Check echo pattern
        if ((reply & 0xFF) != 0xAA) {
            return SD_ERR_BAD_RESPONSE;
        }

        // Check voltage accepted
        if (((reply >> 8) & 0x0F) != 0x01) {
            return SD_ERR_BAD_SUPPLY_V;
        }

        return SD_ERR_NOERR;

    } while (--retries >= 0);

    return SD_ERR_BAD_CARD;
}

// --------------------------------------------------------------------------------------------
SdErr_t SdCardSDIO::initializeCard()
{
    uint32_t reply;
    uint32_t t0 = time_us_32();
    const uint32_t timeout_us = 1000000;  // 1 second timeout for ACMD41

    do {
        // CMD55 - Application command prefix
        if (rp2350_sdio_command_u32(CMD55, 0, &reply, 0) != SDIO_OK) {
            return SD_ERR_NO_INIT;
        }

        // ACMD41 - Start initialization, indicate HC support
        // Must use SDIO_FLAG_NO_CRC | SDIO_FLAG_NO_CMD_TAG per library example
        // SDIO_CARD_OCR_MODE = bit 30 (HC support) | bit 28 (max performance) | bit 20 (3.3V)
        if (rp2350_sdio_command_u32(ACMD41, SDIO_CARD_OCR_MODE, &reply, SDIO_FLAG_NO_CRC | SDIO_FLAG_NO_CMD_TAG) != SDIO_OK) {
            return SD_ERR_NO_INIT;
        }

        // Check for timeout
        if ((time_us_32() - t0) > timeout_us) {
            return SD_ERR_NO_INIT;
        }

        // Check if initialization complete (bit 31 = 1)
        if (reply & 0x80000000) {
            // Success! Track init time for diagnostics
            uint32_t delta_mS = (time_us_32() - t0) / 1000;
            if (delta_mS > initTime_max_mS) {
                initTime_max_mS = delta_mS;
            }

            // Check if this is SDHC/SDXC (bit 30 = CCS)
            isSDHC = (reply & 0x40000000) != 0;

            return SD_ERR_NOERR;
        }

        // Not ready yet, wait 1ms before retry
        vTaskDelay(pdMS_TO_TICKS(1));

    } while (true);
}

// --------------------------------------------------------------------------------------------
SdErr_t SdCardSDIO::readCSD()
{
    uint32_t reply[4];

    // CMD9 - Send CSD register
    if (rp2350_sdio_command(CMD9, rca, reply, 16, SDIO_FLAG_NO_CRC | SDIO_FLAG_NO_CMD_TAG) != SDIO_OK) {
        return SD_ERR_IO;
    }

    // Copy response directly - SDIO library returns R2 response in correct byte order
    memcpy(regCSD, reply, 16);

    return calculateCapacity();
}

// --------------------------------------------------------------------------------------------
SdErr_t SdCardSDIO::init()
{
    isSDHC = false;
    rca = 0;

    // Check card present
    if (!cardPresent()) {
        return SD_ERR_NO_CARD;
    }

    // CRITICAL: Enable pullups BEFORE power-up delay
    // DAT3 (same as CS) MUST be high during card power-up to keep card in SDIO mode
    // If DAT3 is low or floating during power-up, card enters SPI mode and won't respond to SDIO commands
    gpio_init(SDIO_CLK);
    gpio_init(SDIO_CMD);
    gpio_init(SDIO_D0);
    gpio_init(SDIO_D1);
    gpio_init(SDIO_D2);
    gpio_init(SDIO_D3);
    gpio_set_dir(SDIO_CLK, GPIO_IN);
    gpio_set_dir(SDIO_CMD, GPIO_IN);
    gpio_set_dir(SDIO_D0, GPIO_IN);
    gpio_set_dir(SDIO_D1, GPIO_IN);
    gpio_set_dir(SDIO_D2, GPIO_IN);
    gpio_set_dir(SDIO_D3, GPIO_IN);
    gpio_pull_up(SDIO_CLK);
    gpio_pull_up(SDIO_CMD);
    gpio_pull_up(SDIO_D0);
    gpio_pull_up(SDIO_D1);
    gpio_pull_up(SDIO_D2);
    gpio_pull_up(SDIO_D3);

    // Wait for card power-up with pullups enabled
    vTaskDelay(pdMS_TO_TICKS(30));

    // Initialize SDIO at 300 kHz
    rp2350_sdio_timing_t timing = rp2350_sdio_get_timing(SDIO_INITIALIZE);
    rp2350_sdio_init(timing);

    // Re-enable pullups after init (library disables them when it configures pins for PIO)
    gpio_pull_up(SDIO_CLK);
    gpio_pull_up(SDIO_CMD);
    gpio_pull_up(SDIO_D0);
    gpio_pull_up(SDIO_D1);
    gpio_pull_up(SDIO_D2);
    gpio_pull_up(SDIO_D3);

    // Reset card
    SdErr_t err = resetCard();
    if (err != SD_ERR_NOERR) {
        return err;
    }

    // Card needs time to process reset before responding to CMD8
    vTaskDelay(pdMS_TO_TICKS(10));

    // Check voltage
    err = checkVoltage();
    if (err != SD_ERR_NOERR) {
        return err;
    }

    // Initialize card
    err = initializeCard();
    if (err != SD_ERR_NOERR) {
        return err;
    }

    // CMD2 - Get CID (don't store it, just part of init sequence)
    uint32_t cid[4];
    if (rp2350_sdio_command(CMD2, 0, cid, 16, SDIO_FLAG_NO_CRC | SDIO_FLAG_NO_CMD_TAG) != SDIO_OK) {
        return SD_ERR_IO;
    }

    // CMD3 - Get RCA (Relative Card Address)
    uint32_t reply;
    if (rp2350_sdio_command_u32(CMD3, 0, &reply, 0) != SDIO_OK) {
        return SD_ERR_IO;
    }
    rca = reply & 0xFFFF0000;

    // Read CSD before selecting card
    err = readCSD();
    if (err != SD_ERR_NOERR) {
        return err;
    }

    // CMD7 - Select card
    if (rp2350_sdio_command_u32(CMD7, rca, &reply, SDIO_FLAG_NO_CRC) != SDIO_OK) {
        return SD_ERR_IO;
    }

    // CMD55 + ACMD6 - Set bus width to 4-bit
    if (rp2350_sdio_command_u32(CMD55, rca, &reply, 0) != SDIO_OK) {
        return SD_ERR_IO;
    }
    if (rp2350_sdio_command_u32(ACMD6, 2, &reply, 0) != SDIO_OK) {  // 2 = 4-bit mode
        return SD_ERR_IO;
    }

    // CMD16 - Set block length to 512 bytes
    if (rp2350_sdio_command_u32(CMD16, 512, &reply, 0) != SDIO_OK) {
        return SD_ERR_IO;
    }

    // Negotiate clock speed based on SDIO_DEFAULT_SPEED config
    // Start with configured default speed (SDIO_HIGHSPEED = 50 MHz or SDIO_STANDARD = 25 MHz)
    rp2350_sdio_mode_t target_mode = SDIO_DEFAULT_SPEED;

    if (target_mode >= SDIO_HIGHSPEED) {
        // Try to negotiate high-speed mode with the card via CMD6
        // Pattern from library's initialization at lines 396-405

        // CMD6 argument for high-speed: 0x80FFFF01
        // Bit 31: Mode 1 (switch function)
        // Bits 3-0: Function group 1 = 1 (SDR25, 50 MHz high-speed)
        uint8_t status_buf[64] __attribute__((aligned(4)));

        if (rp2350_sdio_command_u32(CMD6, 0x80FFFF01, &reply, SDIO_FLAG_STOP_CLK) != SDIO_OK) {
            printf("CMD6 high-speed negotiation failed, falling back to 25 MHz\n");
            target_mode = SDIO_STANDARD;
        } else {
            // Read 64-byte status response
            if (rp2350_sdio_rx_start(status_buf, 1, 64) != SDIO_OK) {
                printf("CMD6 status read failed, falling back to 25 MHz\n");
                target_mode = SDIO_STANDARD;
            } else {
                sdio_status_t status;
                do {
                    status = rp2350_sdio_rx_poll(nullptr);
                } while (status == SDIO_BUSY);

                rp2350_sdio_stop();
                busy_wait_us_32(1000);  // Wait for function switch to complete

                if (status != SDIO_OK) {
                    printf("CMD6 response error, falling back to 25 MHz\n");
                    target_mode = SDIO_STANDARD;
                } else if (reply & 0x80) {
                    printf("Card rejected high-speed mode, falling back to 25 MHz\n");
                    target_mode = SDIO_STANDARD;
                } else {
                    printf("High-speed mode negotiated successfully\n");
                    // target_mode already set to SDIO_HIGHSPEED
                }
            }
        }
    }

    // Apply the negotiated clock speed
    timing = rp2350_sdio_get_timing(target_mode);
    rp2350_sdio_init(timing);

    // Re-enable pullups after speed change
    gpio_pull_up(SDIO_CLK);
    gpio_pull_up(SDIO_CMD);
    gpio_pull_up(SDIO_D0);
    gpio_pull_up(SDIO_D1);
    gpio_pull_up(SDIO_D2);
    gpio_pull_up(SDIO_D3);

    // Store and report the actual clock speed achieved
    clockFrequency_Hz = (target_mode == SDIO_HIGHSPEED) ? 50000000 :
    (target_mode == SDIO_STANDARD) ? 25000000 : 0;

    const char* mode_name = (target_mode == SDIO_HIGHSPEED) ? "50 MHz high-speed" :
    (target_mode == SDIO_STANDARD) ? "25 MHz standard" : "unknown";
    printf("SDIO clock: %s\n", mode_name);

    return SD_ERR_NOERR;
}

// --------------------------------------------------------------------------------------------
SdErr_t SdCardSDIO::testCard()
{
    uint8_t buffer[512] __attribute__((aligned(4)));

    printf("Testing SDIO card read access...\n");

    // Test first sector
    if (readSectors(0, 1, buffer) != SD_ERR_NOERR) {
        printf("SDIO test failed: could not read sector 0\n");
        return SD_ERR_IO;
    }

    // Test last sector
    uint32_t lastSector = getSectorCount() - 1;
    if (readSectors(lastSector, 1, buffer) != SD_ERR_NOERR) {
        printf("SDIO test failed: could not read last sector\n");
        return SD_ERR_IO;
    }

    printf("SDIO card read access test passed.\n");

    #if 0
    // Now run speed test
    return speedTest();
    #else
    return SD_ERR_NOERR;
    #endif
}


// --------------------------------------------------------------------------------------------
#define SPEEDTEST_NUM_BLOCKS 16  // Number of consecutive blocks to read for performance test

SdErr_t SdCardSDIO::speedTest()
{
    // Use static buffer for multi-block read (16 blocks = 8KB)
    // Static to avoid stack overflow and avoid need for delete
    static uint8_t speedTestBuffer[512 * SPEEDTEST_NUM_BLOCKS] __attribute__((aligned(4)));

    printf("\nRunning SDIO speed test (best-case: %d consecutive blocks)...\n", SPEEDTEST_NUM_BLOCKS);

    // === MULTI-BLOCK READ TEST (No PIO swapping - best case) ===
    uint32_t total_cmd_time = 0;
    uint32_t total_dma_start_time = 0;
    uint32_t total_poll_time = 0;
    uint32_t total_time = 0;

    printf("\n=== Best-Case Performance: %d Consecutive Reads (No PIO Swapping) ===\n", SPEEDTEST_NUM_BLOCKS);

    for (int i = 0; i < SPEEDTEST_NUM_BLOCKS; i++) {
        uint32_t block_addr = isSDHC ? i : (i * 512);
        uint32_t reply;

        // TIMING: Measure command time
        uint32_t t_cmd = time_us_32();
        if (rp2350_sdio_command_u32(CMD17, block_addr, &reply, SDIO_FLAG_STOP_CLK) != SDIO_OK) {
            printf("SDIO speed test failed: CMD17 failed at block %d\n", i);
            return SD_ERR_IO;
        }
        uint32_t cmd_time = time_us_32() - t_cmd;

        // TIMING: Measure DMA start time
        uint32_t t_dma = time_us_32();
        if (rp2350_sdio_rx_start(&speedTestBuffer[i * 512], 1, 512) != SDIO_OK) {
            printf("SDIO speed test failed: rx_start failed at block %d\n", i);
            return SD_ERR_IO;
        }
        uint32_t dma_start_time = time_us_32() - t_dma;

        // TIMING: Measure poll loop time
        uint32_t t_poll = time_us_32();
        sdio_status_t status;
        do {
            status = rp2350_sdio_rx_poll(nullptr);
        } while (status == SDIO_BUSY);
        uint32_t poll_time = time_us_32() - t_poll;

        if (status != SDIO_OK) {
            printf("SDIO speed test failed: transfer error at block %d\n", i);
            return SD_ERR_DATA_ERROR;
        }

        if (i < SPEEDTEST_NUM_BLOCKS) {
            printf("Block %2d: cmd=%3lu dma_start=%3lu poll=%3lu total=%4lu us\n",
                i, cmd_time, dma_start_time, poll_time, cmd_time + dma_start_time + poll_time);
            }

            total_cmd_time += cmd_time;
            total_dma_start_time += dma_start_time;
            total_poll_time += poll_time;
            total_time += (cmd_time + dma_start_time + poll_time);
        }

        // Calculate statistics
        uint32_t avg_cmd = total_cmd_time / SPEEDTEST_NUM_BLOCKS;
        uint32_t avg_dma_start = total_dma_start_time / SPEEDTEST_NUM_BLOCKS;
        uint32_t avg_poll = total_poll_time / SPEEDTEST_NUM_BLOCKS;
        uint32_t avg_total = total_time / SPEEDTEST_NUM_BLOCKS;
        uint32_t total_bytes = 512 * SPEEDTEST_NUM_BLOCKS;
        float throughput_kbps = (total_bytes / 1024.0) / (total_time / 1000000.0);

        printf("\n=== Best-Case Performance Summary (%d blocks = %d KB) ===\n", SPEEDTEST_NUM_BLOCKS, total_bytes / 1024);
        printf("Average per block:\n");
        printf("  Command:   %lu us\n", avg_cmd);
        printf("  DMA start: %lu us\n", avg_dma_start);
        printf("  Poll:      %lu us\n", avg_poll);
        printf("  Total:     %lu us\n", avg_total);
        printf("Total time: %lu us\n", total_time);
        printf("Throughput: %.2f KB/s (%.2f MB/s)\n\n", throughput_kbps, throughput_kbps / 1024.0);

        printf("SDIO speed test passed.\n");
        return SD_ERR_NOERR;
    }

    // --------------------------------------------------------------------------------------------
    // SDIO read implementation following library reference code pattern
    SdErr_t SdCardSDIO::readSectors(uint32_t sector_num, uint32_t num_sectors, void *buffer)
    {
        if (!operational()) {
            return SD_ERR_NOT_OPERATIONAL;
        }

        // Check alignment: buffer must be 4-byte aligned (library requirement)
        if (((uintptr_t)buffer & 3) != 0) {
            return SD_ERR_BAD_ARG;
        }

        uint32_t reply;
        sdio_status_t status;

        // Use block addressing for SDHC, byte addressing for SDSC
        uint32_t addr = isSDHC ? sector_num : (sector_num * 512);

        if (num_sectors == 1) {
            // Single-block read: CMD16 (SET_BLOCKLEN) + CMD17 (READ_SINGLE_BLOCK)
            // Pattern from library's read_single_sector() at lines 142-164
            if (rp2350_sdio_command_u32(CMD16, 512, &reply, 0) != SDIO_OK ||
            rp2350_sdio_command_u32(CMD17, addr, &reply, SDIO_FLAG_STOP_CLK) != SDIO_OK ||
            rp2350_sdio_rx_start((uint8_t*)buffer, 1, 512) != SDIO_OK) {
                rp2350_sdio_stop();
                return SD_ERR_IO;
            }

            do {
                status = rp2350_sdio_rx_poll(nullptr);
            } while (status == SDIO_BUSY);

            rp2350_sdio_stop();

            if (status != SDIO_OK) {
                return SD_ERR_DATA_ERROR;
            }
        } else {
            // Multi-block read: CMD18 (READ_MULTIPLE_BLOCK)
            // Pattern from library's readStart() + readData() at lines 711, 1159
            if (rp2350_sdio_command_u32(CMD18, addr, &reply, SDIO_FLAG_STOP_CLK) != SDIO_OK) {
                rp2350_sdio_stop();
                return SD_ERR_IO;
            }

            // Start DMA reception for all blocks
            if (rp2350_sdio_rx_start((uint8_t*)buffer, num_sectors, 512) != SDIO_OK) {
                rp2350_sdio_stop();
                return SD_ERR_IO;
            }

            // Poll until complete
            do {
                status = rp2350_sdio_rx_poll(nullptr);
            } while (status == SDIO_BUSY);

            // Send CMD12 (STOP_TRANSMISSION) for multi-block reads
            rp2350_sdio_command_u32(CMD12, 0, &reply, SDIO_FLAG_NO_LOGMSG);
            rp2350_sdio_stop();

            if (status != SDIO_OK) {
                return SD_ERR_DATA_ERROR;
            }
        }

        return SD_ERR_NOERR;
    }

    // --------------------------------------------------------------------------------------------
    // SDIO write implementation following library reference code pattern
    SdErr_t SdCardSDIO::writeSectors(uint32_t sector_num, uint32_t num_sectors, const void *buffer)
    {
        uint32_t t0 = time_us_32();

        if (!operational()) {
            return SD_ERR_NOT_OPERATIONAL;
        }

        // Check alignment: buffer must be 4-byte aligned (library requirement)
        if (((uintptr_t)buffer & 3) != 0) {
            return SD_ERR_BAD_ARG;
        }

        uint32_t reply;
        sdio_status_t status;

        // Use block addressing for SDHC, byte addressing for SDSC
        uint32_t addr = isSDHC ? sector_num : (sector_num * 512);

        if (num_sectors == 1) {
            // Single-block write: CMD16 (SET_BLOCKLEN) + CMD24 (WRITE_BLOCK)
            // Pattern from library's write_single_sector() at lines 190-201
            if (rp2350_sdio_command_u32(CMD16, 512, &reply, 0) != SDIO_OK ||
            rp2350_sdio_command_u32(CMD24, addr, &reply, SDIO_FLAG_STOP_CLK) != SDIO_OK ||
            rp2350_sdio_tx_start((const uint8_t*)buffer, 1, 512) != SDIO_OK) {
                rp2350_sdio_stop();
                return SD_ERR_IO;
            }

            do {
                status = rp2350_sdio_tx_poll(nullptr);
            } while (status == SDIO_BUSY);

            rp2350_sdio_stop();

            if (status != SDIO_OK) {
                return SD_ERR_WRITE_FAILURE;
            }
        } else {
            // Multi-block write: CMD25 (WRITE_MULTIPLE_BLOCK)
            // Pattern from library's writeStart() + writeData() at lines 861, 1026
            if (rp2350_sdio_command_u32(CMD25, addr, &reply, SDIO_FLAG_STOP_CLK) != SDIO_OK) {
                rp2350_sdio_stop();
                return SD_ERR_IO;
            }

            // Start DMA transmission for all blocks
            if (rp2350_sdio_tx_start((const uint8_t*)buffer, num_sectors, 512) != SDIO_OK) {
                rp2350_sdio_stop();
                return SD_ERR_IO;
            }

            // Poll until complete
            do {
                status = rp2350_sdio_tx_poll(nullptr);
            } while (status == SDIO_BUSY);

            if (status != SDIO_OK) {
                rp2350_sdio_stop();
                return SD_ERR_WRITE_FAILURE;
            }

            // Send CMD12 (STOP_TRANSMISSION) to terminate multi-block write
            // Pattern from library's stopTransmission() at line 799
            rp2350_sdio_command_u32(CMD12, 0, &reply, SDIO_FLAG_NO_LOGMSG);
            rp2350_sdio_stop();

            // Wait for card to exit data state and be ready for next operation
            // Pattern from library's stopTransmission() blocking wait at lines 812-822
            uint32_t start = time_us_32();
            while ((time_us_32() - start) < 1000000) {  // 1 second timeout
                // Check if DAT0 is high (card not busy)
                if (gpio_get(SD_DAT0) == 0) {
                    busy_wait_us_32(100);  // Wait a bit if still busy
                    continue;
                }

                // Get card status via CMD13
                uint32_t card_status;
                if (rp2350_sdio_command_u32(CMD13, rca, &card_status, 0) == SDIO_OK) {
                    // Extract CURRENT_STATE field (bits [12:9])
                    int state = (card_status >> 9) & 0x0F;
                    if (state != 5) {
                        // Card is out of data state (state 5), all done: ready for next operation
                        goto done;
                    }
                }
                busy_wait_us_32(100);
            }

            printf("writeSectors: timeout waiting for card to exit data state after multi-block write\n");
            return SD_ERR_WRITE_FAILURE;
        }

        done:
        uint32_t elapsed = time_us_32() - t0;
        if (false) {
            printf("%s: wrote %u sectors in %u us (%.2f KB/s)\n", __FUNCTION__, num_sectors, elapsed,
                (num_sectors * 512.0f) / (elapsed / 1000000.0f) / 1024.0f);
            }

            return SD_ERR_NOERR;
        }
