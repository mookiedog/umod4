# To Do

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
