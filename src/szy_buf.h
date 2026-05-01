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
  uint8_t* p;
  size_t n;
  size_t cap;
} szy_bw_t;

void  szy_bw_init(szy_bw_t* w);
void  szy_bw_free(szy_bw_t* w);
int   szy_bw_reserve(szy_bw_t* w, size_t extra);
int   szy_bw_put(szy_bw_t* w, const void* data, size_t len);

int   szy_bw_u8(szy_bw_t* w, uint8_t v);
int   szy_bw_u16le(szy_bw_t* w, uint16_t v);
int   szy_bw_i16le(szy_bw_t* w, int16_t v);
int   szy_bw_f32le(szy_bw_t* w, float v);
int   szy_bw_f64le(szy_bw_t* w, double v);

uint8_t* szy_bw_steal(szy_bw_t* w, size_t* out_len); /* caller owns */

#ifdef __cplusplus
}
#endif
