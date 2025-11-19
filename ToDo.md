# To Do

## Tracking Time

* Get rid of time marks. THey have been replaced by overflow interrupt events.

The log decoder should start time at 0.

The following events are allowed to advance the decoder time:

* OFLO_TS
* CRANK_TS
* CAM_TS
* SPRK_X1_TS, SPRK_X2_TS

We define time 0 at reset.
Time will not start advancing until the timer gets programmed and the first overflow is observed.

As CRANK, CAM, SPRK events arrive, for the most part, they will arrive in the middle of a complete timer period. However, it is possible that a rollover event could be observed by a CRANK/CAM/SPRK event before the OFLO event is observed.

Consider the simplest case:
  16-bit timer      reconstructed 32-bit time
  0x0004 OFLO       0x0001_0004
  0x4000 CRANK      0x0001_4000
  0x8000 CRANK      0x0001_8000
  0xC000 CRANK      0x0001_C000
  0x0006 OFLO       0x0002_0004
  0x0010 CRANK      0x0002_0010

Consider another simple case:
  0x0004 OFLO       0x0001_0004
  0x4000 CRANK      0x0001_4000
  0x8000 CRANK      0x0001_8000
  0xC000 CRANK      0x0001_C000
  0x0006 OFLO       0x0002_0004 This item and the next must be out of order
  0xFFFF CRANK      0x0001_FFFF

Consider a case that cannot be determined:
  0x0004 OFLO       0x0001_0004
  0x0001 CRANK      0x000?_0001 could be interpreted as 0x0001_0001 (out of order with the previous event) OR 0x0001_0001 (after rollover, but before rollover got reported)
  0x0006 OFLO       0x0001_0006


The previous case would be solved if we could guarantee that at least one more TS event occurred somewhere between the two OFLO events:

caseA:
  0x0004 OFLO       0x0000_0004
  0x2000 CRANK      0x0000_2000
  0x0001 CRANK      0x0001_0001 (must indicate rollover, to be proven true by the next event)
  0x0006 OFLO       0x0001_0006

caseB:
  0x0004 OFLO       0x0000_0004
  0x0002 CRANK      0x0000_0002 (out-of-order: must have occurred before the OFLO)
  0x2000 CRANK      0x0001_0001 (must have occurred after the rollover)
  0x0006 OFLO       0x0001_0006

Data shows me that even when the engine is running, I can get two overflows in a row without any AAP events:

[ 56523 @    52.0297s]: OFLO_TS: 6
[ 56549 @    52.0509s]: AAP:    187
[ 56595 @    52.0899s]: AAP:    186
[ 56615 @    52.1059s]: AAP:    187
[ 56639 @    52.1226s]: AAP:    185
[ 56664 @    52.1447s]: AAP:    186
[ 56685 @    52.1608s]: OFLO_TS: 6
[ 56850 @    52.2919s]: OFLO_TS: 8
[ 56979 @    52.4002s]: AAP:    187
[ 57006 @    52.4231s]: OFLO_TS: 76

With the engine running or not, I was getting around 57 VTA events per overflow.



## Modify UM4.S To Track Time When Engine Not Rotating

* The mechanism to detect crank-not-rotating and emit time markers is not working
* The 'find eprom' mechanism in the EP should either document that it is finding an eprom, or not bother emitting the EPROM name. If the 'load range' operation correctly documents that it could not find an EPROM, then the find operation does not need to do anything.

## Bugs To Fix

* Hardware.h only defines PCB 4V0

## Data Path From ECU to WP

### Recombine All XX_log.h files into logid.h?

Right now, I have 4 log header files.
The ECU, EP, and WP all include the base file and they are forced to include it from the same include dir.

The point of doing this was so that changes to say the WP log would not force the ecu and EP firmware to be rebuilt.
That is _perhaps_ useful, but probably not totally useful.
In the long-term, a software update would almost certainly reflash WP and EP/ECU.
Note that the ECU image is always part of the EP image (in the BSON partition), so it cannot be reflashed independently.

It would make things a tiny bit easier on the toolchain if ECU/EP/WP all just included the same global log file that defined everything.
The build subsystem doesn't really care though.

## PCB Support

1) See if I can make 4-bit SD Card operations work

1) Supercap operation
    1) charging
    1) switchover after power fail

1) Do I need a star GND on bottom layer?

## Experiments

1) Power the Pico2W externally to relieve the ECU from powering the umod4 at all.

    Theory: if the ECU power supply has a problem due to the extra load, either by
    average power consumption or short term power spikes, this experiment will remove
    the umod4 board from the equation.

1) Graph all the data to see if there is anything that correlates to the RPM changes and engine loss of power.



## Software Short Term

### Log ALL Ignition Events

At the moment, logs such as 2025-10-22/log.10.decoded show that FC_OFF events are missing for large stretches of time
even though the engine is clearly running.

### Find Out Why Board Draws High Current at ECU Power-Off

If the umod4 is powered via USB, when the ECU gets

## Short-Term Plan

* Test running the system off a 5.4V power supply fed into the Pico2 WP USB port.
  Goal is to see if removing the umod4 load from the ECU power supply affects the engine operation for the better.

## ECU Power Supply Noise From Ignition

## ECU Loses Crank Posn

Seems to be noise-related, perhaps when an ignition event occurs at the same time as the leading edge of a crank event.

## Log Writing Bug

I have many examples where a log appears to be writting starting well after boot.
The log entries that should have been generated by the initial boot HC11 boot process is missing.
Instead, the log begins with the engine already running, meaning at least 3 seconds of data is gone.
The 3 seconds comes from me always letting the fuel pump shut down before starting the bike.

Data is missing at the end of some files too, but that could simply be that LittleFS takes so long to write data
and a write could have been in progress while the key was turned off.

The missing data at the start is more critical right now.
That should simply never happen.

As of now, the EP writes the FIFO data stream in pairs using blocking byte-writes, as fast as it can.


## Debug Power Supply Noise Coupling into CAM and CRANK

1) With bike OFF, check +5V output

    Just in case the inductor does bad things!

1) Check noise on +5

    Using the same cheap scope probes, scope out +5V analog, CAM digital (HC11 pin 7), and CAM analog (G+)

    If it is noisy, then I know that +12 in is still noisy

2) Try out the better scope probes



A high-level list of bugs and features to consider.

## Gah: loading images

There should be one single bottom-level routine that does all loading:

loadEprom(bsonDoc_t epromDoc, uint32_t startOffset, uint32_t length)

On top of that would be:
  loadEprom(const char* name)     which converts the name to a bson doc* and if successful, invokes loadEprom(bson, 0, 32768)
  loadEprom(bsondoc*)             which invokes loadEprom(bson, 0, 32768)
  loadMapBlob(const char* name)   which converts the name to a bson doc* and if successful invokes loadEprom(bson, blobstartaddr, bloblen)
  loadMapBlob(bsondoc*)           which invokes loadEprom(bson, mapblobstart, mabbloblen)


## Logging

* Get the EP to log info about every image it processes while creating the ECU runtime image:
    * 16 Image name (truncated to N chars max)
    * 4 Image M3 ID
    * Image found
    * Image complete M3 checksum OK
    * Image range.addr (16 bits)
    * Image range.length (16 bits)
    * Image range verified after copy to RAM

For the purposes of the big bang bug, I can send out the data as 1 complete packet.
The WP will not get an interrupt until the packet is totally received.


* Does the EP logger need to enforce a minimal time between messages going out?
    The RX timeout interrupt triggers when no more data is received during a 32-
bit period.
At 921600 baud, that's just under 35 uSec of dead time.
Would also need to consider that the ISR will not run until at least 35 uSec later,
but it takes time to do its job, so maybe it is safer to never transmit faster than 100 uSec between packets.
That might be an argument for DMA meaning that the data is already in a RAM buffer, and all that needs to be done is to call the log function.


### WP Logging from EP

Right now, the UART ISR expects character pairs to arrive, signalled by an RX Timeout interrupt.
This could be changed to wait for packets of data to arrive, signalled by RX Timeout.
The RX FIFO is 32 bytes, so a packet could be up to 32 bytes long.

The ISR would pull the data out of the FIFO, put it in a static RAM buffer, then log it as 1 transaction.

## EP Startup

* track all failures while loading images
    * Perform retries on failures
    * Have a backup, like trying to load 549USA if everything fails
        * flash LED if absolutely everything fails

## EP

* Remove all panic()s
  * Add ability to log panic and error information as strings. This would allow panic-style error messages from the EP to make their way to the WP logfile, if nothing else.
* Look harder for a binfile to run if the UM4 file cannot be accessed
  * Perhaps a list of EPROMs to try running in case of flash corruption issues

## ECU/UM4

* Allow the ECU to log string messages?
  * This would let the ECU log its own EPROM string ID. This could also be done from the EP itself because in theory, the EP knows what EPROM it loaded, but the absolute proof would be if the code running in the ECU identified itself. Of course, stock EPROMs would not identify themselves, so maybe this is not useful except for UM4 eproms. Given that the UM4 will be changing over time, maybe this is still useful though.

## EPROM Lib

* Always build a BSON object for every eprom that has a .dsc file, even if the corresponding .bin file is not present.

  If no .bin file is present for a given .dsc file, just leave the .bin element out of the bson object that gets created.
  This simplifies the build process because the CMakeLists.txt file does not need to comment out adding eproms just because the .bin file is not present.

## WP

* Look into OTA mechanisms
  * OTA is problematic on RP2040 processors because they run code from SPI flash, meaning that you can't put two copies in flash at the same time unless the second copy has been built to execute in a different part of the address space. If the code ran from RAM, this would not be an issue, but codebase will be too big to run from RAM.
  * The RP2350 adds address translation for the SPI addresses which allows multiple images to reside in the SPI flash.
  By programming the address translation registers properly, any of the multiple images can be made to appear at a specific, consistent place in the address map.

* Get serial output mechanisms working as a debug channel when the ECU is mounted on the bike
