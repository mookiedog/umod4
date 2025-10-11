# Bugs

## Excessive USB Power Consumption when ECU is Powered Down?

Situation: the ECU was powered from a 12V DC supply, plus a 5V LiPo battery plugged into the WP's USB.
Current consumption is approximately 180 mA from the 12V supply.
When the 12V power is removed, the battery should take over.
This appears to happen, but the current consumption out of the battery measures over 300 mA.

If the battery power is removed and restored (no 12V present), battery power consumption will be about 180 mA, as normal.

The HC11 R/!W signal will go to 0 when ECU power is lost.
The voltage translation inverter will convert that to W/!R being '1' when the ECU has no power.
In theory, that should mean that the HC11 data bus will get driven into the umod4.
It also means that VCCA will be 0V, and that the busses will be disconnected.



## Log Is Missing Initial Data

I still have situations where logs are missing many seconds of data.
For example, the logs from 2025-08-30 show that log 1 and 3 show the EP boot, but logs 5 and 7 both start with the engine running.
This is not possible.

Judging by log.1 and log.3, logs 5 and 7 are missing at least 5000 events, or approximately 10K bytes of data.
If we are able to write approx. 500 bytes of data per write operation, that's about 20 write operations that completely disappeared.
Another consideration is that it takes about 3 seconds from key-on until the fuel pump goes off.
If the bike is not started until after pump-off (as is typical), that's about 4 seconds of time when all writes are lost.

Another note: I am seeing lots of empty logs.
The runs from 2025-08-03 had zero-length logs 2/4/6.

So: is the problem that the writes are being lost, or is the problem that littlefs is losing them?

The logger task was not calling deinit() before starting.
This would potentially leave the lfs pointer as being invalid but non-null because it is a class variable, not defined in BSS space.



## Ejecting a card needs to re-init GPS

Any new log file that gets created like due to a hotplug needs to reset the GPS logging back to its default.
Specifically, a new log file needs to store the time-of-day info and fix status, even if the GPS already knew that info.

## Plans

1) See if it is possible for EP core0 or WP to manage logging de-duplication

    The point is to minimize changes in UM4 HC11 code.
    The HC11 would always dump out all sensor readings.
    It would be up to EP core0 or the WP to decide what data to actually log.
    This could be for multiple reasons.
    Minimization of log size by not storing data which has not changed.
    It could also allow the WP to identify a user-defined subset of data to log. Maybe a user does not care about dwell time, for example.

1) Get the EP to log more data at boot time

    1) EPROM IDs of everything that gets loaded (i.e. what base EPROM, and what mapset (if any))

    1) Get the EP to log the M3 checksum the result of the final loaded EPROM image

    Any data that gets logged at boot time should be sync'd immediately, not waiting for the magic 512-byte limit to show up.
    This would ensure that every time the key gets turned on, something gets permanently logged into the filesystem.

1) Build a more complete test-bench setup

    Specifically, add more adjustable sensors:
    1) crank and cam signal generator
    1) throttle position
    1) spark plugs
    1) injectors (maybe as just LED flashes)
    1) temp sensors (mainly because I have them)



IC302: 4052 Dual 4-1 analog mux
IC101: C177G  Quad comparator

IC601: 4049BF Hex Inverter

IC711: TLC274A
IC712: TLC274A

