#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int szy_tile_decode_into_v8(
  const uint8_t* buf, size_t n, size_t* io_pos,
  int B,
  double* dst, int dst_stride
);

#ifdef __cplusplus
}
#endif