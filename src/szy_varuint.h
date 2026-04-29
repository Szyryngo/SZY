#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int szy_varuint_decode(const uint8_t* p, size_t n, size_t* io_pos, uint64_t* out);

/* encode to memory buffer (dst must have >=10 bytes), returns bytes written */
size_t szy_varuint_encode(uint64_t v, uint8_t dst10[10]);

#ifdef __cplusplus
}
#endif