# PIO Files

This directory holds the PIO programs that are specific to this project: the two halves of the 32-bit UART that carries the ECU log stream from the EP to the WP.

* `uart_tx32.pio` is the transmitter. The EP builds it.
* `uart_rx32.pio` is the receiver. The WP builds it.

The baud rate and the framing of the link are described in [lib/inc/ep_wp_uart.h](../inc/ep_wp_uart.h).
The PIO programs cannot include a header, so if the framing ever changes, both programs must change together.

Ordinary PIO programs, such as the WS2812 one that drives the status LED, are not kept here: the builds use the copies in the pico-examples checkout.
