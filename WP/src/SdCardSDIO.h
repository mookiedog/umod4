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

    // Sector-based interface (512-byte sectors)
    // Note: Legacy implementation, uses single-block transfers only (CMD17/CMD24)
    SdErr_t readSectors(uint32_t sector_num, uint32_t num_sectors, void *buffer) override;
    SdErr_t writeSectors(uint32_t sector_num, uint32_t num_sectors, const void *buffer) override;
    SdErr_t sync() override { return SD_ERR_NOERR; }  // SDIO writes are synchronous

    bool cardPresent() override;
    uint32_t getSectorSize() override { return 512; }
    uint32_t getSectorCount() override { return capacity_blocks; }

    /// @brief Get the SD card interface mode name
    /// @return String describing the interface mode
    const char* getInterfaceMode() const override { return "SDIO 4-bit"; }

    /// @brief Get the SD card interface clock frequency in Hz
    /// @return Clock frequency in Hz
    uint32_t getClockFrequency_Hz() const override { return clockFrequency_Hz; }

    private:
    int32_t cardPresentPad;
    uint32_t blockSize_bytes;
    uint32_t capacity_blocks;
    uint64_t capacity_bytes;
    bool isSDHC;
    uint32_t initTime_max_mS;
    uint8_t regCSD[16];
    uint32_t rca;  // Relative Card Address assigned during init
    uint32_t clockFrequency_Hz;  // Actual negotiated clock frequency

    SdErr_t testCard() override;
    SdErr_t speedTest();
    SdErr_t resetCard();
    SdErr_t checkVoltage();
    SdErr_t initializeCard();
    SdErr_t readCSD();
    SdErr_t calculateCapacity();
};

#endif
