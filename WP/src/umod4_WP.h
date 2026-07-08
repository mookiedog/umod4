// -----------------------------------------------------
// NOTE: THIS HEADER IS ALSO INCLUDED BY ASSEMBLER SO
//       SHOULD ONLY CONSIST OF PREPROCESSOR DIRECTIVES
// -----------------------------------------------------

#ifndef UMOD4_WP_H
#define UMOD4_WP_H

// For board detection
#define UMOD4_WP

// At its heart, the umod4_WP board is a pico2_w connected to a bunch of extra stuff.
#include "pico.h"
#include "boards/pico2_w.h"

#include "hardware/uart.h"

// The Pico2W uses an RP2350 which has 3 PIO blocks, 4 state machines per PIO.
// The WP needs to use all of them. SWD and SDIO need to be active at the same time
// while flashing the EP, so they must have their own PIO blocks.
// Fortunately, PIO2 has enough resource to simultaneously handle WiFi, NeoPixel, and the 32-bit UART RX/TX.
//
// Pio Unit allocations:
// PIO0: SWD driver. Can't share: it uses all available instruction memory.
// PIO1: SDIO driver. Can't share: it uses all available instruction memory.
// PIO2: Will be shared among 3 of the 4 available state machines:
//         - WiFi (6 instructions)
//         - WS2812 NeoPixel (4 instructions)
//         - UART_RX32 (9 instructions)
//         - <spare> (13 instructions)

#define PIO_SWD             pio0

#define PIO_SDIO            pio1
#define   SD_GPIO_FUNC      GPIO_FUNC_PIO1

#define PIO_WS2812          pio2

#define PIO_UART            pio2
#define   PIO_UART_RX_IRQ   PIO2_IRQ_0

// GPIO pin assignments as per PCB 4V2.
//
// The design changes from 4V1 to 4V2 were intended to be backwards compatible.
// Executing 4V2 code on a 4V1 would of course not have access to new 4V2 features,
// but there would be no harmful effects either.
// WARNING! there is 1 potential incompatibility: using SPARE6_ADC on a 4V1 board
// could cause the HC11 to reset!
//
// GP   | 4V1 Name      | 4V2 Name      | Notes
// -----|---------------|---------------|-------------------------------------------------
// GP5  | SPARE2        | EP_SWD_DIS    | Now has physical 0.1 jumper header on PCB
// GP16 | LCD_MISO      | SPARE3        | LCD connector retired; footprint for LED present
// GP17 | LCD_CS        | SPARE4        | LCD connector retired; footprint for LED present
// GP18 | LCD_SCK       | SPARE5        | LCD connector retired
// GP19 | LCD_MOSI      | EN_VDD_SD     | SD card power switch (SY6280) enable
// GP20 | LCD_DC        | VCCB_PWR      | ECU bus power detect; not wired on 4V1
// GP21 | LCD_BKLT      | EP_BOOTSEL    | Force EP into BOOTSEL; no-op on 4V1
// GP26 | SPARE1        | DISK_BSY      | SD activity LED (was already SPARE1_LED on 4V1)
// GP27 | SPARE0        | SPARE0_ADC    | Renamed to reflect ADC capability
// GP28 | RESET_HC11    | SPARE6_ADC    | HC11 reset from WP removed in 4V2; GP28 now spare ADC
// GP22 | WS2812_PIN    | WS2812_PIN    | 4V2 supports a second pixel not present on 4V1

// Now we define the extra stuff on the umod4 board that the PicoW will be driving:
// "Pin" numbers actually refer to GPIO ID, not a package pin number!

// GPS connections:
#define GPS_UART_ID         uart1
#define GPS_TX_PIN          8           // WP transmits to the GPS on this pin
#define GPS_RX_PIN          9           // WP receives from the GPS on this pin
#define GPS_PPS_PIN         7

// Note: This pin can be left undefined to turn it into a general purpose IO.
// This feature has been used to allow transitions on this pin to control
// the HC11 bus logging feature, for example. Obviously, you would lose the flow
// control mechanism, but that's the tradeoff for debugging some other issue.
#define EPLOG_FLOWCTRL_PIN  0           // POR default is '1'. WP drives this GPIO to '0' to indicate it is ready for ECU log data

#define EPLOG_RX_PIN        1           // WP receives 16-bit PIO UART data on this GPIO

// WP can drive the EP's SWD port using these GPIOs
#define EP_SWCLK_PIN        2
#define EP_SWDAT_PIN        3

// Spare IOs
#define SPARE0_ADC_PIN      27          // 4V1: SPARE0_PIN (renamed to reflect ADC capability)
#define SCOPE_TRIGGER_PIN   (SPARE0_ADC_PIN)

#define DISK_BSY_PIN        26          // 4V1: SPARE1_PIN / SPARE1_LED_PIN — SD activity LED
#define EP_SWD_DIS_PIN      5           // 4V1: SPARE2_PIN — now has physical jumper header

// Former LCD connector pins (SPI0) repurposed in 4V2 (4V1 LCD connector was retired):
#define SPARE3_PIN          16          // 4V1: LCD_MISO
#define SPARE4_PIN          17          // 4V1: LCD_CS
#define SPARE5_PIN          18          // 4V1: LCD_SCK
#define EN_VDD_SD_PIN       19          // 4V1: LCD_MOSI — SD power switch (SY6280) enable
#define VCCB_PWR_PIN        20          // 4V1: LCD_DC   — ECU bus power detect; absent on 4V1
#define EP_BOOTSEL_PIN      21          // 4V1: LCD_BKLT — force EP into BOOTSEL; no-op on 4V1

// Interface to MicroSD card uses PIO for 4-bit SD mode
#define SD_SCK_PIN          10
#define SD_MOSI_PIN         11
#define SD_MISO_PIN         12
#define SD_CS_PIN           15
#define SD_CARD_PIN         6

// Alternate names for data GPIOs in 4-bit SD mode.
// Must be 4 consecutively increasing GPIO numbers starting with SD_MOSI_PIN
#define SD_DAT0             12
#define SD_DAT1             13
#define SD_DAT2             14
#define SD_DAT3             15

// The GPIO used to drive the WS2812 DataIn signal
#define WS2812_PIN          22
// The number of WS2812 chips daisy-chained on the PCB
#define WS2812_PIXCNT       2           // 4V1: 1 — second pixel silently unused on 4V1

// Controls the EP 'RUN' (A.K.A. "!Reset") signal. Active low to reset the EP.
#define EP_RUN_PIN          4

#define SPARE6_ADC_PIN      28          // 4V1: RESET_HC11 — removed in 4V2; GP28 now spare ADC

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
