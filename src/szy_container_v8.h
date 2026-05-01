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
