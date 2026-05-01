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

/**
 * Bitplane shuffle dla u16 residuals.
 * Transformuje array 16-bit values do 16 bitplanes (LSB do MSB).
 * 
 * Input format:  [v0, v1, v2, ...] gdzie każdy v = b15|b14|...|b1|b0
 * Output format: [plane0_bytes][plane1_bytes]...[plane15_bytes]
 *                gdzie plane_i = wszystkie bity 'i' z wszystkich values
 * 
 * Każdy plane zajmuje ceil(npix/8) bytes (packed bits).
 * Total output size: npix * 2 bytes (16 planes × ceil(npix/8))
 * 
 * @param in      Input array (npix × uint16_t)
 * @param out     Output buffer (musi mieć co najmniej npix*2 bytes)
 * @param npix    Liczba pikseli
 * @return        0 on success, -1 on error
 */
int szy_bitplane_shuffle_u16(
    const uint16_t* in,
    uint8_t* out,
    size_t npix
);

/**
 * Inverse bitplane shuffle: 16 bitplanes → u16 array.
 * 
 * @param in      Input buffer (bitplane format, npix*2 bytes)
 * @param out     Output array (npix × uint16_t)
 * @param npix    Liczba pikseli
 * @return        0 on success, -1 on error
 */
int szy_bitplane_unshuffle_u16(
    const uint8_t* in,
    uint16_t* out,
    size_t npix
);

#ifdef __cplusplus
}
#endif
