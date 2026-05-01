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
#include "szy_varuint.h"

int szy_varuint_decode(const uint8_t* p, size_t n, size_t* io_pos, uint64_t* out) {
  if (!p || !io_pos || !out) return -1;
  size_t pos = *io_pos;
  uint64_t val = 0;
  int shift = 0;

  while (1) {
    if (pos >= n) return -1;
    
    /* POPRAWKA #3: Sprawdź shift PRZED użyciem (max 9 bajtów = 63 bity) */
    if (shift > 56) return -1;
    
    uint8_t b = p[pos++];
    val |= (uint64_t)(b & 0x7Fu) << shift;
    if ((b & 0x80u) == 0) {
      *io_pos = pos;
      *out = val;
      return 0;
    }
    shift += 7;
  }
}

size_t szy_varuint_encode(uint64_t v, uint8_t dst10[10]) {
  size_t n = 0;
  while (1) {
    uint8_t b = (uint8_t)(v & 0x7Fu);
    v >>= 7;
    if (v) dst10[n++] = (uint8_t)(0x80u | b);
    else   { dst10[n++] = b; break; }
  }
  return n;
}
