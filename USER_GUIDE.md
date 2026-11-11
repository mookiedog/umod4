# Umod4 User Guide

This document is aimed at Aprilia riders who are interested in finding out what the umod4 project could do for them, and how they would use it on their bike.

For people who have a umod4 and want to get it up and running, see:

* [GETTING_STARTED.md](GETTING_STARTED.md): umod4 installation and prep

For the project overview, and for lower-level information, see:

* [README.md](README.md): the project overview
* [HOW_IT_WORKS.md](HOW_IT_WORKS.md): how the hardware and software work
* [BUILDING.md](BUILDING.md): setting up a development environment and building the software

## What the umod4 is

The Gen1 fuel injection computer (AKA: Engine Control Unit, or 'ECU') was designed to run its program from an EPROM chip.
The umod4 physically replaces the EPROM chip with a circuit board containing a collection of new processors and memory.

The umod4's most important job is to act just like an EPROM so that the ECU will run like normal when the ignition key turns on.
The first improvement over a standard ECU is that a umod4 has space for over 100 EPROM images, all stored in an on-board flash memory.
Flash memory means that EPROM image updates are trivial compared to erasing and burning EPROMs.

The second big job the umod4 does is data logging.
The point of the umod4 is to give a rider a window into exactly what the ECU was doing throughout an entire ride.
The umod4 will record:

* **what the rider is doing** during a ride. For example, a rider's throttle inputs are logged over 100 times a second.
* **how the ECU is responding to its inputs** while you ride. This includes engine sensor data and the results of ECU calculations.
* **how the motorbike responds to it all**. The umod4 tracks the bike's 3D location and velocity 10 times per second via GPS.

The data logs are recorded to an on-board micro SD card.

The last big improvement over a standard ECU is that the umod4 adds a WiFi connection to the system.
A rider can use the WiFi connection to select a new EPROM image to run or see certain data streams 'live' in a running engine.
The nicest feature though has to be automatic data log downloads after a ride.
After coming home, a rider would park their bike in the garage, plug the umod4 into a USB power supply, and the server will automatically pull all new ride logs off the umod4 via WiFi.

Once the server has pulled the latest ride logs, a rider can examine them graphically on their PC/laptop.
The level of detail in a log is broad and deep: a ride log contains every rotation of the crankshaft, every change in throttle position, every spark event, every injection pulse event, every intake air and coolant temperature measurement, etc.
For the most fun, all of these log events can be correlated to the bike's location and velocity on a map display of their ride.
Think track day fun!

## The pieces

A rider interacts with the umod4 using a web browser and a WiFi interface.

That said, the WiFi connection is slightly different if you are at home or on the road.

**At home**, the bike is in your garage and the umod4 is connected to your home WiFi.
Everything shares your router:

```text
                       Home WiFi router
                              |
      +-----------------------+-----------------------+
      | WiFi                  | WiFi                  | WiFi
      |                       |                       |
 +----+--------+       +------+-------+       +-------+-----+
 | umod4 board |       | Your PC      |       |  Phone      |
 | (in the ECU |       |  Server      |       |  (browser)  |
 | on the bike)|       |  Visualizer  |       |             |
 +-------------+       +--------------+       +-------------+
  holds images          downloads logs         opens the umod4's
  and ride logs         from the umod4,        control web page
                        graphs your rides      to change images
```

**Away from home**, typically, there would be no router and no PC.
If the umod4 can't see your home network, it automatically enables its own WiFi network hotspot.
In this situation, a rider could still interact with the umod4 by connecting their phone's WiFi to the bike's own hotspot WiFi network:

```text
 +-------------+      umod4's own hotspot      +-------------+
 | umod4 board | <---------------------------> |  Phone      |
 | (on the     |    no router, no internet     |  (browser)  |
 |  bike)      |                               |             |
 +-------------+                               +-------------+
  serves the control web page at 192.168.4.1
```

In either case, the control website that the umod4 serves to a phone's browser is identical.

* **The umod4 board** sits in the ECU.
  It holds the EPROM images the ECU can run, and the logs of your rides.
  It serves a control web page via its WiFi interface.
  Logs are stored on a micro SD card.
  EPROM images are stored in a special flash memory on the umod4 board meaning that your engine will run even if there is no SD card present.
* **The phone** (or tablet, or PC) needs only a web browser.
  There is no app to install.
  You use it to open the umod4's control web page, for example to select a different EPROM image.
  On your home network the address is `http://<device name>.local`.
  That name *should* work, but some networks and devices do not support it.
  If your browser cannot find it, use the umod4's IP address instead.
  You can find the address in your router's web interface (its list of connected devices) or in the server's device list, if you are at home.
  If you end up using the IP address, consider telling your router to always give the umod4 the same address (usually called a "DHCP reservation" or "fixed IP").
  Then the address never changes, and you can memorize it or bookmark it.
  On the umod4's own hotspot the address is always 192.168.4.1.
* **The server** is a program on your PC.
  It finds the umod4 over your home WiFi, and does things like:

  * automatically downloading new ride logs
  * performing umod4 firmware upgrades via WiFi
  * giving you a way into the umod4's control web page

  The server would typically be used at home.
  It is possible to use a laptop running a server in a remote location though, like at a track.
  There are many ways to do that.
  One easy-ish way would be to set up your laptop to broadcast its own WiFi network where the SSID and password match your own home network.
  Under those circumstances, the umod4 would happily connect to the laptop's WiFi network, thinking that it was at home.

  The server is not a downloadable app.
  To run it, your PC needs a copy of the umod4 project (a git "clone" of the repository) and a Linux environment: a Linux machine, or Windows with WSL.
  Windows also needs a networking setup and firewall rules so the umod4 can find the server.
  [GETTING_STARTED.md](GETTING_STARTED.md) walks through all of this.
  The same copy of the project also provides the visualizer.
* **The visualizer** is a separate program that turns a downloaded ride log into graphs and a map.
  See [Tools.md](Tools.md) for how it works; this guide only says when to use it.

### WiFi

Everything is controlled over WiFi, through a web page the umod4 serves.
This was chosen so there is no phone app to write or install: any phone, tablet or PC with a browser works.

The umod4 connects in one of two ways:

* **Your home network**, if it is configured and in range.
* **Its own hotspot**, if it cannot find your home network.
  Your phone or PC joins the umod4's network directly.
  Its password is the one you gave it when you set up its AP WiFi credentials.

Two things to know:

* **A phone can sometimes connect where the umod4 cannot.**
  The umod4's antenna is smaller than a phone's, so a weak signal that works for your phone may not work for the umod4.
  If it will not connect, move the access point closer.
* **The hotspot turns itself off when the bike is moving** (see [The routine: ride, collect, look](#the-routine-ride-collect-look)).

## One-time setup, on the bench

It is easiest to set up a umod4 for the first time on a workbench in the house before it gets installed on the bike.
It makes for easier physical access to the micro SD card and the micro-USB connector used to power the umod4.
It also makes it easier to ensure that the umod4 will have access to a good home WiFi signal.

**What you need on your PC.**
The server and visualizer run from a copy of the project's source code, on a Linux machine or on Windows 11 with WSL2.
[GETTING_STARTED.md](GETTING_STARTED.md#6-set-up-your-pc) walks through setting that up.

There is no installer or packaged release.
Getting a machine ready to build and run the project is a long, fiddly job, and you do not have to do it alone: an AI coding agent such as Claude Code, working inside the cloned project, can check what is installed, install what is missing, and fix errors as they come up.
Ask it to help you get set up.

Follow [GETTING_STARTED.md](GETTING_STARTED.md) for the one-time setup: building and fitting the hardware, installing the software, and getting the umod4 onto your WiFi and your server.
This guide does not repeat it.

Once GETTING_STARTED is complete, the umod4 and ECU are mounted and ready to go.
Choosing which EPROM image the ECU runs is described in [Choosing what the ECU runs](#choosing-what-the-ecu-runs-eprom-images).

## The routine: ride, collect, look

### Before a ride

1. Pick the EPROM image you want to run.
   Usually you already did, and it stays selected; changing it is uncommon.
   See [Choosing what the ECU runs](#choosing-what-the-ecu-runs-eprom-images).
2. If the umod4 is on USB power, unplug it.
3. Ignition on, and go riding.
   Logging starts by itself.

### During a ride

The WiFi hotspot **shuts off above 20 mph and stays off until you power-cycle the bike**.

The check applies when the umod4 is acting as its own hotspot, which is the normal case on the road.

To use WiFi while out: stop, power-cycle the bike, join the umod4's hotspot, and open its control web page.
From there you can, for example, select a different EPROM image.

### After getting home

1. Ignition off.
2. Plug the umod4 into USB power.
3. With WiFi in the garage and the **server running**, the server downloads new ride logs automatically.
   If the server was not running, start it.
   It looks for your umod4 and downloads whatever is new.

The server keeps checking for new logs the whole time it is running and the umod4 has power.
That includes logs recorded while the bike is parked, because the umod4 logs GPS data even when you are not riding.
They are not very useful, but you will see them, and the server downloads them too.
Lots of small files are normal.

### Clearing space on the bike

Logs stay on the micro SD card until you delete them, so clear them out now and then.
Use the server's device files dialog:

* A log can only be deleted **after the server has downloaded it**, so you cannot lose a ride by deleting.
  Empty (zero-length) logs are the one exception and can always be deleted.
* The log the umod4 is writing right now cannot be deleted.
* There are buttons to delete selected files or all logs.

### Looking at your rides

The umod4 writes compact **raw logs** (`.um4`, binary, small).
To graph one, a .um4 file must be **decoded** into an HDF5 file that the visualizer can load quickly.
If the visualizer needs to decode a log file, it will ask you for permission.
The decoded HDF5 versions of the log files remain on the PC so that you can reopen a log without having to reconvert it.
The details, and what each graph means, are in [Tools.md](Tools.md).

## Choosing what the ECU runs (EPROM images)

Three ideas to know:

* The **image library** is the collection of 32 KB EPROM images stored in the umod4's flash memory.
* The **boot selector** is an ordered list that says which library image (or combination of images) the ECU should run when the ignition goes on.
* **Limp mode** is the fallback.
  If nothing in the selector loads, the umod4 gives the ECU a built-in image.
  The limp-mode maps currently are from a stock 549USA.
  Depending on the extent of mods to your driveline, these maps should allow the engine to run (though perhaps badly), and hopefully get you home.

You manage both the library and the selector on the umod4's control web page.
The page is called Image Store, and you can reach it from a phone or from the server.
Its controls are disabled while the engine is running.

### Where images come from

**UM4, the datalogging image.**
This is the program that provides the logging.
It is built from this project's source, which is one reason you need the cloned project and a working build environment (see [One-time setup, on the bench](#one-time-setup-on-the-bench)).
The build writes it out as `UM4.bin`.
UM4 comes with the 549USA maps, so you can run it as-is and get logging without bringing any EPROM image of your own.

**Your own maps.**
If you want to run your own maps, you need them as a 32 KB `.bin` file.
How you get that depends on what is in your ECU today:

1. **A stock Aprilia EPROM.**
   This project does not include stock images, but `.bin` files for many of them can be found with a web search.
   Or read your own EPROM with an EPROM reader, as in case 2.
2. **An aftermarket EPROM, with no daughterboard.**
   You need an EPROM reader to read the chip into a `.bin` file.
   If you bought the chip from a tuner, ask whether they can give you the file.
3. **A protected (scrambled) EPROM, which uses a daughterboard.**
   This case is common, and it needs special handling.
   See "Protected EPROMs" below.
4. **Software like TunerPro (or equivalent) that you use to program an EPROM or a 29C256 flash chip.**
   The software itself produces the `.bin` file it uses to program the chip.
   That file is an exact 32 KB EPROM image, so you can upload the same file to the umod4.

Whichever case you are in, once you have a `.bin` file, check that it is RP58-compatible (see below) before you combine it with UM4.

### Protected EPROMs

Many EPROMs you will encounter are **protected**, meaning their contents are scrambled.
A protected EPROM only works in an ECU that has a hardware **daughterboard**, which unscrambles the data on the fly as the ECU reads it.
Some Aprilia EPROMs are protected, as are many aftermarket ones.
[eprom_lib/README.md](eprom_lib/README.md) has the background.

What it means for you:

* A stock umod4 board cannot descramble a scrambled image.
  A bin file generated by reading the scrambled chip in an EPROM reader will not run correctly.
* **Ask for the unscrambled file.**
  Whoever made your chip likely worked from a normal, unscrambled image before it was scrambled.
  A tuner who sold you the chip may give you that file, but it's unlikely.
* **Writing a decoder is possible but is real work.**
  The umod4 source code is set up to accept an appropriate descrambler routine (this is what the Protection setting on the upload form is for).

  **This project does not include the descrambler source!**
  This keeps the umod4 project clean with respect to other peoples IP that they have protected with daughterboards.
  *If you want to write your own, it's possible: I did it, and I'm pretty sure I was neither the first nor the last person to do so.*

### Putting a .bin file into the umod4

Typically, the bin files would be stored on your PC, not a phone.
They *can* be stored on a phone, but it's less convenient than a PC.

1. On your PC, use a browser to open the umod4's control web page, then go to the image library section.
2. Click **+ Add**.
3. Choose your `.bin` file.
   It must be exactly 32 KB (32768 bytes).
4. Give it a **Name** (up to 31 characters) and a **Description**.
   You will pick the image by this name later.
5. Leave **Protection** at `N - None`.
   A stock umod4 cannot run protected images (see "Protected EPROMs" above).
   **If you wrote you own descrambler**, you have two choices.
   If you saved a .bin of the descrambled EPROM, then select protection 'none'.
   If the .bin file is a direct copy of the original scrambled EPROM, the select protection 'A - Aprilia'.
   This tells the umod4 that it will need to descramble the EPROM image as it gets loaded.
6. Click **Upload**.

You can also edit an image's name and description, or delete it, from the same section.
The name and description fields are not stored in the EPROM data.
They are meant to allow you to attach some metadata to your EPROM images so you can tell them apart.
It is especially important if you are doing tuning experiments so that you can track which image was doing what.

### What a mapblob is, and why you would combine images

For simplicity, we split every 32 KB EPROM image into two pieces, plus a gap between them:

* the **code**: the program the ECU runs, in the upper half of the EPROM, at offsets `0x4000` through `0x7FFF`
* the **mapblob**: the fuel and spark tables and the other data that drive the ECU's decisions, at offsets `0x0000` through `0x1BFF` (`0x1C00` bytes)
* the **gap**: `0x1C00` through `0x3FFF`, which the RP58 program does not use and which can be ignored

```text
 0x0000 +-----------------------+
        |  mapblob              |  RP58-style fuel and spark tables, and the
        |                       |  other data the ECU's decisions use
 0x1C00 +-----------------------+
        |  gap                  |  unused by RP58 code, ignored
 0x4000 +-----------------------+
        |  code                 |  the code the ECU runs
 0x7FFF +-----------------------+
```

The UM4 code is an RP58 codebase that has been modified for datalogging.
The UM4 codebase knows how to send data to the EP (Eprom Processor), a secondary processor on the umod4 board.
The UM4 code has been modified so that as it measures sensors or calculates interesting results, it sends the data to the EP so that the data can be logged.
The modifications are purposely as light as possible to avoid making significant timing changes to the existing code.

The stock UM4 code generated by the build system contains 549USA maps, just because that's what the original EPROM contained.

But because the UM4 code is RP58-compatible, the UM4 code can be combined with the maps from *any* RP58-compatible EPROM image.
In short, it turns any RP58-compatible EPROM image in your collection into a datalogging version of that same EPROM.

So if you run your favorite stock or tuned image as-is, you get no logging.
To get both, the umod4 can **combine** two images when it boots: it takes the code from one image (UM4) and the mapblob from another (yours).
The result is the UM4 code, with logging, running your maps.

This works because of one fact: for EPROMs that are **RP58-compatible**, the mapblob has exactly the same memory layout in every image.
So the UM4 code can be combined with the mapblob from *any* RP58-compatible EPROM, and the result is a datalogging version of the ECU running the maps you chose.

**What RP58-compatible means.**
An EPROM is RP58-compatible if its data in the code range (`0x4000` through `0x7FFF`) is identical to that of an RP58 EPROM.
The mapblobs of such EPROMs can differ in their contents, but they all use the same layout.

**How to check yours.**
The project includes a `checksum` tool.
Run it on your `.bin` file:

```bash
checksum -o 0x4000 -3 YourImage.bin
```

If it prints `0x18E7E9F2` (the Murmur3 checksum of the code range of an RP58), your image is RP58-compatible.
Any other value means it is not, and combining it with UM4 will not work.
The check applies to the unscrambled `.bin` file.

If your image is not RP58-compatible, its maps are laid out differently, so combining it with UM4 would give the ECU the wrong data.

You do this in the boot selector, with no building or editing of binaries on your part.

### Choosing what runs at boot

In the boot selector section, each row has two choices:

* **code**: which library image supplies the program.
  For logging, this is UM4.
  The EP code itself always contains a built-in UM4 datalogging image.
  Older or experimental UM4 images can also be stored in the image library like any other image.
* **mapblob**: which library image supplies the maps, or `(none)` to use the maps that come with the code image.

Entries are tried from the top.
The first one that loads successfully is what the ECU runs.
If an entry fails (a missing or corrupted image, say), the umod4 tries the next one.
If they all fail, it uses limp mode: the built-in UM4 code with its own 549USA maps, which is always available.

A sensible selector, to run datalogging code combined with your own tweaked maps:

1. code = UM4 (built-in), mapblob = your current image
2. code = UM4 (built-in), mapblob = your previous image

The second row is the fallback: if your current maps fail to load, the ECU tries to run your previous mapset.
Use the arrow buttons to reorder rows, **+** to add a row and the **X** button to delete one.
Leave the **continue** toggle off.
It makes the umod4 keep loading later entries after one succeeds, which is for testing.

When you are done, click **Save & Reboot EP**.
The new selection takes effect when the EP restarts.
You do not need to power-cycle the bike.

## Safety

The umod4 runs inside your engine's ECU.
A bad image can stall or damage a running engine or leave you stranded.

**Be extremely careful if you are editing ECU tables, or making changes to the UM4 code itself.**
**Any changes you make can be life-threatening!**

## What the umod4 produces

Each log file contains:

* normal data: engine events, GPS position and velocity
* other things: the full mapblob that was running and the firmware versions, so a ride can be matched to the setup that produced it

The umod4 starts a new log file every time it reboots.
That means every power-up creates one, including each time you power-cycle the bike to get WiFi mid-ride.
A single outing can therefore produce several log files, and you view them one piece at a time.

The server stores downloaded logs by device, then by date:

```text
~/.umod4_server/logs/<device-name>/<YYYY-MM-DD>/log_N.um4
```

This is the default location, and you can choose a different one.
Storing logs per device lets you keep several umod4 units apart, for example two bikes, or one bike and a bench test unit.

A log's file name does not contain the date, because the umod4 does not know the date when it creates the file.
The `N` number goes up each time the umod4 creates a new log.
As soon as the GPS gets a fix, the date and time are recorded inside the log.
So every log can be traced back to a place and a time, even though its name does not say.
