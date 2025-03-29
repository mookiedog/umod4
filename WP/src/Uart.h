#ifndef UART_H
#define UART_H

#include <stdint.h>

#include "hardware/uart.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

// These are the choices for setting the RX_FIFO watermark levels:
//  0b001: 1/4 full ( 8 chars)
//  0b010: 1/2 full (16 chars)
//  0b011: 3/4 full (24 chars)
#define RX_FIFO_WATERMARK_LEVEL_1_4  0b001
#define RX_FIFO_LENGTH_1_4               8
#define RX_FIFO_WATERMARK_LEVEL_1_2  0b010
#define RX_FIFO_LENGTH_1_2              16
#define RX_FIFO_WATERMARK_LEVEL_3_4  0b011
#define RX_FIFO_LENGTH_3_4              24

// Define which setting we will use:
#define RX_FIFO_WATERMARK_LEVEL     RX_FIFO_WATERMARK_LEVEL_1_2
#define RX_FIFO_LENGTH              RX_FIFO_LENGTH_1_2

void isr_uart0();
void isr_uart1();

class Uart {
  public:
    // Pad naming is from the UART's point of view: it will transmit via the txPad and receive on the rxPad.
    Uart(uart_inst_t* uartId, int32_t txPad, int32_t rxPad);

    void configFormat(int32_t dataBits, int32_t stopBits, uart_parity_t parity);
    void configFlowControl(bool ctsEnabled, bool rtsEnabled);
    int32_t configBaud(int32_t newBaudRate);


    // Control overall UART operation
    void enable();
    void disable();

    // Control interrupt operation
    void enableInts();
    void disableInts();

    void isr();

    int32_t tx(uint8_t byte);
    int32_t tx(uint8_t* bytes, uint8_t len);
    int32_t tx(char* string);
    bool txBusy();

    void rxIntEnable();

    BaseType_t rx(uint16_t &c, TickType_t xTicksToWait);

    void notifyOnRx(TaskHandle_t t) {rxTask = t;}

  private:
    // This is a pointer to the specific instance of the UART hardware registers.
    // HOWEVER... the pico header file says that it may not remain that way in the future.
    uart_inst_t* uartId;

    // THEREFORE, we make our own pointer to the specific instance of the UART hardware registers
    uart_hw_t* hw;

    int32_t irqId;

    int32_t txPad;
    int32_t rxPad;

    // The rxQ_len needs to be longer than the hardware FIFO length (32)
    static const uint32_t rxQ_len = 256;
    uint16_t rxQ[rxQ_len];
    uint16_t* rxQ_head;
    uint16_t* rxQ_tail;

    bool rxQ_full();
    bool rxQ_empty();

    bool rxIntsEnabled;
    bool txIntsEnabled;

    // The task to be notified when we receive serial data
    TaskHandle_t rxTask;
};


#endif