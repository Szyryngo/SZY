#include "szy_tile_v8.h"
#include "szy_bytes.h"
#include "szy_varuint.h"
#include "szy_endian.h"
#include "szy_rc.h"
#include "szy_bitplane.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

/* zstd */
#include "zstd.h"
#include "zlib.h"

/* IDs must match Python / encoder historical format */
enum { ENT_ZLIB=0, ENT_LZMA=1, ENT_NONE=2, ENT_ZSTD=3 };
enum { TREND_NONE=0, TREND_PLANE3=1, TREND_POLY2_6=2 };
enum { EVENTS_OFF=0, EVENTS_ON=1 };
enum { PRED_LORENZO2D=0, PRED_DIFFX=1, PRED_DIFFY=2, PRED_GAP=3, PRED_LINEAR=4 };

/* flags */
#define FLAG_SHUFFLE16 1
#define FLAG_ZIGZAG    2
#define FLAG_BITPLANE  4

#define MAX_TILE_DECOMPRESSED_SIZE (100u * 1024u * 1024u)

static uint16_t u16_mask(int qbits) { return (qbits==8) ? 0x00FFu : 0xFFFFu; }

static int byteunshuffle16(const uint8_t* in, uint8_t* out, size_t nbytes) {
  if (!in || !out) return -1;
  if (nbytes % 2) { memcpy(out, in, nbytes); return 0; }
  size_t n = nbytes / 2;
  const uint8_t* lo = in;
  const uint8_t* hi = in + n;
  for (size_t i=0; i<n; i++) {
    out[2*i+0] = lo[i];
    out[2*i+1] = hi[i];
  }
  return 0;
}

static int bitplane_unshuffle_u16(const uint8_t* payload, size_t payload_n, uint16_t* out_u16, size_t npix) {
  if (!payload || !out_u16) return -1;
  if (payload_n < 2*npix) return -1;

  size_t total_bits = 16 * npix;
  size_t bit_index = 0;

  memset(out_u16, 0, npix * sizeof(uint16_t));

  for (size_t byte_i = 0; byte_i < payload_n && bit_index < total_bits; byte_i++) {
    uint8_t b = payload[byte_i];
    for (int k = 7; k >= 0 && bit_index < total_bits; k--) {
      uint8_t bit = (b >> k) & 1u;
      size_t plane = bit_index / npix;
      size_t idx   = bit_index % npix;
      if (bit) out_u16[idx] |= (uint16_t)(1u << plane);
      bit_index++;
    }
  }

  return (bit_index == total_bits) ? 0 : -1;
}

static int16_t zigzag_decode_u16(uint16_t z) {
  int32_t zz = (int32_t)z;
  int32_t x = (zz >> 1) ^ (-(zz & 1));
  return (int16_t)x;
}
static int8_t zigzag_decode_u8(uint8_t z) {
  int32_t zz = (int32_t)z;
  int32_t x = (zz >> 1) ^ (-(zz & 1));
  return (int8_t)x;
}

/* kept for backward compatibility with old files that may use amp mode 2 */
static float half_to_float(uint16_t h) {
  uint32_t sign = (uint32_t)(h & 0x8000u) << 16;
  uint32_t exp  = (h >> 10) & 0x1Fu;
  uint32_t mant = (uint32_t)(h & 0x03FFu);

  uint32_t f;
  if (exp == 0) {
    if (mant == 0) {
      f = sign;
    } else {
      exp = 1;
      while ((mant & 0x0400u) == 0) { mant <<= 1; exp--; }
      mant &= 0x03FFu;
      uint32_t exp_f = (exp + (127 - 15)) & 0xFFu;
      f = sign | (exp_f << 23) | (mant << 13);
    }
  } else if (exp == 31) {
    f = sign | 0x7F800000u | (mant << 13);
  } else {
    uint32_t exp_f = (exp + (127 - 15)) & 0xFFu;
    f = sign | (exp_f << 23) | (mant << 13);
  }

  float out;
  memcpy(&out, &f, 4);
  return out;
}

static int predictor_inverse_u16(const uint16_t* du, uint16_t* q, int h, int w, int qbits, int pred_id) {
  if (!du || !q) return -1;
  uint16_t mask = u16_mask(qbits);

  if (pred_id == PRED_DIFFX) {
    for (int i=0; i<h; i++) {
      size_t row = (size_t)i * (size_t)w;
      uint32_t acc = 0;
      for (int j=0; j<w; j++) {
        acc = (acc + (uint32_t)du[row + (size_t)j]) & mask;
        q[row + (size_t)j] = (uint16_t)acc;
      }
    }
    return 0;
  }

  if (pred_id == PRED_DIFFY) {
    for (int j=0; j<w; j++) {
      uint32_t acc = 0;
      for (int i=0; i<h; i++) {
        size_t idx = (size_t)i*(size_t)w + (size_t)j;
        acc = (acc + (uint32_t)du[idx]) & mask;
        q[idx] = (uint16_t)acc;
      }
    }
    return 0;
  }

  if (pred_id == PRED_LORENZO2D) {
    for (int i=0; i<h; i++) {
      for (int j=0; j<w; j++) {
        size_t idx = (size_t)i*(size_t)w + (size_t)j;
        uint32_t a  = (uint32_t)du[idx];
        uint32_t n  = (i>0) ? (uint32_t)q[(size_t)(i-1)*(size_t)w + (size_t)j] : 0;
        uint32_t wv = (j>0) ? (uint32_t)q[(size_t)i*(size_t)w + (size_t)(j-1)] : 0;
        uint32_t nw = (i>0 && j>0) ? (uint32_t)q[(size_t)(i-1)*(size_t)w + (size_t)(j-1)] : 0;
        uint32_t v = (a + n + wv - nw) & mask;
        q[idx] = (uint16_t)v;
      }
    }
    return 0;
  }

  return -1;
}

static double trend_eval(int trend_id, const double* params, int i, int j) {
  double x = (double)j;
  double y = (double)i;
  if (trend_id == TREND_NONE) return 0.0;
  if (trend_id == TREND_PLANE3) return params[0] + params[1]*x + params[2]*y;
  if (trend_id == TREND_POLY2_6) {
    return params[0]
         + params[1]*x + params[2]*y
         + params[3]*x*x + params[4]*x*y + params[5]*y*y;
  }
  return 0.0;
}

static int unpack_event_indices(const uint8_t* idx_bytes, size_t idx_len, uint32_t ne, uint32_t tile_npix, uint32_t* out_idx) {
  if (ne == 0) return 0;
  if (!idx_bytes || !out_idx) return -1;

  size_t pos = 0;
  uint64_t cur = 0;
  for (uint32_t i=0; i<ne; i++) {
    uint64_t d=0;
    if (szy_varuint_decode(idx_bytes, idx_len, &pos, &d)) return -1;
    cur += d;
    if (cur >= tile_npix) return -1;
    out_idx[i] = (uint32_t)cur;
  }
  return 0;
}

static int unpack_event_amps(szy_rd_t* r, uint32_t ne, uint8_t mode, double* out_amp) {
  if (ne == 0) return 0;
  if (!r || !out_amp) return -1;

  if (mode == 0) {
    double scale=0;
    if (szy_rd_f64le(r, &scale)) return -1;
    if (scale == 0.0 || !isfinite(scale)) return -1;
    const uint8_t* qbytes = NULL;
    if (szy_rd_bytes(r, ne, &qbytes)) return -1;
    for (uint32_t i=0; i<ne; i++) {
      int8_t q = (int8_t)qbytes[i];
      double a = (double)q / scale;
      if (!isfinite(a)) return -1;
      out_amp[i] = a;
    }
    return 0;
  }

  if (mode == 1) {
    double scale=0;
    if (szy_rd_f64le(r, &scale)) return -1;
    if (scale == 0.0 || !isfinite(scale)) return -1;
    const uint8_t* qbytes = NULL;
    if (szy_rd_bytes(r, (size_t)ne * 2u, &qbytes)) return -1;
    for (uint32_t i=0; i<ne; i++) {
      uint16_t u = szy_load_u16le(qbytes + 2u*i);
      int16_t q = (int16_t)u;
      double a = (double)q / scale;
      if (!isfinite(a)) return -1;
      out_amp[i] = a;
    }
    return 0;
  }

  /* backward compatibility for old files only */
  if (mode == 2) {
    const uint8_t* qbytes = NULL;
    if (szy_rd_bytes(r, (size_t)ne * 2u, &qbytes)) return -1;
    for (uint32_t i=0; i<ne; i++) {
      uint16_t h = szy_load_u16le(qbytes + 2u*i);
      double a = (double)half_to_float(h);
      if (!isfinite(a)) return -1;
      out_amp[i] = a;
    }
    return 0;
  }

  if (mode == 3) {
    const uint8_t* qbytes = NULL;
    if (szy_rd_bytes(r, (size_t)ne * 4u, &qbytes)) return -1;
    for (uint32_t i=0; i<ne; i++) {
      float f = szy_load_f32le(qbytes + 4u*i);
      double a = (double)f;
      if (!isfinite(a)) return -1;
      out_amp[i] = a;
    }
    return 0;
  }

  return -1;
}

static int entropy_decompress(uint8_t ent_id, const uint8_t* src, size_t src_n, uint8_t* dst, size_t dst_n) {
  if (!src || !dst) return -1;

  if (ent_id == ENT_NONE) {
    if (src_n != dst_n) return -1;
    memcpy(dst, src, dst_n);
    return 0;
  }

  if (ent_id == ENT_ZSTD) {
    size_t got = ZSTD_decompress(dst, dst_n, src, src_n);
    if (ZSTD_isError(got)) return -1;
    if (got != dst_n) return -1;
    return 0;
  }

  if (ent_id == ENT_ZLIB) {
    uLongf got = (uLongf)dst_n;
    int ret = uncompress(dst, &got, src, (uLong)src_n);
    if (ret != Z_OK) return -1;
    if (got != dst_n) return -1;
    return 0;
  }

  return -1;
}

int szy_tile_decode_into_v8(
  const uint8_t* buf, size_t n, size_t* io_pos,
  int B,
  double* dst, int dst_stride
) {
  if (!buf || !io_pos || !dst) return SZY_EINVAL;
  if (B <= 0 || dst_stride <= 0) return SZY_EINVAL;

  szy_rd_t r;
  if (szy_rd_init(&r, buf, n, *io_pos)) return SZY_EINVAL;

  uint16_t th=0, tw=0;
  uint8_t trend_id=0, qbits=0, qmode_ignored=0, events_id=0, ent_id=0, flags=0, pred_id=0;
  if (szy_rd_u16le(&r, &th)) return SZY_EINVAL;
  if (szy_rd_u16le(&r, &tw)) return SZY_EINVAL;
  if (szy_rd_u8(&r, &trend_id)) return SZY_EINVAL;
  if (szy_rd_u8(&r, &qbits)) return SZY_EINVAL;
  if (szy_rd_u8(&r, &qmode_ignored)) return SZY_EINVAL;
  if (szy_rd_u8(&r, &events_id)) return SZY_EINVAL;
  if (szy_rd_u8(&r, &ent_id)) return SZY_EINVAL;
  if (szy_rd_u8(&r, &flags)) return SZY_EINVAL;
  if (szy_rd_u8(&r, &pred_id)) return SZY_EINVAL;

  if ((int)th != B || (int)tw != B) return SZY_EINVAL;
  if (!(qbits == 8 || qbits == 16)) return SZY_EINVAL;

  uint8_t plen=0;
  if (szy_rd_u8(&r, &plen)) return SZY_EINVAL;
  if (!(plen==0 || plen==3 || plen==6)) return SZY_EINVAL;

  double params_local[6] = {0};
  if (plen) {
    for (uint8_t i=0; i<plen; i++) {
      if (szy_rd_f64le(&r, &params_local[i])) return SZY_EINVAL;
    }
  }

  double inv_scale=0, mean=0;
  float clip_rate_ignored=0;
  if (szy_rd_f64le(&r, &inv_scale)) return SZY_EINVAL;
  if (szy_rd_f64le(&r, &mean)) return SZY_EINVAL;
  if (szy_rd_f32le(&r, &clip_rate_ignored)) return SZY_EINVAL;

  int16_t bias=0;
  if (szy_rd_i16le(&r, &bias)) return SZY_EINVAL;

  uint8_t has_pred_params=0;
  if (szy_rd_u8(&r, &has_pred_params)) return SZY_EINVAL;
  if (has_pred_params) {
    if (szy_rd_skip(&r, 16)) return SZY_EINVAL;
  }

  uint64_t ne64=0, idx_len64=0;
  if (szy_varuint_decode(buf, n, &r.pos, &ne64)) return SZY_EINVAL;
  if (szy_varuint_decode(buf, n, &r.pos, &idx_len64)) return SZY_EINVAL;
  if (ne64 > 1000000ULL) return SZY_EINVAL;
  if (idx_len64 > 0x7fffffffULL) return SZY_EINVAL;
  uint32_t ne = (uint32_t)ne64;
  size_t idx_len = (size_t)idx_len64;

  const uint8_t* idx_bytes = NULL;
  if (szy_rd_bytes(&r, idx_len, &idx_bytes)) return SZY_EINVAL;

  uint8_t amp_mode=0;
  if (szy_rd_u8(&r, &amp_mode)) return SZY_EINVAL;

  uint32_t* ev_idx = NULL;
  double*   ev_amp = NULL;

  if (ne > 0) {
    if (ne > (uint32_t)th * (uint32_t)tw) {
      return SZY_EINVAL;
    }

    ev_idx = (uint32_t*)malloc((size_t)ne * sizeof(uint32_t));
    ev_amp = (double*)malloc((size_t)ne * sizeof(double));
    if (!ev_idx || !ev_amp) { free(ev_idx); free(ev_amp); return SZY_E_OOM; }

    uint32_t tile_npix = (uint32_t)th * (uint32_t)tw;
    if (unpack_event_indices(idx_bytes, idx_len, ne, tile_npix, ev_idx)) {
      free(ev_idx); free(ev_amp); return SZY_EINVAL;
    }
    if (unpack_event_amps(&r, ne, amp_mode, ev_amp)) {
      free(ev_idx); free(ev_amp); return SZY_EINVAL;
    }
  }

  uint64_t pay_len64=0;
  if (szy_varuint_decode(buf, n, &r.pos, &pay_len64)) {
    free(ev_idx); free(ev_amp); return SZY_EINVAL;
  }
  if (pay_len64 > 0x7fffffffULL) { free(ev_idx); free(ev_amp); return SZY_EINVAL; }
  size_t pay_len = (size_t)pay_len64;

  const uint8_t* payload = NULL;
  if (szy_rd_bytes(&r, pay_len, &payload)) { free(ev_idx); free(ev_amp); return SZY_EINVAL; }

  *io_pos = r.pos;

  size_t npix = (size_t)th * (size_t)tw;
  size_t expected = npix * (size_t)(qbits / 8);

  if (expected > MAX_TILE_DECOMPRESSED_SIZE) {
    free(ev_idx); free(ev_amp);
    return SZY_EINVAL;
  }

  uint8_t* body = (uint8_t*)malloc(expected);
  if (!body) { free(ev_idx); free(ev_amp); return SZY_E_OOM; }

  if (entropy_decompress(ent_id, payload, pay_len, body, expected)) {
    free(body); free(ev_idx); free(ev_amp); return SZY_EINVAL;
  }

  uint8_t use_zz = (flags & FLAG_ZIGZAG) ? 1 : 0;
  uint8_t shuffle16 = (flags & FLAG_SHUFFLE16) ? 1 : 0;
  uint8_t use_bitplane = (flags & FLAG_BITPLANE) ? 1 : 0;

  uint16_t* du16 = NULL;
  uint8_t*  du8  = NULL;

  if (qbits == 16) {
    uint8_t* body0 = body;

    uint8_t* tmp_unshuf = NULL;
  if (use_bitplane) {
    uint16_t* zz_u16 = (uint16_t*)malloc(npix * sizeof(uint16_t));
    if (!zz_u16) { free(body); free(ev_idx); free(ev_amp); return SZY_E_OOM; }
    
    /* NEW: Call proper bitplane unshuffle */
    if (szy_bitplane_unshuffle_u16(body0, zz_u16, npix)) {
      free(zz_u16); free(body); free(ev_idx); free(ev_amp); 
      return SZY_EINVAL;
    }
    
    du16 = (uint16_t*)malloc(npix * sizeof(uint16_t));
    if (!du16) { free(zz_u16); free(body); free(ev_idx); free(ev_amp); return SZY_E_OOM; }
    
    for (size_t k=0; k<npix; k++) {
      int16_t ds = use_zz ? zigzag_decode_u16(zz_u16[k]) : (int16_t)zz_u16[k];
      if (bias) ds = (int16_t)((int32_t)ds + (int32_t)bias);
      du16[k] = (uint16_t)ds;
    }
    free(zz_u16);
  } else {
      if (shuffle16) {
        tmp_unshuf = (uint8_t*)malloc(expected);
        if (!tmp_unshuf) { free(body); free(ev_idx); free(ev_amp); return SZY_E_OOM; }
        if (byteunshuffle16(body0, tmp_unshuf, expected)) {
          free(tmp_unshuf); free(body); free(ev_idx); free(ev_amp); return SZY_EINVAL;
        }
        body0 = tmp_unshuf;
      }

      const uint8_t* qbytes = body0;
      du16 = (uint16_t*)malloc(npix * sizeof(uint16_t));
      if (!du16) { free(tmp_unshuf); free(body); free(ev_idx); free(ev_amp); return SZY_E_OOM; }

      for (size_t k=0; k<npix; k++) {
        uint16_t z = szy_load_u16le(qbytes + 2*k);
        int16_t ds = use_zz ? zigzag_decode_u16(z) : (int16_t)z;
        if (bias) ds = (int16_t)((int32_t)ds + (int32_t)bias);
        du16[k] = (uint16_t)ds;
      }

      free(tmp_unshuf);
    }
  } else {
    du8 = (uint8_t*)malloc(npix);
    if (!du8) { free(body); free(ev_idx); free(ev_amp); return SZY_E_OOM; }
    for (size_t k=0; k<npix; k++) {
      uint8_t z = body[k];
      int8_t ds = use_zz ? zigzag_decode_u8(z) : (int8_t)z;
      if (bias) ds = (int8_t)((int32_t)ds + (int32_t)bias);
      du8[k] = (uint8_t)ds;
    }
  }

  free(body);

  if (pred_id == PRED_GAP || pred_id == PRED_LINEAR) {
    free(du16); free(du8); free(ev_idx); free(ev_amp);
    return SZY_EINVAL;
  }

  if (qbits == 16) {
    uint16_t* q = (uint16_t*)malloc(npix * sizeof(uint16_t));
    if (!q) { free(du16); free(ev_idx); free(ev_amp); return SZY_E_OOM; }

    if (predictor_inverse_u16(du16, q, (int)th, (int)tw, (int)qbits, (int)pred_id)) {
      free(q); free(du16); free(ev_idx); free(ev_amp); return SZY_EINVAL;
    }
    free(du16);

    if (inv_scale == 0.0 || !isfinite(inv_scale) || !isfinite(mean)) { free(q); free(ev_idx); free(ev_amp); return SZY_EINVAL; }

    for (int i=0; i<(int)th; i++) {
      for (int j=0; j<(int)tw; j++) {
        size_t k = (size_t)i*(size_t)tw + (size_t)j;
        int16_t qs = (int16_t)q[k];
        double v = ((double)qs) / inv_scale + mean;
        v += trend_eval((int)trend_id, params_local, i, j);
        if (!isfinite(v)) { free(q); free(ev_idx); free(ev_amp); return SZY_EINVAL; }
        dst[(size_t)i*(size_t)dst_stride + (size_t)j] = v;
      }
    }

    if (events_id == EVENTS_ON && ne > 0) {
      for (uint32_t t=0; t<ne; t++) {
        uint32_t idx = ev_idx[t];
        uint32_t ii = idx / (uint32_t)tw;
        uint32_t jj = idx % (uint32_t)tw;
        double v = dst[(size_t)ii*(size_t)dst_stride + (size_t)jj] + ev_amp[t];
        if (!isfinite(v)) { free(q); free(ev_idx); free(ev_amp); return SZY_EINVAL; }
        dst[(size_t)ii*(size_t)dst_stride + (size_t)jj] = v;
      }
    }

    free(q);
  } else {
    uint16_t* du = (uint16_t*)malloc(npix * sizeof(uint16_t));
    uint16_t* q  = (uint16_t*)malloc(npix * sizeof(uint16_t));
    if (!du || !q) { free(du); free(q); free(du8); free(ev_idx); free(ev_amp); return SZY_E_OOM; }

    for (size_t k=0; k<npix; k++) du[k] = (uint16_t)du8[k];
    free(du8);

    if (predictor_inverse_u16(du, q, (int)th, (int)tw, (int)qbits, (int)pred_id)) {
      free(du); free(q); free(ev_idx); free(ev_amp); return SZY_EINVAL;
    }
    free(du);

    if (inv_scale == 0.0 || !isfinite(inv_scale) || !isfinite(mean)) { free(q); free(ev_idx); free(ev_amp); return SZY_EINVAL; }

    for (int i=0; i<(int)th; i++) {
      for (int j=0; j<(int)tw; j++) {
        size_t k = (size_t)i*(size_t)tw + (size_t)j;
        int8_t qs = (int8_t)(uint8_t)q[k];
        double v = ((double)qs) / inv_scale + mean;
        v += trend_eval((int)trend_id, params_local, i, j);
        if (!isfinite(v)) { free(q); free(ev_idx); free(ev_amp); return SZY_EINVAL; }
        dst[(size_t)i*(size_t)dst_stride + (size_t)j] = v;
      }
    }

    if (events_id == EVENTS_ON && ne > 0) {
      for (uint32_t t=0; t<ne; t++) {
        uint32_t idx = ev_idx[t];
        uint32_t ii = idx / (uint32_t)tw;
        uint32_t jj = idx % (uint32_t)tw;
        double v = dst[(size_t)ii*(size_t)dst_stride + (size_t)jj] + ev_amp[t];
        if (!isfinite(v)) { free(q); free(ev_idx); free(ev_amp); return SZY_EINVAL; }
        dst[(size_t)ii*(size_t)dst_stride + (size_t)jj] = v;
      }
    }

    free(q);
  }

  free(ev_idx);
  free(ev_amp);
  return SZY_OK;
}