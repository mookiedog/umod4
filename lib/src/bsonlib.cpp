#include "bsonlib.h"
#include <string.h>
#include <stdio.h>

// --------------------------------------------------------------------------------
// Standard unaligned read helper
uint32_t Bson::read_unaligned_uint32(const uint8_t* p) {
    uint32_t result = 0;
    for (int i = 0; i < 4; i++) {
        result |= static_cast<uint32_t>(p[i]) << (8 * i);
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
    e.name = reinterpret_cast<const char*>(p + 1);

    // 1. Generate a pointer to the first byte of the value field
    // When you go to use this pointer, remember: it is NOT necessarily aligned to a word boundary!
    // That's what read_unaligned_uint32() is for!
    e.data = p + 1 + strlen(e.name) + 1;

    // 2. Determine the size of the value field based on type
    int32_t valueSize = 0;
    switch (e.elementType) {
        case TYPE_DOUBLE:         valueSize = SIZEOF_DOUBLE; break;
        case TYPE_BOOLEAN:        valueSize = SIZEOF_BOOLEAN; break;
        case TYPE_INT32:          valueSize = SIZEOF_INT32; break;
        case TYPE_INT64:
        case TYPE_UTC_DATETIME:
        case TYPE_TIMESTAMP:      valueSize = SIZEOF_INT64; break;
        case TYPE_FLOAT128:       valueSize = SIZEOF_FLOAT128; break;
        case TYPE_OBJECT_ID:      valueSize = SIZEOF_OBJECT_ID; break;

        case TYPE_UTF8:
        case TYPE_JS_CODE:
        case TYPE_SYMBOL:
            valueSize = SIZEOF_STRING_LENGTH + read_unaligned_uint32(e.data);
            break;

        case TYPE_EMBEDDED_DOC:
        case TYPE_ARRAY:
            valueSize = read_unaligned_uint32(e.data);
            break;

        case TYPE_BINARY_DATA:
            valueSize = SIZEOF_STRING_LENGTH + SIZEOF_BINARY_SUBTYPE + static_cast<int32_t>(read_unaligned_uint32(e.data));
            break;

        case TYPE_REGEXP: {
            const char* s1 = reinterpret_cast<const char*>(e.data);
            const char* s2 = s1 + strlen(s1) + 1;
            valueSize = static_cast<int32_t>((s2 + strlen(s2) + 1) - s1);
            break;
        }

        case TYPE_DBPOINTER:
            valueSize = SIZEOF_STRING_LENGTH + read_unaligned_uint32(e.data) + SIZEOF_OBJECT_ID;
            break;

        default: // MINKEY, MAXKEY, NULL_VALUE, UNDEFINED_VALUE
            valueSize = 0;
            break;
    }

    // 3. Final total length calculation
    e.elementLength = static_cast<int32_t>(e.data - e.elementP) + valueSize;
}

// --------------------------------------------------------------------------------
// Main Search: Now the only raw uint8_t* pointer is 'cursor', which
// is used to walk through the document examining the elements as we encounter them.
bool Bson::findElement(const uint8_t* docP, const char* elementName, element_t &e)
{
    uint32_t docLen = read_unaligned_uint32(docP);
    const uint8_t* cursor = docP + SIZEOF_DOC_LENGTH;
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