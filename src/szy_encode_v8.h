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
#pragma once
#include <stdint.h>
#include <stddef.h>

#include "szy_api.h"   /* defines SZY_API */

#ifdef __cplusplus
extern "C" {
#endif

/* Entropy IDs must match tile decoder IDs */
typedef enum {
  SZY_ENT_ZLIB = 0,
  SZY_ENT_LZMA = 1, /* not implemented yet in C */
  SZY_ENT_NONE = 2,
  SZY_ENT_ZSTD = 3,
  SZY_ENT_AUTO_RATIO = 4, /* not implemented yet in C */
  SZY_ENT_AUTO_FAST  = 5  /* not implemented yet in C */
} szy_entropy_t;

typedef enum {
  SZY_QMODE_STRICT = 0,
  SZY_QMODE_ROBUST = 1
} szy_quant_mode_t;

typedef struct {
  int block;                 /* e.g. 64/128 */
  int qbits;                 /* 8 or 16 */
  szy_quant_mode_t qmode;    /* strict/robust */
  double qpercentile;        /* e.g. 99.5 when robust */

  /* bounded error model */
  double target_abs_err;     /* eps; <=0 means disabled */
  double bounded_safety;     /* e.g. 0.98 */
  double quant_err_factor;   /* e.g. 2.0 */

  /* events */
  int events_on;             /* 0/1 */
  int events_max_per_tile;   /* default 2048 */
  double events_max_frac;    /* default 0.01 */

  /* entropy */
  szy_entropy_t ent;
  int zstd_level;            /* 1..22 */

  /* transforms */
  int use_zigzag;
  int byte_shuffle_16;
  int use_bitplane_shuffle;  /* not implemented yet in C encoder */
  
  /* NEW: Post-compression verification for compliance */
  int verify_after_encode;   /* 0=disabled, 1=verify max_err after compress */
} szy_enc_cfg_t;

/* Core encoder (single-thread default; implemented in szy_encode_v8.c) */
int szy_encode_2d_f64_v8(
  const double* x, int H, int W,
  const szy_enc_cfg_t* cfg,
  uint8_t** out_buf, size_t* out_n
);

/* NEW: Core encoder with configurable parallelism.
   num_workers: 0=auto (relaxed clamp), >=1 fixed (relaxed clamp in core)
*/
int szy_encode_2d_f64_v8_ex(
  const double* x, int H, int W,
  const szy_enc_cfg_t* cfg,
  int num_workers,
  uint8_t** out_buf, size_t* out_n
);

/* Convenience wrappers */
int szy_encode_2d_u16_v8(
  const uint16_t* x, int H, int W,
  const szy_enc_cfg_t* cfg,
  uint8_t** out_buf, size_t* out_n
);

int szy_encode_2d_u16_v8_ex(
  const uint16_t* x, int H, int W,
  const szy_enc_cfg_t* cfg,
  int num_workers,
  uint8_t** out_buf, size_t* out_n
);

int szy_decode_2d_u16_v8(
  const uint8_t* compressed_data, size_t compressed_size,
  uint16_t** out_data, int* H, int* W
);

int szy_encode_2d_f32_v8(
  const float* x, int H, int W,
  const szy_enc_cfg_t* cfg,
  uint8_t** out_buf, size_t* out_n
);

int szy_encode_2d_f32_v8_ex(
  const float* x, int H, int W,
  const szy_enc_cfg_t* cfg,
  int num_workers,
  uint8_t** out_buf, size_t* out_n
);

int szy_decode_2d_f32_v8(
  const uint8_t* compressed_data, size_t compressed_size,
  float** out_data, int* H, int* W
);

/* DLL helpers */
SZY_API void szy_free_buffer(void* ptr);
SZY_API const char* szy_strerror(int code);

#ifdef __cplusplus
}
#endif
