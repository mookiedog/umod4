// General Notes
//
// This driver is designed to work with SDSC/SDHC/SDXC version 2.00 or later cards.
// Simplistically, if a card's capacity is between 1G and 2TB, it should work with this driver.
//
// Supported:
//   - SDSC (Standard Capacity) cards:
//        - SDSC cards have a capacity up to 2GB
//   - SDHC or SHXC (High Capacity) cards:
//        - SDHC cards have a capacity of >2GB to 32GB, so in practical terms, 4GB to 32GB
//        - SDXC cards have a capacity of >32GB to 2TB, so in practical terms: 64GB to 2TB
//
// Not Supported:
//    - SD version 1.X cards: these are tiny, ancient, and not available in the marketplace.
//    - Ultra Capacity SDUC cards: They do not implement an SPI interface.
//

#include <stdio.h>
#include "SdCard.h"

#include "string.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "Crc.h"

#include "NeoPixelConnect.h"
extern NeoPixelConnect* rgb_led;
extern void hello(int32_t count);
extern void pico_set_led(bool on);

#if 0
#define BREAKPT() __breakpoint();
#else
#define BREAKPT()
#endif

static const uint32_t dbg = 0;

// The command packets all start with a '0' bit (the 'start bit'),
// followed by a '1' bit (the 'transmission bit'), then the 6-bit 'command index'.
// To simplify construction of command packets, we define the command values in a way
// to precalculate the first byte including the start and transmission bits (0x40):
static const uint8_t CMD0      = (0x40+0);        // GO_IDLE_STATE
static const uint8_t CMD1      = (0x40+1);        // SEND_OP_COND
static const uint8_t CMD8      = (0x40+8);        // SEND_IF_COND
static const uint8_t CMD9      = (0x40+9);        // SEND_CSD
static const uint8_t CMD10     = (0x40+10);       // SEND_CID
static const uint8_t CMD12     = (0x40+12);       // STOP_TRANSMISSION
static const uint8_t CMD13     = (0x40+13);       // SEND_STATUS
static const uint8_t CMD16     = (0x40+16);       // WRITE_BLOCKLEN (SDSC cards only!)
static const uint8_t CMD17     = (0x40+17);       // READ_SINGLE_BLOCK
static const uint8_t CMD18     = (0x40+18);       // READ_MULTIPLE_BLOCK
static const uint8_t CMD24     = (0x40+24);       // WRITE_BLOCK
static const uint8_t CMD25     = (0x40+25);       // WRITE_MULTIPLE_BLOCK
static const uint8_t CMD55     = (0x40+55);       // APP_CMD
static const uint8_t CMD58     = (0x40+58);       // READ_OCR

// These ACMDxx commands must be prefixed by sending a CMD55 first:
static const uint8_t ACMD41    = (0x40+41);       // SD_SEND_OP_COND

// Define the response tokens in the form of complete bytes instead of just the 3-bit field.
// This makes them easier to compare when they arrive.
#define SD_RESPONSE_TOKEN_DATA_ACCEPTED      (0X05)
#define SD_RESPONSE_TOKEN_REJECTED_CRC       (0x0B)
#define SD_RESPONSE_TOKEN_REJECTED_WRERR     (0x0D)

// ----------------------------------------------------------------------------------
// Extract a big-endian bitfield from an array of bytes.
//
// The SD spec defines all of its register bit fields in terms of big-endian bit numbering.
// For example, a 32-bit OCR field would be transferred as 4 bytes, where B31 would refer to
// the MS bit of the first byte and B0 would refer to the LS bit of the fourth byte.

// Example: extracting a field starting at big-endian B14 for a count of 13 from a array of length 32 bits
// would result in a 32-bit value of xxxx xxxx xxxx xxxx xx1 2345 6789 ABCD:
//    byte0   xxxx xxxx
//    byte1   xxxx xx12
//    byte2   3456 789A
//    byte3   BCDx xxxx
uint32_t extract_bits_BE(uint8_t* data, int32_t dataLen_bits, int32_t be_start_bit, int32_t num_bits)
{
    if ((be_start_bit >= dataLen_bits) || (be_start_bit - num_bits) < 0) {
        panic("Extraction field extends outside source array");
    }

    uint32_t bits_remaining = num_bits;
    uint8_t* p = &data[(((dataLen_bits-1)/8) - (be_start_bit/8))];

    uint32_t value = 0;

    if (bits_remaining != 8) {
        // Extract low-order bits from the starting byte, from msb down to the lsb, but not past bit 0.
        // We will strip off the bits that extend past msb in the last step.
        int32_t msb = be_start_bit & 7;
        int32_t lsb = msb - num_bits + 1;
        if (lsb<0) {
            lsb = 0;
        }

        value = *p++;
        value >>= lsb;
        bits_remaining -= (msb-lsb+1);
    }

    while (bits_remaining >= 8) {
        value = (value<<8) | *p++;
        bits_remaining -= 8;
    }

    // Extract high-order bits from the end byte, if any
    if (bits_remaining > 0) {
        value = (value << bits_remaining);
        uint32_t final_bits = *p >> (8-bits_remaining);
        final_bits &= ((1<<bits_remaining)-1);
        value |= final_bits;
    }

    value &= (1<<num_bits)-1;
    return value;
}

// ----------------------------------------------------------------------------------
// Removed getBlockSize_bytes() and getCardCapacity_blocks() - now inline in header as getSectorSize() and getSectorCount()
// ----------------------------------------------------------------------------------
// Perform some simple interactions with the card as a operational sanity check
SdErr_t SdCard::testCard()
{
    SdErr_t err;
    uint8_t buffer[512];

    err = checkVoltage();
    if (err != SD_ERR_NOERR) {
        BREAKPT();
        return err;
    }

    for (uint32_t i=0; i<4; i++) {
        err = readCSD();
        if (err != SD_ERR_NOERR) {
            BREAKPT();
            return err;
        }
    }

    for (uint32_t i=0; i<4; i++) {
        err = readOCR();
        if (err != SD_ERR_NOERR) {
            BREAKPT();
            return err;
        }
    }

    uint32_t sectorCount = getSectorCount();

    uint32_t t0 = time_us_32();

    const uint32_t blockCount = 32;
    for (int i=0; i<blockCount; i++) {
        // Read forwards starting from the first sector on the device
        err = readSectors(i, 1, buffer);
        if (err != SD_ERR_NOERR) {
            BREAKPT();
            return err;
        }

        // Read backwards starting from the final sector on the device.
        // There should be no problems
        err = readSectors(sectorCount-1-i, 1, buffer);
        if (err != SD_ERR_NOERR) {
            BREAKPT();
            return err;
        }
    }

    uint32_t elapsed_usec = time_us_32() - t0;
    printf("%s: Elapsed time to read %d blocks: %d uSec\n", __FUNCTION__, blockCount*2, elapsed_usec);
    #if 0
    // Well, this turned into a real shit show.
    // It would appear that a variety of cards deal with errors very poorly.
    // Some cards wedge up to the point that they need a power cycle.
    // The moral of the story is that it is just not worth trying to create an error.
    //
    // Attempt to read one past the last block on the device.
    // Sadly, there is no consistency about how to deal with this:
    //    - some cards report this as a data range error
    //    - some cards send an empty data error token
    //    - some cards simply time out on the transfer request
    err = read(blkCount, 0, buffer, sizeof(buffer));
    if (err != SD_ERR_NOERR) {
        // We were expecting *some* kind of error. Given that there is no consistency as to how this
        // error gets reported, we'll treat any error as being "success":
        err = SD_ERR_NOERR;
    }
    else {
        BREAKPT();
        // If the card didn't complain, we consider that as an error:
        if (err == SD_ERR_NOERR) {
            err = SD_ERR_BAD_RESPONSE;
        }
        return err;
    }

    // We need to make sure we can recover from the read failure in the previous test.
    // There are cards that get into an annoying variety of bad places after an error.
    // Specifically, some cards seem to issue an error on the *next* read command.
    // Therefore, after an error, we issue a SEND_STATUS command to give the card
    // a chance to complain on an operation that is not a READ.
    uint8_t r2[2];
    err = sendCmd(CMD13, 0, r2, sizeof(r2));
    if (err == SD_ERR_NOERR) {
        // The card might have reported one or more of PARAMETER ERROR, ADDRESS ERROR, or OUT_OF_RANGE
        // Seeing none of those is a problem:
        if (((r2[0] & 0x60) == 0x00) || ((r2[1] & 0x80) == 0x00)) {
            BREAKPT();
            err = SD_ERR_BAD_RESPONSE;
        }
    }

    // Sadly, after the out-of-range read access, a Transcend 128G Endurance card will:
    //    - report an appropriate error on the SEND_STATUS cmd,
    //    - respond to a second SEND_STATUS with all errors cleared,
    //    - but never respond to a valid read command ever again :(


    int32_t retries = 20;
    do {
        err = read(0, 0, buffer, sizeof(buffer));
        endTransaction();  // this shouldn't be required
        if (err == SD_ERR_NOERR) {
            break;
        }
    } while (--retries >= 0);
    #endif

    return err;
}

// ----------------------------------------------------------------------------------
// Warning: This routine is meant to be executed as a FreeRTOS task: it never returns!
void SdCard::hotPlugManager(void* arg)
{
    hotPlugMgrCfg_t* hotPlug_cfg;
    SdCardBase* sdCard;

    int32_t verifyPresenceCount;
    SdErr_t sdErr;
    int32_t initRetries;

    hotPlug_cfg = static_cast<hotPlugMgrCfg_t*>(arg);
    if (!hotPlug_cfg) {
        panic("hotPlugManager: null hotPlug_cfg ptr");
    }

    sdCard = hotPlug_cfg->sdCard;
    if (!sdCard) {
        panic("hotPlugManager: null SdCard ptr");
    }

    sdCard->state = NO_CARD;

    while (1) {
        switch (sdCard->state) {
            case NO_CARD:
            // Turn the card power off
            // Turn the LED RED
            rgb_led->neoPixelSetValue(0, 16, 0, 0, true);

            // The 'card present' signal indicates that a card is physically present in the socket.
            if (sdCard->cardPresent()) {
                verifyPresenceCount = 20;
                sdCard->state = MAYBE_CARD;
            }
            else {
                // If no card is present, we poll 10 times a second in case one gets inserted
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            break;

            case MAYBE_CARD:
            if (!sdCard->cardPresent()) {
                sdCard->state = NO_CARD;
            }
            else {
                // Delay between presence checks
                vTaskDelay(pdMS_TO_TICKS(10));
                verifyPresenceCount--;
                if (verifyPresenceCount<0) {
                    // The socket has reported a card being 'present' for enough times that we trust
                    // it and are ready to boot it up.
                    sdCard->state = POWER_UP;
                }
            }
            break;

            case POWER_UP:
            // V9 Spec 6.1.4.2
            // When power-cycling a card, the host needs to keep card supply voltage below 0.5V for more than 1 mSec.
            // Turn LED WHITE
            rgb_led->neoPixelSetValue(0, 10, 10, 10, true);
            vTaskDelay(pdMS_TO_TICKS(200));

            // From a hardware perspective, the card supply voltage needs to ramp up
            // no faster than 100 uSec and no slower than 35 mSec.
            // Once the supply voltage is stable, we need to wait at least 1 mSec
            // before talking to the card.
            //poweron();
            vTaskDelay(pdMS_TO_TICKS(50));
            sdCard->state = INIT_CARD;
            break;

            case INIT_CARD:
            initRetries = 10;
            do {
                // BLUE indicates we are init'ing the card
                rgb_led->neoPixelSetValue(0, 0, 0, 16, true);
                vTaskDelay(pdMS_TO_TICKS(200));
                sdErr = sdCard->init();

                if (sdErr == SD_ERR_NOERR) {
                    // No error after init turns LED GREEN
                    rgb_led->neoPixelSetValue(0, 0, 16, 0, true);
                    vTaskDelay(pdMS_TO_TICKS(200));
                    break;
                }
                else {
                    // error: set the LED RED
                    rgb_led->neoPixelSetValue(0, 10, 0, 0, true);
                    vTaskDelay(pdMS_TO_TICKS(200));
                }
            } while (--initRetries >= 0);

            if (sdErr != SD_ERR_NOERR) {
                // Multiple attempts at card initialization failed.
                // Power cycle the card (if possible), and keep retrying the init
                sdCard->state = NO_CARD;
            }
            else {
                sdCard->state = VERIFYING;
            }
            break;

            case VERIFYING:
            // The card intialized properly.
            // Run a few simple tests to verify that it seems operational.
            sdErr = sdCard->testCard();
            if (sdErr == SD_ERR_NOERR) {
                // The tests passed: invoke the callback to tell the system that a card is now online and usable
                if (hotPlug_cfg->comingUp(sdCard)) {
                    // PURPLE is GOOD!
                    rgb_led->neoPixelSetValue(0, 16, 0, 16, true);
                    sdCard->state = OPERATIONAL;
                }
                else {
                    // Give the card a long 5-second rest before retrying.
                    // This should never happen...
                    vTaskDelay(pdMS_TO_TICKS(5000));
                    sdCard->state = NO_CARD;
                }
            }
            else {
                sdCard->state = NO_CARD;
            }
            break;

            case OPERATIONAL:
            // Check periodically to make sure the card is still present
            if (!sdCard->cardPresent()) {
                sdCard->state = NO_CARD;
                hotPlug_cfg->goingDown(sdCard);
            }
            else {
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            break;

            default:
            panic("vFsTask: Illegal state");
        }
    }
}

// --------------------------------------------------------------------------------------------
SdCard::SdCard(Spi* _spi, int32_t _cardPresentPad, int32_t _csPad)
{
    spi = _spi;
    cardPresentPad = _cardPresentPad;
    csPad = _csPad;

    // Init the card detection signal with a pullup.
    // If a card is present, it will pull this pad to GND.
    gpio_init(cardPresentPad);
    gpio_set_dir(cardPresentPad, GPIO_IN);
    gpio_pull_up(cardPresentPad);

    gpio_init(csPad);
    gpio_set_dir(csPad, GPIO_OUT);
    spi->deassertCs(csPad);

    vMin_mV = 0;
    vMax_mV = 0;

    initTime_max_mS = 0;
    isSDHC = false;
}


// --------------------------------------------------------------------------------------------
// The SD socket grounds the CARD GPIO when a card is present.
bool SdCard::cardPresent()
{
    bool present = (gpio_get(cardPresentPad) == 0);
    return present;
}


// --------------------------------------------------------------------------------------------
// The spec is not entirely clear about how one should wait for data or errors.
// It would appear that after issuing a command, the card will return 'FF' bytes
// for as many as Ncr bytes before responding. The response can take 3 forms:
// 0b11111111: all 1 bits means that no response was detected before the Ncr timeout
// 0b11111110: a single zero bit in the LS bit position indicates that a data block
//             is about to arrive
// 0b0000xxx1: an error token indicating that the device is not going to send data

uint8_t SdCard::waitForData()
{
    uint8_t response;

    // The amount of time to wait for Nac is not clear to me.
    // It would appear to be typical for a card to respond very slowly to the first couple
    // of accesses. After that, they seem to respond much more quickly.
    // One SanDisk64G card I have takes over 6 mSec to respond to the second of two consecutive reads.
    // Worse than that, if you give up too soon before the data arrives, that particular Sandisk card
    // gets locked into some internal state that will not even respond to CMD0 reset commands.
    // The only recourse is a power cycle.
    int32_t Nac = 500;
    SdErr_t err = SD_ERR_NOERR;

    do {
        spi->rx(&response, 1);
        // 0b11111110 indicates that the data we are waiting for will start with the next byte
        // 0b0000xxxx represents an error token, indicating that our expected data will not be arriving
        if ((response == 0xFE) || ((response & 0xF0) == 0x00)) {
            return response;
        }
    } while (--Nac >= 0);

    return 0xFF;
}

// --------------------------------------------------------------------------------------------
void SdCard::endTransaction()
{
    spi->deassertCs(csPad);
    uint8_t junk = 0xFF;
    spi->tx(&junk, 1);
}

// --------------------------------------------------------------------------------------------
// sendCmd() always asserts CS before transmitting.
// By default, the atTermination parameter will complete the command packet by deasserting CS
// and then pumping 8 clocks with CS deasserted, as per the spec.
//
// Any error in this routine will terminate the transaction regardless of atTermination.
//
SdErr_t SdCard::sendCmd(uint8_t cmd, uint32_t arg, uint8_t* responseBuf, int32_t responseLen, transaction_t atTermination)
{
    SdErr_t err = SD_ERR_NOERR;

    // A command packet (CMD + ARG + CRC7) is always 6 bytes in length
    uint8_t buf[6];

    buf[0] = cmd;

    // Arg is sent MSbyte first
    buf[1] = (arg >> 24);
    buf[2] = (arg >> 16);
    buf[3] = (arg >>  8);
    buf[4] = (arg >>  0);

    uint8_t crc7 = 0;
    for (uint8_t i=0; i<(sizeof(buf)-1); i++) {
        crc7 = Crc::crc7_byte(crc7, buf[i]);
    }

    // The 7-bit CRC occupies the most significant 7 bits of the final byte.
    // The least significant bit of the final byte in the command packet is always set to '1' as an 'end bit'.
    buf[5] = (crc7 << 1) | 0x01;

    spi->assertCs(csPad);
    spi->tx(buf, sizeof(buf));

    // If we just sent a CMD12 (STOP_TRANSMISSION) to interrupt a multiple block read
    // we have a special case to deal with:
    if (cmd == CMD12) {
        // The way the full-duplex SPI bus works, while we were sending the CMD12 above, the card was
        // already sending back data for the next block (that we don't even want).
        // Once the card receives our STOP command, it takes the card a couple of bit transmission
        // times to get the data read transmission shut down. The short story is that the next byte
        // we receive will contain junk as the read shuts down. We must ignore that byte to
        // avoid confusing it for the CMD12's R1 response:
        uint8_t junk;
        spi->rx(&junk, 1);
    }

    // 'Ncr' defines the number of non-response bytes that the card is allowed to send after
    // receiving the last command byte and before the first byte of the response must appear.
    // The Simplified SPI Spec defines that Ncr is a minimum of 1 byte and a maximum of 8 bytes.
    // Real life shows that some cards violate this spec, so we use the value below instead.
    int32_t Ncr = 20;
    do {
        spi->rx(responseBuf, 1);
        // The response byte is identified because its MSbit will be '0'.
        if ((*responseBuf & 0x80) == 0) {
            break;
        }
    } while (--Ncr > 0);

    if (Ncr < 0) {
        BREAKPT();
        err = SD_ERR_NCR_TIMEOUT;
    }
    else if (--responseLen > 0) {
        spi->rx(responseBuf+1, responseLen);
    }

    if ((atTermination == CLOSE_TRANSACTION) || (err != SD_ERR_NOERR)) {
        endTransaction();
    }

    return err;
}


// --------------------------------------------------------------------------------------------
// Decode the CSD data to determine the capacity of the Card.
SdErr_t SdCard::calculateCapacity()
{
    SdErr_t err = SD_ERR_NOERR;
    uint32_t csize;

    uint32_t csdStructure = extract_bits_BE(regCSD, REG_CSD_BITLEN, CSD_STRUCTURE_START, CSD_STRUCTURE_LENGTH);
    if (csdStructure > 1) {
        BREAKPT();
        err = SD_ERR_CSD_VERSION;
    }
    else {
        // The read block length is interpreted as 2**N. For example, a value of 9 means a read block is 2**9 or 512 bytes long.
        // The spec indicates that the only valid values are 9 (512 B), 10 (1024 B), 11 (2048 B).
        uint32_t rdBlkLen = extract_bits_BE(regCSD, REG_CSD_BITLEN, CSD_RD_BLK_LEN_START, CSD_RD_BLK_LEN_LENGTH);
        // This is dangerous, but here we go anyway: if a card reports anything outside the appropriate settings,
        // force the block size to be 512 bytes (2**9)
        if ((rdBlkLen < 9) || (rdBlkLen > 11)) {
            BREAKPT();
            rdBlkLen = 9;
        }
        blockSize_bytes = (1 << rdBlkLen);

        if (csdStructure == 0) {
            // This is an SDSC card
            // This calc is only good for the old V1 (non-SDHC) SD card standard.
            uint32_t csize = extract_bits_BE(regCSD, REG_CSD_BITLEN, CSD_V1_CSIZE_START, CSD_V1_CSIZE_LENGTH);
            uint32_t raw_c_size_mult = extract_bits_BE(regCSD, REG_CSD_BITLEN, CSD_V1_CSIZE_MULT_START, CSD_V1_CSIZE_MULT_LENGTH);
            uint32_t c_size_mult = 1 << (raw_c_size_mult + 2);

            capacity_blocks = (csize+1) * c_size_mult;
            capacity_bytes = capacity_blocks * blockSize_bytes;
        }
        else {
            // This is an SDHC/SDXC card
            uint32_t csize = extract_bits_BE(regCSD, REG_CSD_BITLEN, CSD_V2_CSIZE_START, CSD_V2_CSIZE_LENGTH);

            if (blockSize_bytes != 512) {
                // This is not cool since all SDHC/SDXC cards use 512 byte blocks!
                // Force the blocksize to the correct size
                BREAKPT();
                blockSize_bytes = 512;
            }

            capacity_blocks = (csize+1) * 1024;
            capacity_bytes = (uint64_t)capacity_blocks * blockSize_bytes;
        }
    }

    return err;
}


// --------------------------------------------------------------------------------------------
SdErr_t SdCard::readCSD()
{
    SdErr_t err;
    uint8_t r1;
    uint8_t buff[18];

    // Read the CSD information. This is used to establish the card's memory capacity.
    memset(buff, 0, sizeof(buff));
    memset(regCSD, 0, sizeof(regCSD));
    err = sendCmd(CMD9, 0, &r1, sizeof(r1), KEEP_TRANSACTION_OPEN);
    if (err != SD_ERR_NOERR) {
        BREAKPT();
        return err;
    }
    else if ((r1 & 0xFE) != 0) {
        endTransaction();
        BREAKPT();
        return SD_ERR_IO;
    }

    bool success = false;
    uint8_t response = waitForData();
    if (response == 0xFE) {
        // We got a start-of-data token. Read the 16 bytes of CSD data plus the 2 CRC bytes in one go.
        // Some cards are OK if you don't bother to read the CRC.
        // Others cards care very much: not reading the CRC will mess up their next transaction.
        spi->rx(&buff[0], 18);
        endTransaction();

        if (((buff[0] & ~0x40) == 0x00) &&
        ((buff[3]==0x32) || (buff[3]==0x5A) || (buff[3]==0x0B) || (buff[3]==0x2B))) {

            isSDHC = ((buff[0] & 0x40) != 0);
            // Save the CSD data, ignoring the CRC bytes
            memcpy(regCSD, buff, sizeof(regCSD));
            calculateCapacity();
            success = true;
        }
    }

    return success ? SD_ERR_NOERR : SD_ERR_BAD_RESPONSE;
}


#if 0
// --------------------------------------------------------------------------------------------
SdErr_t SdCard::writeBlkLen()
{
    SdErr_t err;
    uint8_t r1;

    err = sendCmd(CMD16, 512, &r1, sizeof(r1));
    return err;
}
#endif

// --------------------------------------------------------------------------------------------
SdErr_t SdCard::resetCard()
{
    SdErr_t err = SD_ERR_NOERR;
    uint8_t r1;
    int32_t retries = 3;

    do {
        // Put the SD Card into SPI mode by sending a CMD0 (Software Reset) command with CS asserted.
        // This will put the card into an idle state.
        err = sendCmd(CMD0, 0, &r1, sizeof(r1));
        if (err == SD_ERR_NOERR) {
            // At this point, B0 should be '1' indicating that the card is in its initialization phase
            if ((r1 & 0x01) != 0x01) {
                BREAKPT();
                err = SD_ERR_NO_INIT;
            }
        }

        if (err != SD_ERR_NOERR) {
            // Put in a small delay to allow the card to see CS get deasserted before we retry
            busy_wait_us_32(10);
        }
    } while ((err != SD_ERR_NOERR) && (--retries >= 0));

    return err;
}

// --------------------------------------------------------------------------------------------
// This routine currently assumes that the card is being operated on a 3.3V supply.
SdErr_t SdCard::checkVoltage()
{
    uint8_t r3[5];
    uint8_t r7[5];
    int32_t retries = 3;
    SdErr_t err = SD_ERR_NOERR;

    // CMD8 arg: specifies what voltage is being supplied to the card. The card response will let us know
    // if it can operate at that voltage.
    uint32_t arg =
    // upper 20 bits are reserved, set to '0'
    (0x1 << 8) |  // 4 bit field where 0b0001 means that we will supply a voltage range of 2.7 to 3.6V
    (0xAA<< 0) ;  // An apparently arbitrary 8 bit pattern that will be echoed in the CMD response


    do {

        // An R7 response is a 5-byte response:
        //   - the first byte is the same as an R1 response
        //   - the next 4 bytes response bytes are specific to an R7
        err = sendCmd(CMD8, arg, r7, sizeof(r7));

        if (err == SD_ERR_NOERR) {
            // The first byte of the response is a type R1 response
            if (r7[0] & R1_ILLEGAL_CMD) {
                // If the card considers CMD8 to be an illegal command, it is either a V1.X SD memory card, or
                // it is not an SD memory card at all. Either way, this driver does not support it!
                BREAKPT();
                err = SD_ERR_BAD_CARD;
            }
            else if ((r7[0] & 0xFE) != 0x00) {
                BREAKPT();
                err = SD_ERR_BAD_RESPONSE;
            }
            else if ((r7[1] != 0) || (r7[2] != 0) || ((r7[3]&0xF0)!= 0)) {
                BREAKPT();
                err = SD_ERR_BAD_RESPONSE;
            }
            else if (r7[3] != 0x01) {
                BREAKPT();
                err = SD_ERR_BAD_SUPPLY_V;
            }
            else if (r7[4] != 0xAA) {
                BREAKPT();
                err = SD_ERR_BAD_RESPONSE;
            }
        }

        if (err != SD_ERR_NOERR) {
            // Put in a small delay to allow the card to see CS get deasserted before we retry
            busy_wait_us_32(10);
        }
    } while ((err != SD_ERR_NOERR) && (--retries >= 0));

    if (err != SD_ERR_NOERR) {
        return err;
    }

    // If we get here, everything is fine: This must be a V2.00 SDSC card or an even more
    // modern SDHC/SDXC Memory card that can operate withing our supply voltage range.

    // The next step is to issue a READ_OCR/CMD58 to verify that the card can use the
    // Vdd range we are providing. This seems peculiar since the CMD8 response would
    // seem to have done the same thing already, but this is what the spec says to do.
    // A CMD58 requires 4 bytes of zeros as an argument.
    err = sendCmd(CMD58, 0, r3, sizeof(r3));

    if (err == SD_ERR_NOERR) {
        // The first byte of the response is a type R1 response
        if (r3[0] & R1_ILLEGAL_CMD) {
            // This is either a V1.X SD memory card, or it is not an SD memory card at all.
            BREAKPT();
            err = SD_ERR_BAD_CARD;
        }
        else if ((r3[0] & 0xFE) != 0x00) {
            BREAKPT();
            err = SD_ERR_BAD_RESPONSE;
        }
        else {
            // Check the voltage window.
            // The voltage window bits are a 9-bit field in the 32-bit response:
            // b15: 2700-2800 mV
            // b16: 2800-2900 mV
            // ...
            // b23: 3500-3600 mV
            // We extract the 9-bit field first
            uint32_t vWinBits = (r3[2] << 1) | ((r3[3] & 0x80) != 0);

            vMin_mV = 0;
            vMax_mV = 0;
            // We scan the bits from low to high to find the first set bit. This will define the lowest voltage
            // that the card can handle.
            uint32_t mask = 0x001;
            for (uint32_t i=2700; i<3500; mask<<1, i+=100) {
                if (vWinBits & mask) {
                    vMin_mV = i;
                    break;
                }
            }

            // Now we scan the bits from high to low find the first set bit. This will define the highest voltage
            // that the card can handle.
            mask = 0x100;
            for (uint32_t i=3600; i>=2800; mask>>1, i-=100) {
                if (vWinBits & mask) {
                    vMax_mV = i;
                    break;
                }
            }

            // We now know the range of voltages that the card supports.
            // Make sure that at least one bit was set (vMin_mV != 0), and that our nominal 3.3V supply
            // falls within the reported range:
            if ((vMin_mV == 0) || (vMin_mV > 3300) || (vMax_mV < 3300)) {
                BREAKPT();
                err = SD_ERR_BAD_SUPPLY_V;
            }
        }
    }

    return err;
}

// --------------------------------------------------------------------------------------------
SdErr_t SdCard::initializeCard()
{
    bool done;
    uint8_t r3[5];
    SdErr_t err;
    uint8_t r1;

    uint32_t t0 = time_us_32();
    do {
        // To start the initialization process, we send an ACMD41.
        // All ACMDxx commands need to be prefixed with a CMD55.
        // A CMD55 has an argument of 32 zero bits, and returns a 1-byte R1 response.
        // As per the spec: while the card is initializing, the only commands permitted
        // are ACMD41 and CMD0.
        err = sendCmd(CMD55, 0, &r1, 1);
        if (err != SD_ERR_NOERR) {
            BREAKPT();
            break;
        }
        if ((r1 & 0x7E) != 0) {
            BREAKPT();
            return SD_ERR_BAD_RESPONSE;
        }

        // Arg B30 '1' informs the card that we are capable of dealing with HC (High Capacity) cards
        err = sendCmd(ACMD41, 0x40000000, &r1, 1);
        if (err != SD_ERR_NOERR) {
            BREAKPT();
            break;
        }
        if ((r1 & 0x7E) != 0){
            BREAKPT();
            err = SD_ERR_BAD_RESPONSE;
            break;
        }

        // The R1 response to the ACMD41 tells us when initialization is complete.
        // Note that the initialization process takes way longer after a power-on event.
        // Once a card has been initialized, it would appear that it this initialization
        // loop completes the first time.
        done = (r1 == 0x00);
        if (!done) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    } while (!done);

    // Strictly informational: track how long it takes for the card to initialize itself.
    // Using random cards in my posession, I have observed times range from 10-ish milliseconds
    // for new-ish cards to over 300 milliseconds for old ones.
    uint32_t t1 = time_us_32();
    uint32_t delta_mS = (t1-t0)/1000;
    if (delta_mS > initTime_max_mS) {
        initTime_max_mS = delta_mS;
    }

    return err;
}

// --------------------------------------------------------------------------------------------
SdErr_t SdCard::readOCR()
{
    SdErr_t err;
    uint8_t r3[5];

    err = sendCmd(CMD58, 0, r3, sizeof(r3));
    if (err == SD_ERR_NOERR) {
        // The first byte of the response is a type R1 response
        if (r3[0] & R1_ILLEGAL_CMD) {
            // This is either a V1.X SD memory card, or it is not an SD memory card at all.
            BREAKPT();
            err = SD_ERR_BAD_CARD;
        }
        else if ((r3[0] & 0xFE)!= 0x00) {
            BREAKPT();
            err = SD_ERR_BAD_RESPONSE;
        }
        else {
            regOCR = (r3[1]<<24) | (r3[2]<<16) | (r3[3]<<8) | (r3[4]<<0);
        }
    }

    return err;
}

// --------------------------------------------------------------------------------------------
SdErr_t SdCard::init()
{
    uint8_t r1;
    SdErr_t err;

    isSDHC = false;

    // Make sure a card is present
    if (!cardPresent()) {
        // No need to retry this error
        return SD_ERR_NO_CARD;
    }

    // We are not allowed to talk to an SD card until at least 30 mSec after it powers up.
    // We assume that it powered up just now and wait the full 30 mSec here:
    //powerOn();
    vTaskDelay(pdMS_TO_TICKS(30));

    // Run slowly while we init:
    spi->setBaud(1000000);

    // Before we start, we are required to set MOSI and CS to '1', then apply "more than 74" clock pulses.
    // 10 bytes of FF would be 80 clocks of '1' bits:
    uint8_t txb[10];
    memset(txb, 0xFF, sizeof(txb));
    spi->deassertCs(csPad);
    spi->tx(txb, sizeof(txb));

    // At this point, the card should be able to accept commands.
    // Start off by resetting the card with CS asserted to put the card into SPI mode:
    err = resetCard();
    if (err != SD_ERR_NOERR) {
        BREAKPT();
        return err;
    }

    // Verify that the card can run with our 3.3V power supply
    err = checkVoltage();
    if (err != SD_ERR_NOERR) {
        BREAKPT();
        return err;
    }

    // Get the card intitialized. This can take from 50-ish mSec to hundreds of mSec on old cards.
    err = initializeCard();
    if (err != SD_ERR_NOERR) {
        BREAKPT();
        return err;
    }

    // Get the CCS info from the OCR now that the card initialization is complete.
    err = readOCR();
    if (err != SD_ERR_NOERR) {
        BREAKPT();
        return err;
    }

    // Extract the CCS information from bit 30 of the OCR: 0 means SDSC, 1 means SDHC
    isSDHC = (regOCR >> 30) & 1;

    // Read the CSD register
    err = readCSD();
    if (err != SD_ERR_NOERR) {
        BREAKPT();
        return err;
    }

    // Somewhere, we need to adjust the baud rate to the max that the card can support.
    // On the other hand, SPI cards are limited to 25 MHz, so let's just do that.
    spi->setBaud(25000000);

    return err;
}


// --------------------------------------------------------------------------------------------
// Read sectors from SD Card (sector-based interface)
// Note: num_sectors can be 1 (single block) or > 1 (multi-block)
SdErr_t SdCard::readSectors(uint32_t sector_num, uint32_t num_sectors, void *_buffer)
{
    uint8_t r1;
    uint16_t crc16be;

    SdErr_t err = SD_ERR_NOERR;

    if (!operational()) {
        return SD_ERR_NOT_OPERATIONAL;
    }

    if (num_sectors == 0) {
        BREAKPT();
        return SD_ERR_BAD_ARG;
    }

    uint8_t* buffer = (uint8_t*)_buffer;
    uint32_t size_bytes = num_sectors * 512;

    // SDSC cards are addressed using byte-addressing, so they need the sector address converted to a byte address.
    // SDHC/SDXC are addressed in terms of 512-byte blocks (sectors).
    uint32_t addr = isSDHC ? sector_num : (sector_num * 512);

    if (num_sectors == 1) {
        // There is a specific command to read a single block
        SdErr_t res = sendCmd(CMD17, addr, &r1, 1, KEEP_TRANSACTION_OPEN);
        if ((res != SPI_NOERR) || (r1 != 0)) {
            BREAKPT();
            err = SD_ERR_IO;
        }
        else {
            uint8_t response = waitForData();
            if (response == 0xFE) {
                uint16_t crc16;
                if (SPI_NOERR != spi->rx(buffer, 512, &crc16)) {
                    BREAKPT();
                    err = SD_ERR_IO;
                }
                else {
                    if (SPI_NOERR != spi->rx((uint8_t*)&crc16be, 2)) {
                        BREAKPT();
                        err = SD_ERR_IO;
                    }
                    // The Pi Pico CRC calculator takes essentially zero time to calculate the CRC.
                    // Reorder the Pico CRC byte orientation to match how we receive it from the SD card:
                    crc16 = __builtin_bswap16(crc16);
                    if (crc16 != crc16be) {
                        err = SD_ERR_CRC;
                        BREAKPT();
                    }
                }
            }
            else if ((response & 0xF0) == 0) {
                // This is a data error token
                switch (response) {
                    case 0x00:
                    err = SD_ERR_DATA_UNSPECIFIED;
                    break;

                    case 0x01:
                    err = SD_ERR_DATA_ERROR;
                    break;

                    case 0x02:
                    err = SD_ERR_DATA_CC;
                    break;

                    case 0x04:
                    err = SD_ERR_DATA_ECC;
                    break;

                    case 0x08:
                    err = SD_ERR_DATA_RANGE;
                    break;
                }
            }
            else {
                // Probably a timeout:
                //BREAKPT();
                err = SD_ERR_IO;
            }
        }
    }
    else {
        // There is a different command to begin reading multiple blocks
        SdErr_t res = sendCmd(CMD18, addr, &r1, 1, KEEP_TRANSACTION_OPEN);
        if ((res != SPI_NOERR) || (r1 !=0)) {
            BREAKPT();
            err = SD_ERR_IO;
        }
        else {
            do {
                SdErr_t res = spi->rx(buffer, 512);
                if (res != SD_ERR_NOERR) {
                    err = SD_ERR_IO;
                    BREAKPT();
                    goto abort;
                }
                buffer += 512;
                size_bytes -= 512;
            } while (size_bytes);

            // Send the command to stop the multiple read operation
            res = sendCmd(CMD12, 0, &r1, 1);
            if (res != SD_ERR_NOERR) {
                BREAKPT();
                err = SD_ERR_IO;
            }
        }
    }

    abort:
    endTransaction();

    return err;
}

// --------------------------------------------------------------------------------------------
// Write sectors to SD Card (sector-based interface)
// Note: Currently only supports single-block writes (num_sectors == 1)
SdErr_t SdCard::writeSectors(uint32_t sector_num, uint32_t num_sectors, const void *buffer)
{
    if (!operational()) {
        return SD_ERR_NOT_OPERATIONAL;
    }

    if (num_sectors != 1) {
        // Multi-block writes not implemented in SPI mode (original limitation)
        BREAKPT();
        return SD_ERR_BAD_ARG;
    }

    uint32_t size_bytes = num_sectors * 512;

    SdErr_t err;
    uint8_t r1;

    // SDSC cards are addressed using byte-addressing, SDHC/SDXC use block addressing
    // BUG FIX: Was using block_num directly - now properly converts for SDSC cards
    uint32_t addr = isSDHC ? sector_num : (sector_num * 512);

    err = sendCmd(CMD24, addr, &r1, sizeof(r1), KEEP_TRANSACTION_OPEN);

    if ((err == SD_ERR_NOERR) && ((r1 & 0xFE) == 0x00)) {
        // We are OK to send write data

        // The start token has a 0 in its LSbit. That tells the card that the data is next.
        uint8_t start_token = 0xFE;
        spi->tx(&start_token, 1);

        spi->tx((const uint8_t*)buffer, size_bytes);

        uint8_t fakeCrc[2] = {0,0};
        spi->tx(fakeCrc, 2);

        // Now we wait for the card to send a response indicating that it received the data
        uint8_t response_token;
        int32_t Ncr = 20;  // should only be 8
        do {
            spi->rx(&response_token, 1);
            // Upper 3 bits are not specified - remove them
            response_token &= 0x1F;

            if ((response_token & 0x11) == 0x01) {
                // We got a response token!
                break;
            }
        } while (--Ncr > 0);

        if (Ncr<0) {
            err = SD_ERR_BAD_RESPONSE;
            goto abort;
        }

        if (response_token == SD_RESPONSE_TOKEN_REJECTED_CRC) {
            BREAKPT();
            err = SD_ERR_BAD_RESPONSE;
            goto abort;
        }
        else if (response_token == SD_RESPONSE_TOKEN_REJECTED_WRERR) {
            BREAKPT();
            err = SD_ERR_BAD_RESPONSE;
            goto abort;
        }
        else {
            // Data was accepted.
            // Now we need to wait for the write to complete.
            // The card indicates that it is still busy writing by responding to reads with bytes containing 0x00.
            uint8_t busy;
            do {
                spiErr_t rxerr = spi->rx(&busy, sizeof(busy));
                if (rxerr) {
                    BREAKPT();
                    err = SD_ERR_BAD_RESPONSE;
                    goto abort;
                }
            } while (busy == 0x00);

            // Once the write completes, we need to check the result status by issuing a CMD13
            uint16_t r2;
            err = sendCmd(CMD13, 0, (uint8_t*)&r2, sizeof(r2));
            if (err == SD_ERR_NOERR) {
                if (r2 != 0x0000) {
                    BREAKPT();
                    err = SD_ERR_WRITE_FAILURE;
                }
            }
        }
    }


    abort:
    endTransaction();

    // Until we get something working...
    return err;
}

#if 0
// --------------------------------------------------------------------------------------------
SdErr_t SdCard::sync()
{
    // SPI mode writes are synchronous - they complete before writeSectors returns
    return SD_ERR_NOERR;
}

// --------------------------------------------------------------------------------------------
// Prepare the SD card for safe removal or system reboot
// This properly shuts down the card to avoid initialization issues after reboot
SdErr_t SdCard::shutdown()
{
    printf("SPI SD: Shutting down SD card\n");

    // 1. Make sure CS is deasserted
    endTransaction();

    // 2. Reset card to idle state (CMD0)
    // In SPI mode, CMD0 with CS asserted resets the card
    printf("SPI SD: Resetting card to idle (CMD0)\n");
    uint8_t r1;
    sendCmd(CMD0, 0, &r1, sizeof(r1));

    // 3. Deassert CS and send clocks to let card finish
    endTransaction();
    busy_wait_us_32(1000);

    // 4. Leave CS deasserted (card not selected)
    spi->deassertCs(csPad);

    // 5. Clear internal state
    state = NO_CARD;

    printf("SPI SD: Shutdown complete\n");
    return SD_ERR_NOERR;
}
#endif
