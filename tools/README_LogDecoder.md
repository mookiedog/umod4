# Log Decoding Tool

The umod4 stores the combined ECU and GPS data streams into a logfile on micro SD card.
It should be noted that these log files can be huge.
A sample 33 minute test ride generated a umod4 log of over 8 megabytes of data.
If the engine had been running at high RPMs, the file would have been even larger.

These log files need to be processed on a laptop or PC before they become useful to humans.

The first step involves the [decodeLog](src/decodelog.py) tool.
This tool can do 2 things to a raw umod4 logfile:

* Convert it into 'human readable' form
* Convert it into HDF5 form, suitable for display by '[viz](src/viz.py)', a graphics visualizer program

## Running the Decoder

To run the decoder, type:

    decodelog.py --format hr <path-to-log-file>

The format 'hr' says to make a Human-Readable version of the log.
The format 'h5' would tell the decoder to produce and HDF5 version of the log, suitable for the visualizer to display.

Currently: it's not _remotely_ as simple to run decodelog.py as shown above.
The decoder and visualizer programs require a lot of python packages to be installed.
One way to do it is to install the entire umod4 project so that it can be built on your machine.
Once you can build the project, you can activate a Python virtual environment created by the project that makes everything work, including taking care of getting all the required Python packages installed.

At some point in the future, I will package up the decoder and visualizer so that they are more standalone and can be run without having to build the entire umod4 software universe.

## DecodeLog: Human Readable 

The human readable format is really just for debugging by software developers.
It is unlikely that a rider would want to be digging into the raw log at that level, but you never know.

Here is an example of some "human readable" log output:

    0x00000078: D0 00        [    58 @     0.0000s]: LOAD:   UM4
    0x0000007A: D2 00 00     [    59 @     0.0000s]: ADDR:   0x0000
    0x0000007D: D4 00 80     [    60 @     0.0000s]: LEN:    0x8000
    0x00000080: D6 00        [    61 @     0.0000s]: STAT:   ERR_NOERR
    0x00000082: 01 00        [    62 @     0.0000s]: ECU_VR: 0
    0x00000084: 10 07        [    63 @     0.0000s]: CPU:    7
    0x00000086: 60 85 00     [    64 @     0.0000s]: VTA:    133
    0x00000089: 65 96        [    65 @     0.0000s]: THA:    9.2C
    0x0000008B: 64 97        [    66 @     0.0000s]: THW:    8.8C
    0x0000008D: 63 BB        [    67 @     0.0000s]: AAP:    187
    0x0000008F: 4A 01        [    68 @     0.0000s]: FP:     1

This log snippet shows the EP processor reporting that it is loading a UM4 data-logging EPROM image, preparing it for the ECU to use.
The loader code logs that it has loaded all 32K bytes (0x8000) of the EPROM image, starting at address 0 (0x0000).
The STAT record indicates that there were no errors during the LOAD operation.
Because there were no errors, the EP allows the HC11 processor in the ECU to run.
As the HC11 processor inside the ECU boots using the UM4 EPROM image, its first job is to report what version of the log it is generating (version 0 in this case).
Secondly, the ECU reports CPU code 7, meaning that it just came out of RESET.
Logging why the ECU restarted would be hugely useful if for some reason, the ECU CPU crashed while the bike was operating.

Finally, the ECU gets down to work by reading its sensors.

The first sensor to be read is VTA, which is the throttle angle, or how far the throttle is open.
The VTA value is measured in ADC counts, and currently displayed that way, too.
It would be possible to convert that to something like degrees, but at this point, all you need to know is that a bigger number means "more open'.

The next sensor reading is THA, or the thermistor that measures the intake Air Temperature.
This reading is reported in the log as ADC units (you can see the '96' at log location 0x0000008A which is the thermistor ADC reading).
As a nod to readability, the log decoder decodes the ADC reading to a far more human-friendly 9.2 degrees C.

THW is the same as THA, but it measures the Water (coolant) temperature instead.
In this case, you can see that the water is 0.4C colder than the air.
That could be for two reasons:

* the garage was warming up after cooling off overnight, and the air in the garage will warm faster than the larger thermal mass of the engine
* the two thermistors (and and water) may have slightly different manufacturing tolerances

Next up is AAP, or Ambient Air Pressure.
This reading comes from the ambient air pressure, which is built directly onto the ECU circuit board itself.
In contrast, the MAP, or Manifold Air Pressure, measures the pressure inside the intake manifold.
It is reported in ADC units in this version of the log decoder.

Finally, you can see FP:1.
That means "Fuel Pump ON" which is responsible for that whir you hear every time you switch the ignition key ON.
If you were to look in the log 3-ish seconds later, you would see FP:0, which is that little noise when the fuel pump turns off again because the engine is not running.

Again, the 'human readable' version of the log is mainly for debugging issues with the log itself.
It rarely needs to be generated.
That is a good thing because an 8 megabyte umod4 log translates to a nearly 250 megabyte human-readable file.

## DecodeLog: HDF5 Format

The HDF5 file format is used to store large datasets for efficient access.
In contrast, the raw umod4 log is not designed for efficient retrieval, it is designed for efficient storage.
This can be made clear when you see that a umod4 log file of 8 megabytes becomes an HDF5 file of 33 megabytes after conversion, or about 4x the size.

The payoff for the expansion in log size is HDF5's capability for efficient retrieval.
That is precisely what a visualizer needs in order to quickly scan the large logs to display them graphically.

## Visualization

Now that an h5 version of the log file can be created, it is time to use the visualizer.
See the visualizer [README](./README_Visualizer.md) for more info.

