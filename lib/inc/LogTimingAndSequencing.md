# Logfile Format

The logfile format is primarily designed for space-efficiency since they can be potentially huge (hundreds of megabytes).

A log file will contain data from a number of different data streams.
Data is written in chunks called events.
Events will comprise different amounts of data, depending on the event's log identifier, or LOGID.
Each LOGID is a unique numeric value allowing software to determine how many bytes following the LOGID in the log data belong to that LOGID's event, and how to interpret that data (bool, float, a byte of raw sensor units, etc).

### Time Ordering of Streams

The logfile generation process guarantees that events sharing a LOGID will always appear in the log in increasing time order.
However, it is _not_ guaranteed that the log's interleaved events from different data streams will appear in the log in increasing time order.
Specifically, a log event N of stream type S1 at time T may be followed by a log event N+1 of stream type S2 at time T-X, which is before time T of the event that preceeded it.
This will be rare, but it will happen.

## Log Timestamps and Event Sequencing

To save log space, the only events to get timestamped are those that need know precisely when they occured.
For example, tracking the precise time that the crankshaft passed through its 60 degree subrotations
is critical to understanding how fast the engine is turning at any given point in time.
In contrast, it is not important to know precisely when the ECU measured the coolant temperature.
Knowing the approximate time that the coolant temperature was measured is close enough.
This is accomplished through sequencing: knowing that the coolant temp measurement event occurred somewhere
between two known timestamped events provides enough precision as to when it was actually taken.

In short, while only certain events get timestamped, untimestamped events will always get _sequenced_ correctly in relation to the timestamped events.

There is a small complication in that certain events look like timestamps, but they are not.
We break the events that report times into two categories:

* Retrospective events (true timestamps: the event _has happened_ at this time), with LOGID naming convention ending in _TYPE_TS. Example:
    * LOGID_ECU_CRANKREF_START_TYPE_TS: marks that we have just seen the start of a new 60 degree crankshaft subrotation period
* Prospective times (the event _will happen_ at this time, but has not happened yet) use a LOGID naming convention ending in _TYPE_PTS. Example:
    * LOGID_ECU_F_INJ_ON_TYPE_PTS: A calculation event has completed that defines precisely _when_ in the future the front injector should turn on. 


## Creating New Visualizer Log Events

The injector event is useful to consider: knowing _when_ the calculation was completed is only interesting in terms of event sequencing. However, the precise time that the injector will turn on will be interesting if someone wanted to visualize the log events.
As a result, when encountering an injector ON event, the logdecoder generating the H5 file for the visualizer might want to put two events in the H5 log:

1) an event that visualizes when the calculation was completed (the actual log event, where the event time comes from event sequencing information)
1) an new event not present in the original log that shows precisely when the prospective event actually occurred (based on the prospective timestamp)


## Event Reording

_Note: The binary log as stored on the SD Card in the ECU will never be modified.
It is important that its data always remains as originally recorded._

There are a number of corner cases that need to be dealt with when logging events using a free-running counter that can roll over periodically.

Certain events are based on input-capture silicon in the HC11.
An example is when the crankshaft sensor sees the leading edge of a protrusion on the alternator rotor indicating the beginning of another 60 degree crank sub-period.
The timestamp for this event is captured by silicon, but reported some time later by an interrupt service routine (ISR).

The HC11 timer rollover event causes a timer overflow ISR to run.
The ISR reads the current timer count (should be close to 0x0000) and logs it.

In either of the above cases, it is possible that the ISR needed to log the data items is delayed due to other ISRs running ahead of it, or perhaps some code that disabled interrupts momentarily.
In the case of the crankshaft timestamp, the delay does not change the captured timestamp for the event.
In the case of the timer rollover, it does affect the timestamp time that gets reported.

### Ordering Problems

It is possible for timestamped events to get logged 'out of order' by small amounts.
Imagine that at timestamp 0x8005, a timer half-overflow is detected.
The half-overflow reporting code will lock out interrupts slightly later at say time 0x8007.
A crankshaft input capture event occurs right after interrupts are disabled.
With interrupts locked out, the half-overflow routine reads the time at time 0x8009, logs it, and reenables interrupts.
The crank ISR runs immediately and logs the captured timestamp of the crank event at 0x8008.

The log will now show a timestamp of 0x8009 (half overflow event), followed immediately by a timestamp showing 0x8008 (crank subrotation event).

This same sort of thing could happen with the timer overflow interrupt.
It must read the free-running timer, not a captured value so if a crank event
occurred after the timer overflow ISR started to get serviced, the timer event will go into the log ahead of the crank event even though the crank event occurred beforehand.

What do we know:

* The only things that can get out of order are timestamp events.
* The only time that timestamp might be out of order is when they occur as adjacent log items.
  * If there is an untimestamped event between two timestamped events, it is not possible that the timestamped events are out of order.
* timestamps will never be massively out of order.
    The timestamps will appear to go backwards by small amounts, i.e. much, much less than half a timer period.

A log decoder must be stateful.
When collecting a 'current' timestamp event, it must look at the next event in the log before writing anything to the H5 file.

* If there is no next event (like EOF), it can write the current event to the H5 file
* If the next event is not a timestamp event, it can write the current event to the file
* if the next event is a timestamp event and its timestamp value is no more than 4096 counts earlier, then reorder the writing of the two events in the H5 file: write the next event to the H5 file first, then the 'current' event after it. Increment a counter of reordered events and track the maximum difference in time between reordered events
  * If the next event is more than 4096 counts before, flag it as an error.

When complete, the decoder should write to stdout the number of reordered events, and the maximum difference in time observed when reordering them.