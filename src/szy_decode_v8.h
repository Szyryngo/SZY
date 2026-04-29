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