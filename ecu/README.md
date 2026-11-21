# ECU: Engine Control Unit / Ultramod Software

## ECU

The ECU is the box that controls the fuel injection system for the motorbike.
It is based on a Motorola 68HC11G5 processor.
The stock ECU has a 32Kx8 EPROM for ECU code storage and 512 bytes of RAM.
Yes, you read that right: 512 *bytes* of RAM.

The same Gen 1 ECU was used on a number of Aprilia motorcycles, including the Mille, Tuono, Futura, and Falco.
Various stock code images exist for the different bikes as well as for different years of production.
The Umod4 Flash has room for well over 100 ECU images.
It's unlikely that it would ever need to hold that many images though.
I figure that I will end up with a favorite, and that will be the only one I use.

## Ultramod

The Ultramod (UM4) firmware replaces the stock ECU firmware with a custom data logging firmware.
The main change from a stock firmware is that whenever the UM4 firmware calculates something "interesting", it sends that data to the EP (the EPROM processor) on the umod4 board.
From there, the rest of the umod4 system arranges for the interesting data to get logged and correlated in time with GPS position and velocity information.

The Ultramod firmware started off as a stock 549USA firmware.
This basic firmware was used on Milles and Tuonos.
The code portion of the 549USA is identical to the code portion of the RP58 firmware, which was the famous firmware containing the "Street" maps and the "Off Road Only" maps.
The "Off-Road Only" maps were accessible by derestricting the intake and exhaust, then clipping a wire going into the ECU to tell the ECU to activate the alternate maps.
The 549USA contains "Street" maps in both locations in the EPROM so clipping the wire has no effect on ECU operation.

## Notes

### 68HC11G5 Processor

It took me a long time to figure out what the ECU processor was.
The G5 variant was not publicized except to OEM designers.
It's like Motorola never admitted that it even existed: datasheets were never released to the general public.
That said, I did manage to find a datasheet long ago on a website in Italy.
The datasheet is included in the ecu/doc directory for your convenience.

### HC11 Toolchain

The HC11 processor was already 15 years old when the Gen 1 ECU appeared on Aprilia products.
It has been effectively obsolete for decades at this point.
As such, it is becoming hard to find tools like compilers for the HC11 anymore.
Fortunately, this project is written entirely in assembly language.
GNU binutils can still target the HC11, and it provides the assembler, linker and objcopy that the project needs.
The one catch is that a 68HC11 build of binutils is not available as an ordinary package: you build it from source, as described in [BUILDING.md](../BUILDING.md).
And since the project does not need a C compiler, we are good to go!

## Where to look

* [src/UM4.S](src/UM4.S) is the ECU firmware, as HC11 assembly language source code.
* [doc/](doc) has the HC11 datasheet and notes on how the ECU works.
