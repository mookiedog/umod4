#include "bsonlib.h"
#include <string.h>
#include <stdio.h>

// --------------------------------------------------------------------------------
// Standard unaligned read helper
uint32_t Bson::read_unaligned_uint32(const uint8_t* p) {
    uint32_t result = 0;
    for (int i = 0; i < 4; i++) {
        result |= (uint32_t)(p[i]) << (8 * i);
    }
    return result;
}

// --------------------------------------------------------------------------------
// Populate an element_t struct from a raw pointer into the BSON document.
// This routine takes care of all alignment issues when populating the element_t struct fields.
// We always calculate the length of the entire element, in case the caller
// wants to skip over it without looking at the value. This is typically
// the case when seaching for a specific key in a document with many elements.
void Bson::populate_element(element_t &e, const uint8_t* p) {
    e.elementP = p;
    e.elementType = (int8_t)*p;
    e.name = (const char*)(p + 1);

    // 1. Generate a pointer to the first byte of the value field
    // When you go to use this pointer, remember: it is NOT necessarily aligned to a word boundary!
    // That's what read_unaligned_uint32() is for!
    e.data = p + 1 + strlen(e.name) + 1;

    // 2. Determine the size of the value field based on type
    int32_t valueSize = 0;
    switch (e.elementType) {
        case BSON_TYPE_DOUBLE:         valueSize = 8; break;
        case BSON_TYPE_BOOLEAN:        valueSize = 1; break;
        case BSON_TYPE_INT32:          valueSize = 4; break;
        case BSON_TYPE_INT64:
        case BSON_TYPE_UTC_DATETIME:
        case BSON_TYPE_TIMESTAMP:      valueSize = 8; break;
        case BSON_TYPE_FLOAT128:       valueSize = 16; break;
        case BSON_TYPE_OBJECT_ID:      valueSize = 12; break;

        case BSON_TYPE_UTF8:
        case BSON_TYPE_JS_CODE:
        case BSON_TYPE_SYMBOL:
            valueSize = 4 + read_unaligned_uint32(e.data);
            break;

        case BSON_TYPE_EMBEDDED_DOC:
        case BSON_TYPE_ARRAY:
            valueSize = read_unaligned_uint32(e.data);
            break;

        case BSON_TYPE_BINARY_DATA:
            valueSize = 5 + (int32_t)read_unaligned_uint32(e.data);
            break;

        case BSON_TYPE_REGEXP: {
            const char* s1 = (const char*)e.data;
            const char* s2 = s1 + strlen(s1) + 1;
            valueSize = (int32_t)((s2 + strlen(s2) + 1) - (const char*)e.data);
            break;
        }

        case BSON_TYPE_DBPOINTER:
            valueSize = 4 + read_unaligned_uint32(e.data) + 12;
            break;

        default: // MINKEY, MAXKEY, NULL_VALUE, UNDEFINED_VALUE
            valueSize = 0;
            break;
    }

    // 3. Final total length calculation
    e.elementLength = (int32_t)(e.data - e.elementP) + valueSize;
}

// --------------------------------------------------------------------------------
// Main Search: Now the only raw uint8_t* pointer is 'cursor', which
// is used to walk through the document examining the elements as we encounter them.
bool Bson::findElement(const uint8_t* docP, const char* elementName, element_t &e)
{
    int32_t docLen = (int32_t)read_unaligned_uint32(docP);
    const uint8_t* cursor = docP + 4;
    const uint8_t* docEndP = docP + docLen - 1;

    // Safety check for null-terminated document
    if (*docEndP != 0x00) return false;

    //printf("%s: Searching for element \"%s\"\n", __FUNCTION__, elementName);
    while ((cursor < docEndP) && (*cursor != 0x00)) {
        // get the element_t object at the current cursor position
        populate_element(e, cursor);

        // Is it the element name we are looking for?
        //printf("  checking \"%s\"\n", e.name);
        if (strcmp(elementName, e.name) == 0) {
            return true;
        }

        // Jump to the next element using the length we just calculated
        cursor += e.elementLength;
    }

    return false;
}

// --------------------------------------------------------------------------------
//  Find a document with the specified key in the top-level of a sequence of BSON documents.
const uint8_t* Bson::findDoc(const char* key, const uint8_t* startAddr, const uint8_t* endAddr)
{
    const uint8_t* p = startAddr;

    // Ensure we have at least 5 bytes to read (4 for length + 1 for terminator)
    while (p && (p < (endAddr - 4))) {
        uint32_t docLen = Bson::read_unaligned_uint32(p);

        // Safety/Flash Sentinels
        if (docLen == 0 || docLen == 0xFFFFFFFF) return nullptr;
        if (p + docLen > endAddr) return nullptr;

        element_t e;
        // Search ONLY the top-level of the current document
        if (Bson::findElement(p, key, e)) {
            return p;
        }

        p += docLen;
    }
    return nullptr;
}