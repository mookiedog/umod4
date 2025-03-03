#if !defined SPI_H
#define SPI_H
// Basic SPI device driver that uses DMA

#include "stdint.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"

enum spiErr_t {
  SPI_NOERR = 0,

};

class Spi {
  public:
    /// @brief Create an Spi interface.
    /// @param spiId - Must be spi0 or spi1
    /// @param clkPad - pad number for SPI SCK signal
    /// @param mosiPad - pad number for SPI MOSI signal
    /// @param misoPad - pad number for SPI MISO signal
    Spi(spi_inst_t* spiId, int32_t clkPad, int32_t mosiPad, int32_t misoPad);

    /// @brief Transmit only
    /// @param txBuffer : buffer containing data to transmit
    /// @param txLen : number of bytes to transmit
    /// @return error code defined by spiErr_t
    spiErr_t tx(const uint8_t* txBuffer, uint32_t txLen);

    /// @brief Receive only
    /// @param rxBuffer : buffer where read data will be placed
    /// @param rxLen : number of bytes to transmit
    /// @return error code defined by spiErr_t
    spiErr_t rx(uint8_t* rxBuffer, uint32_t rxLen, uint16_t* crc = nullptr);

    /// @brief Full duplex transfer
    /// @param txBuffer : data to be transmitted
    /// @param rxBuffer : data where received data will be placed
    /// @param len : number of bytes to transfer
    /// @return error code defined by spiErr_t
    spiErr_t transfer(const uint8_t* txBuffer, uint8_t* rxBuffer, uint32_t len);

    /// @brief Set the baud rate. Note that the SPI hardware may not be able to generate the desired baud rate.
    /// @param desiredBaudRate - the desired baud rate
    /// @return - The actual baud rate
    uint32_t setBaud(uint32_t desiredBaudRate);

    void assertCs(int32_t pad)   {gpio_put(pad, 0);}
    void deassertCs(int32_t pad) {gpio_put(pad, 1);}


  private:
    spi_inst_t* instance;
    spi_hw_t* hw;

    int32_t clkPad;
    int32_t mosiPad;
    int32_t misoPad;

    // Make a note of the DMA channel numbers this Spi interface will be using
    uint32_t dma_tx_chan;
    uint32_t dma_rx_chan;
};



#endif
