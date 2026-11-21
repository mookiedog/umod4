# Bin Files

This directory is an optional place to keep EPROM .bin files while you work with them.
Nothing requires you to use it: your .bin files can live anywhere on your PC, because you load them onto the umod4 through its web page.
See [Putting a .bin file into the umod4](../../USER_GUIDE.md#putting-a-bin-file-into-the-umod4) in the User Guide.

Stock Aprilia EPROM images are deliberately not part of this repository.
The `.gitignore` in this directory keeps any `.bin` or `.bin.*` file you put here from being committed by accident.
See [Bin Files](../README.md#bin-files) in the EPROM library for the reasoning.

## The one thing the build does with this directory

The EP firmware contains a built-in 'limp mode' image that it loads when it cannot load anything else.
By default that image is `UM4.bin` as built: the UM4 code with UM4's own maps, which are the 549USA maps.

To use the maps from another stock image instead, make a symlink named `LIMP_MODE_MAPS` in this directory that points at a 32 KB `.bin` file here:

```bash
cd eprom_lib/bin_files
ln -s RP58.bin LIMP_MODE_MAPS
```

The build then takes the maps (the first 7168 bytes) from that file and the code (the rest) from `UM4.bin`.
The file must be an unprotected image with the stock RP58 code base, marked "RP58-compatible" in the [table](../README.md): maps from anything else cannot be used by the UM4 code.
The build checks this with the `checksum` tool and stops with an explanation if the file does not qualify.
This check happens only when the firmware is built: at run time the EP never refuses an image.
To point the link at a different file, use `ln -sfn <file>.bin LIMP_MODE_MAPS`.
To go back to the default, remove the link with `rm LIMP_MODE_MAPS`.

Git ignores the link, so choosing maps never shows up as a change.
Every build ends by printing what the limp mode image is made of, for example `Limp Mode image is UM4 code, maps from RP58.bin`.
A link that points at a missing file, or at a file that is not 32768 bytes, stops the configure step with an explanation.

The details are in the "Limp mode maps" section of the top-level [CMakeLists.txt](../../CMakeLists.txt) and the limp mode section of [EP/CMakeLists.txt](../../EP/CMakeLists.txt).

[Notes.md](Notes.md) describes how to compare the map sets of two EPROMs.
