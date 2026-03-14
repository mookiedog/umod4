# To Do

## Issues

## Next Checkin

* Come up with a way for the EP and WP to detect if the ECU is powered
  * True solution might be a hardware check of +5_ECU into a GPIO
  * For the purposes of the webpages: we know that if the ECU is powered, it will be sending a data stream. If we see no RX32 data for even a short period of time (0.1 Sec), then the ECU must not be powered

* .dsc files should get incorporated into the EPROM library process
  It's mainly about capturing the descriptive information in the .dsc files so it can be displayed on the phone screen.

* ECU Live View
  * Is it worth it? ECU data flies by too fast. Displaying things like injector pulse width or CRID is pointless. What really needs to be observed in real time?
    * CO1/CO2 deadband trim pot settings, just so a user could make them replicatable
    * VTA throttle angle, to allow setting the TPS without the diag connection
    * L000C though L000F: Error registers
    * THA/THW Air/water temp
    * VM Voltage monitor
    * CAM error
    * NoSpark error

* GPS Live View
  * Should this just be part of "Live View"
  * GPS fix status, satellite count, lat, long, time

* Log Info: This not real time, but instead tracks peaks during a ride
  * high-water in the merge buffer
  * max write time
  * max sync time
  * max data rate

  * Max data rate could actually be calculated by the log decoder. It knows the time, and it can calculate the number of bytes between any two timestamps approx 1 second apart.
  * It cannot calculate the high-water in the merge buffer though.

## Bugs

## Development

General:

* Log build information
  * ECU
  * EP
  * WP

### EP

### WP

### Server

* General cleanup.
  * UI should get examined

## Goals for 2026

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


## ECU

### Find Out Why 4V1 PCB Draws High Current at ECU Power-Off

### ECU Power Supply Noise From Ignition

#### ECU Loses Crank Posn

Seems to be noise-related, perhaps when an ignition event occurs at the same time as the leading edge of a crank event.


## EP

* Remove all panic()s
  * Add ability to log panic and error information as strings. This would allow panic-style error messages from the EP to make their way to the WP logfile, if nothing else.

## PCB

### Remaining Things to Think About

1) Supercap operation?
    1) charging
    1) switchover after power fail

1) Do I need a star GND on bottom layer?

