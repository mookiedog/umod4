# umod4EP
This codebase defines the fake EPROM functionality for the Umod4 circuit board.

This portion of the overall Umod4 project uses a Raspberry Pi RP2040 to replace
the 27C256 EPROM found inside an Aprilia Gen1 fuel injection computer (ECU).

In conjunction with a special ECU firmware, the RP2040 'fake eprom' is designed
to receive a data stream of 'interesting' information from the modified firmware
in the ECU. This information is passed on to another processor on the Umod4 board
where the ECU data stream is correlated to GPS location and velocity information
which gets logged to a Micro SD card. The resulting log is a source of information
that can be used for performance tuning, or just fun.

Because the RP2040 Flash address space is so much larger than a 27C256 (16M vs. 32K),
the RP2040 has space for more than 200 EPROM images. Future enhancements will allow
a rider to select a specific ECU EPROM image before going for a ride.

More information on the Umod4 project can be found here:
https://www.island-underground.com/projects/umod4
