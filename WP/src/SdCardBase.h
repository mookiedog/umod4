#if !defined SDCARDBASE_H
#define SDCARDBASE_H

#include <stdint.h>

// SD errors are broadly defined as 0 means OK, negative numbers are errors
typedef int32_t SdErr_t;
#define SD_ERR_NOERR             0
#define SD_ERR_NO_CARD          -1    // Card is physically not present
#define SD_ERR_BAD_CARD         -2    // Either a SD 1.x card, or a bad card
#define SD_ERR_NCR_TIMEOUT      -3    // Card failed to respond to an SPI command within timeout Ncr
#define SD_ERR_NO_INIT          -4    // Card failed to report that it was in its initialization phase
#define SD_ERR_BAD_SUPPLY_V     -5    // Card does not support our supply voltage range
#define SD_ERR_BAD_RESPONSE     -6    // Catchall: Some part of the response was not as expected
#define SD_ERR_CRC              -7    // This is used when the CRC for a payload does not match

// These errors are generated specifically in response to data transfer requests:
#define SD_ERR_DATA_ERROR       -10   // A general 'Error' indication
#define SD_ERR_DATA_CC          -11   // A "CC Error", whatever that is
#define SD_ERR_DATA_ECC         -12   // An Error Correction Code error
#define SD_ERR_DATA_RANGE       -13   // The block address in the data request goes beyond the size of the card
#define SD_ERR_DATA_UNSPECIFIED -14   // Sadly, I see cards generating data error tokens without setting any of the specific error bits, above

// There are many ways that a write can fail. This is a catchall:
#define SD_ERR_WRITE_FAILURE    -16

#define SD_ERR_CSD_VERSION      -20   // only version 1 and 2 are defined; we don't support 3 or 4

#define SD_ERR_NOT_OPERATIONAL  -32   // The hotplug manager is not happy with the card
#define SD_ERR_BAD_ARG          -33   // Bad argument passed to an SdCard method
#define SD_ERR_IO               -34   // Some sort of IO error when performing SD access

#define SD_ERR_NOINIT   -99

/// @brief Abstract base class for SD card access
/// Provides common interface for both SPI and SDIO implementations
/// Interface is filesystem-agnostic - works with 512-byte sectors
class SdCardBase {
    public:
    // State machine for hotplug manager
    typedef enum {
        NO_CARD, MAYBE_CARD, POWER_UP, INIT_CARD,
        VERIFYING, OPERATIONAL
    } state_t;

    state_t state;

    virtual ~SdCardBase() {}

    virtual SdErr_t init() = 0;
    virtual SdErr_t testCard() = 0;

    // Pure sector-based interface (512-byte sectors)
    virtual SdErr_t readSectors(uint32_t sector_num, uint32_t num_sectors, void *buffer) = 0;
    virtual SdErr_t writeSectors(uint32_t sector_num, uint32_t num_sectors, const void *buffer) = 0;
    virtual SdErr_t sync() = 0;

    virtual bool cardPresent() = 0;
    virtual uint32_t getSectorSize() = 0;      // Always returns 512
    virtual uint32_t getSectorCount() = 0;     // Total sectors on card

    /// @brief Get the SD card interface mode name
    /// @return String describing the interface mode
    virtual const char* getInterfaceMode() const = 0;

    /// @brief Get the SD card interface clock frequency in Hz
    /// @return Clock frequency in Hz
    virtual uint32_t getClockFrequency_Hz() const = 0;

    /// @brief Test if the system is operational
    /// @return true if in VERIFYING or OPERATIONAL states
    bool operational() { return state >= VERIFYING; }
};

#endif
