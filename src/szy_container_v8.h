#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint16_t H, W, block;
  uint8_t version;
  uint8_t flags;

  int index_present;

  uint32_t ntiles;
  uint32_t* tile_lengths; /* optional, malloc'd; can be NULL */

  size_t data_start;   /* right after data_sha */
  size_t tiles_start;  /* after index if present */
  size_t data_end;     /* end of data section (before final container sha) */
} szy_container_hdr_t;

int  szy_container_unpack_v8(const uint8_t* buf, size_t n, szy_container_hdr_t* out_hdr);
void szy_container_free(szy_container_hdr_t* hdr);

#ifdef __cplusplus
}
#endif