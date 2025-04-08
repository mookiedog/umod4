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
    typedef uint8_t* bsonDoc_t;

    // Load a sequential range of bytes from an eprom image. Bytes are always loaded to
    // the same offset in the SRAM eprom image array.
    // startOffset: the starting EPROM offset (0x0000..0x7FFF)
    // length: the number of bytes to load
    // Note: (startOffset+length) must be <= 32768 (0x8000)
    static bool loadRange(bsonDoc_t epromDoc, uint32_t startOffset, uint32_t length);

    // Returns a pointer to the BSON epromDoc corresponding to the named EPROM from within the BSON document partition
    // Returns nullptr if the BSON doc cannot be located
    static bsonDoc_t findEprom(const char* epromName);

    // Load an EPROM image, from a specific EPROM document or by searching the BSON partition by name.
    static bool loadImage(const char* imageName);
    static bool loadImage(bsonDoc_t epromDoc);

    // Load a map blob, either from a specific EPROM document or by searching the BSON partition by name.
    // The mapblob is the complete set of map data extracted from the EPROM binary image.
    // Only works for RP58-compatible EPROMs!
    static bool loadMapblob(const char* imageName);
    static bool loadMapblob(bsonDoc_t epromDoc);

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
    static bool getMemInfo(bsonDoc_t doc, meminfo_t& meminfo);
};

#endif
