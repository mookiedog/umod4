# EP To Do

The critical path is keeping the intercore FIFO from overflowing.

## Logging Mechanisms

The EP is meant to boot quickly so that it does not delay a rider from getting the engine running.
The WP will not boot as quickly.
This means that any data that the EP will need to buffer its logging data stream until the WP is ready.
The WP will signal the EP that is ready to accept stream data by driving the FLOWCTRL_GPIO to '1'.

The WP needs to receive its log data in "packets", where packets must be shorter than the length of the WP's UART RX FIFO.
A packet is defined as a sequence of data bytes with no transmission gaps between them, followed by a transmission gap long enough to trigger the RX timeout.
Packet arrivals at the WP are signaled by a UART RX TIMEOUT interrupt.
The WP ISR has already received the entire packet, so it can log it from inside the ISR.
Because the logging occurs inside the

On the EP side:
* Insertion requests fail if there is not enough room to hold the entire packet.

As bytes are inserted, the head pointer is NOT advanced.
When the last byte of a packet has been stored, the head pointer gets bumped past the last byte in the new packet as one operation.



Problems:
* how to unload the stream buffer?
    * The WP expects to see byte pairs from the ECU under normal operation
    * This is fine, except that the EPROM info msgs come in binary blobs which are longer than 2 bytes

The log gets decoded via knowing the length associated with each log ID byte

## Create a Log routine

Right now, all logging is performed directly inside the core0 mainloop.
This should get broken out into a subroutine so that the eprom loader code can log info about the EPROMs that it loads.

## Log EPROM Info At Boot
