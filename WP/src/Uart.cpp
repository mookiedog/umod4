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
  else if (_uartId == uart0) {
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

  rxTask = nullptr;

  gpio_set_function(txPad, GPIO_FUNC_UART);
  gpio_set_function(rxPad, GPIO_FUNC_UART);

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

  int32_t bufferSpaceRemaining = rxQ_tail - rxQ_head - 1;
  if (bufferSpaceRemaining<0) {
    bufferSpaceRemaining += rxQ_len;
  }
  if (bufferSpaceRemaining == 0) {
    // Things are getting critical. The hardware RX FIFO has not overflowed yet, but since our
    // RAM FIFO is full, we can't service this RX interrupt, not any more RX interrupts until
    // somebody calls the rx() method and makes room in the RAM FIFO.
    // All we can do is notify the task waiting on RX data to get to work.
    // When the task calls rx(), RX interrupts will be automatically re-enabled.
    notify = true;

    rxIntsEnabled = false;
    uart_set_irq_enables(uartId, txIntsEnabled, rxIntsEnabled);
  }

  if (hw->mis & UART_UARTMIS_RXMIS_BITS) {
    // The FIFO has hit its watermark trigger level.
    // Note: there is no mechanism to know exactly how many characters are in the RX FIFO.
    // If you get an interrupt for some watermark trigger level, all you know is that there are
    // at least as many characters in the FIFO as the watermark level.
    // Figure out how many bytes we intend to remove from the UART FIFO.
    int32_t readCount = (bufferSpaceRemaining < RX_FIFO_LENGTH) ? bufferSpaceRemaining : RX_FIFO_LENGTH;

    // Calculate if the write pointer into the RAM FIFO is going to wrap during the copy.
    // If it is, we split the copy into two separate loops to avoid performing a wrap test on every
    // character we copy.
    uint16_t* terminalP = rxQ_head + readCount;
    bool wrap = terminalP >= &rxQ[rxQ_len];
    if (wrap) {
      // Adjust the terminalP to reflect its wrapped location
      terminalP -= rxQ_len;

      // Copy data until we fill the last address in the buffer...
      while (rxQ_head < &rxQ[rxQ_len]) {
        *rxQ_head++ = (uint16_t)(hw->dr);
      }
      // ...then wrap the head pointer
      rxQ_head = rxQ;
    }

    // At this point, we are either copying the remainder of the data after wrapping (if any),
    // or we are copying all of the data for an operation that we know can't wrap:
    while (rxQ_head < terminalP) {
      *rxQ_head++ = (uint16_t)(hw->dr);
    }

    notify = true;
  }
  else if (hw->mis & UART_UARTMIS_RTMIS_BITS) {
    // We had a receive timeout. Start off by clearing the interrupt request.
    hw->icr = UART_UARTICR_RTIC_BITS;

    // The FIFO has data, but we don't know how much.
    // We will not read more than we have buffer space to hold though.
    uint32_t maxReadCount = bufferSpaceRemaining;

    while ((!(hw->fr & UART_UARTFR_RXFE_BITS)) && (maxReadCount>0)) {
      maxReadCount--;
      *rxQ_head++ = (uint16_t)(hw->dr);
      if (rxQ_head >= &rxQ[rxQ_len]) {
        rxQ_head = rxQ;
      }
    }
    notify = true;
  }

  // Notify the receiving task that more data has arrived
  BaseType_t higherPriorityTaskWoken = pdFALSE;

  if (notify && rxTask) {
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
  hw_write_masked(&hw->ifls, RX_FIFO_WATERMARK_LEVEL_1_2 << UART_UARTIFLS_RXIFLSEL_LSB, UART_UARTIFLS_RXIFLSEL_BITS);
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