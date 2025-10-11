# EPROM Library

The eprom_lib sub-project contains informational descriptions for a number of Aprilia EPROMs that are known to exist.
The descriptions are written as JSON documents.
This makes the descriptions human-readable, and easily converted to BSON documents that are easier for the EP project to include in software builds.

If nothing else, these JSON description files might be useful for people on the internet trying to figure out what a particular Aprila EPROM might be used for.

Feel free to look around in the [eprom_lib/src](./src) directory to see what EPROMs have been documented.

Some of the infomation in a description file is required for the umod4 firmware to load and run the EPROM. Other things, like the list of models that the EPROM was used in, or a list of hyperlinks to information regarding that EPROM are strictly informational for a human reading the description file.

## Important Notes

### Bin Files

This project does not contain any stock Aprilia EPROM .bin files.
On the one hand, it would be super handy if this repository contained a library of .bin images.
On the other hand, I don't want to cause any trouble (real or imagined!) that might occur by including stock EPROM images as part of this repository.
The description files in the eprom_lib are merely that: descriptions of an EPROM, without its contents.

A variety of stock Aprilia EPROM .bin files can be found on the web if you google for them.
Obviously, if you own a specific EPROM and have access to an EPROM reader, you can make your own '.bin' files and include them in your own umod4 build.

The idea of a umod4 board containing every EPROM image ever released is quite possible, and seemingly cool.
But, as someone pointed out to me, it's also pretty useless in the sense that once you get an image ironed out exactly the way you want it, all those other images are just wasted space.
Kind of like that collection of duplicated VHS video tapes in boxes in your basement that you will never watch ever again.

But if you really want to find and fill a umod4 with EPROM images, it's got a lot of space.
Nothing is stopping you!

### Daughterboards

Certain EPROMs required using a daughterboard to operate inside an ECU.
The EPROMs that needed daughterboards had their contents pre-scrambled.
A daughterboard would unscramble those pre-scrambled contents 'on-the-fly' as the ECU would read it.
Plugging a copied scrambled EPROM into another ECU would not work unless you also had a daughterboard.

In essence, these scrambled EPROMs were freely copyable, but useless without their descrambling board. But once a rider owned a single descrambler board, any scrambled EPROM could be duplicated in the normal fashion using an EPROM reader/writer, then installed into the daughterboard and used in an ECU. So 'scrambling' was certainly not the ultimate in security.

The description files in this eprom_lib will indicate if a specific EPROM needs a daughterboard.

Before you ask, it _is_ possible to descramble the EPROM images that need a daughterboard using software.
In fact, the umod4 software is already set up to use a software-based 'daughterboard' to descramble images on-the-fly as whole images or partial maps get loaded.
But for the same reasons as not including .bin files in this repository, I'm not including the daughterboard feature in the project.
I'm not looking for any sort of trouble.
On the other hand, nothing precludes anyone from writing their own descrambler.

Honestly, it took a day during a winter storm, of having a power failure and no internet to give me time with no distractions to imagine what the daughterboard had to be doing. Then, maybe a week to work it out precisely.
So obviously, it can be done, and I am not the sharpest tool in the toolbox.
