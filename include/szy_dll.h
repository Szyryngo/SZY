#pragma once
#include <stddef.h>
#include <stdint.h>
#include "szy_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* float64 */
SZY_API int szy_compress_buffer(
    double* input_data,
    int H, int W,
    double eps,
    int block_size,
    uint8_t** out_buf,
    size_t* out_size
);

SZY_API int szy_decompress_buffer(
    uint8_t* compressed_data,
    size_t compressed_size,
    double** out_data,
    int* H,
    int* W
);

/* uint16 */
SZY_API int szy_compress_u16(
    uint16_t* input_data,
    int H, int W,
    double eps,
    int block_size,
    uint8_t** out_buf,
    size_t* out_size
);

SZY_API int szy_decompress_u16(
    uint8_t* compressed_data,
    size_t compressed_size,
    uint16_t** out_data,
    int* H,
    int* W
);

/* float32 */
SZY_API int szy_compress_f32(
    float* input_data,
    int H, int W,
    float eps,
    int block_size,
    uint8_t** out_buf,
    size_t* out_size
);

SZY_API int szy_decompress_f32(
    uint8_t* compressed_data,
    size_t compressed_size,
    float** out_data,
    int* H,
    int* W
);

/* helpers */
SZY_API void szy_free_buffer(void* ptr);
SZY_API const char* szy_strerror(int code);

/* NEW: query hardware threads (>=1) */
SZY_API int szy_hw_threads(void);

/* NEW: peek shape without decoding */
SZY_API int szy_peek_shape_v8(
    const uint8_t* compressed_data,
    size_t compressed_size,
    int* H,
    int* W,
    int* B
);

/* NEW: decode directly into user-provided buffer (float64) */
SZY_API int szy_decompress_into_buffer(
    const uint8_t* compressed_data,
    size_t compressed_size,
    double* out_data,
    size_t out_elems,
    int* H,
    int* W
);

/* -------------------- NEW EX APIs (strict thread validation) -------------------- */

/* float64 encode/decode with user-chosen thread count.
   num_workers:
     0 = auto (all hw threads)
     1 = single-thread
    >1 = exact number of threads; ERROR if > hw threads
*/
SZY_API int szy_compress_buffer_ex(
    double* input_data,
    int H, int W,
    double eps,
    int block_size,
    int num_workers,
    uint8_t** out_buf,
    size_t* out_size
);

SZY_API int szy_compress_u16_ex(
    uint16_t* input_data,
    int H, int W,
    double eps,
    int block_size,
    int num_workers,
    uint8_t** out_buf,
    size_t* out_size
);

SZY_API int szy_compress_f32_ex(
    float* input_data,
    int H, int W,
    float eps,
    int block_size,
    int num_workers,
    uint8_t** out_buf,
    size_t* out_size
);

SZY_API int szy_decompress_into_buffer_ex(
    const uint8_t* compressed_data,
    size_t compressed_size,
    double* out_data,
    size_t out_elems,
    int num_workers,
    int* H,
    int* W
);

/* Optional: decode threshold control */
SZY_API int szy_decompress_into_buffer_ex2(
    const uint8_t* compressed_data,
    size_t compressed_size,
    double* out_data,
    size_t out_elems,
    int num_workers,
    int parallel_min_tiles,
    int* H,
    int* W
);

#ifdef __cplusplus
}
#endif