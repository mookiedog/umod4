#if !defined EPUART_H
#define EP_UART_H

#include "Uart.h"

class EpUart : public Uart
{
    public:
      EpUart(uart_inst_t* uartId, int32_t txPad, int32_t rxPad);

    private:
      void isr();
};

#endif
