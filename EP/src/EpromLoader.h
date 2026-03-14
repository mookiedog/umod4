#if !defined EPROMLOADER_H
#define EPROMLOADER_H

#include <stdint.h>

#include "bsonlib.h"

class EpromLoader {

    typedef struct{
        uint32_t startOffset;
        uint32_t length;
        uint32_t m3;
        const uint8_t* binData;
    } meminfo_t;

    public:
    typedef const uint8_t* bsonDoc_t;

    // Load a sequential range of bytes from an eprom image. Bytes are always loaded to
    // the same offset in the SRAM eprom image array.
    // startOffset: the starting EPROM offset (0x0000..0x7FFF)
    // length: the number of bytes to load
    // epromName: used for logging (the name is no longer stored inside the BSON subdoc)
    // Note: (startOffset+length) must be <= 32768 (0x8000)
    static uint8_t loadRange(bsonDoc_t epromDoc, uint32_t startOffset, uint32_t length, const char* epromName);

    // Load an EPROM image from the image_store.
    static uint8_t loadImage(bsonDoc_t epromDoc, const char* epromName);

    // Load the first loadable image from the image_store partition.
    // Slot 0 holds the image_selector BSON document: {"images": [{"slot": N, "m3": hash, "name": "...", "dsc": "..."}, ...]}
    // Tries each entry in order; falls back to the built-in limp mode image on failure.
    // If the loaded entry has a "dsc" key, its value is logged as an informational string.
    static void loadImage();

    // Load a map blob from a specific EPROM document.
    // The mapblob is the complete set of map data extracted from the EPROM binary image.
    // Only works for RP58 map-style EPROMs!
    static uint8_t loadMapblob(bsonDoc_t epromDoc, const char* epromName);

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
    static uint8_t getMemInfo(bsonDoc_t doc, meminfo_t& meminfo);

    private:
};

#endif
