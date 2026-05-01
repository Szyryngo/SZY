/*
 * SZY_v8 Compressor
 * Copyright (C) 2026 Robert Szyryngo
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * ---
 * COMMERCIAL LICENSE
 * If you want to use this software in proprietary, closed-source projects
 * or in any other way that does not comply with the AGPLv3 license,
 * a commercial license is required. Contact robert.szyryngo@gmail.com for pricing.
 */
#include "szy_api.h"
#include "szy_encode_v8.h"
#include "szy_decode_v8.h"
#include "szy_container_v8.h"
#include "szy_tile_v8.h"
#include "szy_parallel.h"
#include "szy_hw.h"
#include "szy_rc.h"

#include <stdlib.h>
#include <string.h>
#include <limits.h>

/* Internal default configuration used by DLL API */
static void fill_cfg(szy_enc_cfg_t* cfg, int block, double eps) {
  memset(cfg, 0, sizeof(*cfg));
  cfg->block = block;

  cfg->qbits = 16;
  cfg->qmode = SZY_QMODE_STRICT;
  cfg->qpercentile = 99.5;

  cfg->target_abs_err = eps;
  cfg->bounded_safety = 0.98;
  cfg->quant_err_factor = 2.0;

  cfg->events_on = 1;
  cfg->events_max_per_tile = 2048;
  cfg->events_max_frac = 0.01;

  cfg->ent = SZY_ENT_ZSTD;
  cfg->zstd_level = 3;

  cfg->use_zigzag = 1;
  cfg->byte_shuffle_16 = 1;
  cfg->use_bitplane_shuffle = 0;
}

SZY_API void szy_free_buffer(void* ptr) {
  free(ptr);
}

/* -------------------- float64 -------------------- */

SZY_API int szy_compress_buffer(
    double* input_data,
    int H, int W,
    double eps,
    int block_size,
    uint8_t** out_buf,
    size_t* out_size
) {
  if (!input_data || !out_buf || !out_size) return SZY_EINVAL;
  szy_enc_cfg_t cfg;
  fill_cfg(&cfg, block_size, eps);

  /* old API: single-thread encode */
  return szy_encode_2d_f64_v8_ex((const double*)input_data, H, W, &cfg, 1, out_buf, out_size);
}

SZY_API int szy_compress_buffer_ex(
    double* input_data,
    int H, int W,
    double eps,
    int block_size,
    int num_workers,
    uint8_t** out_buf,
    size_t* out_size
) {
  if (!input_data || !out_buf || !out_size) return SZY_EINVAL;

  int workers = 1;
  int nrc = szy_normalize_num_workers_strict(num_workers, &workers);
  if (nrc != SZY_OK) return nrc;

  szy_enc_cfg_t cfg;
  fill_cfg(&cfg, block_size, eps);
  return szy_encode_2d_f64_v8_ex((const double*)input_data, H, W, &cfg, workers, out_buf, out_size);
}

SZY_API int szy_decompress_buffer(
    uint8_t* compressed_data,
    size_t compressed_size,
    double** out_data,
    int* H,
    int* W
) {
  /* old: decode uses defaults (4 workers, min tiles 64; relaxed clamp) */
  return szy_decode_2d_f64_v8((const uint8_t*)compressed_data, compressed_size, out_data, H, W);
}

/* -------------------- uint16 -------------------- */

SZY_API int szy_compress_u16(
    uint16_t* input_data,
    int H, int W,
    double eps,
    int block_size,
    uint8_t** out_buf,
    size_t* out_size
) {
  if (!input_data || !out_buf || !out_size) return SZY_EINVAL;
  szy_enc_cfg_t cfg;
  fill_cfg(&cfg, block_size, eps);

  /* old API: single-thread encode */
  return szy_encode_2d_u16_v8_ex((const uint16_t*)input_data, H, W, &cfg, 1, out_buf, out_size);
}

SZY_API int szy_compress_u16_ex(
    uint16_t* input_data,
    int H, int W,
    double eps,
    int block_size,
    int num_workers,
    uint8_t** out_buf,
    size_t* out_size
) {
  if (!input_data || !out_buf || !out_size) return SZY_EINVAL;

  int workers = 1;
  int nrc = szy_normalize_num_workers_strict(num_workers, &workers);
  if (nrc != SZY_OK) return nrc;

  szy_enc_cfg_t cfg;
  fill_cfg(&cfg, block_size, eps);
  return szy_encode_2d_u16_v8_ex((const uint16_t*)input_data, H, W, &cfg, workers, out_buf, out_size);
}

SZY_API int szy_decompress_u16(
    uint8_t* compressed_data,
    size_t compressed_size,
    uint16_t** out_data,
    int* H,
    int* W
) {
  return szy_decode_2d_u16_v8((const uint8_t*)compressed_data, compressed_size, out_data, H, W);
}

/* -------------------- float32 -------------------- */

SZY_API int szy_compress_f32(
    float* input_data,
    int H, int W,
    float eps,
    int block_size,
    uint8_t** out_buf,
    size_t* out_size
) {
  if (!input_data || !out_buf || !out_size) return SZY_EINVAL;
  szy_enc_cfg_t cfg;
  fill_cfg(&cfg, block_size, (double)eps);

  /* old API: single-thread encode */
  return szy_encode_2d_f32_v8_ex((const float*)input_data, H, W, &cfg, 1, out_buf, out_size);
}

SZY_API int szy_compress_f32_ex(
    float* input_data,
    int H, int W,
    float eps,
    int block_size,
    int num_workers,
    uint8_t** out_buf,
    size_t* out_size
) {
  if (!input_data || !out_buf || !out_size) return SZY_EINVAL;

  int workers = 1;
  int nrc = szy_normalize_num_workers_strict(num_workers, &workers);
  if (nrc != SZY_OK) return nrc;

  szy_enc_cfg_t cfg;
  fill_cfg(&cfg, block_size, (double)eps);
  return szy_encode_2d_f32_v8_ex((const float*)input_data, H, W, &cfg, workers, out_buf, out_size);
}

SZY_API int szy_decompress_f32(
    uint8_t* compressed_data,
    size_t compressed_size,
    float** out_data,
    int* H,
    int* W
) {
  return szy_decode_2d_f32_v8((const uint8_t*)compressed_data, compressed_size, out_data, H, W);
}

/* -------------------- peek shape -------------------- */

SZY_API int szy_peek_shape_v8(
    const uint8_t* compressed_data,
    size_t compressed_size,
    int* H,
    int* W,
    int* B
) {
  if (!compressed_data || !H || !W || !B) return SZY_EINVAL;

  szy_container_hdr_t hdr;
  int rc = szy_container_unpack_v8(compressed_data, compressed_size, &hdr);
  if (rc != SZY_OK) return rc;

  *H = (int)hdr.H;
  *W = (int)hdr.W;
  *B = (int)hdr.block;

  szy_container_free(&hdr);
  return SZY_OK;
}

/* -------------------- decode into user buffer (float64) -------------------- */

static int decompress_into_impl(
    const uint8_t* compressed_data,
    size_t compressed_size,
    double* out_data,
    size_t out_elems,
    int workers_relaxed,         /* already normalized */
    int parallel_min_tiles,      /* already normalized */
    int* H,
    int* W
) {
  if (!compressed_data || !out_data || !H || !W) return SZY_EINVAL;

  szy_container_hdr_t hdr;
  int rc = szy_container_unpack_v8(compressed_data, compressed_size, &hdr);
  if (rc != SZY_OK) return rc;

  int h = (int)hdr.H;
  int w = (int)hdr.W;
  int B = (int)hdr.block;

  if (h <= 0 || w <= 0 || B <= 0) { szy_container_free(&hdr); return SZY_E_DIMS; }
  if ((h % B) || (w % B))         { szy_container_free(&hdr); return SZY_E_BLOCK; }

  if (w != 0 && (size_t)h > (SIZE_MAX / (size_t)w)) { szy_container_free(&hdr); return SZY_E_OVERFLOW; }
  size_t npix = (size_t)h * (size_t)w;

  if (out_elems < npix) { szy_container_free(&hdr); return SZY_E_OVERFLOW; }

  int use_parallel = (hdr.index_present && (int)hdr.ntiles >= parallel_min_tiles && workers_relaxed > 1);

  if (use_parallel) {
    rc = szy_decode_parallel_tiles(compressed_data, &hdr, out_data, workers_relaxed);
    if (rc != SZY_OK) { szy_container_free(&hdr); return (rc < 0) ? rc : SZY_E_TILE; }
  } else {
    size_t pos = hdr.tiles_start;
    for (int r0 = 0; r0 < h; r0 += B) {
      for (int c0 = 0; c0 < w; c0 += B) {
        double* dst = out_data + (size_t)r0 * (size_t)w + (size_t)c0;
        int trc = szy_tile_decode_into_v8(compressed_data, hdr.data_end, &pos, B, dst, w);
        if (trc != SZY_OK) { szy_container_free(&hdr); return SZY_E_TILE; }
      }
    }
  }

  *H = h;
  *W = w;
  szy_container_free(&hdr);
  return SZY_OK;
}

SZY_API int szy_decompress_into_buffer(
    const uint8_t* compressed_data,
    size_t compressed_size,
    double* out_data,
    size_t out_elems,
    int* H,
    int* W
) {
  /* old default: 4 workers + min tiles 64 (relaxed clamp) */
  int workers = 1;
  int nrc = szy_normalize_num_workers_relaxed(4, &workers);
  if (nrc != SZY_OK) return nrc;
  return decompress_into_impl(compressed_data, compressed_size, out_data, out_elems, workers, 64, H, W);
}

SZY_API int szy_decompress_into_buffer_ex(
    const uint8_t* compressed_data,
    size_t compressed_size,
    double* out_data,
    size_t out_elems,
    int num_workers,
    int* H,
    int* W
) {
  /* strict validation requested by user */
  int workers = 1;
  int nrc = szy_normalize_num_workers_strict(num_workers, &workers);
  if (nrc != SZY_OK) return nrc;
  return decompress_into_impl(compressed_data, compressed_size, out_data, out_elems, workers, 64, H, W);
}

SZY_API int szy_decompress_into_buffer_ex2(
    const uint8_t* compressed_data,
    size_t compressed_size,
    double* out_data,
    size_t out_elems,
    int num_workers,
    int parallel_min_tiles,
    int* H,
    int* W
) {
  if (parallel_min_tiles < 0) return SZY_EINVAL;
  if (parallel_min_tiles == 0) parallel_min_tiles = 64;

  int workers = 1;
  int nrc = szy_normalize_num_workers_strict(num_workers, &workers);
  if (nrc != SZY_OK) return nrc;

  return decompress_into_impl(compressed_data, compressed_size, out_data, out_elems, workers, parallel_min_tiles, H, W);
}
