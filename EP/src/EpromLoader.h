#if !defined EPROMLOADER_H
#define EPROMLOADER_H

#include <stdint.h>

#include "bsonlib.h"

class EpromLoader {

  typedef struct{
    uint32_t startOffset;
    uint32_t length;
    uint32_t m3;
    uint8_t* binData;
  } meminfo_t;

  public:
    // Load a sequential range of bytes from an eprom image. Bytes are always loaded to
    // the same offset in the SRAM eprom image array.
    // startOffset: the starting EPROM offset (0x0000..0x7FFF)
    // length: the number of bytes to load
    // Note: (startOffset+length) must be <= 32768 (0x8000)
    static bool loadRange(uint8_t* epromDoc, uint32_t startOffset, uint32_t length);

    // Load the entire binary image from the specified "eprom" sub-document.
    static bool loadImage(uint8_t* epromDoc);

    // Load the "mapblob", meaning the complete set of map data from the binary image specified by the "eprom" sub-document.
    // Only works for RP58-compatible EPROMs!
    // Any RP58-compatible EPROM can get logging capabilities by loading a UM4 logging image,
    // then loading the maps from the other EPROM on top of the UM4 image.
    static bool loadMapblob(uint8_t* epromDoc);

    // Search a BSON doc to find a top-level element named "mem"
    // with a value of type BSON_TYPE_EMBEDDED_DOC. The embedded doc
    // must contain elements named:
    //    "startOffset" (the EPROM offset where the bin data should be stored)
    //    "length" (how much data to store)
    //    "m3" (the M3 checksum of the bin area from offset 0 through length bytes)
    //    "bin" (pointer to the actual binary data, might be scrambled if daughterboard is in use)
    // Paramters:
    // doc: points at the BSON subdoc containing the image's mem definition
    // meminfo: gets initialized as per the BSON data
    // returns true on success, false for any sort of failure
    static bool getMemInfo(uint8_t* doc, meminfo_t& meminfo);
};

#endif
