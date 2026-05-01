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

int szy_decode_2d_f64_v8(
    const uint8_t* compressed_data,
    size_t compressed_size,
    double** out_data,
    int* H,
    int* W
);

/* NEW: configurable worker count and parallel threshold.
   num_workers: 0=auto (relaxed clamp), >=1 fixed (relaxed clamp in core)
   parallel_min_tiles: 0 -> default (64)
*/
int szy_decode_2d_f64_v8_ex(
    const uint8_t* compressed_data,
    size_t compressed_size,
    double** out_data,
    int* H,
    int* W,
    int num_workers,
    int parallel_min_tiles
);

#ifdef __cplusplus
}
#endif
