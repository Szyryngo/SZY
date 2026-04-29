#include "szy_decode_v8.h"
#include "szy_container_v8.h"
#include "szy_tile_v8.h"
#include "szy_parallel.h"
#include "szy_hw.h"
#include "szy_rc.h"

#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define PARALLEL_DECODE_MIN_TILES_DEFAULT 64

int szy_decode_2d_f64_v8_ex(
    const uint8_t* compressed_data, size_t compressed_size,
    double** out_data, int* H, int* W,
    int num_workers,
    int parallel_min_tiles
) {
  if (!compressed_data || !out_data || !H || !W) return SZY_EINVAL;

  *out_data = NULL;
  *H = 0;
  *W = 0;

  if (parallel_min_tiles < 0) return SZY_EINVAL;
  if (parallel_min_tiles == 0) parallel_min_tiles = PARALLEL_DECODE_MIN_TILES_DEFAULT;

  /* relaxed clamp so old behavior doesn't fail on low-core CPUs */
  int workers = 1;
  int nrc = szy_normalize_num_workers_relaxed(num_workers, &workers);
  if (nrc != SZY_OK) return nrc;

  szy_container_hdr_t hdr;
  int urc = szy_container_unpack_v8(compressed_data, compressed_size, &hdr);
  if (urc != SZY_OK) {
    return urc;
  }

  int total_h = (int)hdr.H;
  int total_w = (int)hdr.W;
  int B = (int)hdr.block;

  if (total_h <= 0 || total_w <= 0 || B <= 0) {
    szy_container_free(&hdr);
    return SZY_E_DIMS;
  }
  if ((total_h % B) != 0 || (total_w % B) != 0) {
    szy_container_free(&hdr);
    return SZY_E_BLOCK;
  }

  if (hdr.index_present) {
    size_t nty = (size_t)total_h / (size_t)B;
    size_t ntx = (size_t)total_w / (size_t)B;
    size_t expected = nty * ntx;
    if ((size_t)hdr.ntiles != expected) {
      szy_container_free(&hdr);
      return SZY_E_TILECOUNT;
    }
  }

  if ((size_t)total_h > (SIZE_MAX / (size_t)total_w)) {
    szy_container_free(&hdr);
    return SZY_E_OVERFLOW;
  }
  size_t npix = (size_t)total_h * (size_t)total_w;
  if (npix > (SIZE_MAX / sizeof(double))) {
    szy_container_free(&hdr);
    return SZY_E_OVERFLOW;
  }

  double* data = (double*)malloc(npix * sizeof(double));
  if (!data) {
    szy_container_free(&hdr);
    return SZY_E_OOM;
  }
  memset(data, 0, npix * sizeof(double));

  int use_parallel = (hdr.index_present && (int)hdr.ntiles >= parallel_min_tiles && workers > 1);

  if (use_parallel) {
    int prc = szy_decode_parallel_tiles(compressed_data, &hdr, data, workers);
    if (prc != SZY_OK) {
      free(data);
      szy_container_free(&hdr);
      return (prc < 0) ? prc : SZY_E_TILE;
    }
  } else {
    size_t io_pos = hdr.tiles_start;
    for (int r = 0; r < total_h; r += B) {
      for (int c = 0; c < total_w; c += B) {
        double* dst_ptr = data + ((size_t)r * (size_t)total_w + (size_t)c);
        int trc = szy_tile_decode_into_v8(compressed_data, hdr.data_end, &io_pos, B, dst_ptr, total_w);
        if (trc != SZY_OK) {
          free(data);
          szy_container_free(&hdr);
          return SZY_E_TILE;
        }
      }
    }
  }

  *out_data = data;
  *H = total_h;
  *W = total_w;

  szy_container_free(&hdr);
  return SZY_OK;
}

int szy_decode_2d_f64_v8(const uint8_t* compressed_data, size_t compressed_size, double** out_data, int* H, int* W) {
  /* preserve old defaults: 4 workers, min tiles 64; relaxed clamp */
  return szy_decode_2d_f64_v8_ex(compressed_data, compressed_size, out_data, H, W, 4, PARALLEL_DECODE_MIN_TILES_DEFAULT);
}