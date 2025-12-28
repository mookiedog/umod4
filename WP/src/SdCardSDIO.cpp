// SDIO 4-bit SD Card Driver for WP (RP2350)
// Minimal implementation following SDIO_RP2350 library pattern
// Target: 20-25 MB/s throughput vs ~3 MB/s SPI

#include "SdCardSDIO.h"
#include "SdCard.h"  // For extract_bits_BE and CSD constants
#include "sdio/sdio_rp2350.h"
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "FreeRTOS.h"
#include "task.h"

// SD Commands
static const uint8_t CMD0  = 0;   // GO_IDLE_STATE
static const uint8_t CMD2  = 2;   // ALL_SEND_CID
static const uint8_t CMD3  = 3;   // SEND_RELATIVE_ADDR
static const uint8_t CMD7  = 7;   // SELECT_CARD
static const uint8_t CMD8  = 8;   // SEND_IF_COND
static const uint8_t CMD9  = 9;   // SEND_CSD
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
uint32_t SdCardSDIO::getBlockSize_bytes()
{
  return blockSize_bytes;
}

// --------------------------------------------------------------------------------------------
uint32_t SdCardSDIO::getCardCapacity_blocks()
{
  return capacity_blocks;
}

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

  // Increase clock to 25 MHz (SDIO_STANDARD mode - SD spec default)
  timing = rp2350_sdio_get_timing(SDIO_STANDARD);
  rp2350_sdio_init(timing);

  // Re-enable pullups again after speed change
  gpio_pull_up(SDIO_CLK);
  gpio_pull_up(SDIO_CMD);
  gpio_pull_up(SDIO_D0);
  gpio_pull_up(SDIO_D1);
  gpio_pull_up(SDIO_D2);
  gpio_pull_up(SDIO_D3);

  return SD_ERR_NOERR;
}

// --------------------------------------------------------------------------------------------
SdErr_t SdCardSDIO::testCard()
{
  uint8_t buffer[512] __attribute__((aligned(4)));

  printf("Testing SDIO card read access...\n");

  // Test first block
  if (read(0, 0, buffer, sizeof(buffer)) != SD_ERR_NOERR) {
    printf("SDIO test failed: could not read block 0\n");
    return SD_ERR_IO;
  }

  // Test last block
  uint32_t lastBlock = getCardCapacity_blocks() - 1;
  if (read(lastBlock, 0, buffer, sizeof(buffer)) != SD_ERR_NOERR) {
    printf("SDIO test failed: could not read last block\n");
    return SD_ERR_IO;
  }

  printf("SDIO card read access test passed.\n");

  // Now run speed test
  return speedTest();
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
// Minimal read implementation following library pattern
SdErr_t SdCardSDIO::read(lfs_block_t block_num, lfs_off_t off, void *buffer, lfs_size_t size)
{
  if (!operational()) {
    return SD_ERR_NOT_OPERATIONAL;
  }

  // Check alignment: size must be multiple of 512, buffer must be 4-byte aligned
  if ((size & 0x1FF) != 0 || ((uintptr_t)buffer & 3) != 0) {
    return SD_ERR_BAD_ARG;
  }

  uint32_t num_blocks = size / 512;

  // Use block addressing for SDHC, byte addressing for SDSC
  uint32_t addr = isSDHC ? block_num : (block_num * 512);

  // Use CMD17 for single block, CMD18 for multiple blocks (following library pattern)
  uint8_t cmd = (num_blocks == 1) ? CMD17 : CMD18;
  uint32_t reply;
  if (rp2350_sdio_command_u32(cmd, addr, &reply, SDIO_FLAG_STOP_CLK) != SDIO_OK) {
    rp2350_sdio_stop();
    return SD_ERR_IO;
  }

  // Start DMA reception
  if (rp2350_sdio_rx_start((uint8_t*)buffer, num_blocks, 512) != SDIO_OK) {
    rp2350_sdio_stop();
    return SD_ERR_IO;
  }

  // Simple poll loop (following library pattern)
  sdio_status_t status;
  do {
    status = rp2350_sdio_rx_poll(nullptr);
  } while (status == SDIO_BUSY);

  // Only call stop on error - successful transfers leave state ready for next operation
  if (status != SDIO_OK) {
    rp2350_sdio_stop();
    return SD_ERR_DATA_ERROR;
  }

  return SD_ERR_NOERR;
}

// --------------------------------------------------------------------------------------------
// Minimal write implementation following library pattern
SdErr_t SdCardSDIO::prog(lfs_block_t block_num, lfs_off_t off, const void *buffer, lfs_size_t size_bytes)
{
  if (!operational()) {
    return SD_ERR_NOT_OPERATIONAL;
  }

  // Check alignment: size must be multiple of 512, buffer must be 4-byte aligned
  if ((size_bytes & 0x1FF) != 0 || ((uintptr_t)buffer & 3) != 0) {
    return SD_ERR_BAD_ARG;
  }

  uint32_t num_blocks = size_bytes / 512;

  // Use block addressing for SDHC, byte addressing for SDSC
  uint32_t addr = isSDHC ? block_num : (block_num * 512);

  // Use CMD24 for single block, CMD25 for multiple blocks (following library pattern)
  uint8_t cmd = (num_blocks == 1) ? CMD24 : CMD25;
  uint32_t reply;
  if (rp2350_sdio_command_u32(cmd, addr, &reply, SDIO_FLAG_STOP_CLK) != SDIO_OK) {
    rp2350_sdio_stop();
    return SD_ERR_IO;
  }

  // Start DMA transmission
  if (rp2350_sdio_tx_start((const uint8_t*)buffer, num_blocks, 512) != SDIO_OK) {
    rp2350_sdio_stop();
    return SD_ERR_WRITE_FAILURE;
  }

  // Simple poll loop
  sdio_status_t status;
  do {
    status = rp2350_sdio_tx_poll(nullptr);
  } while (status == SDIO_BUSY);

  // Only call stop on error - successful transfers leave state ready for next operation
  if (status != SDIO_OK) {
    rp2350_sdio_stop();
    return SD_ERR_WRITE_FAILURE;
  }

  return SD_ERR_NOERR;
}
