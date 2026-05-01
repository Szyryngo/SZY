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
#include "src/szy_encode_v8.h"
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

/* Deklaracje forward dla funkcji z szy_encode_u16.c i szy_encode_f32.c */
int szy_encode_2d_u16_v8(const uint16_t* x, int H, int W, const szy_enc_cfg_t* cfg, uint8_t** out_buf, size_t* out_n);
int szy_decode_2d_u16_v8(const uint8_t* data, size_t n, uint16_t** out, int* H, int* W);
int szy_encode_2d_f32_v8(const float* x, int H, int W, const szy_enc_cfg_t* cfg, uint8_t** out_buf, size_t* out_n);
int szy_decode_2d_f32_v8(const uint8_t* data, size_t n, float** out, int* H, int* W);

static void _fill_cfg(szy_enc_cfg_t* cfg, int block, double eps) {
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

// Zamiast: DLLEXPORT void szy_free_buffer(void* ptr) { ... }

void szy_free_buffer(void* ptr) {
    if (ptr) free(ptr);
}

/* float64 */
DLLEXPORT int szy_compress_buffer(
    double* input_data, int H, int W, double eps, int block_size,
    uint8_t** out_buf, size_t* out_size)
{
    szy_enc_cfg_t cfg;
    _fill_cfg(&cfg, block_size, eps);
    return szy_encode_2d_f64_v8(input_data, H, W, &cfg, out_buf, out_size);
}

DLLEXPORT int szy_decompress_buffer(
    uint8_t* compressed_data, size_t compressed_size,
    double** out_data, int* H, int* W)
{
    return szy_decode_2d_f64_v8(compressed_data, compressed_size, out_data, H, W);
}

/* uint16 */
DLLEXPORT int szy_compress_u16(
    uint16_t* input_data, int H, int W, double eps, int block_size,
    uint8_t** out_buf, size_t* out_size)
{
    szy_enc_cfg_t cfg;
    _fill_cfg(&cfg, block_size, eps);
    return szy_encode_2d_u16_v8(input_data, H, W, &cfg, out_buf, out_size);
}

DLLEXPORT int szy_decompress_u16(
    uint8_t* compressed_data, size_t compressed_size,
    uint16_t** out_data, int* H, int* W)
{
    return szy_decode_2d_u16_v8(compressed_data, compressed_size, out_data, H, W);
}

/* float32 */
DLLEXPORT int szy_compress_f32(
    float* input_data, int H, int W, float eps, int block_size,
    uint8_t** out_buf, size_t* out_size)
{
    szy_enc_cfg_t cfg;
    _fill_cfg(&cfg, block_size, (double)eps);
    return szy_encode_2d_f32_v8(input_data, H, W, &cfg, out_buf, out_size);
}

DLLEXPORT int szy_decompress_f32(
    uint8_t* compressed_data, size_t compressed_size,
    float** out_data, int* H, int* W)
{
    return szy_decode_2d_f32_v8(compressed_data, compressed_size, out_data, H, W);
}
