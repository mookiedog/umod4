# Bugs

## Excessive USB Power Consumption when ECU is Powered Down

Situation: the ECU was powered from a 12V DC supply, plus a 5V LiPo battery plugged into the WP's USB.
Current consumption is approximately 180 mA from the 12V supply.
When the 12V power is removed, the battery should take over.
This appears to happen, but the current consumption out of the battery measures over 300 mA.

If the battery power is removed and restored (no 12V present), battery power consumption will be about 180 mA, as normal.

The HC11 R/!W signal will go to 0 when ECU power is lost.
The voltage translation inverter will convert that to W/!R being '1' when the ECU has no power.
In theory, that should mean that the HC11 data bus will get driven into the umod4.
It also means that VCCA will be 0V, and that the busses will be disconnected.

## Ejecting a card needs to re-init GPS

Any new log file that gets created like due to a hotplug needs to reset the GPS logging back to its default.
Specifically, a new log file needs to store the time-of-day info and fix status, even if the GPS already knew that info.
