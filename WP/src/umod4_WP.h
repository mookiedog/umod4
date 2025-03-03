// -----------------------------------------------------
// NOTE: THIS HEADER IS ALSO INCLUDED BY ASSEMBLER SO
//       SHOULD ONLY CONSIST OF PREPROCESSOR DIRECTIVES
// -----------------------------------------------------

#ifndef UMOD4_WP_H
#define UMOD4_WP_H

// For board detection
#define UMOD4_WP

// At its heart, the umod4_WP board is a pico_w connected to a bunch of extra stuff.
#include "boards/pico_w.h"

#include "hardware/uart.h"


// Now we define the extra stuff on the umod4 board that the PicoW will be driving:
// Pin numbers refer to GPIO#

// GPS connections:
#define GPS_UART_ID       uart0
#define GPS_TX_PIN        0       // We transmit to the GPS on this pin
#define GPS_RX_PIN        1       // We receive from the GPS on this pin
#define GPS_PPS_PIN       28

// WP can drive the EP's SWD port
#define EP_SWCLK_PIN      2
#define EP_SWDAT_PIN      3

// This is the bidirectional link between the WP and EP
#define BDLINK_UART_ID    uart1
#define BDLINK_TX_PIN     4       // WP transmits on this pin
#define BDLINK_RX_PIN     5       // WP receives on this pin

#define BTN_PIN           26      // WP general purpose tacswitch

// Spare IOs for future use
#define SPARE1_PIN        9
#define SPARE2_PIN        8

// Interface for driving a local LCD using RP2040 SPI1
#define LCD_SPI_PORT      spi1
#define LCD_BKLT_PIN      6
#define LCD_DC_PIN        7
#define LCD_SCK_PIN       10
#define LCD_MOSI_PIN      11
#define LCD_MISO_PIN      12
#define LCD_CS_PIN        13

// Interface to MicroSD card using RP2040 SPI0
#define SD_SPI_PORT       spi0
#define SD_SCK_PIN        18
#define SD_MOSI_PIN       19
#define SD_MISO_PIN       16
#define SD_CS_PIN         17
#define SD_CARD_PIN       20

// The DataIn pin for driving a WS2812
#define WS2812_PIN        22

// Controls the EP 'RUN' (reset) signal. Active low to reset the EP.
#define EP_RUN_PIN        27


// This might not be the right place, but it'll do for now:
#define TASK_NORMAL_PRIORITY  ((tskIDLE_PRIORITY)+1)
#define TASK_HIGH_PRIORITY    ((tskIDLE_PRIORITY)+2)
#define TASK_ISR_PRIORITY     ((tskIDLE_PRIORITY)+3)
#define TASK_MAX_PRIORITY     ((tskIDLE_PRIORITY)+4)

#define STRINGIFY(x) STRINGIFY2(x)
#define STRINGIFY2(x) #x

#define LOCATION(msg) __FILE__ "[" STRINGIFY(__LINE__) "] " msg

// Choices are:
// 115200 (char time of 87 uSec)
// 230400 (char time of 43 uSec)
// 460800 (char time of 22 uSec)
#define GPS_BAUD_RATE   460800

// Theoretically, 10Hz is max rate for a NEO-8
#define GPS_MEASUREMENT_PERIOD_MS   100

#endif
