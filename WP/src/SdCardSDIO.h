#if !defined SDCARDSDIO_H
#define SDCARDSDIO_H

#include <stdint.h>
#include "lfs.h"

// Forward declaration
class Spi;

// Error codes (same as SdCard.h)
typedef int32_t SdErr_t;
#define SD_ERR_NOERR             0
#define SD_ERR_NO_CARD          -1
#define SD_ERR_BAD_CARD         -2
#define SD_ERR_NCR_TIMEOUT      -3
#define SD_ERR_NO_INIT          -4
#define SD_ERR_BAD_SUPPLY_V     -5
#define SD_ERR_BAD_RESPONSE     -6
#define SD_ERR_CRC              -7
#define SD_ERR_DATA_ERROR       -10
#define SD_ERR_DATA_CC          -11
#define SD_ERR_DATA_ECC         -12
#define SD_ERR_DATA_RANGE       -13
#define SD_ERR_DATA_UNSPECIFIED -14
#define SD_ERR_WRITE_FAILURE    -16
#define SD_ERR_CSD_VERSION      -20
#define SD_ERR_NOT_OPERATIONAL  -32
#define SD_ERR_BAD_ARG          -33
#define SD_ERR_IO               -34

// SdCardSDIO provides SDIO 4-bit interface to an SD flash card
// Provides ~20-25 MB/s throughput vs ~3 MB/s for SPI
// Does NOT inherit from SdCard since it doesn't use SPI

class SdCardSDIO {
  public:
    /// @brief Create an object that provides SDIO 4-bit interface to an SD flash card
    /// @param cardPresentPad The pad/pin number of a GPIO used to detect if a card is present
    SdCardSDIO(int32_t cardPresentPad);

    SdErr_t init();

    // LittleFS interface
    SdErr_t read(lfs_block_t block_num, lfs_off_t off, void *buffer, lfs_size_t size);
    SdErr_t prog(lfs_block_t block_num, lfs_off_t off, const void *buffer, lfs_size_t size_bytes);
    SdErr_t erase(lfs_block_t block_num) { return SD_ERR_NOERR; }  // Not needed for LittleFS
    SdErr_t sync() { return SD_ERR_NOERR; }  // SDIO writes are synchronous

    bool cardPresent();
    uint32_t getBlockSize_bytes();
    uint32_t getCardCapacity_blocks();

  private:
    int32_t cardPresentPad;
    uint32_t blockSize_bytes;
    uint32_t capacity_blocks;
    uint64_t capacity_bytes;
    bool isSDHC;
    uint32_t initTime_max_mS;
    uint8_t regCSD[16];
    uint32_t rca;  // Relative Card Address assigned during init

    typedef enum {
      NO_CARD, MAYBE_CARD, POWER_UP, INIT_CARD,
      VERIFYING, OPERATIONAL
    } state_t;
    state_t state;

    bool operational() { return state >= VERIFYING; }

    SdErr_t testCard();
    SdErr_t resetCard();
    SdErr_t checkVoltage();
    SdErr_t initializeCard();
    SdErr_t readCSD();
    SdErr_t calculateCapacity();
};

#endif
