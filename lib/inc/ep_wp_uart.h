#if !defined EP_WP_UART_H
#define EP_WP_UART_H

// The EP sends its data stream to the WP over a one-way UART that both processors implement in PIO:
//   - the EP transmits with lib/pio/uart_tx32.pio
//   - the WP receives with lib/pio/uart_rx32.pio
// The wire in the other direction is not a UART: the WP uses it as a flow-control line.
//
// The PIO programs cannot include this header, so the framing below is written into the PIO programs themselves.
// If the framing ever changes, both PIO programs must change together.
//
// Framing ("32n1"):
//   - the line idles high
//   - a start bit ('0'), then 32 data bits with the LS bit of the LS byte first, then a stop bit ('1')
//   - no parity bit
//   - each PIO program spends 8 PIO clock cycles per bit
//
// Each word on the link carries a log ID plus up to 2 bytes of data.

// Data bits per word on the link.
#define EP_WP_UART_WORD_BITS    32

// The EP-to-WP link runs at 2.5 megabaud.
#define EP_TO_WP_BAUDRATE       2500000

#endif
