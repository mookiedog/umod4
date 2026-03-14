#include "EpromLoader.h"
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"

#include "murmur3.h"
#include "epromEmulator.h"
#include "RP58_memorymap.h"

#include "limp_mode_image.h"

#include "log_ids.h"
extern void enqueue(uint8_t id, uint8_t data);

#if defined HAS_DESCRAMBLER
#include "descramble.h"
#else
extern uint8_t readEpromViaDaughterboard(uint32_t ecuAddr, const uint8_t* scrambledEpromImage);
#endif

extern uint32_t __IMAGE_STORE_PARTITION_START_ADDR;
extern uint32_t __IMAGE_STORE_PARTITION_SIZE_BYTES;

// Note "xxx_addr" refers to byte addresses in the RP2040 address space, not block addresses in the flash device!
const uint32_t image_store_PartitionStart_addr = (uint32_t)&__IMAGE_STORE_PARTITION_START_ADDR;
const uint32_t image_store_PartitionSize_bytes = (uint32_t)&__IMAGE_STORE_PARTITION_SIZE_BYTES;

// Forward declarations for logging helpers defined later in this file
void logEpromName_load(const char* nameP);

// --------------------------------------------------------------------------------------------
// Log a self-describing informational string using LOGID_EP_INFO_TYPE_CS.
// The string is emitted character by character, NULL-terminated.
static void logInfo(const char* str)
{
    if (str) {
        char c;
        do {
            c = *str++;
            enqueue(LOGID_EP_INFO_TYPE_CS, c);
        } while (c != '\0');
    }
}

// Slot layout constants for the new 64K self-describing slot format.
static const uint32_t SLOT_SIZE_BYTES   = 65536;  // 64K per slot
static const uint32_t SLOT_HEADER_SIZE  = 32768;  // First 32KB: BSON header {name, description, image_m3, protection}
static const uint32_t SLOT_IMAGE_OFFSET = 32768;  // Second 32KB: raw EPROM binary
static const uint32_t MAX_IMAGE_SLOTS   = 127;    // Slots 1..127; slot 0 is the selector
static const uint32_t MAPBLOB_SIZE      = 0x1C00; // Bytes 0x0000–0x1BFF are the mapblob region

// Per-slot directory entry built by pre-scanning all slots once at boot.
struct SlotInfo {
    uint8_t  index;          // 1–127
    char     name[32];       // "name" field from slot BSON header
    uint32_t image_m3;       // "image_m3" field from slot BSON header
    char     protection;     // 'A' = Aprilia daughterboard, 'N' = none
};

// --------------------------------------------------------------------------------------------
// Load the image data from a slot into a destination buffer, applying the unscrambler if
// the slot has protection == 'A'.  For 'A' protection, readEpromViaDaughterboard() is called
// byte-by-byte with the real EPROM address (0x0000..0x7FFF); memcpy cannot be used because
// the descrambler is address-dependent.
//
// slotIndex:   1-based slot index
// dest:        destination buffer, must be at least `length` bytes
// protection:  'A' or 'N'
// startOffset: starting EPROM address (0x0000..0x7FFF)
// length:      number of bytes to copy
static void loadSlotImage(uint8_t slotIndex, uint8_t* dest, char protection,
                          uint32_t startOffset = 0, uint32_t length = EPROM_IMAGE_SIZE_BYTES)
{
    const uint8_t* src = (const uint8_t*)(image_store_PartitionStart_addr
                                          + (uint32_t)slotIndex * SLOT_SIZE_BYTES
                                          + SLOT_IMAGE_OFFSET);
    if (protection == 'A') {
        // Protected images must get copied on a byte-by-byte basis through the daughterboard decoder
        for (uint32_t i = 0; i < length; i++) {
            dest[i] = readEpromViaDaughterboard(startOffset + i, src);
        }
    } else {
        memcpy(dest, src + startOffset, length);
    }
}

// --------------------------------------------------------------------------------------------
// With no params, load the first valid image defined by the image_store selector.
//
// Slot layout (new 64K format):
//   Slot 0  : image_selector BSON document (zero-padded to 64K)
//             Schema: {"images": [{"code": "name", "mapblob": "name" (opt),
//                                  "continue": true (opt)}, ...]}
//   Slots 1–127: self-describing 64K slots
//             First 32KB : BSON header {name, description, image_m3, protection (opt)}
//             Next  32KB : raw 32KB EPROM binary
//
// Boot sequence:
//   0. Pre-scan slots 1–127 into slot_dir[] to save time for what follows
//          - read first 4 bytes to detect empty slots
//          - parse BSON header for populated ones
//   1. Validate image selector data structure that is stored in slot 0.
//   2. For each selector entry:
//          - look up named slots in slot_dir[]
//          - verify hashes
//          - load image + optional mapblob, if needed
//   3. Fall back to built-in limp mode image if all entries fail.
//
void EpromLoader::loadImage()
{
    element_t imagesArray;

    printf("%s: Loading image via image_store (64K slot format)\n", __FUNCTION__);

    // -------------------------------------------------------------------------
    // Images can be stored 'sparsely' throughout the image store.
    // Pre-scan all the slots one time into a directory array to detect
    // which slots are known to be populated.
    // Populated slots get parsed for their name, image_m3, and protection.
    static SlotInfo slot_dir[MAX_IMAGE_SLOTS];
    uint8_t slot_dir_count = 0;

    for (uint8_t i = 1; i <= MAX_IMAGE_SLOTS; i++) {
        const uint8_t* headerPtr = (const uint8_t*)(image_store_PartitionStart_addr
                                                     + (uint32_t)i * SLOT_SIZE_BYTES);
        uint32_t docSize = Bson::read_unaligned_uint32(headerPtr);
        if (docSize == 0xFFFFFFFF || docSize < 5 || docSize > SLOT_HEADER_SIZE) {
            continue;  // empty or corrupt slot header
        }

        SlotInfo& si = slot_dir[slot_dir_count];
        si.index      = i;
        si.name[0]    = '\0';
        si.image_m3   = 0;
        si.protection = 'N';

        // Extract "name"
        element_t nameElem;
        if (Bson::findElement(headerPtr, "name", nameElem) && nameElem.elementType == BSON_TYPE_UTF8) {
            const char* n = (const char*)nameElem.data + 4;
            strncpy(si.name, n, sizeof(si.name) - 1);
            si.name[sizeof(si.name) - 1] = '\0';
        }

        // Extract "image_m3"
        element_t m3Elem;
        if (Bson::findElement(headerPtr, "image_m3", m3Elem)) {
            if (m3Elem.elementType == BSON_TYPE_INT32 || m3Elem.elementType == BSON_TYPE_INT64) {
                si.image_m3 = Bson::read_unaligned_uint32(m3Elem.data);
            }
        }

        // Extract "protection" (optional; default 'N')
        element_t protElem;
        if (Bson::findElement(headerPtr, "protection", protElem) && protElem.elementType == BSON_TYPE_UTF8) {
            const char* p = (const char*)protElem.data + 4;
            if (p[0] == 'A') si.protection = 'A';
        }

        printf("%s: slot_dir[%u]: slot=%u name=\"%s\" m3=0x%08X prot=%c\n",
               __FUNCTION__, slot_dir_count, i, si.name, si.image_m3, si.protection);
        slot_dir_count++;
    }

    printf("%s: Found %u populated image slots\n", __FUNCTION__, slot_dir_count);

    // Load-result JSON array, built up as we process selector entries.
    // Accessible at limp_mode so we can append a final limp-mode entry.
    static char imgsel_str[256];
    int imgsel_pos = 0;
    bool imgsel_first = true;
    imgsel_str[imgsel_pos++] = '[';
    char indexStr[8];

    // -------------------------------------------------------------------------
    // Validate the image selector, which will be sitting in slot 0.
    const uint8_t* selectorDoc = (const uint8_t*)image_store_PartitionStart_addr;
    uint32_t selectorSize = Bson::read_unaligned_uint32(selectorDoc);
    if (selectorSize < 5 || selectorSize > SLOT_SIZE_BYTES) {
        printf("%s: Invalid selector size %u in slot 0\n", __FUNCTION__, selectorSize);
        enqueue(LOGID_EP_LOAD_ERR_TYPE_U8, LOGID_EP_LOAD_ERR_VAL_BADMAGIC);
        goto limp_mode;
    }

    if (!Bson::findElement(selectorDoc, "images", imagesArray) ||
        imagesArray.elementType != BSON_TYPE_ARRAY) {
        printf("%s: No \"images\" array in selector BSON\n", __FUNCTION__);
        enqueue(LOGID_EP_LOAD_ERR_TYPE_U8, LOGID_EP_LOAD_ERR_VAL_NOIMAGES);
        goto limp_mode;
    }

    for (int32_t index = 0; ; index++) {
        snprintf(indexStr, sizeof(indexStr), "%d", index);

        element_t entry;
        if (!Bson::findElement(imagesArray.data, indexStr, entry)) {
            break;  // no more selector entries
        }
        if (entry.elementType != BSON_TYPE_EMBEDDED_DOC) {
            continue;
        }

        // ---------------------------------------------------------------
        // Extract selector entry fields.
        element_t codeElem;
        const char* codeName = nullptr;
        if (Bson::findElement(entry.data, "code", codeElem) && codeElem.elementType == BSON_TYPE_UTF8) {
            codeName = (const char*)codeElem.data + 4;
        }
        if (!codeName || codeName[0] == '\0') {
            printf("%s: Entry %s: missing \"code\" field, skipping\n", __FUNCTION__, indexStr);
            continue;
        }

        element_t mapblobElem;
        const char* mapblobName = nullptr;
        if (Bson::findElement(entry.data, "mapblob", mapblobElem) && mapblobElem.elementType == BSON_TYPE_UTF8) {
            mapblobName = (const char*)mapblobElem.data + 4;
            if (mapblobName[0] == '\0') mapblobName = nullptr;
        }

        element_t continueElem;
        bool continueFlag = Bson::findElement(entry.data, "continue", continueElem);

        logEpromName_load(codeName);
        printf("%s: Entry %s: code=\"%s\"", __FUNCTION__, indexStr, codeName);
        if (mapblobName) printf(" mapblob=\"%s\"", mapblobName);
        printf("\n");

        // Helper: append a fail result for this entry
        auto append_fail = [&]() {
            int w = snprintf(imgsel_str + imgsel_pos, sizeof(imgsel_str) - imgsel_pos - 4,
                                "%s{\"code\":\"%s\",\"load\":\"fail\"}", imgsel_first ? "" : ",", codeName);
            if (w > 0 && imgsel_pos + w < (int)sizeof(imgsel_str) - 4) imgsel_pos += w;
            imgsel_first = false;
        };

        // ---------------------------------------------------------------
        // Find the code slot in slot_dir[].
        const SlotInfo* codeSlot = nullptr;
        for (uint8_t d = 0; d < slot_dir_count; d++) {
            if (strcmp(slot_dir[d].name, codeName) == 0) {
                codeSlot = &slot_dir[d];
                break;
            }
        }
        if (!codeSlot) {
            printf("%s: Code slot \"%s\" not found\n", __FUNCTION__, codeName);
            enqueue(LOGID_EP_LOAD_ERR_TYPE_U8, LOGID_EP_LOAD_ERR_VAL_NOTFOUND);
            append_fail();
            continue;
        }

        // ---------------------------------------------------------------
        // Verify code slot hash.
        const uint8_t* codeImagePtr = (const uint8_t*)(image_store_PartitionStart_addr
                                                        + (uint32_t)codeSlot->index * SLOT_SIZE_BYTES
                                                        + SLOT_IMAGE_OFFSET);
        uint32_t codeHash = murmur3_32(codeImagePtr, EPROM_IMAGE_SIZE_BYTES, ~0x0);
        if (codeHash != codeSlot->image_m3) {
            printf("%s: Code slot \"%s\" hash mismatch: expected 0x%08X, got 0x%08X\n",
                    __FUNCTION__, codeName, codeSlot->image_m3, codeHash);
            enqueue(LOGID_EP_LOAD_ERR_TYPE_U8, LOGID_EP_LOAD_ERR_VAL_BAD_HASH);
            append_fail();
            continue;
        }

        // ---------------------------------------------------------------
        // Load code image into EPROM_IMAGE_BASE.
        enqueue(LOGID_EP_LOAD_IMAGESLOT_TYPE_U8, codeSlot->index);
        loadSlotImage(codeSlot->index, (uint8_t*)EPROM_IMAGE_BASE, codeSlot->protection);
        printf("%s: Loaded code \"%s\" from slot %u (prot=%c)\n",
                __FUNCTION__, codeName, codeSlot->index, codeSlot->protection);

        // ---------------------------------------------------------------
        // Optional: If "mapblob" specified, find, verify, and overlay on top of code image
        if (mapblobName) {
            const SlotInfo* mapSlot = nullptr;
            for (uint8_t d = 0; d < slot_dir_count; d++) {
                if (strcmp(slot_dir[d].name, mapblobName) == 0) {
                    mapSlot = &slot_dir[d];
                    break;
                }
            }
            if (!mapSlot) {
                printf("%s: Mapblob slot \"%s\" not found\n", __FUNCTION__, mapblobName);
                enqueue(LOGID_EP_LOAD_ERR_TYPE_U8, LOGID_EP_LOAD_ERR_VAL_NOTFOUND);
                append_fail();
                continue;
            }

            const uint8_t* mapImagePtr = (const uint8_t*)(image_store_PartitionStart_addr
                                                            + (uint32_t)mapSlot->index * SLOT_SIZE_BYTES
                                                            + SLOT_IMAGE_OFFSET);
            uint32_t mapHash = murmur3_32(mapImagePtr, EPROM_IMAGE_SIZE_BYTES, ~0x0);
            if (mapHash != mapSlot->image_m3) {
                printf("%s: Mapblob slot \"%s\" hash mismatch: expected 0x%08X, got 0x%08X\n",
                        __FUNCTION__, mapblobName, mapSlot->image_m3, mapHash);
                enqueue(LOGID_EP_LOAD_ERR_TYPE_U8, LOGID_EP_LOAD_ERR_VAL_BAD_HASH);
                append_fail();
                continue;
            }

            // Overlay mapblob bytes 0x0000–0x1BFF directly onto EPROM_IMAGE_BASE
            loadSlotImage(mapSlot->index, (uint8_t*)EPROM_IMAGE_BASE, mapSlot->protection, 0, MAPBLOB_SIZE);
            printf("%s: Overlaid mapblob \"%s\" from slot %u (prot=%c)\n",
                    __FUNCTION__, mapblobName, mapSlot->index, mapSlot->protection);
        }

        // ---------------------------------------------------------------
        // Log success and send result to WP
        {
            int w;
            if (mapblobName) {
                w = snprintf(imgsel_str + imgsel_pos, sizeof(imgsel_str) - imgsel_pos - 4,
                                "%s{\"code\":\"%s\",\"mapblob\":\"%s\",\"load\":\"ok\"}",
                                imgsel_first ? "" : ",", codeName, mapblobName);
            } else {
                w = snprintf(imgsel_str + imgsel_pos, sizeof(imgsel_str) - imgsel_pos - 4,
                                "%s{\"code\":\"%s\",\"load\":\"ok\"}", imgsel_first ? "" : ",", codeName);
            }
            if (w > 0 && imgsel_pos + w < (int)sizeof(imgsel_str) - 4) imgsel_pos += w;
            imgsel_first = false;
        }

        if (!continueFlag) {
            // Normal: stop at first successful load
            imgsel_str[imgsel_pos++] = ']';
            imgsel_str[imgsel_pos] = '\0';
            const char* p = imgsel_str;
            char c;
            do { c = *p++; enqueue(LOGID_EP_IMGSEL_TYPE_CS, c); } while (c != '\0');
            enqueue(LOGID_EP_LOADED_SLOT_TYPE_U8, codeSlot->index);
            printf("%s: %s\n", __FUNCTION__, imgsel_str);
            return;
        }
        // continue flag: keep iterating through remaining selector entries
        printf("%s: Continue flag present, trying next...\n", __FUNCTION__);
    }
    // All selector entries exhausted; fall through to limp_mode.

limp_mode:
    // We were unsuccessful in loading any image from the image_store.
    printf("%s: No loadable images found in image_selector!\n", __FUNCTION__);
    enqueue(LOGID_EP_LOAD_ERR_TYPE_U8, LOGID_EP_LOAD_ERR_VAL_NOIMAGES);

    printf("%s: Loading limp mode image built into EP firmware\n", __FUNCTION__);
    memcpy((uint8_t*)EPROM_IMAGE_BASE, limp_mode_image, EPROM_IMAGE_SIZE_BYTES);
    enqueue(LOGID_EP_LOAD_ERR_TYPE_U8, LOGID_EP_LOAD_ERR_VAL_LIMP_MODE);

    // Append limp-mode entry to the result array and send it to WP.
    int w = snprintf(imgsel_str + imgsel_pos, sizeof(imgsel_str) - imgsel_pos - 2,
                     "%s{\"code\":\"limp-mode\",\"load\":\"ok\"}", imgsel_first ? "" : ",");
    if (w > 0) imgsel_pos += w;
    imgsel_str[imgsel_pos++] = ']';
    imgsel_str[imgsel_pos] = '\0';
    printf("%s: %s\n", __FUNCTION__, imgsel_str);

    const char* p = imgsel_str;
    char c;
    do { c = *p++; enqueue(LOGID_EP_IMGSEL_TYPE_CS, c); } while (c != '\0');
    enqueue(LOGID_EP_LOADED_SLOT_TYPE_U8, 0);
}


#if 0
const char* s = "The quick brown fox jumps over the lazy dog";
uint32_t hash = murmur3_32((uint8_t*)s, strlen(s), 0x9747b28c);
if (hash != 0x2FA826CD) {
    panic("Hash fail");
}
#endif

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
uint8_t EpromLoader::loadImage(bsonDoc_t epromDoc, const char* epromName)
{
    return loadRange(epromDoc, 0, EPROM_IMAGE_SIZE_BYTES, epromName);
}

// --------------------------------------------------------------------------------------------
uint8_t EpromLoader::loadMapblob(bsonDoc_t epromDoc, const char* epromName)
{
    printf("%s: Loading Mapblob from epromDoc\n", __FUNCTION__);
    uint8_t err;
    err = loadRange(epromDoc, RP58_MAPBLOB_OFFSET, RP58_MAPBLOB_LENGTH, epromName);
    return err;
}

// --------------------------------------------------------------------------------------------
uint8_t EpromLoader::loadRange(bsonDoc_t epromDoc, uint32_t startOffset, uint32_t length, const char* epromName)
{
    bool found;

    // The eprom name is passed in by the caller (it's the key name in the combined "eproms" doc,
    // not stored inside the eprom subdoc itself)
    logEpromName_load(epromName);

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

    if ((startOffset+length)>EPROM_IMAGE_SIZE_BYTES) {
        printf("%s: ERR: requested startOffset+length [%u] goes past end EPROM [%d]\n", __FUNCTION__, startOffset+length, EPROM_IMAGE_SIZE_BYTES);
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
    const uint8_t* imageMemDoc = mem_element.data;
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
        uint8_t* buffer = (uint8_t*)EPROM_IMAGE_BASE;
        for (uint32_t offset=startOffset; offset<startOffset+length; offset++) {
            uint8_t byte = readEpromViaDaughterboard(offset, imageMemInfo.binData);
            *buffer++ = byte;
        }
    }
    else {
        // Unscrambled images can simply be copied
        printf("%s: Loading data from unprotected image [0x%04X..0x%04X]\n", __FUNCTION__, startOffset , (startOffset + length)-1);
        memcpy((uint8_t*)EPROM_IMAGE_BASE + startOffset, imageMemInfo.binData + startOffset , length);
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
    if (length != EPROM_IMAGE_SIZE_BYTES) {
        printf("%s: ERR: bad length field: expected %d, saw %u\"\n", __FUNCTION__, EPROM_IMAGE_SIZE_BYTES, length);
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

