#include "szy_encode_v8.h"
#include "szy_decode_v8.h"
#include "szy_rc.h"

#include <stdlib.h>

int szy_encode_2d_u16_v8(
    const uint16_t* x, int H, int W,
    const szy_enc_cfg_t* cfg,
    uint8_t** out_buf, size_t* out_n
) {
  return szy_encode_2d_u16_v8_ex(x, H, W, cfg, 1, out_buf, out_n);
}

int szy_encode_2d_u16_v8_ex(
    const uint16_t* x, int H, int W,
    const szy_enc_cfg_t* cfg,
    int num_workers,
    uint8_t** out_buf, size_t* out_n
) {
  if (!x || !cfg || !out_buf || !out_n) return SZY_EINVAL;
  size_t npix = (size_t)H * (size_t)W;
  if (npix == 0) return SZY_EINVAL;

  double* tmp = (double*)malloc(npix * sizeof(double));
  if (!tmp) return SZY_E_OOM;

  for (size_t i = 0; i < npix; i++) tmp[i] = (double)x[i];

  int rc = szy_encode_2d_f64_v8_ex(tmp, H, W, cfg, num_workers, out_buf, out_n);
  free(tmp);
  return rc;
}

int szy_decode_2d_u16_v8(
    const uint8_t* compressed_data, size_t compressed_size,
    uint16_t** out_data, int* H, int* W
) {
  if (!compressed_data || !out_data || !H || !W) return SZY_EINVAL;

  double* tmp = NULL;
  int rc = szy_decode_2d_f64_v8(compressed_data, compressed_size, &tmp, H, W);
  if (rc != SZY_OK) return rc;

  size_t npix = (size_t)(*H) * (size_t)(*W);
  uint16_t* result = (uint16_t*)malloc(npix * sizeof(uint16_t));
  if (!result) {
    free(tmp);
    return SZY_E_OOM;
  }

  for (size_t i = 0; i < npix; i++) {
    double v = tmp[i];
    if (v < 0.0) v = 0.0;
    if (v > 65535.0) v = 65535.0;
    result[i] = (uint16_t)(v + 0.5);
  }

  free(tmp);
  *out_data = result;
  return SZY_OK;
}