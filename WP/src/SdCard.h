#if !defined SDCARD_H
#define SDCARD_H

#include <stdint.h>
#include "Spi.h"
#include "lfs.h"

// FreeRTOS tasks needs "C" linkage.
// The arg must be a pointer to a hotPlugMgrCfg_t object telling the hotPlugManager
// what SdCard to use and how to get the filesystem mounted.
extern "C" void hotPlugManager(void* arg);
#define HOTPLUG_MGR_STACK_SIZE_WORDS 1024

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

// Define the response bits in an R1 response. Note that some bits have multiple meanings!
#define R1_IN_IDLE_STATE              0x01
#define R1_ERASE_RESET                0x02
#define R1_ILLEGAL_CMD                0x04
#define R1_SWITCH_ERR                 0x04
#define R1_CRC_ERR                    0x08
#define R1_ERASE_SEQ_ERR              0x10
#define R1_ADDRESS_MISALIGN           0x20
#define R1_ADDRESS_OUT_OF_RANGE_ERR   0x40
#define R1_BLOCK_LENGTH_ERR           0x40

// Define various fields inside the CSD register in terms of their BigEndian start bit location and length.
// This first set of symbols have the same locations in both the V1 and V2 CSD structures:
#define REG_CSD_BITLEN                (16*8)            // CSD register is 16 bytes long
#define CSD_STRUCTURE_START           127
#define CSD_STRUCTURE_LENGTH          2
#define CSD_MAX_DATA_XFER_RATE        103
#define CSD_MAX_DATA_XFER_LENGTH      8
#define CSD_RD_BLK_LEN_START          83
#define CSD_RD_BLK_LEN_LENGTH         4

// These symbols are defined differently in V1 and V2 CSD structures:
#define CSD_V2_CSIZE_START            69
#define CSD_V2_CSIZE_LENGTH           22

#define CSD_V1_CSIZE_START            73
#define CSD_V1_CSIZE_LENGTH           12
#define CSD_V1_CSIZE_MULT_START       49
#define CSD_V1_CSIZE_MULT_LENGTH      3

/// @brief This struct defines how the hotPlugManager task will configure itself when it gets started.
/// sdCard: a pointer to the SdCard instance it should be using.
/// comingUp: a function pointer callback to a routine that gets invoked after a card is inserted and initialized. It should mount a filesystem.
/// goingDown: a function pointer callback to a routine that gets invoked if a card is removed
typedef struct {
  class SdCard* sdCard;
  bool (*comingUp)(class SdCard*);
  void (*goingDown)(class SdCard*);
} hotPlugMgrCfg_t;


class SdCard {
  public:
    /// @brief Create an object that provides an interface to an SD flash card. The card will be operated in SPI mode.
    /// @param spiSd A pointer to the Spi object connected to the SD card socket
    /// @param cardPresentPad The pad/pin number of a GPIO used to detect if a card is present. The specified GPIO pad will be configured with a pullup.
    /// the expectation is that the GPIO will be pulled to '0' if a card is present, else '1'.
    /// @param cs_pad The pad/pin number used to drive CS for the card socket.
    SdCard(Spi* spiSd, int32_t cardPresentPad, int32_t cs_pad);

    SdErr_t init();

    // These next four routines are needed to support littlefs operations.
    // We don't need to pass around the cfg structure because the SdCard class always contains a copy.
    SdErr_t read(lfs_block_t block_num, lfs_off_t off, void *buffer, lfs_size_t size);
    SdErr_t prog(lfs_block_t block_num, lfs_off_t off, const void *buffer, lfs_size_t size_bytes);
    SdErr_t erase(lfs_block_t block_num);
    SdErr_t sync();

    /// @brief Detect if a card is physically inserted in the socket.
    /// @return 'true' if a card is physically present, else 'false'
    bool cardPresent();

    /// @brief Determine the block size of the inserted card's capacity in bytes
    /// @return Block size in bytes. If no card is present, returns 0.
    uint32_t getBlockSize_bytes();

    /// @brief Determine the overall storage capacity of the inserted card measured in blocks
    /// @return Returns size of the card in blocks (1 block == 512 bytes). If no card is present, returns 0.
    uint32_t getCardCapacity_blocks();

    /// @brief The hotPlugManager is meant to be invoked as a FreeRTOS task. It will manage
    /// card insertion and removal events. Insertion events will get the new card initialized
    /// and ready for use.
    /// @param arg Points at the specific SdCard instance to be managed.
    static void hotPlugManager(void* arg);

  private:
    Spi* spi;
    int32_t csPad;
    int32_t cardPresentPad;

    uint32_t blockSize_bytes;
    uint32_t capacity_blocks;
    uint64_t capacity_bytes;

    bool isSDHC;

    uint32_t initTime_max_mS;
    uint32_t vMax_mV;
    uint32_t vMin_mV;

    uint32_t regOCR;
    uint8_t regCSD[16];

    uint8_t waitForData();
    typedef enum {KEEP_TRANSACTION_OPEN, CLOSE_TRANSACTION} transaction_t ;
    SdErr_t sendCmd(uint8_t cmd, uint32_t arg, uint8_t* responseBuf, int32_t responseLen, transaction_t termination=CLOSE_TRANSACTION);

    void endTransaction();

    SdErr_t calculateCapacity();
    SdErr_t resetCard();
    SdErr_t testCard();
    SdErr_t checkVoltage();
    SdErr_t initializeCard();
    SdErr_t readOCR();
    SdErr_t readCSD();
    SdErr_t writeBlkLen();


    // The VERIFYING state must be declared just before OPERATIONAL
    // as per the definition of operational(), below.
    typedef enum {
      NO_CARD, MAYBE_CARD, POWER_UP, INIT_CARD,
      VERIFYING, OPERATIONAL} state_t;
    state_t state;

    /// @brief Test if the system is operational. The system is defined to be operational if
    /// it is in either of the VERIFYING or OPERATIONAL states.
    /// @return
    bool operational() {return state >= VERIFYING;}
};

#endif
