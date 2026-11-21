# Bin Files

This directory is an optional place to keep EPROM .bin files while you work with them.
Nothing requires you to use it: your .bin files can live anywhere on your PC, because you load them onto the umod4 through its web page.
See [Putting a .bin file into the umod4](../../USER_GUIDE.md#putting-a-bin-file-into-the-umod4) in the User Guide.

Stock Aprilia EPROM images are deliberately not part of this repository.
The `.gitignore` in this directory keeps any `.bin` or `.bin.*` file you put here from being committed by accident.
See [Bin Files](../README.md#bin-files) in the EPROM library for the reasoning.

## The one thing the build does with this directory

The EP firmware contains a built-in 'limp mode' image that it loads when it cannot load anything else.
By default, the build makes that image from the maps in `RP58.bin` combined with the code from `UM4.bin`.
If there is an `RP58.bin` in this directory, the build uses its maps.
If there is not, the limp mode image is simply `UM4.bin`.
The details are in the limp mode section of [EP/CMakeLists.txt](../../EP/CMakeLists.txt).

[Notes.md](Notes.md) describes how to compare the map sets of two EPROMs.
