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
#include "szy_container_v8.h"
#include "szy_bytes.h"
#include "szy_varuint.h"
#include "szy_sha256.h"
#include "szy_rc.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

static int mul_overflow_size(size_t a, size_t b, size_t* out) {
  if (!out) return -1;
  if (a == 0 || b == 0) { *out = 0; return 0; }
  if (a > (SIZE_MAX / b)) return -1;
  *out = a * b;
  return 0;
}

int szy_container_unpack_v8(const uint8_t* buf, size_t n, szy_container_hdr_t* out_hdr) {
  if (!buf || !out_hdr) return SZY_EINVAL;
  memset(out_hdr, 0, sizeof(*out_hdr));

  /* Minimum: magic(4) + ver(1) + flags(1) + H/W/B/meta_len(8) + data_sha(32) + cont_sha(32) */
  if (n < 4 + 1 + 1 + 2 + 2 + 2 + 2 + 32 + 32) return SZY_EINVAL;

  if (n < 32) return SZY_EINVAL;

  /* Verify container SHA (last 32 bytes): SHA256(buf[0:n-32]) == buf[n-32:n] */
  {
    uint8_t calc_cont[32];
    szy_sha256(buf, n - 32, calc_cont);
    if (memcmp(calc_cont, buf + (n - 32), 32) != 0) {
      return SZY_E_SHA_CONT;
    }
  }

  szy_rd_t r;
  if (szy_rd_init(&r, buf, n, 0)) return SZY_EINVAL;

  const uint8_t* magic = NULL;
  if (szy_rd_bytes(&r, 4, &magic)) return SZY_EINVAL;
  if (memcmp(magic, "SZ2D", 4) != 0) return SZY_EINVAL;

  uint8_t ver = 0, flags = 0;
  if (szy_rd_u8(&r, &ver)) return SZY_EINVAL;
  if (szy_rd_u8(&r, &flags)) return SZY_EINVAL;
  if (ver != 8) return SZY_EINVAL;

  uint16_t H = 0, W = 0, B = 0, meta_len = 0;
  if (szy_rd_u16le(&r, &H)) return SZY_EINVAL;
  if (szy_rd_u16le(&r, &W)) return SZY_EINVAL;
  if (szy_rd_u16le(&r, &B)) return SZY_EINVAL;
  if (szy_rd_u16le(&r, &meta_len)) return SZY_EINVAL;

  if (H == 0 || W == 0 || B == 0) return SZY_EINVAL;

  if (szy_rd_skip(&r, (size_t)meta_len)) return SZY_EINVAL;

  /* Read stored data SHA (32 bytes) */
  uint8_t stored_data_sha[32];
  {
    const uint8_t* psha = NULL;
    if (szy_rd_bytes(&r, 32, &psha)) return SZY_EINVAL;
    memcpy(stored_data_sha, psha, 32);
  }

  size_t data_start = r.pos;

  /* data section ends before final container sha */
  if (data_start > n - 32) return SZY_EINVAL;
  size_t data_end = n - 32;

  /* Verify data SHA */
  {
    uint8_t calc_data[32];
    szy_sha256(buf + data_start, data_end - data_start, calc_data);
    if (memcmp(calc_data, stored_data_sha, 32) != 0) {
      return SZY_E_SHA_DATA;
    }
  }

  int index_present = (flags & 1) ? 1 : 0;

  uint32_t ntiles = 0;
  uint32_t* lens = NULL;

  size_t tiles_start = data_start;

  if (index_present) {
    size_t pos = tiles_start;

    uint64_t nt64 = 0;
    if (szy_varuint_decode(buf, data_end, &pos, &nt64)) return SZY_EINVAL;
    if (nt64 > 100000000ULL) return SZY_EINVAL; /* sanity */
    ntiles = (uint32_t)nt64;

    size_t bytes_need = 0;
    if (mul_overflow_size((size_t)ntiles, sizeof(uint32_t), &bytes_need)) return SZY_E_OVERFLOW;
    lens = (uint32_t*)malloc(bytes_need);
    if (!lens) return SZY_E_OOM;

    uint64_t sum_lengths = 0;

    for (uint32_t i = 0; i < ntiles; i++) {
      uint64_t L = 0;
      if (szy_varuint_decode(buf, data_end, &pos, &L)) { free(lens); return SZY_EINVAL; }
      if (L == 0) { free(lens); return SZY_EINVAL; }
      if (L > 0x7fffffffULL) { free(lens); return SZY_EINVAL; }
      lens[i] = (uint32_t)L;

      if (sum_lengths > UINT64_MAX - L) { free(lens); return SZY_E_OVERFLOW; }
      sum_lengths += L;
    }

    tiles_start = pos;
    if (tiles_start > data_end) { free(lens); return SZY_EINVAL; }

    {
      uint64_t avail = (uint64_t)(data_end - tiles_start);
      if (sum_lengths > avail) {
        free(lens);
        return SZY_E_INDEX;
      }
    }
  }

  out_hdr->H = H;
  out_hdr->W = W;
  out_hdr->block = B;
  out_hdr->version = ver;
  out_hdr->flags = flags;
  out_hdr->index_present = index_present;
  out_hdr->ntiles = ntiles;
  out_hdr->tile_lengths = lens;
  out_hdr->data_start = data_start;
  out_hdr->tiles_start = tiles_start;
  out_hdr->data_end = data_end;
  return SZY_OK;
}

void szy_container_free(szy_container_hdr_t* hdr) {
  if (!hdr) return;
  free(hdr->tile_lengths);
  hdr->tile_lengths = NULL;
}
