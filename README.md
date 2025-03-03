# Umod4: The UltraMod, Version 4

_Note: This READEME is currently a placeholder for a more thorough description of the overall project and its goals._

![Voiding A Warranty](doc/images/voiding-warranty.jpg)

## History

This project has been ongoing since about 2004. It all started as a project to reverse engineer how the fuel injection system worked on my spanking new 2003 Aprilia Tuono. To make a long story short, it was pretty successful and I had a ton of fun figuring things out.

Along the way, the learning process turned into a massive undertaking to allow me to write my own code for the Aprilia Gen 1 ECU that would support real-time datalogging of ECU data that would get combined with a GPS data stream of position and velocity. The net result of all that work is source code containing my interpretation of the operation of the original ECU firmware. Be warned, it is just my opinion on what is going on in there, and it is certainly not complete. But if you have ever had the desire to stare at some impenetrable 68HC11 assembly code, check out the [ultraMod source code](ecu/src/ultraMod.S)!

Once the version 3 hardware was working back in 2006-ish timeframe, I had to go riding. Here is a graph generated from a datalog from way back when. For this graph, the only data I included was RPM, throttle position, and the state of the clutch switch.  It shows me doing a 0-100MPH run:

![0 to 100](doc/images/0-100.jpg)

You can learn many things from that data log. Mostly, it proves that I'm slow: it took me about 7.9 seconds to go from 0 to 100MPH. A better rider than me would have given it a lot more throttle (green trace) in first gear. You can see I was modulating the clutch around 4000 RPM or so. A better rider would probably have been using a higher RPM and might have slipped the clutch a bit longer before letting it lock up. It took me forever to get the throttle wide open in second gear. I was shifting at about 9500 RPM (1000 RPM below redline, not optimal!) and clearly, my shifts are slow. It's all proof as to why my day job was in software, not as a drag racer.

The RPM (red trace) may look surprisingly thick & ragged because, if only because a non 90 degree V-Twin's crankshaft rotation speed is suprisingly inconsistent through the course of any adjacent pair of rotations. The Umod4 logs the timing of the crankshaft rotation in increments of 1/6 of a rotation. The graph makes it clear that the crank does not rotate at a nice smooth speed: the 60 degree V-twin means that the crank is speeding up and slowing down as it makes a single rotation. Those effects are more pronounced at larger throttle openings.

## Umod4 Updated

The UltraMod4 revisits the original hardware design from the 2005-ish timeframe and brings it about 20 years into the future. Fortunately, Moore's Law has been active during the interim. The expensive and obsolete 32Kx8 Flash chip from the Ultramod V3 has been discarded in place of a $1 processor that is fast enough to emulate an EPROM. Making the EPROM into a software construct opens the door to some new, interesting features when writing custom ECU software. See the EP (Eprom Processor) [README](EP/README.md) for more info on what a software-based memory chip can do.

The latest V4 hardware has the following features:

* EPROM Processor (EP)
  * RP2040 Dual-core Cortex M0+ processor
  * One core pretends to be the EPROM for the Gen I ECU
  * One core passes the ECU data stream to a secondary processor for logging
  * 16 Megabytes of Flash for holding potentially hundreds of ECU images
  * ECU images can be selected before going for a ride
* Wireless Processor (WP)
  * Correlates the ECU data stream with GPS position and velocity information, then logs the correlated data stream
  * Bluetooth - The main user interface to the Umod4
    * EPROM image selection
    * real time ECU and system status
    * Uploading new ECU firmware images
    * Uploading new Umod4 firmware images
  * WiFi - A faster interface used for:
    * Dumping ECU data logs to a server after a ride

## Block Diagram

The Umod4 replaces the EPROM in a stock ECU with a circuit board to extend the capabilities of the overall system:
![Umod4 Hardware Block Diagram](doc/images/BlockDiag.jpg)

The SPI flash is the non-volatile storage that replaces the original EPROM. As mentioned earlier, the SPI Flash has room to contain hundreds of ECU code images.

The "V-Cvt" block performs voltage conversion between the ECU's legacy 5 volt logic domain and the Umod4's 3.3V logic domain.

The SD card is used to log the data stream arriving from the ECU. The NEO-8 GPS provides position and velocity location. The Pico-W logs everything to the SD card and provides WiFi and Bluetooth interfaces to control Umod4 operation.

The GPS module is a generic Chinese uBlox NEO-8. The NEO-8 can report position and velocity data 10 times a second.

The board also contains a socketed Micro SD card. The card is treated as non-removable, only because it is a bit of a pain to physically access due to where the ECU is mounted on the boke. The goal is to have the data logs stored on the SC card filesystem get transferred to a local server. For example, when you get back from a ride, the bike could detect a wifi access point in the garage, connect to it and then dump the logs to a designated server on the home network.

## PCB Hardware

I will be putting the circuit board design on Github at some point, too. It is designed to be fabricated at JLCPCB.com so the part selections and circuit board CAM setup is ready to go for the JLCPCB process. This is kind of important because the RP2040 processor is not suited to soldering by hand. It works better to spend a few bucks and have JLCPCB perform the fabrication process using commercial pick&place machines and a real reflow oven.

![Umod4 PCB](doc/images/ultraMod4_pcb.jpg)

This is the first version of the V4 PCB. To this point during bringup and software development, there have been no hardware bugs that required a respin to make forward progress. All improvements and minor fixes detected during software development are being tracked and accumulated until a respin is required.

Installing a Umod4 PCB requires added a connecter to the ECU. There is an unused connector marked "CN1" located beside the HC11 CPU. The solder needs to be removed from the connector holes as seen here:

![Umod4 ECU Prep 1](doc/images/ECU-CN1-before.jpg)

Then, a new connector strip is added. The connections on this new strip are critical. First off, they add 2 more power and ground connections so that I am not trying to power everything on the board through the single power and GND connections on the EPROM. Secondly, the connecter gives me access to three critical signals required for working with the HC11 processor on the ECU:

* HC11 RESET: allows my board to prevent the ECU processor from running while I set up the code that will run inside my fake EPROM
* HC11 E clock: lets me synchronize the timing between my fake EPROM interface and the ECU's processor clock
* HC11 RW signal: lets my board know if the current bus operation is a read or a write. Reads are normal, writes are used to send the ECU data stream to my board where they can be logged.

![Umod4 ECU Prep 2](doc/images/ECU-CN1-after.jpg)

With the new connector strip added, the Ultramod4 board can be installed. It gets held in place with 4 screws and standoffs (not show in this picture):
![Umod4 ECU Installed](doc/images/ECU-installed.jpg)

The little white box with the three wires just above the ECU is a hardware debugger that was used to develop and debug the "Fake EPROM" software in the RP2040.

The fake EPROM is a busy little thing. The ECU sends it read and write requests 2 million times a second. Each request needs to be performed properly and within the HC11's timing requirements. It has to be verifiably perfect in its timing and operation. The last thing I want is a software bug that makes my engine stop running. While my bike is straddling some railway tracks. With a train coming...

## Status

At the moment, things are in a state of flux. On the plus side:

* The entire project got cleaned up, reorganized, and put into a Github repository in preparation for beginning to work with the WP processor
* The fake EPROM code works great: my Tuono runs fine!

Since the Bluetooth interface is not yet developed, the existing code serves a single EPROM image.
But the interesting part is that I can get the system to "mix and match" my special data-logging firmware with the maps out of any Aprilia EPROM that is compatible with the RP58 codebase.
In short, basically any firmware except the early small valve engines.

### Next Steps

Here is an incomplete list of what comes next:

* EP
  * Create the 2-way communication mechanism between EP and WP. This will probably take the form of a megabit-ish UART serial link because that's all I have to work with.

* WP
  * Get the Pi Pico-W WP codebase started
  * Start testing general wireless processing
  * Update my old GPS drivers to work with the newer NEO-8 GPS module
  * Get the Micro SD card interface working with LittleFS
    * Do some performance testing

There will be lots of other stuff, too. That list above should keep me busy for months. Maybe years.

## Further Reading

At the moment, this repository contains 3 large pieces to the project:

* The [EP](EP/README.md) (EPROM Processor): the 'fake' ERPOM used by the ECU
* The [WP](WP/README.md) (Wireless Processor): the system that provides the user interface control over the Umod4
* The [ECU](ecu/README.md): this is the special data-logging firmware
* the [eprom_lib](eprom_lib/README.md): contains JSON descriptions of a number of stock Aprilia EPROMs

Check out the README.md files in each of the repository subdirectories: [EP](EP/README.md), [WP](WP/README.md), and [ECU/Ultramod](ecu/README.md).

For a real challenge, try [building the software system](BUILDING.md) yourself.
It's not much use without a circuit board, but if you are a software person, you might give it a shot just for fun.

## ...As Always

![Proudly-made-on-earth](doc/images/proudly-made-1.jpg)
