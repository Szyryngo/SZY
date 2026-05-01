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
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline void szy_store_u16le(uint8_t out[2], uint16_t v) {
  out[0] = (uint8_t)(v & 0xFFu);
  out[1] = (uint8_t)((v >> 8) & 0xFFu);
}
static inline void szy_store_u32le(uint8_t out[4], uint32_t v) {
  out[0] = (uint8_t)(v & 0xFFu);
  out[1] = (uint8_t)((v >> 8) & 0xFFu);
  out[2] = (uint8_t)((v >> 16) & 0xFFu);
  out[3] = (uint8_t)((v >> 24) & 0xFFu);
}
static inline void szy_store_u64le(uint8_t out[8], uint64_t v) {
  for (int i = 0; i < 8; i++) out[i] = (uint8_t)((v >> (8 * i)) & 0xFFu);
}

static inline uint16_t szy_load_u16le(const uint8_t in[2]) {
  return (uint16_t)(in[0] | ((uint16_t)in[1] << 8));
}
static inline uint32_t szy_load_u32le(const uint8_t in[4]) {
  return (uint32_t)in[0]
       | ((uint32_t)in[1] << 8)
       | ((uint32_t)in[2] << 16)
       | ((uint32_t)in[3] << 24);
}
static inline uint64_t szy_load_u64le(const uint8_t in[8]) {
  uint64_t v = 0;
  for (int i = 0; i < 8; i++) v |= ((uint64_t)in[i] << (8 * i));
  return v;
}

static inline void szy_store_f32le(uint8_t out[4], float v) {
  uint32_t u;
  memcpy(&u, &v, 4);
  szy_store_u32le(out, u);
}
static inline void szy_store_f64le(uint8_t out[8], double v) {
  uint64_t u;
  memcpy(&u, &v, 8);
  szy_store_u64le(out, u);
}
static inline float szy_load_f32le(const uint8_t in[4]) {
  uint32_t u = szy_load_u32le(in);
  float v;
  memcpy(&v, &u, 4);
  return v;
}
static inline double szy_load_f64le(const uint8_t in[8]) {
  uint64_t u = szy_load_u64le(in);
  double v;
  memcpy(&v, &u, 8);
  return v;
}

#ifdef __cplusplus
}
#endif
