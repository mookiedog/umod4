/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

// Example of writing via DMA to the SPI interface and similarly reading it back via a loopback.

#include "Spi.h"


//#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/dma.h"


// -------------------------------------------------------------------------------------------------
Spi::Spi(spi_inst_t* spiId, int32_t _clkPad, int32_t _mosiPad, int32_t _misoPad)
{
  instance = spiId;
  hw = spi_get_hw(spiId);

  clkPad = _clkPad;
  misoPad = _misoPad;
  mosiPad = _mosiPad;

  // Enable SPI at 1 MHz
  spi_init(instance, 1000 * 1000);

  // Assign the desired pads to SPI functionality:
  gpio_set_function(misoPad, GPIO_FUNC_SPI);
  gpio_set_function(clkPad, GPIO_FUNC_SPI);
  gpio_set_function(mosiPad, GPIO_FUNC_SPI);

  // We need a pair of DMA channels for each SPI object.
  // 'true' means we panic if we can't allocate the channels
  dma_tx_chan = dma_claim_unused_channel(true);
  dma_rx_chan = dma_claim_unused_channel(true);

  //dma_tx_config = dma_channel_get_default_config(dma_tx_chan);
  //dma_rx_config = dma_channel_get_default_config(dma_tx_chan);
}


// -------------------------------------------------------------------------------------------------
uint32_t Spi::setBaud(uint32_t desiredBaudRate)
{
  uint32_t actualBaud = spi_set_baudrate(instance, desiredBaudRate);
  return actualBaud;
}

// -------------------------------------------------------------------------------------------------
spiErr_t Spi::tx(const uint8_t* txBuffer, uint32_t txLen)
{
  uint8_t rxTrash;

  // This write operation does not care about the read data coming back.
  // We set up the read channel to put all the incoming data into a single trash byte
  // that we ignore aferwards.
  dma_channel_config c = dma_channel_get_default_config(dma_tx_chan);
  channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
  channel_config_set_dreq(&c, spi_get_dreq(instance, true));
  channel_config_set_read_increment(&c, true);  // sequentially read our way through the TX buffer
  channel_config_set_write_increment(&c, false);
  dma_channel_configure(dma_tx_chan, &c,
                        &hw->dr,                // write address
                        txBuffer,               // read starting address
                        txLen,                  // element count (each element is of size transfer_data_size)
                        false);                 // don't start yet

  // Inbound DMA gets transferred to a single-byte trash buffer with no address increment.
  c = dma_channel_get_default_config(dma_rx_chan);
  channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
  channel_config_set_dreq(&c, spi_get_dreq(instance, false));
  channel_config_set_read_increment(&c, false);
  channel_config_set_write_increment(&c, false);
  dma_channel_configure(dma_rx_chan, &c,
                        &rxTrash,               // write address
                        &hw->dr,                // read address
                        txLen,                  // element count (each element is of size transfer_data_size)
                        false);                 // don't start yet


  //printf("Starting DMAs...\n");
  // start them exactly simultaneously to avoid races (in extreme cases the FIFO could overflow)
  dma_start_channel_mask((1u << dma_tx_chan) | (1u << dma_rx_chan));
  //printf("Wait for RX complete...\n");
  dma_channel_wait_for_finish_blocking(dma_rx_chan);
  if (dma_channel_is_busy(dma_tx_chan)) {
      panic("RX completed before TX");
  }

  return SPI_NOERR;
}


// -------------------------------------------------------------------------------------------------
spiErr_t Spi::rx(uint8_t* rxBuffer, uint32_t len, uint16_t* crc16)
{
  uint8_t txTrash = 0xFF;

  // This dummy write operation sends out trash since we only care about the read data coming back.
  dma_channel_config c = dma_channel_get_default_config(dma_tx_chan);
  channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
  channel_config_set_dreq(&c, spi_get_dreq(instance, true));
  channel_config_set_read_increment(&c, false);   // always read from the same dummy location
  channel_config_set_write_increment(&c, false);  // always send the dummy data to the dr register
  dma_channel_configure(dma_tx_chan, &c,
                        &hw->dr,                  // DMA writes to this address
                        &txTrash,                 // DMA reads from this address
                        len,                      // element count (each element is of size transfer_data_size)
                        false);                   // don't start yet


  // Inbound DMA gets transferred to a read buffer.
  c = dma_channel_get_default_config(dma_rx_chan);

  channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
  channel_config_set_dreq(&c, spi_get_dreq(instance, false));
  channel_config_set_read_increment(&c, false);   // read the incoming dr every time
  channel_config_set_write_increment(&c, true);   // increment where the data gets stored in the rx buffer

  if (crc16) {
    // Set up the CRC sniffing hardware to calculate the CRC as the data is processed by the DMA unit
    channel_config_set_sniff_enable(&c, true);
    dma_sniffer_set_data_accumulator(0x0000);
    dma_sniffer_set_output_reverse_enabled(false);
    dma_sniffer_enable(dma_rx_chan, DMA_SNIFF_CTRL_CALC_VALUE_CRC16, true);
  }

  dma_channel_configure(dma_rx_chan, &c,
                        rxBuffer,                 // DMA writes to this address
                        &hw->dr,                  // DMA reads from this address
                        len,                      // element count (each element is of size transfer_data_size)
                        false);                   // don't start yet


  // Start both DMA channels simultaneously to avoid races (in extreme cases the FIFO could overflow)
  dma_start_channel_mask((1u << dma_tx_chan) | (1u << dma_rx_chan));

  dma_channel_wait_for_finish_blocking(dma_rx_chan);
  if (dma_channel_is_busy(dma_tx_chan)) {
      panic("Spi: RX completed before TX");
  }

  // Update the crc result pointer with the data that was calculated across the received block
  if (crc16) {
    uint32_t sniffedCrc = dma_sniffer_get_data_accumulator();
    *crc16 = sniffedCrc & 0xFFFF;
  }

  return SPI_NOERR;
}

// -------------------------------------------------------------------------------------------------
spiErr_t Spi::transfer(const uint8_t* txBuffer, uint8_t* rxBuffer, uint32_t len)
{
  // This transfer operation writes an output buffer while receiving into an input buffer.
  dma_channel_config c = dma_channel_get_default_config(dma_tx_chan);
  channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
  channel_config_set_dreq(&c, spi_get_dreq(instance, true));
  channel_config_set_read_increment(&c, true);    // sequentially read our way through the TX buffer
  channel_config_set_write_increment(&c, false);  // always write to the dr register
  dma_channel_configure(dma_tx_chan, &c,
                        &hw->dr,                  // DMA TX writes to this address
                        txBuffer,                 // DMA TX reads from this address
                        len,                      // element count (each element is of size transfer_data_size)
                        false);                   // don't start yet

  // Inbound DMA gets transferred to a read buffer.
  c = dma_channel_get_default_config(dma_rx_chan);
  channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
  channel_config_set_dreq(&c, spi_get_dreq(instance, false));
  channel_config_set_read_increment(&c, false);   // read the incoming dr every time
  channel_config_set_write_increment(&c, true);   // increment where the data gets stored in the rx buffer
  dma_channel_configure(dma_rx_chan, &c,
                        rxBuffer,                 // DMA RX writes to this address
                        &hw->dr,                  // DMA RX reads from this address
                        len,                      // element count (each element is of size transfer_data_size)
                        false);                   // don't start yet


  // Start both DMA channels simultaneously to avoid races (in extreme cases the FIFO could overflow)
  dma_start_channel_mask((1u << dma_tx_chan) | (1u << dma_rx_chan));

  dma_channel_wait_for_finish_blocking(dma_rx_chan);
  if (dma_channel_is_busy(dma_tx_chan)) {
      panic("Spi: RX completed before TX");
  }

  return SPI_NOERR;
}



#if 0
int main() {
    // Enable UART so we can print status output
    stdio_init_all();
#if !defined(spi_default) || !defined(PICO_DEFAULT_SPI_SCK_PIN) || !defined(PICO_DEFAULT_SPI_TX_PIN) || !defined(PICO_DEFAULT_SPI_RX_PIN) || !defined(PICO_DEFAULT_SPI_CSN_PIN)
#warning spi/spi_dma example requires a board with SPI pins
    puts("Default SPI pins were not defined");
#else

    printf("SPI DMA example\n");

    // Enable SPI at 1 MHz and connect to GPIOs
    spi_init(spi_default, 1000 * 1000);
    gpio_set_function(PICO_DEFAULT_SPI_RX_PIN, GPIO_FUNC_SPI);
    gpio_init(PICO_DEFAULT_SPI_CSN_PIN);
    gpio_set_function(PICO_DEFAULT_SPI_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(PICO_DEFAULT_SPI_TX_PIN, GPIO_FUNC_SPI);
    // Make the SPI pins available to picotool
    bi_decl(bi_3pins_with_func(PICO_DEFAULT_SPI_RX_PIN, PICO_DEFAULT_SPI_TX_PIN, PICO_DEFAULT_SPI_SCK_PIN, GPIO_FUNC_SPI));
    // Make the CS pin available to picotool
    bi_decl(bi_1pin_with_name(PICO_DEFAULT_SPI_CSN_PIN, "SPI CS"));

    // Grab some unused dma channels
    const uint dma_tx_chan = dma_claim_unused_channel(true);
    const uint dma_rx = dma_claim_unused_channel(true);

    // Force loopback for testing (I don't have an SPI device handy)
    hw_set_bits(&spi_get_hw(spi_default)->cr1, SPI_SSPCR1_LBM_BITS);

    static uint8_t txbuf[TEST_SIZE];
    static uint8_t rxbuf[TEST_SIZE];
    for (uint i = 0; i < TEST_SIZE; ++i) {
        txbuf[i] = rand();
    }

    // We set the outbound DMA to transfer from a memory buffer to the SPI transmit FIFO paced by the SPI TX FIFO DREQ
    // The default is for the read address to increment every element (in this case 1 byte = DMA_SIZE_8)
    // and for the write address to remain unchanged.

    printf("Configure TX DMA\n");
    dma_channel_config c = dma_channel_get_default_config(dma_tx);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_dreq(&c, spi_get_dreq(spi_default, true));
    dma_channel_configure(dma_tx, &c,
                          &spi_get_hw(spi_default)->dr, // write address
                          txbuf, // read address
                          TEST_SIZE, // element count (each element is of size transfer_data_size)
                          false); // don't start yet

    printf("Configure RX DMA\n");

    // We set the inbound DMA to transfer from the SPI receive FIFO to a memory buffer paced by the SPI RX FIFO DREQ
    // We configure the read address to remain unchanged for each element, but the write
    // address to increment (so data is written throughout the buffer)
    c = dma_channel_get_default_config(dma_rx);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_dreq(&c, spi_get_dreq(spi_default, false));
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    dma_channel_configure(dma_rx, &c,
                          rxbuf, // write address
                          &spi_get_hw(spi_default)->dr, // read address
                          TEST_SIZE, // element count (each element is of size transfer_data_size)
                          false); // don't start yet


    printf("Starting DMAs...\n");
    // start them exactly simultaneously to avoid races (in extreme cases the FIFO could overflow)
    dma_start_channel_mask((1u << dma_tx) | (1u << dma_rx));
    printf("Wait for RX complete...\n");
    dma_channel_wait_for_finish_blocking(dma_rx);
    if (dma_channel_is_busy(dma_tx)) {
        panic("RX completed before TX");
    }

    printf("Done. Checking...");
    for (uint i = 0; i < TEST_SIZE; ++i) {
        if (rxbuf[i] != txbuf[i]) {
            panic("Mismatch at %d/%d: expected %02x, got %02x",
                  i, TEST_SIZE, txbuf[i], rxbuf[i]
            );
        }
    }

    printf("All good\n");
    dma_channel_unclaim(dma_tx);
    dma_channel_unclaim(dma_rx);
    return 0;
#endif
}
#endif
