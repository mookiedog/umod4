#include "EpromLoader.h"
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"

#include "murmur3.h"
#include "epromEmulator.h"
#include "RP58_memorymap.h"

#include "EP_log.h"

#if defined HAS_DESCRAMBLER
#include "descramble.h"
#else
extern uint8_t readEpromViaDaughterboard(uint32_t ecuAddr, uint8_t* scrambledEpromImage);
#endif

extern uint32_t __BSON_IMAGE_PARTITION_START_ADDR;
extern uint32_t __BSON_IMAGE_PARTITION_SIZE_BYTES;

#if 0
const char* s = "The quick brown fox jumps over the lazy dog";
uint32_t hash = murmur3_32((uint8_t*)s, strlen(s), 0x9747b28c);
if (hash != 0x2FA826CD) {
    panic("Hash fail");
}
#endif

extern void enqueue(uint8_t id, uint8_t data);

// --------------------------------------------------------------------------------------------
void logEpromName_find(const char* nameP)
{
    char c;

    if (nameP) {
        do {
            c = *nameP++;
            enqueue(LOGID_EP_FIND_NAME_TYPE_CS, c);
        } while (c != '\0');
    }
}

// --------------------------------------------------------------------------------------------
void logEpromName_load(const char* nameP)
{
    char c;

    if (nameP) {
        do {
            c = *nameP++;
            enqueue(LOGID_EP_LOAD_NAME_TYPE_CS, c);
        } while (c != '\0');
    }
}




// --------------------------------------------------------------------------------------------
// Check every BSON document in the BSON partition to see if it defines a
// key called "eprom" where the key value has a type of "BSON_TYPE_EMBEDDED_DOC".
// If so, look inside the embedded doc and check if it defines a "name" key
// with a value that matches the epromName parameter.
uint8_t* EpromLoader::findEprom(const char* epromName)
{
    uint8_t* docP = (uint8_t*)&__BSON_IMAGE_PARTITION_START_ADDR;

    printf("%s: Locating EPROM \"%s\"\n", __FUNCTION__, epromName);
    logEpromName_find(epromName);
    while (1) {
        #if !defined BSON_PARTITION_IS_PADDED
        #error "Need to define if the BSON partition is padded or not!"
        #elif BSON_PARTITION_IS_PADDED != 0
        // WARNING: if this system is compiled with -Os, the EPROM BSON documents
        // are packed into the BSON partition with no space between them.
        // If you compile with -Og, the compiler and/or linker pads the space between
        // each document in the partition forcing every document to start on a word boundary.
        docP = (uint8_t*)(((uint32_t)(docP)+3) & ~3);
        #endif

        uint32_t docLength = Bson::read_unaligned_uint32(docP);
        if (docLength == 0xFFFFFFFF) {
            break;
        }

        // Check the doc, looking for a top-level element named "eprom"
        element_t e;
        bool found = Bson::findElement(docP, "eprom", e);

        if (found) {
            // Make sure that "eprom" element's data type is 'embedded document':
            if (e.elementType == BSON_TYPE_EMBEDDED_DOC) {
                // The element data represents the start of the embedded doc
                bsonDoc_t epromDoc = e.data;
                // Search for the "name" element inside the eprom doc:
                element_t name_e;
                found = Bson::findElement(epromDoc, "name", name_e);
                if (found) {
                    if (name_e.elementType == BSON_TYPE_UTF8) {
                        const char* nameP = (const char*)name_e.data+4;
                        if (0==strcmp(epromName, nameP)) {
                            // Found it!
                            enqueue(LOGID_EP_LOAD_ERR_TYPE_U8, LOGID_EP_LOAD_ERR_VAL_NOERR);
                            return epromDoc;
                        }
                    }
                }
            }
        }

        // We didn't find what we wanted in this doc.
        // Try the next one in the BSON partition
        docP += docLength;
    }

    enqueue(LOGID_EP_LOAD_ERR_TYPE_U8, LOGID_EP_LOAD_ERR_VAL_NOTFOUND);
    return nullptr;
}

// --------------------------------------------------------------------------------------------
uint8_t EpromLoader::loadImage(const char* imageName)
{
    bsonDoc_t epromDoc;
    uint8_t err;
    uint32_t elapsed, t0, t1;

    printf("%s: Loading complete EPROM image \"%s\"\n", __FUNCTION__, imageName);

    t0 = time_us_32();
    epromDoc = findEprom(imageName);
    if (!epromDoc) {
        printf("%s: FATAL: Unable to find BSON eprom doc named \"%s\"!\n", __FUNCTION__, imageName);
        return LOGID_EP_LOAD_ERR_VAL_NOTFOUND;
    }
    else {
        // Load the image using the "mem" info found in the epromDoc
        err = loadImage(epromDoc);
        if (err) {
            printf("%s: Unable to load meminfo from BSON eprom doc \"%s\"!\n", __FUNCTION__, imageName);
            return err;
        }
    }
    t1 = time_us_32();
    elapsed = t1 - t0;
    printf("%s: Image %s loaded in %u milliseconds\n", __FUNCTION__, imageName, (elapsed+500) / 1000);
    return LOGID_EP_LOAD_ERR_VAL_NOERR;
}


// --------------------------------------------------------------------------------------------
uint8_t EpromLoader::loadImage(bsonDoc_t epromDoc)
{
    return loadRange(epromDoc, 0, 32768);
}

// --------------------------------------------------------------------------------------------
uint8_t EpromLoader::loadMapblob(const char* imageName)
{
    bsonDoc_t epromDoc;
    uint8_t err;
    uint32_t elapsed, t0, t1;

    printf("%s: Loading Mapblob from EPROM image \"%s\"\n", __FUNCTION__, imageName);
    t0 = time_us_32();
    epromDoc = findEprom(imageName);
    if (!epromDoc) {
        printf("%s: FATAL: Unable to find BSON eprom doc named \"%s\"!\n", __FUNCTION__, imageName);
        return LOGID_EP_LOAD_ERR_VAL_NOTFOUND;
    }
    else {
        // Load the image using the "mem" info found in the epromDoc
        err = loadMapblob(epromDoc);
        if (err) {
            printf("%s: Unable to load meminfo from BSON eprom doc \"%s\"!", __FUNCTION__, imageName);
            return err;
        }
    }
    t1 = time_us_32();
    elapsed = t1 - t0;
    printf("%s: Image %s loaded in %u milliseconds\n", __FUNCTION__, imageName, (elapsed+500) / 1000);
    return LOGID_EP_LOAD_ERR_VAL_NOERR;
}

// --------------------------------------------------------------------------------------------
uint8_t EpromLoader::loadMapblob(bsonDoc_t epromDoc)
{
    printf("%s: Loading Mapblob from epromDoc\n", __FUNCTION__);
    uint8_t err;
    err = loadRange(epromDoc, RP58_MAPBLOB_OFFSET, RP58_MAPBLOB_LENGTH);
    return err;
}

// --------------------------------------------------------------------------------------------
uint8_t EpromLoader::loadRange(bsonDoc_t epromDoc, uint32_t startOffset, uint32_t length)
{
    bool found;
    element_t name_element;
    const char* name;

    found = Bson::findElement(epromDoc, "name", name_element);
    if (!found) {
        printf("%s: ERR: Unable to find the \"name\" key in the BSON doc\n", __FUNCTION__);
        return LOGID_EP_LOAD_ERR_VAL_NONAME;
    }
    if (name_element.elementType == BSON_TYPE_UTF8) {
        #warning "Fix me!"
        name = (const char*)name_element.data+4;
    }
    logEpromName_load(name);

    // Addr & length are sent out big-endian like the rest of the 16-bit ECU data
    enqueue(LOGID_EP_LOAD_ADDR_TYPE_U16, (startOffset >> 8) & 0xFF);
    enqueue(LOGID_EP_LOAD_ADDR_TYPE_U16+1, startOffset & 0xFF);
    enqueue(LOGID_EP_LOAD_LEN_TYPE_U16, (length >> 8) & 0xFF);
    enqueue(LOGID_EP_LOAD_LEN_TYPE_U16+1, length & 0xFF);

    printf("%s: Loading offset 0x%04X for 0x%04X bytes\n", __FUNCTION__, startOffset, length);

    if (startOffset > 32767) {
        printf("%s: ERR: startOffset out of range [0..32767]: %u\n", __FUNCTION__, startOffset);
        return LOGID_EP_LOAD_ERR_VAL_BADOFFSET;
    }

    if ((startOffset+length)>32768) {
        printf("%s: ERR: requested startOffset+length [%u] goes past end EPROM [32768]: %u\n", __FUNCTION__, startOffset+length);
        return LOGID_EP_LOAD_ERR_VAL_BADLENGTH;
    }

    if (length == 0) {
        printf("%s: Requested length of 0: ignored\n", __FUNCTION__);
        return LOGID_EP_LOAD_ERR_VAL_NOERR;
    }

    char daughterboard = 'N';

    // Find out if this eprom uses a daughterboard
    element_t db_element;
    found = Bson::findElement(epromDoc, "daughterboard", db_element);
    if (!found) {
        printf("%s: ERR: Unable to find the \"daughterboard\" key in the BSON doc\n", __FUNCTION__);
        return LOGID_EP_LOAD_ERR_VAL_NODAUGHTERBOARDKEY;
    }

    if (db_element.elementType == BSON_TYPE_UTF8) {
        // This is problematic: I might need something that converts the data pointed at by *data to a real type
        if (0 == strcmp((const char*)db_element.data+4, "A")) {
            printf("%s: Daughterboard: Aprilia V1\n", __FUNCTION__);
            daughterboard = 'A';
        }
        else if (0 == strcmp((const char*)db_element.data+4, "N")) {
            printf("%s: Daughterboard: none\n", __FUNCTION__);
        }
    }

    // The "mem" document at the top-level inside this epromDoc describes the entire image
    element_t mem_element;
    found = Bson::findElement(epromDoc, "mem", mem_element);
    if (!found || (mem_element.elementType != BSON_TYPE_EMBEDDED_DOC)) {
        printf("%s: ERR: Unable to find the \"mem\" key in the BSON doc\n", __FUNCTION__);
        return LOGID_EP_LOAD_ERR_VAL_NOMEMKEY;
    }

    // Extract the details for the image:
    uint8_t* imageMemDoc = mem_element.data;
    meminfo_t imageMemInfo;
    uint8_t err = getMemInfo(imageMemDoc, imageMemInfo);
    if (err) {
        printf("%s: ERR: Unable to get memInfo\n", __FUNCTION__);
        return err;
    }

    printf("%s: memory info\n"
        "  StartAddr:  0x%04X\n"
        "  Length:     0x%04X\n"
        "  M3:         0x%08X\n",
        __FUNCTION__,
        imageMemInfo.startOffset,
        imageMemInfo.length,
        imageMemInfo.m3
    );

    // Verify the M3 hash:
    uint32_t hash = murmur3_32(imageMemInfo.binData, imageMemInfo.length, ~0x0);
    if (hash != imageMemInfo.m3) {
        printf("%s: Hash checksum failed: calculated 0x%08X, expected 0x%08X\n", __FUNCTION__, hash, imageMemInfo.m3);
        return LOGID_EP_LOAD_ERR_VAL_M3FAIL;
    }

    if (daughterboard == 'A') {
        printf("%s: Loading data from protected image [0x%04X..0x%04X]\n", __FUNCTION__, startOffset , (startOffset + length)-1);
        // If the image uses a standard Aprilia daughterboard, we need to descramble it as we copy
        uint8_t* buffer = (uint8_t*)IMAGE_BASE;
        for (uint32_t offset=startOffset; offset<startOffset+length; offset++) {
            uint8_t byte = readEpromViaDaughterboard(offset, imageMemInfo.binData);
            *buffer++ = byte;
        }
    }
    else {
        // Unscrambled images can simply be copied
        printf("%s: Loading data from unprotected image [0x%04X..0x%04X]\n", __FUNCTION__, startOffset , (startOffset + length)-1);
        memcpy((uint8_t*)IMAGE_BASE + startOffset, imageMemInfo.binData + startOffset , length);
    }

    printf("%s: Success!\n", __FUNCTION__);
    return LOGID_EP_LOAD_ERR_VAL_NOERR;
}

uint8_t EpromLoader::getMemInfo(bsonDoc_t memDoc, meminfo_t& meminfo)
{
    element_t e;

    bool found = Bson::findElement(memDoc, "startOffset", e);
    if (!found || (e.elementType != BSON_TYPE_INT32)) {
        printf("%s: ERR: missing key \"startOffset\"\n", __FUNCTION__);
        return LOGID_EP_LOAD_ERR_VAL_MISSINGKEYSTART;
    }
    meminfo.startOffset = Bson::read_unaligned_uint32(e.data);

    found = Bson::findElement(memDoc, "length", e);
    if (!found || (e.elementType != BSON_TYPE_INT32)) {
        printf("%s: ERR: missing key \"length\"\n", __FUNCTION__);
        return LOGID_EP_LOAD_ERR_VAL_MISSINGKEYLENGTH;
    }
    meminfo.length = Bson::read_unaligned_uint32(e.data);

    found = Bson::findElement(memDoc, "m3", e);
    if (!found) {
        printf("%s: ERR: missing key \"m3\"\n", __FUNCTION__);
        return LOGID_EP_LOAD_ERR_VAL_MISSINGKEYM3;
    }
    // The M3 output is always a 32-bit number. However, the JSON to BSON library will generate a 64-bit BSON data type if the
    // MS bit of the M3 output is a '1'. As a result, we need to be ready to deal with either data type we might find here:
    if ((e.elementType != BSON_TYPE_INT32) && (e.elementType != BSON_TYPE_INT64)) {
        printf("%s: ERR: m3 data has bad BSON data type 0x%02X, expected 0x10 or 0x12\n", __FUNCTION__, e.elementType);
        return LOGID_EP_LOAD_ERR_VAL_BADM3BSONTYPE;
    }
    if (e.elementType == BSON_TYPE_INT64) {
        uint32_t ms_word = Bson::read_unaligned_uint32(e.data+4);
        if (ms_word != 0) {
            printf("%s: ERR: 64-bit M3 value has a non-zero MS word: %u\n", __FUNCTION__, ms_word);
            return LOGID_EP_LOAD_ERR_VAL_BADM3VALUE;
        }
    }
    // Since the data is stored little-endian, this works for either 32 or 64 bit data:
    meminfo.m3 = Bson::read_unaligned_uint32(e.data);


    found = Bson::findElement(memDoc, "bin", e);
    if (!found || (e.elementType != BSON_TYPE_BINARY_DATA)) {
        printf("%s: ERR: missing key \"bin\"\n", __FUNCTION__);
        return LOGID_EP_LOAD_ERR_VAL_NOBINKEY;
    }

    // A binary field starts off with a 32-bit length
    uint32_t length = Bson::read_unaligned_uint32(e.data);
    if (length != 32768) {
        printf("%s: ERR: bad length field: expected 32768, saw %u\"\n", __FUNCTION__, length);
        return LOGID_EP_LOAD_ERR_VAL_BADBINLENGTH;
    }

    // We ignore the subtype, but we need to be aware that it is present:
    uint8_t binaryDataSubtype = (uint8_t)*(e.data+4);
    if (binaryDataSubtype != 0x00) {
        printf("%s: ERR: expected binary data subtype 0x00, saw 0x%02X\n", __FUNCTION__, binaryDataSubtype);
        return LOGID_EP_LOAD_ERR_VAL_BADBINSUBTYPE;
    }

    // The real EPROM binary image starts 1 byte after the binary subtype byte
    meminfo.binData = e.data+5;

    return LOGID_EP_LOAD_ERR_VAL_NOERR;
}
