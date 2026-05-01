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
#include "szy_bitplane.h"
#include <string.h>
#include <stdint.h>

int szy_bitplane_shuffle_u16(
    const uint16_t* in,
    uint8_t* out,
    size_t npix
) {
    if (!in || !out || npix == 0) return -1;
    
    const size_t bytes_per_plane = (npix + 7) / 8;
    
    /* Clear all output planes */
    memset(out, 0, bytes_per_plane * 16);
    
    /* Extract each bit into its corresponding plane */
    for (size_t i = 0; i < npix; i++) {
        uint16_t val = in[i];
        const size_t byte_idx = i / 8;
        const uint8_t bit_pos = (uint8_t)(i % 8);
        
        /* Process all 16 bits */
        for (int plane = 0; plane < 16; plane++) {
            if (val & (1u << plane)) {
                out[plane * bytes_per_plane + byte_idx] |= (1u << bit_pos);
            }
        }
    }
    
    return 0;
}

int szy_bitplane_unshuffle_u16(
    const uint8_t* in,
    uint16_t* out,
    size_t npix
) {
    if (!in || !out || npix == 0) return -1;
    
    const size_t bytes_per_plane = (npix + 7) / 8;
    
    /* Clear output */
    memset(out, 0, npix * sizeof(uint16_t));
    
    /* Reconstruct each pixel from bitplanes */
    for (size_t i = 0; i < npix; i++) {
        const size_t byte_idx = i / 8;
        const uint8_t bit_pos = (uint8_t)(i % 8);
        
        uint16_t val = 0;
        for (int plane = 0; plane < 16; plane++) {
            if (in[plane * bytes_per_plane + byte_idx] & (1u << bit_pos)) {
                val |= (1u << plane);
            }
        }
        out[i] = val;
    }
    
    return 0;
}
