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
