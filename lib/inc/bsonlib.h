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

// Define the BSON data types
#define BSON_TYPE_DOUBLE            ((int8_t)1)
#define BSON_TYPE_UTF8              ((int8_t)2)
#define BSON_TYPE_EMBEDDED_DOC      ((int8_t)3)
#define BSON_TYPE_ARRAY             ((int8_t)4)
#define BSON_TYPE_BINARY_DATA       ((int8_t)5)
#define BSON_TYPE_UNDEFINED_VALUE   ((int8_t)6)     // Deprecated
#define BSON_TYPE_OBJECT_ID         ((int8_t)7)
#define BSON_TYPE_BOOLEAN           ((int8_t)8)
#define BSON_TYPE_UTC_DATETIME      ((int8_t)9)
#define BSON_TYPE_NULL_VALUE        ((int8_t)10)
#define BSON_TYPE_REGEXP            ((int8_t)11)
#define BSON_TYPE_DBPOINTER         ((int8_t)12)    // Deprecated
#define BSON_TYPE_JS_CODE           ((int8_t)13)
#define BSON_TYPE_SYMBOL            ((int8_t)14)    // Deprecated
#define BSON_TYPE_JS_CODE_W_S       ((int8_t)15)    // Deprecated
#define BSON_TYPE_INT32             ((int8_t)16)
#define BSON_TYPE_TIMESTAMP         ((int8_t)17)    // uint64
#define BSON_TYPE_INT64             ((int8_t)18)
#define BSON_TYPE_FLOAT128          ((int8_t)19)
#define BSON_TYPE_MINKEY            ((int8_t)-1)
#define BSON_TYPE_MAXKEY            ((int8_t)127)

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
