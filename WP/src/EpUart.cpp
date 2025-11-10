#include "EpUart.h"
#include "Logger.h"

// TEMP for scope trigger
#include "umod4_WP.h"
#include "pico/stdlib.h"


EpUart::EpUart(uart_inst_t* uartId, int32_t txPad, int32_t rxPad) : Uart(uartId, txPad, rxPad)
{

}

// -------------------------------------------------------------------------------------------------
// This method gets called whenever this UART needs to service an interrupt.
// It executes at ISR level, so all the usual warnings apply!
//
// A better solution to the mess dealing with UART bytes that might go missing or get corrupted
// might be to make a PIO UART that operates on sending 16-bit words. That way, we get
// the entire two-byte transfer or we lose it completely, but we can't end up in a situation
// where one of the two bytes has a problem.
uint32_t epUart_errorCnt;
uint32_t epUart_addrOnly;

void __time_critical_func(EpUart::isr)()
{
  static uint16_t addr;
  static bool addrValid = 0;
  uint16_t rxData;
  uint8_t d;

  # warning "fix this: it's not testing what the comment says it is!"
  if (hw->mis & (UART_UARTMIS_RXMIS_BITS | UART_UARTMIS_RTMIS_BITS)) {
    // We got here because the FIFO reached a trigger level or we had an RX timeout
    // Either way, we read everything out of the FIFO until it is empty

    while (!(hw->fr & UART_UARTFR_RXFE_BITS)) {
      if (!addrValid) {
        if (!(hw->fr & UART_UARTFR_RXFE_BITS)) {
          rxData = (uint16_t)(hw->dr);
          // might want to check for rx errors. But for now, no...
          if (rxData > 0xFF) {
            epUart_errorCnt++;
          }
          addr = rxData & 0xFF;
          addrValid = true;
        }
      }

      if (addrValid) {
        if (!(hw->fr & UART_UARTFR_RXFE_BITS)) {
          uint16_t rxData = (uint16_t)(hw->dr);
          if (rxData > 0xFF) {
            epUart_errorCnt++;
          }

          d = rxData & 0xFF;

          // TEMP!
          #define LOG_CRANKREF_ID_U8 0x92
          bool trigger = ((addr == LOG_CRANKREF_ID_U8) && (d >= 0x30));
          gpio_put(SCOPE_TRIGGER_PIN, trigger);

          logger->logData(addr, d, true);
          #if 0
          uint8_t buf[2];
          buf[0] = addr;
          buf[1] = d;
          logSource->log(sizeof(buf), buf);
          #endif
          addrValid = false;
        }
        else {
          // We received the addr byte, but the data byte has not arrived yet for some reason
          epUart_addrOnly++;
          return;
        }
      }
    }
  }
}
