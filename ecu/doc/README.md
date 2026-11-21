# ECU Documentation

This directory holds the documentation for the Gen 1 ECU: the notes on how it works, and the HC11G5 datasheet.

## Notes on how the ECU works

* [Operation.md](Operation.md): how the engine's four-stroke cycle maps onto the ECU's 12 crank reference (CR) periods, which injector is which, and why the CR periods do not line up exactly with the strokes
* [Ignition.md](Ignition.md): how the ignition tables in the Aprilia RP58 firmware are constructed and used, and how the ECU sees the crankshaft
* [TPS_OPERATION.md](TPS_OPERATION.md): how the throttle position sensor works, and how the umod4 calibrates it

These notes use the images in the `images` directory, and `OperationalSequencing.jpg`, the sequencing diagram from the Aprilia documentation.

## HC11G5 datasheet

The HC11G5 datasheet, [68HC11g5.pdf](68HC11g5.pdf), is stored here mainly for convenience.

The original Motorola documentation was very difficult to find, or at least it was back in the day.
It contains the timing diagrams used to develop the umod4 hardware and software.
