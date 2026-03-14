#ifndef MURMUR3_H
#define MURMUR3_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

extern uint32_t murmur3_32(const uint8_t* key, size_t len, uint32_t seed);

/* Streaming murmur3 API — processes data in multiple calls.
 * Constraint: each call to murmur3_update() must pass a length that is a
 * multiple of 4.  The final total length must also be a multiple of 4.
 * (This covers the 32KB image-store use case exactly.) */
typedef struct {
    uint32_t h1;
    uint32_t total;
} murmur3_state_t;

void     murmur3_begin(murmur3_state_t* s, uint32_t seed);
void     murmur3_update(murmur3_state_t* s, const uint8_t* data, uint32_t len);
uint32_t murmur3_finish(murmur3_state_t* s);

#ifdef __cplusplus
}
#endif

#endif
