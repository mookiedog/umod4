# To Do

## Current Data Path

EP.Core1 puts 32-bit logging transfers captured from the HC11 bus into the inter-processor FIFO 1->0.

EP.core0 runs a polling loop
while (1) {
  if (fifo not empty) {
    remove 32-bit data
    convert to 16-bit data AA/DD
    enqueue 16-bit data
  }
  if (WP is ready) {
    dequeue no more than 4 bytes
    put them in EP PIO 8-bit serial TX FIFO
      8-bit TX chosen because WP uses silicon UART that only does 8 bit RX
      FIFO length is 8 because the example code joins the FIFOs (RX will not be possible)
      FIFO width is 32-bits
      Current PIO code TXs the bottom 8 bits of each FIFO entry
  }
}

## Proposed Changes

Givens:

* We can guarantee that:
  * the EP and ECU will never break up characters being sent as part of a string
  * the ECU will never break up sending MSB/LSB of a 16-bit value

  Conceivably, those rules could be violated if the EP rebooted in the middle of a string transmission or a MSB/LSB transmission.
  We ignore those cases.
  In fact, we could protect ourselves by having the EP always start off by transmitting a two 0x0000 transmissions.
  That could be used to detect an incomplete string or MSB/LSB operation.
  Any 0x0000 pairs that ended up in the log would be ignored anyway since 0x00 bytes are ignored on a single-byte basis.

Proposal:

1) Use PIO UART on both EP and WP, not just the EP as now.
    1) Change PIO transmission size to be 16 bits (total 18 after start and stop)

1) 16-bit transfers will ALWAYS take the form of a combined LogID/LogData pair. This implies that:
    1) Strings are still sent as 16-bit pairs defining single chars of a string
    1) Writes of 16-bit ECU data will arrive as LogID/MSB then LogID+1/LSB

1) Change the WP ISR to interrupt immediately on receiving a 16-bit value
    1) The ISR will need to process data statefully
        1) Characters will get built into strings.
        When the NULL is received (or a non-string LogID is observed), the whole string captured so far gets logged as 1 string of length N.
        1) 16-bit writes will get statefully recombined into 16 bit data.
        When the LSB byte arrives, we log a 3-byte event (1 logid + 2 data)


The PIO FIFO on the WP end could be 8 long, meaning 8 byte-pairs.
It is 4-long at a minimum.
An 8-long fifo would fill in 8 * 18 uSec per byte-pair (1 megabaud) or 144 uSec.

Plan:

1) Before starting: get a GIT checkin of the current state, the last 8-bit UART communication mechanism.

1) Instead of including the PIO Uart code from the examples directory, we will copy in what we need to both EP and WP source trees.
Update code to use 16-bit transmission units.

1) Update the WP UART ISR to deal with the PIO FIFO, not the UART.

1) The EP code that drains the streamBuffer after being enabled by the WP might need to be altered in case rate limiting of the drain process needs to be adjusted.

## Logfile Decoding and Visualization

I think I need to make a pass over the logfile definition.
It must describe how data gets written.
Why are some things MSB/LSB that can even have another event between them?
Why are others ID followed by some number of bytes?
What IDs do not completely follow a naming convention?
Should the naming convention get expanded for GPS (as Claude suggested) so that a position/velo record might end in _PV_10B to explain that it is 10 bytes in length?

I probably need to explain for each data type how to convert it to something else for displaying.
Simple examples:

* ADC to Volts for battery measurements
* ADC to degrees C for thermistor measurements
* placeholder for things I don't know yet like ADC to manifold air pressure value

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
