#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void szy_sha256(const uint8_t* data, size_t len, uint8_t out32[32]);

#ifdef __cplusplus
}
#endif