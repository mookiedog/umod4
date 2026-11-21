# EP: EPROM Processor

The **E**PROM **P**rocessor (**EP** for short) is an RP2040 chip, a dual-core 125 MHz ARM Cortex M0+ processor by Raspberry Pi.

The EP has two major tasks.
Its first task is to act like a replacement for the ECU EPROM chip.
The HC11 processor in the ECU is completely unaware that EP is not just a normal EPROM.
The EP's second major task is to capture the raw ECU data stream arriving from the special [ECU logging firmware](../ecu/README.md), then pass it on to the [Wireless Processor](../WP/README.md) (WP) for logging.

Core 1 in the EP is used to implement the EPROM functionality.
Once the HC11 is running, core 1 spends 100% of its time fielding HC11 bus accesses.
Core 1 captures the raw ECU logging data stream but does no processing: it only has time to hand it off to core 0.

Core 0 is responsible for processing the incoming data stream from core 1, then forwarding it to the WP.
Core 0 also manages the Flash memory to prepare an ECU image when the ignition key turns on.
The ECU images are stored in a special partition of the Flash.
At boot up, core 0 is responsible for constructing an EPROM image that it will present to the ECU.
A library of EPROMs is contained in the EP's serial flash.
The EPROM image presented to the ECU can be constructed by combining the code area of one EPROM with the ECU maps from a different EPROM.
A typical combination might be to combine the ECU logging firmware with the maps from an existing factory or aftermarket EPROM.
This essentially can turn any factory EPROM into a logging firmware.

Core 0 is underutilized from a CPU perspective, but there was no alternative: the EP is IO-limited in this application.
Emulating an EPROM interface uses up 27 of the RP2040's 30 GPIO pins.
One of the remaining pins is used to display status info on an LED, used to debug and verify the HC11 EPROM timing using a scope.
The remaining two GPIOs implement a communication channel between the EP's Core 0 and the WP.

## More than EPROM

Because the EPROM functionality is a software construct now, we can expand on a traditional EPROM's functionality in some interesting and useful ways.
For example, the EPROM code allows for unused parts of the EPROM address space to act like RAM instead of read-only EPROM.
The ultraMod ECU firmware can optionally take advantage of this by relocating the processor stack into a new RAM area deep inside the EPROM address space.
This has a number of potential benefits.
Firstly, it ensures that the unexpected stack excursions will not corrupt the rest of ECU RAM under essentially any circumstances.
If the ECU stack ever wanders outside of its proper boundaries, it will be accessing read-only memory.
This will have a beneficial side effect of causing the processor to crash in short order.
That might sound bad, but it beats a standard stack overflow event that silently corrupts some part of the global RAM space resulting in undefined and peculiar ECU behavior.
Finally, the increased RAM will be beneficial for people writing their own ECU software because more RAM is always a good thing.

Another feature to aid people writing their own ECU code is that the RP2040 is set up to log all HC11 accesses to the EPROM address space.
All accesses to the EPROM address space (including the converted RAM space[s]) are logged to a circular buffer maintaining the last 8,192 bus accesses.
This could be very useful when debugging HC11 code changes.

## Building and Debugging

To build the EP, see [BUILDING.md](../BUILDING.md).
To debug it, see [DEBUGGING.md](../DEBUGGING.md).
