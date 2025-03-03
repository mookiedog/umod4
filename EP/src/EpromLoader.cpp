#include "EpromLoader.h"
#include <string.h>

#include "murmur3.h"
#include "epromEmulator.h"
#include "RP58_memorymap.h"

// Fix this, it should be included from somewhere
uint8_t readEpromViaDaughterboard(uint32_t offset, uint8_t* scrambledEpromImage);


#if 0
const char* s = "The quick brown fox jumps over the lazy dog";
  uint32_t hash = murmur3_32((uint8_t*)s, strlen(s), 0x9747b28c);
  if (hash != 0x2FA826CD) {
    panic("Hash fail");
  }
#endif

// --------------------------------------------------------------------------------------------
bool EpromLoader::loadImage(uint8_t* epromDoc)
{
  return loadRange(epromDoc, 0, 32768);
}

// --------------------------------------------------------------------------------------------
bool EpromLoader::loadMapblob(uint8_t* epromDoc)
{
  return loadRange(epromDoc, RP58_MAPBLOB_OFFSET, RP58_MAPBLOB_LENGTH);
}

bool EpromLoader::loadRange(uint8_t* epromDoc, uint32_t startOffset, uint32_t length)
{
  if (startOffset > 32767) {
    return false;
  }

  if ((startOffset+length)>32768) {
    return false;
  }

  if (length == 0) {
    return true;
  }

  bool found;
  char daughterboard = 'N';

  // Find out if this eprom uses a daughterboard
  element_t db_element;
  found = Bson::findElement(epromDoc, "daughterboard", db_element);
  if (!found) {
    return false;
  }

  if (db_element.elementType == BSON_TYPE_UTF8) {
    // This is problematic: I might need something that converts the data pointed at by *data to a real type
    if (0 == strcmp((const char*)db_element.data+4, "A")) {
      daughterboard = 'A';
    }
  }

  // The "mem" document at the top-level inside this epromDoc describes the entire image
  element_t mem_element;
  found = Bson::findElement(epromDoc, "mem", mem_element);
  if (!found || (mem_element.elementType != BSON_TYPE_EMBEDDED_DOC)) {
    return false;
  }

  // Extract the details for the image:
  uint8_t* imageMemDoc = mem_element.data;
  meminfo_t imageMemInfo;
  bool success = getMemInfo(imageMemDoc, imageMemInfo);

  // Verify the M3 hash:
   uint32_t hash = murmur3_32(imageMemInfo.binData, imageMemInfo.length, ~0x0);
   if (hash != imageMemInfo.m3) {
    __asm("nop");
    //return false;
   }

  if (daughterboard == 'A') {
    // If the image uses a standard Aprilia daughterboard, we need to descramble it as we copy
    uint8_t* buffer = (uint8_t*)IMAGE_BASE;
    for (uint32_t offset=startOffset; offset<startOffset+length; offset++) {
      uint8_t byte = readEpromViaDaughterboard(offset, imageMemInfo.binData);
      *buffer++ = byte;
    }
  }
  else {
    // Unscrambled images can simply be copied
    memcpy((uint8_t*)IMAGE_BASE + startOffset, imageMemInfo.binData + startOffset , length);
  }
  return true;
}

bool EpromLoader::getMemInfo(uint8_t* memDoc, meminfo_t& meminfo)
{
  element_t e;

  bool found = Bson::findElement(memDoc, "startOffset", e);
  if (!found || (e.elementType != BSON_TYPE_INT32)) {
    return false;
  }
  meminfo.startOffset = Bson::read_unaligned_uint32(e.data);

  found = Bson::findElement(memDoc, "length", e);
  if (!found || (e.elementType != BSON_TYPE_INT32)) {
    return false;
  }
  meminfo.length = Bson::read_unaligned_uint32(e.data);

  found = Bson::findElement(memDoc, "m3", e);
  if (!found || (e.elementType != BSON_TYPE_INT32)) {
    return false;
  }
  meminfo.m3 = Bson::read_unaligned_uint32(e.data);

  found = Bson::findElement(memDoc, "bin", e);
  if (!found || (e.elementType != BSON_TYPE_BINARY_DATA)) {
    return false;
  }

  // A binary field starts off with a 32-bit length
  uint32_t length = Bson::read_unaligned_uint32(e.data);
  if (length != 32768) {
    return false;
  }

  // We ignore the subtype, but we need to be aware that it is present:
  uint8_t binaryDataSubtype = (uint8_t)*(e.data+4);
  if (binaryDataSubtype != 0x00) {
    return false;
  }

  // The real EPROM binary image starts 1 byte after the binary subtype byte
  meminfo.binData = e.data+5;

  return true;
}
