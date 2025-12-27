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
  uint32_t reply;

  // CMD0 - Reset card
  if (rp2350_sdio_command_u32(CMD0, 0, &reply, SDIO_FLAG_NO_CRC) != SDIO_OK) {
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
  bool done = false;

  uint32_t t0 = time_us_32();

  do {
    // CMD55 - Application command prefix
    if (rp2350_sdio_command_u32(CMD55, 0, &reply, 0) != SDIO_OK) {
      break;
    }

    // ACMD41 - Start initialization, indicate HC support
    if (rp2350_sdio_command_u32(ACMD41, 0x40300000, &reply, SDIO_FLAG_NO_CRC) != SDIO_OK) {
      break;
    }

    // Check if initialization complete (bit 31 = 1)
    done = (reply & 0x80000000) != 0;

    if (!done) {
      vTaskDelay(pdMS_TO_TICKS(1));
    }

  } while (!done);

  if (!done) {
    return SD_ERR_NO_INIT;
  }

  // Track init time for diagnostics
  uint32_t delta_mS = (time_us_32() - t0) / 1000;
  if (delta_mS > initTime_max_mS) {
    initTime_max_mS = delta_mS;
  }

  // Check if this is SDHC/SDXC (bit 30 = CCS)
  isSDHC = (reply & 0x40000000) != 0;

  return SD_ERR_NOERR;
}

// --------------------------------------------------------------------------------------------
SdErr_t SdCardSDIO::readCSD()
{
  uint32_t reply[4];

  // CMD9 - Send CSD register
  if (rp2350_sdio_command(CMD9, rca, reply, 16, SDIO_FLAG_NO_CRC | SDIO_FLAG_NO_CMD_TAG) != SDIO_OK) {
    return SD_ERR_IO;
  }

  // Response is 128 bits, stored big-endian in reply[3:0]
  // Reorder to match SdCard format (byte array, MSB first)
  for (int i = 0; i < 4; i++) {
    uint32_t word = reply[3 - i];
    regCSD[i * 4 + 0] = (word >> 24) & 0xFF;
    regCSD[i * 4 + 1] = (word >> 16) & 0xFF;
    regCSD[i * 4 + 2] = (word >> 8) & 0xFF;
    regCSD[i * 4 + 3] = (word >> 0) & 0xFF;
  }

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

  // Wait for card power-up
  vTaskDelay(pdMS_TO_TICKS(30));

  // Initialize SDIO at 300 kHz
  rp2350_sdio_timing_t timing = rp2350_sdio_get_timing(SDIO_INITIALIZE);
  rp2350_sdio_init(timing);

  // Re-enable pullups after init (library disables them)
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

  // Increase clock to 20 MHz (SDIO_MMC mode - proven stable)
  timing = rp2350_sdio_get_timing(SDIO_MMC);
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

  // Issue read command
  uint8_t cmd = (num_blocks == 1) ? CMD17 : CMD18;
  uint32_t reply;
  if (rp2350_sdio_command_u32(cmd, addr, &reply, 0) != SDIO_OK) {
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

  // Stop transfer
  rp2350_sdio_stop();

  return (status == SDIO_OK) ? SD_ERR_NOERR : SD_ERR_DATA_ERROR;
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

  // Issue write command
  uint8_t cmd = (num_blocks == 1) ? CMD24 : CMD25;
  uint32_t reply;
  if (rp2350_sdio_command_u32(cmd, addr, &reply, 0) != SDIO_OK) {
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

  // Stop transfer
  rp2350_sdio_stop();

  return (status == SDIO_OK) ? SD_ERR_NOERR : SD_ERR_WRITE_FAILURE;
}
