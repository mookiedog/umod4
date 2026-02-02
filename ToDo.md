# To Do

## Short Term

### Server

* reflash upload is not 100% reliable. Need a retry or something.
* server should store logs by device-name/date/log_xx.um4



## Bugs

### SDIO Checksum errors

SDIO checksum error in data reception 3622225152 4294967295
SDIO checksum error in read 2 12
lfs_read ERROR: sector=52 count=12 err=-10


## Development

General:

* Log build information
  * ECU
  * EP
  * WP

### EP

### WP

* Get WP OTA working.
  Can the WP use the same mechanism as the EP, in the sense that it could run a program in RAM that would reflash itself?
  The big difference is that instead of data magically appearing in mailboxes, the WP would need to read it from a file on the SD Card.
  Perhaps this could be done by ensuring that a bunch of the code needs to be RAM-resident:
    * LittleFS
    * SDIO driver
    * reflash code itself

### Server

* General cleanup.
  * It feels kind of buggy
  * UI should get examined

## Goals for 2026

* WiFi
  * OTA updates
    * WP

* Logging data streams
  * log enough info for viz to be able to display what part of the maps are being used on every engine cycle
  * Ignition
    * Figure out how to tell when the coil dwell cycle starts
    * bonus points for figuring out the calculation for length of dwell and start time
  * Figure out the fuel injection maps
    * Not just the maps, but the post processing for things like
      * cold starts
      * "acceleration pump"

* Get PCB 4V2 designed & fabricated

## Short Term
* Figure out what the target ADC reading is for DIAG mode when setting the TPS position

### Bugs

* Data is getting lost in the log. It looks like certain write operations are failing or getting lost, or a buffer is overflowing.
* looks like time is going backwards with the new VTA/time reports

## Visualizer

* Small Potatoes List
  * Right now, all the graphs tend to get drawn at 85% of height of graph window. That puts them all on top of each other, especially if they are not changing much. Like throttle and GPS velocity lines tend to sit on top of each other.
  * create a mechanism to set preferred stream/marker order in yaml
  * Split the stream selector window into groups
    * Graph streams
    * Marker streams

* Build a Log Viewer into visualizer
  * Right click on any event in the graph window to view it in the log
* Get viz packaged up for others to play with
  * Start with Windows and Linux releases.
  MacOS to follow once the processes are ironed out.

Features to add:

* Coil dwell bars
  * GPS
  * CPU events
  * EPROM load events

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

## PCB

### Remaining Things to Think About

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
