#pragma once
#include "szy_container_v8.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Decode all tiles in parallel using index.
   Returns SZY_OK on success, negative SZY_E_* on error.
*/
int szy_decode_parallel_tiles(
    const uint8_t* buf,
    const szy_container_hdr_t* hdr,
    double* img,
    int num_workers
);

#ifdef __cplusplus
}
#endif