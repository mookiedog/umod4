# To Do

## Super Short Term

* UM4
  * Change VTA reports so that the upper 6 bits contain the upper 6 bits of the TCNT1 timer.
  The idea would be to give supply a crude advancement of time in between crankshaft events.
  It would also get rid of needing OFLO and HOFLO events since there are plenty of VTA events per counter overflow.
  Resolution of time advancement would be 128 uSec
  Logs would get bigger since VTA would always get sent, not just when it changed.

* Work on Getting the EP flashed from the WP
  * Convert the low-level SWD stuff to use the code from [Raspberry Pi Picoprobe](https://github.com/raspberrypi/debugprobe/tree/master)
    * Uses PIO instead of bit-banging
    * 100% sure it works instead of the reflasher code
  * Extract what I need from the reflasher code
    * send data, verify data, issue programming commands, release control, etc.
* 4-bit SD Card accesses
  * Would speed writes when logging, reads when reflashing EP
  * THere is a pi pico version in examples
  * There is another example at zuluscsi, GPL-3 under continuous development too

* Add wifi to WP
  * auto-connect to wifi at boot
  * provide a shell so that I could connect in from my desktop

### Bugs

## Spark Events Not Getting Logged

Proposal: Add ISRs for the spark detectors input capture events, IC3 (coil 1) and TO5I4 (coil 2).
The ISRs would just log the captured timestamp for SPRK1 and SPRK2 events.
It may be that the crankshaft processing code that manually reads injector IC events during CR6/11 is not happening.

Background:

There are times when SPRK events are not logged.
They are missing in terms of spans of seconds when no spark events are recorded.
The engine is obviously running, so somehow the spark events are being lost.
In log.3 for example, they stop at 11.35 seconds and start up again at 13.27 seconds.

I have evidence of missing spark events as far back as 11/15 in log.11, which was when spark events were first added to the log.
Log.11 shows a gap in spark events between events 1576 and 3451 when the engine was clearly running, just like log.3 above.

They could be getting lost in the following places:

1) EP: inter-core FIFO is full when CORE1 inserts a new log event
2) EP: lost because PIO TX32 FIFO is full due to enforced gaps between insertions
3) WP: PIO RX32 FIFO is full when a 32-bit transfer arrives from EP

In the initial log.3 example, spark are lost when they are being generated approximately 30 per second, and there is a nearly 2 second gap in the data.
That's about 60 in a row.

Which of the items above could account that kind of data loss?
Also: it is JUST spark items getting lost.
Crankref events and ADC data keep coming throughout that whole period.

It seems unlikely that it is the inter-core fifo.
The core0 mainloop pulls things from the inter-core fifo and puts them in a gigantic streamBuffer queue immediately.
That queue will never fill.

There is a rate-limiter on how items get removed from the gigantic queue and put in the TX32 PIO UART.
But nothing will get lost between the streamBuffer and the TX32 FIFO.

On the WP side, the PIO RX32 queue is 8 elements long.
If interrupts were inhibited for some reason for long enough (8 * 64 uSec or 512 uSec), then the RS32 FIFO could overflow.

One interesting thing would be that spark events always arrive at the end of a burst: CRANK_TS, CRID, then SPRK, SPRK.
But again, they are immediately buffered in the gigantic queue, then rate-limited transmitted to the WP.
The only way they could be lost is if the intercore fifo overflowed while being buffered into the giant queue.


## Visualizer

* Total Visualizer re-write
  * Includes reworking the HDF5 file, so it affects decodeLog.py too
* work out how to display injector pulses
* Build a Log Viewer into visualizer
  * Right click on any event in the graph window to view it in the log
* Get viz packaged up for others to play with

Features to add:

* Coil dwell bars
* Get rid of undisplyable streams from selection list
* Get time markers events drawn
  * GPS
  * CPU events
  * EPROM load events


## WP

The big items are:

* Complete EP Reflash Mechanism
* WiFi
  * Auto Log Upload
  * OTA reflash:
    * WP
    * EP

### WiFi

* __OTA firmware updates__
* __Unattended Log offload by WP__

## decodeLog.py

### LogDecoder Not Sync'd to Constants in XX_log.h Files

If any of the xx_log.h files are changed, it requires a manual step to make sure that logDecoder and visualizer might mods to deal with the changes.
It would be better if this could be automated, or at least detected and flagged if they get out of sync.

### Recombine All XX_log.h files into logid.h?

Right now, I have 4 log header files.
The ECU, EP, and WP all include the base file and they are forced to include it from the same include dir.

The point of doing this was so that changes to say the WP log would not force the ecu and EP firmware to be rebuilt.
That is _perhaps_ useful, but probably not totally useful.
In the long-term, a software update would almost certainly reflash WP and EP/ECU.
Note that the ECU image is always part of the EP image (in the BSON partition), so it cannot be reflashed independently.

It would make things a tiny bit easier on the toolchain if ECU/EP/WP all just included the same global log file that defined everything.
The build subsystem doesn't really care though.

## ECU

### Log ALL Ignition Events

At the moment, logs such as 2025-10-22/log.10.decoded show that FC_OFF events are missing for large stretches of time even though the engine is clearly running.

### Find Out Why 4V1 PCB Draws High Current at ECU Power-Off

### ECU Power Supply Noise From Ignition

#### ECU Loses Crank Posn

Seems to be noise-related, perhaps when an ignition event occurs at the same time as the leading edge of a crank event.

#### Debug Power Supply Noise Coupling into CAM and CRANK

1) With bike OFF, check +5V output

    Just in case the inductor does bad things!

1) Check noise on +5

    Using the same cheap scope probes, scope out +5V analog, CAM digital (HC11 pin 7), and CAM analog (G+)

    If it is noisy, then I know that +12 in is still noisy

1) Try out the better scope probes

## EP

* Remove all panic()s
  * Add ability to log panic and error information as strings. This would allow panic-style error messages from the EP to make their way to the WP logfile, if nothing else.
* Look harder for a binfile to run if the UM4 file cannot be accessed
  * Perhaps a list of EPROMs to try running in case of flash corruption issues

## EPROM Lib

* Always build a BSON object for every eprom that has a .dsc file, even if the corresponding .bin file is not present.

  If no .bin file is present for a given .dsc file, just leave the .bin element out of the bson object that gets created.
  This simplifies the build process because the CMakeLists.txt file does not need to comment out adding eproms just because the .bin file is not present.

## PCB

### Remaining Things to Think About

1) See if I can make 4-bit SD Card operations work.
I know that they should because the RP2350 Rabbit SCSI adaptor I bought for the logic analyzer uses 4-bit PIO SD.

1) Supercap operation?
    1) charging
    1) switchover after power fail

1) Do I need a star GND on bottom layer?

### Experiments

1) Power the Pico2W externally to relieve the ECU from powering the umod4 at all.

    Theory: if the ECU power supply has a problem due to the extra load, either by
    average power consumption or short term power spikes, this experiment will remove
    the umod4 board from the equation.

1) Graph all the data to see if there is anything that correlates to the RPM changes and engine loss of power.

