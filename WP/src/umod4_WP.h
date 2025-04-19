// -----------------------------------------------------
// NOTE: THIS HEADER IS ALSO INCLUDED BY ASSEMBLER SO
//       SHOULD ONLY CONSIST OF PREPROCESSOR DIRECTIVES
// -----------------------------------------------------

#ifndef UMOD4_WP_H
#define UMOD4_WP_H

// For board detection
#define UMOD4_WP

// At its heart, the umod4_WP board is a pico2_w connected to a bunch of extra stuff.
#include "boards/pico2_w.h"

#include "hardware/uart.h"

// Define the PIO unit assignments here
#define PIO_UART          pio1
#define PIO_UART_SM       0
#define PIO_WS2812        pio0
#define PIO_WS2812_SM     0

// The GPIO pin assignments are as per the PCB 4V1 circuit board.

// Now we define the extra stuff on the umod4 board that the PicoW will be driving:
// "Pin" numbers actually refer to GPIO ID, not a package pin number!

// GPS connections:
#define GPS_UART_ID       uart1
#define GPS_TX_PIN        8       // WP transmits to the GPS on this pin
#define GPS_RX_PIN        9       // WP receives from the GPS on this pin
#define GPS_PPS_PIN       7

// WP <--> EP Serial connections:
//  - The WP TX/GPIO0 is tied to the EP RX signal
//  - The WP RX/GPIO1 is tied to the EP TX signal
#define EP_UART_ID        uart0
#define EP_TX_PIN         -1      // -1 means don't let the UART use it
#define EP_RX_PIN         1

#if EP_TX_PIN == -1
  // The UART is not using the TX pad: we use it as a flow control mechanism.
  // A '1' tells the EP that we are ready to receive the ECU data stream.
  #define EP_FLOWCTRL_PIN   0
#endif

// WP can drive the EP's SWD port
#define EP_SWCLK_PIN      2
#define EP_SWDAT_PIN      3

// Spare IOs for future use
#define SPARE0_PIN        27

// SPARE1 has been redefined as an add-on LED indicator wired as postive logic: 1 means LED ON
#define SPARE1_PIN        26
#define SPARE1_LED_PIN    (SPARE1_PIN)

// We will use SPARE2 as a development aid.
// If grounded, it will indicate to the system that it should reformat the LittleFS filesystem on the SD Card.
#define SPARE2_PIN        5

// Interface for driving a local LCD using SPI1
#define LCD_SPI_PORT      spi0
#define LCD_BKLT_PIN      21
#define LCD_DC_PIN        20
#define LCD_SCK_PIN       18
#define LCD_MOSI_PIN      19
#define LCD_MISO_PIN      16
#define LCD_CS_PIN        17

// We will allow attaching a USB to serial adaptor on the LCD port
#define DBG_UART_TX_PIN   LCD_MISO_PIN//(LCD_MOSI_PIN)

// Interface to MicroSD card using SPI0
#define SD_SPI_PORT       spi1
#define SD_SCK_PIN        10
#define SD_MOSI_PIN       11
#define SD_MISO_PIN       12
#define SD_CS_PIN         15
#define SD_CARD_PIN       6

// Alternate names for data GPIOs in 4-bit mode.
// Must be 4 consecutively increasing GPIO numbers starting with SD_MOSI_PIN
#define SD_DAT0           12
#define SD_DAT1           13
#define SD_DAT2           14
#define SD_DAT3           15

// The DataIn pin for driving a WS2812
#define WS2812_PIN        22
// The number of WS2812 chips daisy-chained on the PCB
#define WS2812_PIXCNT     1

// Controls the EP 'RUN' (A.K.A. "!Reset") signal. Active low to reset the EP.
#define EP_RUN_PIN        4

// The WP retains the hardware ability to reset the ECU, but this feature is not used.
// The reason is that we would not want a malfunctioning WP from preventing the ECU
// from letting the engine run. We always want to be able to ride home!
#define RESET_HC11        28

// This might not be the right place, but it'll do for now:
#define TASK_NORMAL_PRIORITY  ((tskIDLE_PRIORITY)+1)
#define TASK_HIGH_PRIORITY    ((tskIDLE_PRIORITY)+2)
#define TASK_ISR_PRIORITY     ((tskIDLE_PRIORITY)+3)
#define TASK_MAX_PRIORITY     ((tskIDLE_PRIORITY)+4)

#define STRINGIFY(x) STRINGIFY2(x)
#define STRINGIFY2(x) #x

#define LOCATION(msg) __FILE__ "[" STRINGIFY(__LINE__) "] " msg

// Define the GPS Baud rate choices.
// 115200 (char time of 87 uSec)
// 230400 (char time of 43 uSec)
// 460800 (char time of 22 uSec)
#define GPS_BAUD_RATE   460800

// Theoretically, 10Hz is max rate for a NEO-8
#define GPS_MEASUREMENT_PERIOD_MS   100

#endif
