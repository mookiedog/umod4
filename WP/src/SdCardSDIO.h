#if !defined SDCARDSDIO_H
#define SDCARDSDIO_H

#include <stdint.h>
#include "SdCardBase.h"

// SdCardSDIO provides SDIO 4-bit interface to an SD flash card
// Provides ~20-25 MB/s throughput vs ~3 MB/s for SPI

class SdCardSDIO : public SdCardBase {
  public:
    /// @brief Create an object that provides SDIO 4-bit interface to an SD flash card
    /// @param cardPresentPad The pad/pin number of a GPIO used to detect if a card is present
    SdCardSDIO(int32_t cardPresentPad);

    SdErr_t init() override;

    // LittleFS interface
    SdErr_t read(lfs_block_t block_num, lfs_off_t off, void *buffer, lfs_size_t size) override;
    SdErr_t prog(lfs_block_t block_num, lfs_off_t off, const void *buffer, lfs_size_t size_bytes) override;
    SdErr_t erase(lfs_block_t block_num) override { return SD_ERR_NOERR; }  // Not needed for LittleFS
    SdErr_t sync() override { return SD_ERR_NOERR; }  // SDIO writes are synchronous

    bool cardPresent() override;
    uint32_t getBlockSize_bytes() override;
    uint32_t getCardCapacity_blocks() override;

    /// @brief Get the SD card interface mode name
    /// @return String describing the interface mode
    const char* getInterfaceMode() const override { return "SDIO 4-bit"; }

    /// @brief Get the SD card interface clock frequency in Hz
    /// @return Clock frequency in Hz
    uint32_t getClockFrequency_Hz() const override { return 25000000; }  // 25 MHz SDIO_STANDARD mode

  private:
    int32_t cardPresentPad;
    uint32_t blockSize_bytes;
    uint32_t capacity_blocks;
    uint64_t capacity_bytes;
    bool isSDHC;
    uint32_t initTime_max_mS;
    uint8_t regCSD[16];
    uint32_t rca;  // Relative Card Address assigned during init

    SdErr_t testCard() override;
    SdErr_t speedTest();
    SdErr_t resetCard();
    SdErr_t checkVoltage();
    SdErr_t initializeCard();
    SdErr_t readCSD();
    SdErr_t calculateCapacity();
};

#endif
