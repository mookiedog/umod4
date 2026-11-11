# Test Data

This directory contains sample log files for testing the decoder and visualizer.

## Purpose

- Test `decodelog.py` conversion from binary logs to HDF5
- Provide sample data for `viz.py` testing and demonstrations
- Serve as example logs for users learning the system

## File Format

- `.log` or `.um4` files are raw binary logs from the umod4 hardware
- These files can be converted to HDF5 format using `decodelog.py` (run from the umod4 directory):
  ```bash
  uv run tools/logtools/decoder/decodelog.py tools/logtools/test_data/sample_ride.um4 --format h5 -o sample_ride.h5
  ```

## Adding Sample Logs

When adding sample logs to this directory:

1. Keep files small (under 10 MB if possible)
2. Name files descriptively (e.g., `idle.log`, `acceleration.log`, `cruise.log`)
3. Document what each log contains (engine condition, duration, etc.)

## Sample Log Descriptions

### sample_ride.um4

A 23-minute ride, recorded on 2026-06-22 with firmware built on 2026-06-09.
It covers about 16 km at up to 66 mph, with GPS fixes arriving three to five times a second and a few gaps of about 10 seconds.
There are no impossible RPM jumps, because the firmware already had the timer-prescaler fix of 2026-03-26.
This is the sample that the tools bundle ships.

The log ends in the middle of a record, because the power went away with the ignition, so `decodelog.py` prints a warning about an incomplete last record.
That is normal: the incomplete record is ignored and everything before it is decoded.

### log_3.um4

This is the original logfile that incorporates GPS data during a ride.
It was recorded on 2025-11-28, before the timer-prescaler fix of 2026-03-26, so its RPM and event timing are not reliable: the decoded RPM has tens of thousands of impossible jumps.
The GPS data in it is fine.
It was just a short ride around town.
The only point was to collect GPS data so that the vizualizer map display functions could be tested.
The bike was warmed up for about 10 minutes, then a short 10-ish minute ride, immortalized for posterity.

#### Weird Events

The bike was missing just after starting.
It was a pretty cold day (about 9C), so I did not think much of it at the time.
However, the engine misses are clear to see with the visualizer.
There were 8 events, all located in the first 22 seconds of the log.
If you zoom in on those events, you will see that the engine did not actually 'miss', because it clearly sped up during the power stroke.
The funky part is that the engine seemed to slow down a lot just before the power stroke began, like as the piston was approaching TDC.
Enabling the spark data streams will show that the spark events seem to have occured a fair bit earlier than the other power stroke events before and after for that same cylinder.
It looks like the early spark caused the engine to stumble by significantly raising pressures in the combustion chamber before the piston got to TDC.
