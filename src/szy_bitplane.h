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