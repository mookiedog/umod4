# ECU: Engine Control Unit / Ultramod Software

## ECU

The ECU is the box that controls the fuel injection system for the motorbike.
It is based on a Motorola 68HC11G5 processor.
The stock ECU has a 32Kx8 EPROM for ECU code storage and 512 bytes of RAM.
Yes, you read that right: 512 _bytes_ of RAM.

The same Gen 1 ECU was used on a number of Aprilia motorcycles, including the Mille, Tuono, Futura, and Falco.
Various stock code images exist for the different bikes as well as for different years of production.
The Umod4 Flash has room for over 200 ECU images.
It's unlikely that it would ever need to hold that many images though.
I figure that I will end up with a favorite, and that will be the only one I use.

## Ultramod

The [UM4](../ecu/README.md) part of the project replaces the ECU firmware with a custom data logging firmware.
The main change from a stock firmware is that whenever the new UM4 firmware calculates something "interesting", it sends that interesting data to the RP2040 processor on the umod4 board.
From there, the rest of the umod4 system arranges to get the interesting data gets logged as well as correlated in time with GPS position and velocity information.

The Ultramod firmware started off as a stock 549USA firmware.
This basic firmware was used on Milles and Tuonos.
It is identical to the RP58 firmware, which was the famous firmware containing the "Street" maps and the "Off Road Only" maps.
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
It turns out that the HC11 is still well-supported in terms of an assembler and linker via the 'binutils' package in modern Linux systems.
Binutils provides basically everything except a C compiler.
And since the project does not need a C compiler, we are good to go!
