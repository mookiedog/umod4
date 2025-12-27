#if !defined SDCARD_H
#define SDCARD_H

#include <stdint.h>
#include "Spi.h"
#include "SdCardBase.h"

// FreeRTOS tasks needs "C" linkage.
// The arg must be a pointer to a hotPlugMgrCfg_t object telling the hotPlugManager
// what SdCard to use and how to get the filesystem mounted.
extern "C" void hotPlugManager(void* arg);
#define HOTPLUG_MGR_STACK_SIZE_WORDS 2048

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
/// sdCard: a pointer to the SdCardBase instance it should be using.
/// comingUp: a function pointer callback to a routine that gets invoked after a card is inserted and initialized. It should mount a filesystem.
/// goingDown: a function pointer callback to a routine that gets invoked if a card is removed
typedef struct {
  class SdCardBase* sdCard;
  bool (*comingUp)(class SdCardBase*);
  void (*goingDown)(class SdCardBase*);
} hotPlugMgrCfg_t;


class SdCard : public SdCardBase {
  public:
    /// @brief Create an object that provides an interface to an SD flash card. The card will be operated in SPI mode.
    /// @param spiSd A pointer to the Spi object connected to the SD card socket
    /// @param cardPresentPad The pad/pin number of a GPIO used to detect if a card is present. The specified GPIO pad will be configured with a pullup.
    /// the expectation is that the GPIO will be pulled to '0' if a card is present, else '1'.
    /// @param cs_pad The pad/pin number used to drive CS for the card socket.
    SdCard(Spi* spiSd, int32_t cardPresentPad, int32_t cs_pad);

    SdErr_t init() override;

    // These next four routines are needed to support littlefs operations.
    // We don't need to pass around the lfs_cfg structure because the SdCard class always contains a copy.
    SdErr_t read(lfs_block_t block_num, lfs_off_t off, void *buffer, lfs_size_t size) override;
    SdErr_t prog(lfs_block_t block_num, lfs_off_t off, const void *buffer, lfs_size_t size_bytes) override;
    SdErr_t erase(lfs_block_t block_num) override;
    SdErr_t sync() override;

    /// @brief Detect if a card is physically inserted in the socket.
    /// @return 'true' if a card is physically present, else 'false'
    bool cardPresent() override;

    /// @brief Determine the block size of the inserted card's capacity in bytes
    /// @return Block size in bytes. If no card is present, returns 0.
    uint32_t getBlockSize_bytes() override;

    /// @brief Determine the overall storage capacity of the inserted card measured in blocks
    /// @return Returns size of the card in blocks (1 block == 512 bytes). If no card is present, returns 0.
    uint32_t getCardCapacity_blocks() override;

    /// @brief Get the SD card interface mode name
    /// @return String describing the interface mode (e.g., "SPI")
    const char* getInterfaceMode() const override { return "SPI"; }

    /// @brief Get the SD card interface clock frequency in Hz
    /// @return Clock frequency in Hz, or 0 if unknown
    uint32_t getClockFrequency_Hz() const override { return 25000000; }  // 25 MHz for SPI

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
    SdErr_t testCard() override;
    SdErr_t checkVoltage();
    SdErr_t initializeCard();
    SdErr_t readOCR();
    SdErr_t readCSD();
    SdErr_t writeBlkLen();
};

#endif
