# Debugging

Debugging needs real hardware: a umod4 board, a Raspberry Pi Debug Probe, and (for anything involving the ECU) a powered ECU on a bench.
[BENCH_TESTING.md](BENCH_TESTING.md) describes the bench setup.

## Setting Up

The project's `.vscode/launch.json` defines a set of *launch configurations*.
Each one is a saved recipe that tells VS Code how to start the debugger for one target, such as the WP or the EP.
The launch configurations are described in the sections below.

On WSL2, Windows owns the USB debug probe until it is shared with WSL.
[WSL2: USB Device Sharing](BUILDING.md#wsl2-usb-device-sharing) in BUILDING.md explains how to install usbipd and bind the probe, which is a one-time job.
After that, every debug launch configuration attaches the probe to WSL for you and detaches it when the session ends, using the VS Code tasks `usbipd: attach debug probe` and `usbipd: detach debug probe`.

There is a bug in either CMake or VS Code, or both, so that VS Code cannot find the executables produced by the CMake `ExternalProject_Add()` commands.
Because of that, the normal way of picking a debug target does not work.
Instead, press F1, type "Debug: Select And Start Debugging", and choose a launch configuration from the list, as described in the following sections.

## WP

The WP can be debugged by attaching a Raspberry Pi SWD debug unit to the 3 debug pins on the Pico2W.

Debugging has a complication in that in order to support OTA flash programming, the flash memory is partitioned into a pair of image 'slots'.
Each slot can contain a WP image.
Each of these images is built to execute at the *same* start address in the flash.
But neither of these images is actually sitting at that start address.
When an image gets booted, the image loader programs an RP2350 memory address translation manager so that the slot containing the image to be run will appear at the correct starting address in the address space.
This address translation, and the images moving around the address space, are handled entirely by the bootloader: the debugger is not aware of any of this.
That means that if the debugger is invoked in the normal fashion to load a file, it will put the image directly at the image's run-time start address, not the start of the slot address.
The resulting loaded image can be debugged, but because it was not written to its proper slot, it will overwrite and destroy the flash partitions.

To avoid destroying the partition information, images can be debugged in two ways: by attaching to running firmware, or using a special partition-aware debugger image loader.

The debugger can load firmware, but it cannot partition the flash.
The very first install of firmware on a blank WP is therefore done without a debugger, using the BOOTSEL drag-and-drop procedure in [Install the WP software](GETTING_STARTED.md#install-the-wp-software).

### Partition-Aware Loader

Select the launch configuration '*** WP: RP2350 Partition-Aware Debug'.
The partition-aware loader always erases slot 1, then places the image to be debugged into slot 0.
Since slot 0 contains the only valid image, the bootloader will always choose it when deciding which slot to actually boot.

### Attach To Running Firmware

If the software is already loaded to the WP using something like 'F1', 'Tasks: Run Task', 'WP: Flash and Run (force BOOTSEL via SWD)', then the debugger can attach to that running firmware.
To do that, use the 'run and debug' control in VS Code to select the option '*** WP: RP2350 Attach (no flash)'.
Once attached, the debugger will stop the WP firmware.
From there, the best thing to do is reset the WP, set any desired breakpoints, then start it running.

## EP

Debugging the EP is simple: select the launch configuration '*** EP: RP2040 Launch CMSIS-DAP'.
The RP2040 has no address translation mechanism, and only supports a single image.
The debugger will always load it to the proper location in Flash.
When it starts, the debugger stops at the first line of `main()`.
Press F5 to let the EP run: it loads an EPROM image and starts feeding instructions to the ECU.

There is one catch however.
As part of normal umod4 operation, the WP uses the EP's SWD interface to interact with the EP.
If a debugger is attached to the EP's debug pins, the debugger will conflict with the WP: both will attempt to drive the SWD pins.
To get around that, the SPARE2 IO on the WP has special meaning: if SPARE2 is grounded, then the WP will not use the SWD interface.
To debug EP operation:

* Power off the test system
* Ground SPARE2
* Connect the debugger to the EP SWD pins

At this point, you can power up the test system and debug.
When you are done debugging, detach the debugger, let SPARE2 float again, and reboot the WP.

## ECU

The ECU's 68HC11 is an old-school processor that does not support in-circuit debugging.
Even so, the umod4 provides some debugging help.
Because the EP's EPROM emulation is a software construct, the emulation software can help us out.
Specifically, the EP maintains a 32KB circular buffer of every HC11 bus access that targets the EPROM address space, including the converted RAM windows.
Accesses that do not target the EPROM are not logged.
Each bus access uses 4 bytes of RAM, so the buffer holds the last 8K of HC11 bus accesses.

It is a bit of a pain to manually decode what the HC11 was doing, but the bottom line is that the data is there, should you need to see something.

Triggering is not trivial either: you need to set a breakpoint in the EP to detect when to stop so that you can inspect the HC11 buffer.
But again, it's way better than nothing.

## RTT IO

The umod4 supports RTT IO mechanisms.
RTT IO is special in that it does not require any hardware other than the SWD debugger connection.
Output is written to RAM buffers in the processor being debugged.
The debugger process on the PC uses the SWD debug interface to periodically read these output buffers while the target is running.
Because RTT only needs a small RAM buffer, the umod4 software supports a number of IO channels.
The WP takes this one step further: because the WP always has SWD access to the EP RAM, the WP software reads the EP's RTT buffer via SWD and forwards that output to a buffer on the WP, which makes EP output accessible during a WP debugging session.
The channel numbers and TCP ports are listed under [RTT channels](tests/TEST_PLAN.md#rtt-channels) in the test plan.
