# RP2350 Boot Process and OTA Mechanisms

A proper understanding of the RP2350 boot process is required to understand how OTA will work.
The boot process depends on three things:

* The bootrom code
* The partition table
* The images stored in the partition table's image slots (if any)

### Boot Process

The bootloader is in ROM and its operation is fixed.
The bootloader makes all of its decisions based on:

* finding a partion table or not
* if a partition table does not exist, the bootloader simply runs code at 0x10000000??

If a partition table exists, the bootloader locates the A and B images.
At this point, the bootloader needs to pick which which image slot it is going to run.
There is a strict process for this:

1) If only one image is valid, it gets booted.
1) If both images are valid:
    1) if one of the images is marked TBYB (TBYB marking is stored in the image itself) and the other is not, the non-TBYB image gets booted.
    1) The image with the highest version number is chosen
1) if neither image is valid, the bootloader reboots into BOOTSEL/USB Mass Storage mode and waits for a UF2 file to get loaded

Exceptions to this boot sequence are possible.
These occur when a running image asks the bootloader to perform a special warm-boot process.
The warm boot process receives override instructions from the code that invokes it.
For example, the override instructions might specify that the bootloader should boot directly from a specific slot, bypassing the normal image selection process.

Changing active boot slots is done in a fashion that does not require the ROM bootloader to maintain any state of its own.
For example, the confirmation process for a TBYB image works by having the new image literally destroy the previous image by erasing its first sector.
If the system cold boots at this point, the new TBYB image will be the only valid image, so it will be chosen by the bootloader from then on.
If a new TBYB is loaded via OTA beside the original TBYB image, then both images will be TBYB, but the new one will have a higher version number, so it will be chosen to boot.
If the new version decides to commit itself, the older TBYB image gets destroyed and the process repeats.

Takeaways:

* The partition table tells the bootloader where to find potential images
  * It is a truly static data structure - never modified after initial setup
* Bootrom makes all its choices based on data inside the _images_, not the partition table:
  * Basic image validity (i.e. is first sector intact?)
  * TYBY indicators
  * Version numbers

It can be seen that there is no mutable "boot state" that the bootloader needs to maintain. The boot state is contained in the images themselves.

## RAM Remapping

Once an image has been selected by the bootloader, the system must be prepared before it can be run.
The issue is that all application images are linked to run at 0x10000000 (XIP_BASE).
However, these images gets stored at start addresses in flash defined by the partition table.
This means that an image needs to be moved to where it was built for before it can be run.
Instead of physically moving the image down to the proper address, the CPU has a mechanism called XIP RAM Remapping that effectively does the same.
It performs address translation "on the fly" so that when the CPU requests data from a range of addresses, the fetch actually occurs from a different range of addresses.
The bootloader understands this.
When the bootloader wants to run an an image in a particular slot, it programs the XIP RAM remapping mechanism so that the flash corresponding the slot's starting address gets mapped down to the very start of flash, where it was built to run.
At this point, the image sees itself in the address map exactly where it was built for, and runs as it expects.

