#if !defined BSONLIB_H
#define BSONLIB_H

#include <stdint.h>

// In the BSON spec, a document holds a list of zero or more elements.
// Each element is a represented by a key-value pair:
//  - the data type definition of the value comes first, contained in a single signed byte
//  - the key is always a C-string, starting the byte after the value type byte
//  - the value can be many different data types, all defined in the BSON spec
//
// To traverse a BSON document, one needs to know the length of each element in the document
// list in order to know when one element ends and the the next element begins.
//
// A BSON object is a sequence of bytes with no inherent alignment of the underlying
// data values. When reading element values, this needs to be taken into account.

// An 'element' represents a single key-value pair within a BSON document
typedef struct {
    const uint8_t* elementP;    // points to the first byte of the element, mainly used when skipping over the element
    int32_t elementLength;      // the length of the entire element in bytes. Add to elementP to skip to the next element
    const char* name;           // the key name of the element, as a C-string
    int8_t elementType;         // the BSON type of the element value, as defined by the BSON spec
    const uint8_t* data;        // points to the first byte of the element value.
} element_t;


class Bson {
    public:
        // BSON element type codes (per BSON spec)
        static constexpr int8_t TYPE_DOUBLE          =  1;
        static constexpr int8_t TYPE_UTF8            =  2;
        static constexpr int8_t TYPE_EMBEDDED_DOC    =  3;
        static constexpr int8_t TYPE_ARRAY           =  4;
        static constexpr int8_t TYPE_BINARY_DATA     =  5;
        static constexpr int8_t TYPE_UNDEFINED_VALUE =  6;  // Deprecated
        static constexpr int8_t TYPE_OBJECT_ID       =  7;
        static constexpr int8_t TYPE_BOOLEAN         =  8;
        static constexpr int8_t TYPE_UTC_DATETIME    =  9;
        static constexpr int8_t TYPE_NULL_VALUE      = 10;
        static constexpr int8_t TYPE_REGEXP          = 11;
        static constexpr int8_t TYPE_DBPOINTER       = 12;  // Deprecated
        static constexpr int8_t TYPE_JS_CODE         = 13;
        static constexpr int8_t TYPE_SYMBOL          = 14;  // Deprecated
        static constexpr int8_t TYPE_JS_CODE_W_S     = 15;  // Deprecated
        static constexpr int8_t TYPE_INT32           = 16;
        static constexpr int8_t TYPE_TIMESTAMP       = 17;  // uint64
        static constexpr int8_t TYPE_INT64           = 18;
        static constexpr int8_t TYPE_FLOAT128        = 19;
        static constexpr int8_t TYPE_MINKEY          = -1;
        static constexpr int8_t TYPE_MAXKEY          = 127;

        // Fixed value sizes per BSON spec (bytes)
        static constexpr int32_t SIZEOF_DOUBLE    =  8;
        static constexpr int32_t SIZEOF_BOOLEAN   =  1;
        static constexpr int32_t SIZEOF_INT32     =  4;
        static constexpr int32_t SIZEOF_INT64     =  8;
        static constexpr int32_t SIZEOF_FLOAT128  = 16;
        static constexpr int32_t SIZEOF_OBJECT_ID = 12;

        // Structural framing sizes per BSON spec (bytes)
        static constexpr int32_t SIZEOF_DOC_LENGTH     = 4;  // int32 at start of every document
        static constexpr int32_t SIZEOF_STRING_LENGTH  = 4;  // int32 length prefix on strings
        static constexpr int32_t SIZEOF_BINARY_SUBTYPE = 1;  // subtype byte in binary values

        // Search a sequence of BSON documents for a specific top-level key.
        // Documents must be stored contiguously in memory with no padding between them.
        // Document must be well-formed, starting with a 4-byte length field and ending with a 0x00 byte.
        //
        // Returns a pointer to the start of the document if found, else nullptr.
        static const uint8_t* findDoc(const char* key, const uint8_t* startAddr, const uint8_t* endAddr);

        // Search the specified doc for an element with the specified key.
        // The search does not descend into sub-documents.
        // The docP needs to point at the first byte of the document, which is
        // the int32 length of the entire document, including the length field itself.
        static bool findElement(const uint8_t* docP, const char* elementName, element_t &e);

        // There is no guarantee that the little-endian BSON integers are aligned to word addresses.
        // This routine reads the uint32 properly regardless of alignment.
        static uint32_t read_unaligned_uint32(const uint8_t* ptr);

    private:
        // Helper to fill an element_t struct from a raw pointer into the BSON document
        // The standard C trick of overlaying a struct on top of a pointer into the BSON document
        // will not work because of alignment issues.
        static void populate_element(element_t &e, const uint8_t* p);
};

#endif
