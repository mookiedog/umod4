#include "Uart.h"

#include "pico/stdlib.h"
#include "hardware/irq.h"
#include "hardware/regs/intctrl.h"

#include <string.h>

// This array holds a pointer to the object that will handle the uart ISR.
// There need to be as many of these objects as there are uarts in the device:
static Uart* isr_obj[2];

// -------------------------------------------------------------------------------------------------
void isr_uart0()
{
  if (isr_obj[0] != nullptr) {
    isr_obj[0]->isr();
  }
}


// -------------------------------------------------------------------------------------------------
void isr_uart1()
{
  if (isr_obj[1] != nullptr) {
    isr_obj[1]->isr();
  }
}


// -------------------------------------------------------------------------------------------------
Uart::Uart(uart_inst_t* _uartId, int32_t _txPad, int32_t _rxPad)
{
  uartId = _uartId;

  if (_uartId == uart0) {
    irqId = UART0_IRQ;
    irq_set_exclusive_handler(irqId, isr_uart0);
    hw = uart0_hw;
    isr_obj[0] = this;
  }
  else if (_uartId == uart1) {
    irqId = UART1_IRQ;
    irq_set_exclusive_handler(irqId, isr_uart1);
    hw = uart1_hw;
    isr_obj[1] = this;
  }
  else {
    panic("Illegal uartId");
  }

  txPad = _txPad;
  rxPad = _rxPad;

  // To the outside world, a taskhandle_t is an int. Behind the scenes inside FreeRTOS, it is a pointer.
  // We init that pointer to NULL:
  rxTask = 0;

  if (txPad>=0) {
    gpio_set_function(txPad, GPIO_FUNC_UART);
  }
  if (rxPad>=0) {
    gpio_set_function(rxPad, GPIO_FUNC_UART);
  }

  // Enables RX and TX hardware FIFOs
  uart_set_fifo_enabled(uartId, true);

  // Flush the software rxQ
  rxQ_head = rxQ_tail = rxQ;

  // Start with TX and RX interrupts disabled
  txIntsEnabled = rxIntsEnabled = false;
  uart_set_irq_enables(uartId, txIntsEnabled, rxIntsEnabled);

  // UART interrupts are always enabled in the NVIC.
  // Use the individual UART interrupt control mechanisms to select what interrupts to service.
  irq_set_enabled(irqId, true);
}

// -------------------------------------------------------------------------------------------------
void Uart::enable()
{
  hw->cr |= UART_UARTCR_UARTEN_BITS;
}

// -------------------------------------------------------------------------------------------------
void Uart::disable()
{
  hw->cr &= ~UART_UARTCR_UARTEN_BITS;
}


// -------------------------------------------------------------------------------------------------
bool Uart::rxQ_empty()
{
  return (rxQ_head == rxQ_tail);
}

// -------------------------------------------------------------------------------------------------
bool Uart::rxQ_full()
{
  uint16_t* next = rxQ_head++;
  if (next >= &rxQ[rxQ_len]) {
    next = rxQ;
  }
  return (next == rxQ_tail);
}

uint32_t errCnt;

// -------------------------------------------------------------------------------------------------
// This method gets called whenever this UART needs to service an interrupt.
// It executes at ISR level, so all the usual warnings apply!
volatile uint32_t t0_isr, t1_isr, t0, t1, t2, d_isr, dt1, dt2;

// We the FIFO to receive bunches of characters at a time as well as
// the RX timeout mechanism to detect when a UART burst ends.
// In either case, we notify the task that is waiting for serial data.
void __time_critical_func(Uart::isr)()
{
  bool notify = false;
  t0_isr = time_us_32();

  if (hw->mis & (UART_UARTMIS_RXMIS_BITS | UART_UARTMIS_RTMIS_BITS)) {
    // We got here because the FIFO reached a trigger level or we had an RX timeout
    // Either way, we read everything out of the FIFO until it is empty

    notify = true;

    while (!(hw->fr & UART_UARTFR_RXFE_BITS)) {
      uint16_t dr = (uint16_t)(hw->dr);
      if (dr > 0xFF) {
        errCnt++;
      }
      else {
        *rxQ_head++ = dr;
        if (rxQ_head >= &rxQ[rxQ_len]) {
          rxQ_head = rxQ;
        }
      }
    }
  }

  if (notify && rxTask) {
    // Notify the receiving task that more data has arrived
    BaseType_t higherPriorityTaskWoken = pdFALSE;
    t0 = time_us_32();
    BaseType_t result = xTaskNotifyFromISR(rxTask, 0, eNoAction, &higherPriorityTaskWoken);
    t1 = time_us_32();
    portYIELD_FROM_ISR(higherPriorityTaskWoken);
    t2 = time_us_32();
    dt1 = t1 - t0;
    dt2 = t2 - t1;
  }

  t1_isr = time_us_32();
  d_isr = t1_isr - t0_isr;
}

// -------------------------------------------------------------------------------------------------
void Uart::configFormat(int32_t dataBits, int32_t stopBits, uart_parity_t parity)
{
  uart_set_format(uartId, dataBits, stopBits, parity);
}


// -------------------------------------------------------------------------------------------------
void Uart::configFlowControl(bool ctsEnabled, bool rtsEnabled)
{
  uart_set_hw_flow(uartId, ctsEnabled, rtsEnabled);
}

// -------------------------------------------------------------------------------------------------
int32_t Uart::configBaud(int32_t newBaudRate)
{
  uint baud = uart_set_baudrate(uartId, newBaudRate);
  return baud;
}

// -------------------------------------------------------------------------------------------------
// Now enable the UART to send interrupts - RX only
void Uart::rxIntEnable()
{
  rxIntsEnabled = true;
  uart_set_irq_enables(uartId, rxIntsEnabled, txIntsEnabled);

  // Unfortunately, a side effect of calling uart_set_irq_enables() is that it always
  // sets the FIFO length to 4 (the minimum). Set the level here to what we really want.
  hw_write_masked(&hw->ifls, RX_FIFO_WATERMARK_LEVEL << UART_UARTIFLS_RXIFLSEL_LSB, UART_UARTIFLS_RXIFLSEL_BITS);
}


// -------------------------------------------------------------------------------------------------
//
bool Uart::txBusy()
{
  // busy_bits will be 1 until both the TX FIFO is empty and the final stop bit of the char
  // in the tx shift register has been sent
  return hw->fr & UART_UARTFR_BUSY_BITS;
}


// -------------------------------------------------------------------------------------------------
// Since hardly transmit anything, we use a simple blocking implementation to send the byte.
int32_t Uart::tx(uint8_t byte)
{
  while (hw->fr & UART_UARTFR_TXFF_BITS) {
    // Oops: TX FIFO is full
    vTaskDelay(1);
  }

  hw->dr = byte;
  return 1;
}


// -------------------------------------------------------------------------------------------------
int32_t Uart::tx(uint8_t* bytes, uint8_t len)
{
  for (uint32_t i=0; i<len; i++) {
    tx(*bytes++);
  }

  return len;
}


// -------------------------------------------------------------------------------------------------
int32_t Uart::tx(char* string)
{
  uint32_t count = 0;
  while (*string) {
    tx(*string++);
    count++;
  }

  return count;
}

// -------------------------------------------------------------------------------------------------
BaseType_t Uart::rx(uint16_t &c, TickType_t xTicksToWait)
{
  // If someone ever asks to receive a character, we make sure to enable rx interrupts or else we will never receive anything
  rxIntEnable();

  if (rxQ_empty()) {
    if (xTicksToWait == 0) {
      return pdFAIL;
    }
    else {
      // Sleep until we get notified that something has arrived, or we time out
      uint32_t count = ulTaskNotifyTake(pdTRUE, xTicksToWait);
    }
  }

  if (rxQ_empty()) {
    // If the queue is still empty at this point, we must have timed out
    return pdFAIL;
  }
  else {
    // Return the oldest character in the queue
    c = *rxQ_tail++;
    if (rxQ_tail >= &rxQ[rxQ_len]) {
      rxQ_tail = rxQ;
    }
    return pdPASS;
  }
}