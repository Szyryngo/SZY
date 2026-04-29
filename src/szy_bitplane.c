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