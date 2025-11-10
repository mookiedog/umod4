# Binary Data Log Format

The logfile will contains many different varieties of data, perhaps 40 or more kinds.


### Log Entries

Log entries are identified by a ID byte followed by 1 or more bytes of data.
The amount of data that follows is defined on a per ID-byte basis.

### Timestamps

The data associated with certain IDs are explicit timestamps.
The timestamp is always represented by an unsigned 16-bit value, generated from a free-running 16-bit timer inside the ECU hardware with a resolution of 2 microseconds.
It will be sufficient (though inaccurate) to consider that the counter starts at 0x0000 when the ECU boots, and free runs from there.

The ID byte defines what event occurred, and the timestamp defines when it occurred, relative to the last event that was timestamped.
The log generator guarantees that timestamped events will arrive fast enough that we need to worry about missing an overflow of the 16-bit timestamp counter.

Other events in the logfile are not explicitly timestamped.
However, it is certain that the items in the log are stored in the order that they occurred.
If an item with ID A precedes an item in the log with ID B, we can state with certainty that A preceeded B in time.
Likewise, untimestamped events are known to have occured after the last observed timestamped event, and before any timestamped event that occurs after them.
The precise time that they occurred is actually not important, only the sequencing.
We will create fake timestamps for untimestamped log items that guarantee proper sequencing, even across data streams.

While processing a log, we will maintain elapsed 'time' as a 64-bit value with nanosecond resolution.

For the purposes of decoding a log, there is an assumed timestamp event of 0x0000 as the first log entry.
Timestamped events advance time to the appropriate number of microseconds defined by the timestamp counter value.
The log generator guarantees that timestamped events appear often enough in the log that watching the 16-bit microsecond timestamp will be enough information to maintain the 64-bit logtime.
Specifically, any time that a new timestamp is encountered, the log decoder will compare the new timestamp with the previous timestamp. If the new timestamp is <= the previously encountered timestamp, the counter must have rolled over.
This means that the new 64-bit logtime would have to account for the rollover while being set to the proper amount of microseconds for the new timestamp.

Un-timestamped events are known to have occurred in the sequence that they are encountered in the log, even though the exact time they occurred is unknown.
By definition, the exact time an un-timestamped event occurs is not important or else it would have defined to generate a timestamp.
The sequencing of un-timestamped events is important though.
To maintain sequencing, un-timestamped events will be given a fake timestamp.
As each un-timestamped event is encountered in the log after a time-stamped event, the un-timestamped event will be assigned to be the most recent timestamp plus 1 nanosecond for each un-timestamped event that was encountered before it.
Example:

* A timestamped event occurs at time M0 microseconds.
The 64-bit timestamp T maintained by the log decoder is updated to the correct amount of nanoseconds based on the new timer value of M0 microseconds, and if an overflow has occurred since the previous timestamped event.
* An un-timestamped event E0 is found next in the log. It will be defined to have occurred at 64-bit time T+1.
* A second un-timestamped event E1 is found next in the log. It will be defined to have occurred at 64-bit time T+2.
* A timestamped event occurs at time M1 microseconds.
The 64-bit timer is updated appropriately, as before.
* An un-timestamped event occuring next in the log will be assigned the new 64-bit time, plus 1 nanosecond.

Notes:

* If the first log entry is actually a non-timestamped event, it will be defined to have occurred at 1 nanosecond past time 0.
* The first log entry observed in the file is assumed to have occurred before the 16-bit counter overflowed for the first time.
This is almost assuredly NOT the case, but it is a simple way to get started.
From then on, the passage of time is known to be accurate.

#### Timestamp Considerations

The idea of maintaining timestamp resolution at nanoseconds inside the visualizer would allow many thousands of untimestamped events to be sequenced in between timestamp events.
We expect that there might be 10-ish un-timestamped events in between timestamped events in the worst case. 