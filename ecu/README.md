# ECU: Engine Control Unit / Ultramod Software

## ECU

The ECU is the box that controls the fuel injection system for the motorbike. It is based on a Motorola 68HC11G5 processor. The stock ECU has a 32Kx8 EPROM for ECU code storage and 512 bytes of RAM.

The same Gen 1 ECU was used on a number of Aprilia motorcycles, including the Mille, Tuono, Futura, and Falco. Various stock code images exist for the different bikes as well as for different years of production. The Umod4 Flash has room for over 200 ECU images. By default, the Umod4 contains _all_ of the stock images inside it.

## Ultramod

The Ultramod firmware project replaces the ECU firmware with a custom program. The main change is that whenever the new firmware calculates something "interesting", it sends that interesting data to the RP2040 processor on the umod4 board. From there, the interesting data gets logged, correlated in time with GPS position and velocity information.

The Ultramod firmware started off as a stock 549USA firmware. This basic firmware was used on Milles and Tuonos. It is identical to the RP58 firmware, which was the famous firmware containing the "Street" maps and the "Off Road Only" maps. The "Off-Road Only" maps were accessible by derestricting the intake and exhaust, then clipping a wire going into the ECU to tell the ECU to activate the alternate maps. The 549USA contains "Street" maps in both locations in the EPROM so clipping the wire has no effect on ECU operation.

## Notes

### 68HC11G5 Processor

It took me a long time to figure out what the ECU processor was. The G5 variant was not publicized except to OEM designers. It's like Motorola never admitted that it even existed: datasheets were never released to the general public. That said, I did manage to find a datasheet long ago on a website in Italy. The datasheet is included in the ecu/doc directory for your convenience.

### HC11 Toolchain

The HC11 processor was already 15 years old when the Gen 1 ECU appeared on Aprilia products. It has been effectively obsolete for decades at this point. As such, it is becoming hard to find tools like compilers and assemblers and linkers that know how to work with it. The Ultramod code was originally written using some tools I found on the web. Those tools don't work anymore though - modern Windows won't run them even in compatibility modes.

The best I could do was to find an ancient 2005-ish timeframe set of GNU tools that still work with the HC11. Unfortunately, the C compiler for that toolchain does not work anymore. Fortunately, all the Ultramod needs is an assembler (as), a linker (ld), and an objcopy executable. I found working copies of these tools. Because these tools are becoming very hard to find, I have included their binary executables as part of this repository.
