#if !defined HARDWARE_H
#define HARDWARE_H

//#define CREATE_MASK_FROM_RANGE(MSB, LSB) (((1<<((MSB)-(LSB)+1))-1)<<(LSB))

// Define values for the various versions of hardware that exist
// The versions are coded as 3-digit hex values X.Y.Z where:
//    X - major number
//    Y - minor number
//    Z - subminor number, just in case a subminor change to the hardware might redefine some aspect of the HW
// A version 4V0 PCB would be encoded as 0x40Z, where Z is the subminor version, typically '0'

#define HW_PICO       0x100
#define HW_4V0        0x400

// This symbol must be set to one of the HW_<ID> values from the set, above
#define HW_VER  HW_4V0

// -----------------------------------------------------------------------------------------------------
#if HW_VER == HW_PICO
  // We are unable to completely simulate an EPROM on a Pico board because the Pico itself
  // uses a number of the IOs:
  //    - 23 output: Power Supply mode selector
  //    - 24 input: USB Vbus is present (1) or absent (0)
  //    - 25 output: LED output (active high)
  //    -	29 analog input: measures (Vsys/3)

  // HC11_WR is defined as '1' == write, '0' == read
  #define HC11_WR_LSB   16
  #define HC11_WR_BITS  (1<<(HC11_WR_LSB))

  // CS == Active Low
  #define HC11_CE_LSB   17
  #define HC11_CE_BITS  (1<<(HC11_CE_LSB))

  #define HC11_E_LSB    18
  #define HC11_E_BITS   (1<<(HC11_E_LSB))

  #define DBG_BSY_LSB   28
  #define DBG_BSY_BITS  (1<<(DBG_BSY_LSB))

  #define HC11_DB_LSB   0
  #define HC11_DB_MSB   7
  #define HC11_DB_BITS  0x000000FF

  // For testing, we use an abbreviated address bus to leave some pins for the EPROM control signals
  #define HC11_AB_LSB   8
  #define HC11_AB_MSB   15
  #define HC11_AB_BITS  0x0000FF00

  // The Pico version does not support driving the log data out of the UART

// -----------------------------------------------------------------------------------------------------
#elif HW_VER == HW_4V0
  // Note: the 4V0 hardware uses a crystal that starts up more slowly than the RP2040 bootrom expects.
  // The umod4 board file in the SDK root (pico/pico-sdk/src/boards/include/boards/umod4.h)
  // needs to be include the following line:
  // #define PICO_XOSC_STARTUP_DELAY_MULTIPLIER 64

  // Currently, all 30 GPIOs are in use.

  #define HC11_AB_LSB      0
  #define HC11_AB_MSB     14
  #define HC11_AB_BITS    0x00007FFF

  // Warning: the eprom emulator code depends on E sitting in the same location as A15.
  // 'E' is useless information for the log, so it will get replaced with the inferred state of A15.
  #define HC11_E_LSB      15
  #define HC11_E_BITS     (1<<(HC11_E_LSB))

  #define HC11_DB_LSB     16
  #define HC11_DB_MSB     23
  #define HC11_DB_BITS    (0xFF << (HC11_DB_LSB))

  // Note that 24 can be used as a convenient CLKOUT pin for driving the RP2040 clock to a GPIO
  // since our Umod4 board treats GPIO24 as a TX output already.  It may annoy the receiver
  // at the other end of the clkout signal, but it will not be electrically harmful!
#if 0
  // Use GPIO24/GPOUT2:
    #define CLKOUT_GPIO   24
    #define CLKOUT_SOURCE CLOCKS_CLK_GPOUT2_CTRL_AUXSRC_VALUE_CLK_SYS
#else
  #define EP_UART     uart1         // This UART is the one that is used to communicate with the EP
  #define EP_UART_BAUD_RATE 460800  // We could go faster, if needed
  #define TX_GPIO       24          // U1 TX
  #define RX_GPIO       25          // U1 RX
#endif

  // The CE signal passes through an inverting voltage converter. This means that
  // when CE is asserted (meaning the EPROM address space is selected), our firmware will see CE='1'
  #define HC11_CE_LSB     26
  #define HC11_CE_BITS    (1<<(HC11_CE_LSB))

  // WR is the complement of the HC11 RW due to its inverting voltage converter: '1' == write, '0' == read
  #define HC11_WR_LSB     27
  #define HC11_WR_BITS    (1<<(HC11_WR_LSB))

  // HC11_RESET_OUT should be driven to '0' to allow the HC11 to run.  '1' or 'HI-Z' will assert the HC11 RESET signal.
  #define HC11_RESET_LSB  28
  #define HC11_RESET_BITS (1<<(HC11_RESET_LSB))

  // DBG_BSY is active low so it will turn the LED on when driven to '0'.
  #define DBG_BSY_LSB     29
  #define DBG_BSY_BITS    (1<<(DBG_BSY_LSB))


// -----------------------------------------------------------------------------------------------------
#else
  #error "Unknown Hardware version!"
#endif

#endif // HARDWARE_H