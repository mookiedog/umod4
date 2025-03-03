# Bin Files

This directory can be populated with whatever .bin files you can find on the web, or from your own collection.

For each .bin file in this directory, there should be a corresponding ".dsc" file in the source directory
that describes the bin file. The eprom_lib/CMakeLists.txt file should be modified to match your collection
of .bin files. Likewise, the EP's CMakeLists.txt should be modified to pull in whatever .bin files
you want in your system.

In truth, this eprom_lib in its present form is temporary. At some point, the .bin files and .dsc files
should be part of a collection that is managed by an app on someone's phone, to be loaded into the ECU
as desired.

For now, we build the .bin and .dsc files right into the EP firmware as BSON documents.
