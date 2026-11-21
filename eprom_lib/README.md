# Aprilia EPROM Images

This document lists details regarding Aprilia Gen1 ECU EPROM images I know about, and whether each one can be combined with the UM4 datalogging code.
For why that matters, see [What a mapblob is, and why you would combine images](../USER_GUIDE.md#what-a-mapblob-is-and-why-you-would-combine-images) in the User Guide.

This project does not contain any of the images themselves: see [Bin Files](#bin-files), below.

## How to read the table

* **Source**: **Aprilia** for an image made by the factory, **aftermarket** for anything else.
* **RP58-compatible**: **Yes** if the EPROM uses the same code as the stock RP58 (see [RP58 Compatibility](#rp58-compatibility) below).
  **Only RP58-compatible images can supply maps to UM4.**

## The table

| EPROM | Source | Used in | Protected | RP58-compatible | Notes |
| --- | --- | --- | --- | --- | --- |
| RP58 | Aprilia | RSV 2001 | No | Yes | Stock, European dual maps, derestrictable |
| RP58USA | Aprilia | RSV 2001-2003 | No | Yes | Stock, USA maps, not derestrictable |
| 549USA | Aprilia | Tuono 2003-2005 | No | Yes | Stock, USA maps, not derestrictable |
| 549EuroC | Aprilia | Tuono 2003 (Europe only, shiny muffler without catalytic converter) | No | Yes | Stock |
| 549EuroKatD | Aprilia | Tuono 2004-2005 (Europe only, black muffler with catalytic converter) | No | Yes | Stock |
| PA59 | Aprilia | Falco 2003 | No | Yes | Stock |
| Edwards | Aprilia | Edwards Replica | No | Yes | 57mm throttle bodies, not derestrictable |
| 8796505 | Aprilia | RSV 2001-2003 | Yes | Yes | "Haga", 51mm throttle bodies, 2 into 2 SBK replica exhaust |
| 8796529 | Aprilia | RSV 2001-2003 | Yes | Yes | "Super D", 51mm throttle bodies, 2-into-2 Akrapovič SBK replica exhaust |
| 8796539 | Aprilia | Tuono Racing | Yes | Yes | 51mm throttlebodies, 2 into 1 slip-on exhaust |
| 8797512 | Aprilia | - | Yes | not known |  |
| Gabro_F255 | [GABRO](https://www.gabroracing.com/) | - | No | Yes |  |
| A63 | Aprilia | RSV | No | No | Early small valve engines |
| P08 | Aprilia | Early Falco | No | No | Early Falco, pre PA59 |
| Sport854 | Aprilia | RSV | No | No | Early small valve engines |
| RsvTiEvoUsa9901 | Aprilia | RSV | Yes | No | Early small valve engines, slip on exhaust |

## Notes

* **Images that need a daughterboard.**
  They are stored scrambled (see [Daughterboards](#daughterboards) below).
  A stock umod4 cannot run a scrambled image (see [Protected EPROMs](../USER_GUIDE.md#protected-eproms)).
* **Secondary maps.**
  An RP58 codebase lets the rider switch to a secondary "Derestricted" map set.
  The process of "Derestricting" a bike involves:

  * Pulling a plug from the airbox
  * Pulling a baffle from the inlet to the exhaust muffler
  * Cutting a specific wire going into the ECU

  Whether the secondary map set actually differs from the primary one depends on the EPROM.
  For example, the European RP58 contains a secondary map set that differs from the primary maps and can benefit from derestriction.
  In contrast, the 549USA, RP58USA and Edwards EPROMs contain two identical map sets (see [bin_files/Notes.md](bin_files/Notes.md)).
  When running those EPROMs, cutting the wire to activate the secondary maps will do nothing.
  **RP58 compatibility says only that UM4 can use the image's maps, not that the secondary set does anything.**
* **Tuned images.**
  A tuned image based on stock RP58 code can be used with the UM4 data-logging firmware, no matter how different its maps are.

## RP58 Compatibility

The term "RP58-compatibility" means that an EPROM uses an identical codebase as the stock RP58 EPROM.
Early bikes (like small valve RSVs) used a different codebase.
All late-model bikes used the RP58 codebase.

To determine if a specific EPROM is RP58-compatible, the umod4 project's `checksum` tool can be used.
The tool is built as part of the project build: see [BUILDING.md](../BUILDING.md).
To be RP58-compatible, a murmur3 checksum of the code range (offsets `0x4000` through `0x7FFF`) of the unprotected `.bin` file must be `0x18E7E9F2`, the checksum of the stock RP58 code.
You can determine an EPROM's checksum using `checksum -o 0x4000 -3 <file>`, like this (assuming that you have these bin files in that directory, of course):

```bash
$ checksum -o 0x4000 -3 RP58.bin
0x18E7E9F2
$ checksum -o 0x4000 -3 549USA.bin
0x18E7E9F2                                <--- Compatible!
$ checksum -o 0x4000 -3 A63.bin
0xC270CB4A                                <--- NOT compatible!
```

## Bin Files

**This project does not contain any stock Aprilia EPROM .bin files.**
On the one hand, it would be super handy if this repository contained a library of .bin images.
On the other hand, I don't want to cause any trouble (real or imagined!) that might occur by including stock EPROM images as part of this repository.
The table above describes the EPROMs, without their contents.

A variety of stock Aprilia EPROM .bin files can be found on the web if you google for them.
Obviously, if you own a specific EPROM and have access to an EPROM reader, you can make your own '.bin' files.
You can store them wherever you like on your PC.
You load them onto your umod4 through its web page: see [Putting a .bin file into the umod4](../USER_GUIDE.md#putting-a-bin-file-into-the-umod4) in the User Guide.

The idea of a umod4 board containing every EPROM image ever released is quite possible, and seemingly cool.
But, as someone pointed out to me, it's also pretty useless in the sense that once you get an image ironed out exactly the way you want it, all those other images are just wasted space.

But if you really wanted to fill a umod4 with EPROM images, it's got a lot of space.
Nothing is stopping you!

## Daughterboards

Certain EPROMs required using a daughterboard to operate inside an ECU.
The EPROMs that needed daughterboards had their contents pre-scrambled.
A daughterboard would unscramble those contents on the fly as the ECU read them.
Plugging a copied scrambled EPROM into another ECU would not work unless you also had a daughterboard.

Scrambled EPROMs were freely copyable, but useless without their descrambling board.
But once a rider owned a single descrambler board, any scrambled EPROM could be duplicated in the normal fashion using an EPROM reader/writer, then installed into the daughterboard and used in an ECU.
So scrambling was certainly not the ultimate in security.

The **Protected** column in the table above marks the EPROMs that need a daughterboard.
What that means for the umod4 is in [Protected EPROMs](../USER_GUIDE.md#protected-eproms) in the User Guide.

Before you ask, it *is* possible to descramble the EPROM images that need a daughterboard using software.
In fact, the umod4 software is already set up to use a software-based 'daughterboard' to descramble images on-the-fly as whole images or partial maps get loaded.
But for the same reasons as not including .bin files in this repository, I'm not including the daughterboard feature in the project.
I'm not looking for any sort of trouble.
On the other hand, nothing precludes anyone from writing their own descrambler.
The codebase is set up to plug one in, should you choose to write it.

Honestly, it took a winter storm and a day-long power failure with no internet.
That gave me enough distraction-free time to imagine what the daughterboard had to be doing.
Then, maybe a week of work doing experiments to work out precisely what was going on.
So obviously, it can be done, and I am not the sharpest tool in the toolbox.
