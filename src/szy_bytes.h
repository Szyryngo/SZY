#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  const uint8_t* p;
  size_t n;
  size_t pos;
} szy_rd_t;

int  szy_rd_init(szy_rd_t* r, const uint8_t* p, size_t n, size_t pos0);
int  szy_rd_u8(szy_rd_t* r, uint8_t* out);
int  szy_rd_u16le(szy_rd_t* r, uint16_t* out);
int  szy_rd_i16le(szy_rd_t* r, int16_t* out);
int  szy_rd_u32le(szy_rd_t* r, uint32_t* out);
int  szy_rd_f32le(szy_rd_t* r, float* out);
int  szy_rd_f64le(szy_rd_t* r, double* out);
int  szy_rd_bytes(szy_rd_t* r, size_t k, const uint8_t** out); /* zero-copy view */
int  szy_rd_skip(szy_rd_t* r, size_t k);

#ifdef __cplusplus
}
#endif