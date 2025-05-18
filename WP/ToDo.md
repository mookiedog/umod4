# To-Do List

## To Do Features

* EP Feature: change panic() routine to flash the LED "SOS"
Should actually try to get rid of all panic events though!!

* Add renaming of temp files to time-stamped version.
Files of the form log.N get renamed to log.YYMMDD-HHMMSS

* EP must buffer the ECU log data until the WP sends a message saying it is OK.
  At the moment, all the early boot messages disappear because the WP was not ready to receive them.
  Once the WP signals the EP that it is ready, the EP should not immediately dump its data over.
  There could be a lot, and it would swamp the WP.
  The WP buffers incoming serial data directly into the log buffer, which is malloc'd and currently 32K bytes.

* Incorporate Picowota to allow for OTA programming opportunities.

    The issue with Picowota (or perhaps any OTA mechanism) is that the code running on the Pico2W needs to detect
    that I want to push an update from downstairs.
    Like is says in the doc, maybe you need to push a button on the WP or have some web interface.
    The bottom line is that I would need something like a trivial webserver where I could click a webpage hosted by the WP
    telling it to enter wireless bootloader mode so I could send it an update.

## Logging Operation

Get ECU data stream logged.
This requires getting the UART ISR to call the logger method to insert data.
The potentially weird thing is that we must insert complete ECU data pairs.
It would be problematic if the ISR was invoked as data was coming in, and it inserted 3 bytes while the 4th was coming in, but
the GPS snuck in a logging operation before the 4th arrived.
Is there some way to know how many bytes are in the receive Q?
I don't think so.

## Minimizing log usage

The GPS should define a system state as follows:

States:

* unknown
* time valid
* position valid
* time_and_postion_valid
* not_moving
* moving

The idea:

* if time and position are not valid, there is no sense logging any GPS data
  * It does make sense to log ECU data though
* when time becomes valid, we can rename the log file
* if we are not moving, don't bother logging GPS locations or time information
  * Like if the system is being tested on a bench
  * if we are waiting for a traffic light to change
  * If the bike is warming up in the driveway

I think it makes sense that the system should start off in "moving" state,
and transition to "not_moving".
This ensures that we store at least two positions before stopping recording them because they are not changing.
It means that even on a bench, we will record a couple of positions which helps verify that the recording code is doing what we want.

Or is it minimally stateful?

Like, as a nav_pvt msg comes in:
* we check lat/lon to see if we are moving, and just set a flag for the rest of nav_pvt processing.
  * if moving, log lat/lon
* if alt is different enough, log alt
* if year has changed, log year, set log-time-change
* if date has changed, set log-time-change
  * if log-time-change, log date
* if hour has changed, set log-time-change
  * if log-time-change, log hour
* if min has changed, set log-time-change
  * if log-time-change, log minute
* if moving or log-time-change
  * log changes to seconds/msecs


## How Does LittleFS work?

from: https://arduino-pico.readthedocs.io/en/latest/fs.html

* Filenames are assumed to be in the root directory if no initial “/” is present.
* Opening files in subdirectories requires specifying the complete path to the file (i.e. LittleFS.open("/sub/dir/file.txt", "r");).
* Subdirectories are automatically created when you attempt to create a file in a subdirectory, and when the last file in a subdirectory is removed the subdirectory itself is automatically deleted.


## Filesystems, LFS, SD Cards, Logging

How does it all work?

From a system point of view, the Logger object just wants to read and write to a filesystem.
If the filesystem was not working, it would just let writes fail.
The filesystem would exist on an SD card, but honestly, the logger wouldn't care if the filesystem was on the SPI serial flash.

Multiple things could use the filesystem though.
For example,the Shell could be examining the filesytem while the logger was putting stuff into it.

In a perfect world:

* the system would work fine with no SD card installed
* if an SD card got hotplugged, the system would:
  * get the SD card initialized
  * get LFS to mount the filesystem on the SD Card (formatting it, if necessary)
  * get the logger to create a new logfile

If an installed SD card got removed, the system would:

* de-init the SD card in a fashion that lfs operations would fail appropriately

The SD card is stateful to manage the fact that it can be hot-removed or hot-plugged.
The logger is stateful so that it can tolerate:

* not having an SD card
* seeing an SD card appear (or does it see the filesystem appear/disappear?)
  * creating a logfile
* writing to an existing filesystem

What do I know:
* It is the SdCard object that detects when a card gets hotplugged
  * a filesystem can't magically tell when an SdCard is inserted

As things stand, the SdCard remounts the filesystem

## Changes to Consider

### Update BUILDING with everything learned from ptwd project

### Deal with binutils automatically, build the 68HC11 stuff as needed

Binutils can be downloaded from a github mirror.

## WP

### Cmd Interface

Why?

* lets me work with files on SD cards
  * ls, rm files
  * format
* Lets me control the system at runtime instead of build time
  * turning debug info on or off
  * enable/disable logging
* could it be used to control the EP?
  * set default file to run?


### Add EP to WP Serial Link

### Log File Management

Do I want to print out the directory when we boot?
Do I want to auto-delete files?
Is there any way to tell how much space is left on the device?

### Data Logging

How to log the data?
In the old days, I maintained 2 separate log files: one for ECU info, one for GPS data.
ECU data is always 2 bytes: addr, data.

I should reserve a block of addresses for use by GPS.
If an address is in the ECU range, then it is 1 byte to follow.
If an address is in the GPS range, then it has a variable length (defined somehow, probably in a table).

What GPS data do I want to log?

| offset  |  size | data |
| -------- | ------ | ----- |
| E0 | 12 | lat/long/velo |
| E1 | 4 | altitude |
| E2 | 1 | fix status |
| E3 | - | |
| F0 | 1| time 100 msec ticks (signed) |
| F1 | 1 | time secs |
| F2 | 1 | time mins |
| F3 | 1 | time hours |
| F4 | 1 | time day of month |
| F5 | 1 | time month |
| F6 | 1 | time year (plus 2000) |


All data logging writes to a semaphore-protected RAM buffer so that ECU data and GPS data get inserted atomically.
When the buffer gets to be at least 1 block in length, the logger will write a block and remove it from the circular buffer.

GPS messages I process include:
* NAV-TIMELS
  * time info plus validity info
* NAV-PVT
 * lat/lon/alt/velo/fixtype

lat/long are int32 that need to be multiplied by 1e-7.
That means the fractional precision is 7 digits
48.101986, -122.787022

## Outstanding Bugs

### None

## Fixed

### GPS UBX Checksum Errors

Errors are very rare now. Problem seemed to be in RX ISR not properly dealing with draining FIFO.
