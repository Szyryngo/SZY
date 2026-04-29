#include "szy_encode_v8.h"
#include "szy_buf.h"
#include "szy_sha256.h"
#include "szy_varuint.h"
#include "szy_endian.h"
#include "szy_rc.h"
#include "szy_hw.h"
#include "szy_bitplane.h"  /* NEW: Include bitplane header */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <stdint.h>
#include <limits.h>

/* entropy libs */
#include "zstd.h"
#include "zlib.h"

#ifdef _WIN32
  #include <windows.h>
#else
  #include <pthread.h>
#endif

/* container constants */
static const uint8_t MAGIC[4] = { 'S','Z','2','D' };
static const uint8_t VERSION = 8;

/* IDs must match python */
enum { TREND_NONE=0 };
enum { EVENTS_OFF=0, EVENTS_ON=1 };
enum { PRED_LORENZO2D=0 };

/* flags */
#define FLAG_SHUFFLE16 1
#define FLAG_ZIGZAG    2
#define FLAG_BITPLANE  4

static double dmax(double a, double b) { return (a>b)?a:b; }
static double dabs(double x) { return x < 0 ? -x : x; }

static long long round_to_even_ll(double x) {
  double r = floor(x);
  double f = x - r;
  if (f > 0.5) return (long long)(r + 1.0);
  if (f < 0.5) return (long long)r;
  long long ri = (long long)r;
  if (ri & 1LL) return ri + 1LL;
  return ri;
}

static int byteshuffle16(const uint8_t* in, uint8_t* out, size_t nbytes) {
  if (!in || !out) return SZY_EINVAL;
  if (nbytes % 2) { memcpy(out, in, nbytes); return SZY_OK; }
  size_t n = nbytes / 2;
  for (size_t i=0; i<n; i++) {
    out[i]     = in[2*i + 0];
    out[i + n] = in[2*i + 1];
  }
  return SZY_OK;
}

static int entropy_compress(
  szy_entropy_t ent, int zstd_level,
  const uint8_t* src, size_t src_n,
  uint8_t** out, size_t* out_n
) {
  if (!out || !out_n) return SZY_EINVAL;
  *out = NULL; *out_n = 0;
  if (!src && src_n) return SZY_EINVAL;

  if (ent == SZY_ENT_NONE) {
    uint8_t* p = (uint8_t*)malloc(src_n);
    if (!p) return SZY_E_OOM;
    memcpy(p, src, src_n);
    *out = p; *out_n = src_n;
    return SZY_OK;
  }

  if (ent == SZY_ENT_ZSTD) {
    size_t cap = ZSTD_compressBound(src_n);
    uint8_t* p = (uint8_t*)malloc(cap);
    if (!p) return SZY_E_OOM;
    int lvl = (zstd_level > 0) ? zstd_level : 3;
    size_t got = ZSTD_compress(p, cap, src, src_n, lvl);
    if (ZSTD_isError(got)) { free(p); return SZY_EINVAL; }
    *out = p; *out_n = (size_t)got;
    return SZY_OK;
  }

  if (ent == SZY_ENT_ZLIB) {
    uLongf cap = compressBound((uLong)src_n);
    uint8_t* p = (uint8_t*)malloc((size_t)cap);
    if (!p) return SZY_E_OOM;
    uLongf got = cap;
    int ret = compress2(p, &got, src, (uLong)src_n, 9);
    if (ret != Z_OK) { free(p); return SZY_EINVAL; }
    *out = p; *out_n = (size_t)got;
    return SZY_OK;
  }

  return SZY_EINVAL;
}

static double percentile_abs_linear(double* a, size_t n, double perc) {
  if (n == 0) return 0.0;
  if (perc <= 0.0) return a[0];
  if (perc >= 100.0) return a[n-1];

  double idx = (perc / 100.0) * (double)(n - 1);
  size_t i = (size_t)floor(idx);
  double f = idx - (double)i;
  if (i + 1 >= n) return a[n-1];
  return a[i] * (1.0 - f) + a[i+1] * f;
}

static int cmp_double(const void* pa, const void* pb) {
  double a = *(const double*)pa;
  double b = *(const double*)pb;
  return (a < b) ? -1 : (a > b) ? 1 : 0;
}

static void predictor_forward_lorenzo2d_u16(const uint16_t* q, uint16_t* d, int h, int w, uint16_t mask) {
  d[0] = q[0] & mask;
  for (int j=1; j<w; j++) d[j] = (uint16_t)(((uint32_t)q[j] - (uint32_t)q[j-1]) & mask);
  for (int i=1; i<h; i++) d[i*w] = (uint16_t)(((uint32_t)q[i*w] - (uint32_t)q[(i-1)*w]) & mask);
  for (int i=1; i<h; i++) {
    for (int j=1; j<w; j++) {
      uint32_t N  = q[(i-1)*w + j] & mask;
      uint32_t W  = q[i*w + (j-1)] & mask;
      uint32_t NW = q[(i-1)*w + (j-1)] & mask;
      uint32_t pred = (N + W - NW) & mask;
      d[i*w + j] = (uint16_t)(((uint32_t)q[i*w + j] - pred) & mask);
    }
  }
}

static uint8_t zigzag8(int8_t x) {
  int16_t xs = (int16_t)x;
  uint16_t zz = (uint16_t)(((xs << 1) ^ (xs >> 7)) & 0xFF);
  return (uint8_t)zz;
}
static uint16_t zigzag16(int16_t x) {
  int32_t xs = (int32_t)x;
  uint32_t zz = (uint32_t)((xs << 1) ^ (xs >> 15));
  return (uint16_t)(zz & 0xFFFFu);
}

static int pack_event_indices(szy_bw_t* w, const uint32_t* idx, uint32_t ne) {
  if (!w) return SZY_EINVAL;
  if (ne == 0) return SZY_OK;
  if (!idx) return SZY_EINVAL;

  uint32_t prev = 0;
  for (uint32_t i=0; i<ne; i++) {
    uint32_t cur = idx[i];
    uint32_t d = (i==0) ? cur : (cur - prev);
    prev = cur;
    uint8_t tmp10[10];
    size_t vn = szy_varuint_encode((uint64_t)d, tmp10);
    if (szy_bw_put(w, tmp10, vn)) return SZY_EINVAL;
  }
  return SZY_OK;
}

static int pack_event_amps_adaptive(
  const double* amp, uint32_t ne, double target_err,
  uint8_t* out_mode, uint8_t** out_blob, size_t* out_blob_n
) {
  if (!out_mode || !out_blob || !out_blob_n) return SZY_EINVAL;
  *out_mode = 0; *out_blob = NULL; *out_blob_n = 0;
  if (ne == 0) return SZY_OK;
  if (!amp) return SZY_EINVAL;

  double mx = 0.0;
  for (uint32_t i=0; i<ne; i++) {
    double a = dabs(amp[i]);
    if (!isfinite(a)) return SZY_EINVAL;
    mx = dmax(mx, a);
  }
  mx = mx + 1e-15;

  if (mx < 127.0 && target_err >= 0.5) {
    double scale = 127.0 / mx;
    size_t n = 8 + (size_t)ne;
    uint8_t* b = (uint8_t*)malloc(n);
    if (!b) return SZY_E_OOM;

    szy_store_f64le(b, scale);
    for (uint32_t i=0; i<ne; i++) {
      long long q = round_to_even_ll(amp[i] * scale);
      if (q > 127) q = 127;
      if (q < -127) q = -127;
      int8_t qi = (int8_t)q;
      b[8 + i] = (uint8_t)qi;
    }
    *out_mode = 0;
    *out_blob = b;
    *out_blob_n = n;
    return SZY_OK;
  }

  if (mx < 32767.0 && target_err >= 0.05) {
    double scale = 32767.0 / mx;
    size_t n = 8 + (size_t)ne * 2;
    uint8_t* b = (uint8_t*)malloc(n);
    if (!b) return SZY_E_OOM;

    szy_store_f64le(b, scale);
    for (uint32_t i=0; i<ne; i++) {
      long long q = round_to_even_ll(amp[i] * scale);
      if (q > 32767) q = 32767;
      if (q < -32767) q = -32767;
      int16_t qi = (int16_t)q;
      szy_store_u16le(b + 8 + 2u*i, (uint16_t)qi);
    }
    *out_mode = 1;
    *out_blob = b;
    *out_blob_n = n;
    return SZY_OK;
  }

  {
    size_t n = (size_t)ne * 4;
    uint8_t* b = (uint8_t*)malloc(n);
    if (!b) return SZY_E_OOM;
    for (uint32_t i=0; i<ne; i++) {
      float f = (float)amp[i];
      if (!isfinite(f)) { free(b); return SZY_EINVAL; }
      szy_store_f32le(b + 4u*i, f);
    }
    *out_mode = 3;
    *out_blob = b;
    *out_blob_n = n;
    return SZY_OK;
  }
}

static void quant_params(int qbits, double* out_qmax, int* out_minbias) {
  if (qbits == 8) { *out_qmax = 127.0; *out_minbias = 2; return; }
  *out_qmax = 32767.0; *out_minbias = 4; return;
}

/* -------------------- FAST median bias (no qsort) -------------------- */

static void iswap16(int16_t* a, int16_t* b) {
  int16_t t = *a; *a = *b; *b = t;
}

static int16_t median3_i16(int16_t a, int16_t b, int16_t c) {
  if (a > b) { int16_t t=a; a=b; b=t; }
  if (b > c) { int16_t t=b; b=c; c=t; }
  if (a > b) { int16_t t=a; a=b; b=t; }
  return b;
}

static int16_t select_kth_i16(int16_t* v, size_t n, size_t k) {
  if (n == 0) return 0;
  if (k >= n) k = n - 1;

  size_t lo = 0, hi = n - 1;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    int16_t pivot = median3_i16(v[lo], v[mid], v[hi]);

    size_t i = lo, j = hi;
    for (;;) {
      while (v[i] < pivot) i++;
      while (v[j] > pivot) j--;
      if (i >= j) break;
      iswap16(&v[i], &v[j]);
      i++; j--;
    }

    if (k <= j) hi = j;
    else        lo = j + 1;
  }
  return v[lo];
}

static int16_t compute_median_bias_16_fast(const uint16_t* d_u, size_t npix, int minbias, int* error_flag) {
  if (!d_u || npix == 0) return 0;

  if (npix > SIZE_MAX / sizeof(int16_t)) {
    if (error_flag) *error_flag = 1;
    return 0;
  }

  int16_t* tmp = (int16_t*)malloc(npix * sizeof(int16_t));
  if (!tmp) {
    if (error_flag) *error_flag = 1;
    return 0;
  }

  for (size_t i = 0; i < npix; i++) tmp[i] = (int16_t)d_u[i];

  int16_t med = select_kth_i16(tmp, npix, npix / 2);
  free(tmp);

  if (abs((int)med) < minbias) return 0;
  return med;
}

static int8_t compute_median_bias_8_fast(const uint16_t* d_u, size_t npix, int minbias) {
  if (!d_u || npix == 0) return 0;

  if (npix > SIZE_MAX / sizeof(uint16_t)) return 0;

  uint16_t cnt[256] = {0};
  for (size_t i = 0; i < npix; i++) {
    uint8_t idx = (uint8_t)(d_u[i] & 0xFFu);
    if (cnt[idx] < UINT16_MAX) cnt[idx]++;
  }

  size_t k = npix / 2;
  size_t acc = 0;

  for (int si = -128; si <= 127; si++) {
    uint8_t u = (uint8_t)(int8_t)si;
    acc += cnt[u];
    if (acc > k) {
      int8_t med = (int8_t)si;
      if (abs((int)med) < minbias) return 0;
      return med;
    }
  }
  return 0;
}
/* -------------------- NEW: Fallback wrapper for pack_tile_record_v8 -------------------- */

/* Forward declaration */
static int pack_tile_record_v8(
  const double* tile, int B,
  const szy_enc_cfg_t* cfg,
  szy_bw_t* out_tile
);

/**
 * NEW: Wrapper that tries pack_tile_record_v8 with cascading fallback.
 * 
 * Strategy:
 *   1. Try with original config
 *   2. If SZY_E_BOUNDS (events overflow) → relax eps by 2×
 *   3. Still fail → disable events entirely
 *   4. Still fail → return error (true failure)
 * 
 * @param out_fallback_used  0=success, 1=relaxed_eps, 2=disabled_events
 */
static int pack_tile_with_fallback(
    const double* tile, int B,
    const szy_enc_cfg_t* cfg,
    szy_bw_t* out_tile,
    int* out_fallback_used
) {
    if (!tile || !cfg || !out_tile || !out_fallback_used) return SZY_EINVAL;
    
    *out_fallback_used = 0;
    
    /* Attempt 1: Standard config */
    int rc = pack_tile_record_v8(tile, B, cfg, out_tile);
    if (rc == SZY_OK) {
        return SZY_OK;
    }
    
    /* Only fallback on events overflow */
    if (rc != SZY_E_BOUNDS) {
        return rc;
    }
    
    /* Attempt 2: Relax eps by 2× */
    szy_enc_cfg_t cfg2 = *cfg;
    cfg2.target_abs_err *= 2.0;
    cfg2.bounded_safety = (cfg->bounded_safety > 0.9) ? 0.9 : cfg->bounded_safety;
    
    szy_bw_t tmp;
    szy_bw_init(&tmp);
    rc = pack_tile_record_v8(tile, B, &cfg2, &tmp);
    
    if (rc == SZY_OK) {
        /* Copy tmp to out_tile */
        if (szy_bw_put(out_tile, tmp.p, tmp.n)) {
            szy_bw_free(&tmp);
            return SZY_E_OOM;
        }
        szy_bw_free(&tmp);
        *out_fallback_used = 1;
        return SZY_OK;
    }
    szy_bw_free(&tmp);
    
    /* Attempt 3: Disable events (switch to percentile mode) */
    szy_enc_cfg_t cfg3 = *cfg;
    cfg3.events_on = 0;
    cfg3.target_abs_err = 0.0;
    
    szy_bw_init(&tmp);
    rc = pack_tile_record_v8(tile, B, &cfg3, &tmp);
    
    if (rc == SZY_OK) {
        if (szy_bw_put(out_tile, tmp.p, tmp.n)) {
            szy_bw_free(&tmp);
            return SZY_E_OOM;
        }
        szy_bw_free(&tmp);
        *out_fallback_used = 2;
        return SZY_OK;
    }
    szy_bw_free(&tmp);
    
    /* All attempts failed */
    return rc;
}

/* -------------------- CORE: pack_tile_record_v8 (MODIFIED for bitplane) -------------------- */

static int pack_tile_record_v8(
  const double* tile, int B,
  const szy_enc_cfg_t* cfg,
  szy_bw_t* out_tile
) {
  if (!tile || !cfg || !out_tile) return SZY_EINVAL;
  if (!(cfg->qbits == 8 || cfg->qbits == 16)) return SZY_EINVAL;

  const int h = B, w = B;
  const size_t npix = (size_t)h * (size_t)w;

  double qmax; int minbias;
  quant_params(cfg->qbits, &qmax, &minbias);

  double mu = 0.0;
  for (size_t k=0; k<npix; k++) mu += tile[k];
  mu /= (double)npix;

  double* c = (double*)malloc(npix * sizeof(double));
  if (!c) return SZY_E_OOM;
  for (size_t k=0; k<npix; k++) c[k] = tile[k] - mu;

  int eps_on = (cfg->target_abs_err > 0.0);
  double bound_target = 0.0;
  if (eps_on) {
    bound_target = cfg->quant_err_factor * qmax * cfg->target_abs_err * cfg->bounded_safety;
    if (bound_target < 1e-15) bound_target = 1e-15;
  }

  uint32_t* ev_idx = NULL;
  double*   ev_amp = NULL;
  uint32_t  ne = 0;

  if (eps_on && cfg->events_on) {
    ev_idx = (uint32_t*)malloc(npix * sizeof(uint32_t));
    ev_amp = (double*)malloc(npix * sizeof(double));
    if (!ev_idx || !ev_amp) { free(c); free(ev_idx); free(ev_amp); return SZY_E_OOM; }

    for (uint32_t i=0; i<(uint32_t)npix; i++) {
      double a = dabs(c[i]);
      if (a > bound_target) {
        ev_idx[ne] = i;
        ev_amp[ne] = c[i];
        ne++;
      }
    }

    double frac = (double)ne / (double)npix;
    if (ne > (uint32_t)cfg->events_max_per_tile || frac > cfg->events_max_frac) {
      free(c);
      free(ev_idx);
      free(ev_amp);
      return SZY_E_BOUNDS;
    }

    if (ne > 0) {
      for (uint32_t t=0; t<ne; t++) c[ev_idx[t]] -= ev_amp[t];
    }
  }

  double bound = 0.0;
  if (eps_on) {
    bound = bound_target;
  } else {
    if (cfg->qmode == SZY_QMODE_STRICT) {
      double mx = 0.0;
      for (size_t k=0; k<npix; k++) mx = dmax(mx, dabs(c[k]));
      bound = (mx < 1e-15) ? 1e-15 : mx;
    } else {
      double* absvals = (double*)malloc(npix * sizeof(double));
      if (!absvals) { free(c); free(ev_idx); free(ev_amp); return SZY_E_OOM; }
      for (size_t k=0; k<npix; k++) absvals[k] = dabs(c[k]);
      qsort(absvals, npix, sizeof(double), cmp_double);
      bound = percentile_abs_linear(absvals, npix, cfg->qpercentile);
      if (bound < 1e-15) bound = 1e-15;
      free(absvals);
    }
  }

  double inv_scale = qmax / bound;

  size_t clipped = 0;
  float clip_rate = 0.0f;

  uint16_t mask = (cfg->qbits == 8) ? 0x00FFu : 0xFFFFu;

  uint16_t* q_u = (uint16_t*)malloc(npix * sizeof(uint16_t));
  uint16_t* d_u = (uint16_t*)malloc(npix * sizeof(uint16_t));
  if (!q_u || !d_u) { free(c); free(ev_idx); free(ev_amp); free(q_u); free(d_u); return SZY_E_OOM; }

  if (cfg->qbits == 16) {
    for (size_t k=0; k<npix; k++) {
      long long qi = round_to_even_ll(c[k] * inv_scale);
      if (qi > 32767) { qi = 32767; clipped++; }
      if (qi < -32767) { qi = -32767; clipped++; }
      int16_t qs = (int16_t)qi;
      q_u[k] = (uint16_t)qs;
    }
  } else {
    for (size_t k=0; k<npix; k++) {
      long long qi = round_to_even_ll(c[k] * inv_scale);
      if (qi > 127) { qi = 127; clipped++; }
      if (qi < -127) { qi = -127; clipped++; }
      int8_t qs = (int8_t)qi;
      q_u[k] = (uint16_t)(uint8_t)qs;
    }
  }
  clip_rate = (float)((double)clipped / (double)npix);

  free(c);

  predictor_forward_lorenzo2d_u16(q_u, d_u, h, w, mask);
  free(q_u);

  int16_t bias = 0;
  int bias_error = 0;
  if (cfg->qbits == 16) {
    bias = compute_median_bias_16_fast(d_u, npix, minbias, &bias_error);
    if (bias_error) {
      free(d_u); free(ev_idx); free(ev_amp);
      return SZY_E_OOM;
    }
  } else {
    bias = (int16_t)compute_median_bias_8_fast(d_u, npix, minbias);
  }

  uint8_t use_zz = (cfg->use_zigzag != 0);
  uint8_t shuffle16 = (cfg->byte_shuffle_16 != 0);
  uint8_t use_bitplane = (cfg->use_bitplane_shuffle != 0);
  uint8_t flags = 0;
  
  if (use_zz) flags |= FLAG_ZIGZAG;

  /* NEW: Bitplane takes priority over byte shuffle for qbits=16 */
  if (cfg->qbits == 16) {
    if (use_bitplane) {
      flags |= FLAG_BITPLANE;
    } else if (shuffle16) {
      flags |= FLAG_SHUFFLE16;
    }
  }

  size_t body0_n = npix * (size_t)(cfg->qbits / 8);
  uint8_t* body0 = NULL;
  uint8_t* body_final = NULL;

  /* -------------------- NEW: Bitplane path -------------------- */
  if (cfg->qbits == 16 && use_bitplane) {
    /* Apply bias + zigzag first, store in temp u16 array */
    uint16_t* temp_u16 = (uint16_t*)malloc(npix * sizeof(uint16_t));
    if (!temp_u16) { free(d_u); free(ev_idx); free(ev_amp); return SZY_E_OOM; }
    
    for (size_t k = 0; k < npix; k++) {
      int16_t ds = (int16_t)d_u[k];
      ds = (int16_t)((int32_t)ds - (int32_t)bias);
      temp_u16[k] = use_zz ? zigzag16(ds) : (uint16_t)ds;
    }
    
    /* Allocate bitplane output buffer */
    body0 = (uint8_t*)malloc(body0_n);
    if (!body0) { free(temp_u16); free(d_u); free(ev_idx); free(ev_amp); return SZY_E_OOM; }
    
    /* Bitplane shuffle */
    int bprc = szy_bitplane_shuffle_u16(temp_u16, body0, npix);
    free(temp_u16);
    
    if (bprc != 0) { free(body0); free(d_u); free(ev_idx); free(ev_amp); return SZY_EINVAL; }
    
    body_final = body0;
    
  } else if (cfg->qbits == 16 && shuffle16) {
    /* -------------------- Byte shuffle path (existing) -------------------- */
    body0 = (uint8_t*)malloc(body0_n);
    if (!body0) { free(d_u); free(ev_idx); free(ev_amp); return SZY_E_OOM; }
    
    for (size_t k=0; k<npix; k++) {
      int16_t ds = (int16_t)d_u[k];
      ds = (int16_t)((int32_t)ds - (int32_t)bias);
      uint16_t out = use_zz ? zigzag16(ds) : (uint16_t)ds;
      szy_store_u16le(body0 + 2*k, out);
    }
    
    uint8_t* body_shuf = (uint8_t*)malloc(body0_n);
    if (!body_shuf) { free(body0); free(d_u); free(ev_idx); free(ev_amp); return SZY_E_OOM; }
    
    int shrc = byteshuffle16(body0, body_shuf, body0_n);
    if (shrc != SZY_OK) { free(body0); free(body_shuf); free(d_u); free(ev_idx); free(ev_amp); return shrc; }
    
    free(body0);
    body_final = body_shuf;
    
  } else if (cfg->qbits == 16) {
    /* -------------------- No shuffle (16-bit) -------------------- */
    body0 = (uint8_t*)malloc(body0_n);
    if (!body0) { free(d_u); free(ev_idx); free(ev_amp); return SZY_E_OOM; }
    
    for (size_t k=0; k<npix; k++) {
      int16_t ds = (int16_t)d_u[k];
      ds = (int16_t)((int32_t)ds - (int32_t)bias);
      uint16_t out = use_zz ? zigzag16(ds) : (uint16_t)ds;
      szy_store_u16le(body0 + 2*k, out);
    }
    
    body_final = body0;
    
  } else {
    /* -------------------- 8-bit path -------------------- */
    body0 = (uint8_t*)malloc(body0_n);
    if (!body0) { free(d_u); free(ev_idx); free(ev_amp); return SZY_E_OOM; }
    
    for (size_t k=0; k<npix; k++) {
      int8_t ds = (int8_t)(uint8_t)(d_u[k] & 0xFFu);
      ds = (int8_t)((int32_t)ds - (int32_t)bias);
      uint8_t out = use_zz ? zigzag8(ds) : (uint8_t)ds;
      body0[k] = out;
    }
    
    body_final = body0;
  }

  free(d_u);

  /* -------------------- Entropy compress -------------------- */
  uint8_t* payload = NULL;
  size_t payload_n = 0;
  int erc = entropy_compress(cfg->ent, cfg->zstd_level, body_final, body0_n, &payload, &payload_n);
  
  free(body_final);
  
  if (erc != SZY_OK) {
    free(ev_idx); free(ev_amp);
    return erc;
  }

  /* -------------------- Pack events -------------------- */
  size_t idx_bytes_len = 0;
  uint8_t* idx_bytes = NULL;
  uint8_t amp_mode = 0;
  uint8_t* amp_blob = NULL;
  size_t amp_blob_n = 0;

  if (ne > 0) {
    szy_bw_t tmp;
    szy_bw_init(&tmp);
    int prc = pack_event_indices(&tmp, ev_idx, ne);
    if (prc != SZY_OK) { szy_bw_free(&tmp); free(payload); free(ev_idx); free(ev_amp); return prc; }
    idx_bytes = szy_bw_steal(&tmp, &idx_bytes_len);
    if (!idx_bytes && idx_bytes_len) { free(payload); free(ev_idx); free(ev_amp); return SZY_E_OOM; }

    double terr = eps_on ? cfg->target_abs_err : 1.0;
    prc = pack_event_amps_adaptive(ev_amp, ne, terr, &amp_mode, &amp_blob, &amp_blob_n);
    if (prc != SZY_OK) {
      free(idx_bytes); free(payload); free(ev_idx); free(ev_amp);
      return prc;
    }
  }
  free(ev_idx);
  free(ev_amp);

  /* -------------------- Write tile header -------------------- */
  int rc = SZY_OK;

  if (szy_bw_u16le(out_tile, (uint16_t)h)) { rc = SZY_EINVAL; goto tile_cleanup; }
  if (szy_bw_u16le(out_tile, (uint16_t)w)) { rc = SZY_EINVAL; goto tile_cleanup; }
  if (szy_bw_u8(out_tile, (uint8_t)TREND_NONE)) { rc = SZY_EINVAL; goto tile_cleanup; }
  if (szy_bw_u8(out_tile, (uint8_t)cfg->qbits)) { rc = SZY_EINVAL; goto tile_cleanup; }
  if (szy_bw_u8(out_tile, (uint8_t)cfg->qmode)) { rc = SZY_EINVAL; goto tile_cleanup; }
  if (szy_bw_u8(out_tile, (uint8_t)(cfg->events_on ? EVENTS_ON : EVENTS_OFF))) { rc = SZY_EINVAL; goto tile_cleanup; }
  if (szy_bw_u8(out_tile, (uint8_t)cfg->ent)) { rc = SZY_EINVAL; goto tile_cleanup; }
  if (szy_bw_u8(out_tile, flags)) { rc = SZY_EINVAL; goto tile_cleanup; }
  if (szy_bw_u8(out_tile, (uint8_t)PRED_LORENZO2D)) { rc = SZY_EINVAL; goto tile_cleanup; }

  if (szy_bw_u8(out_tile, 0)) { rc = SZY_EINVAL; goto tile_cleanup; }

  if (szy_bw_f64le(out_tile, inv_scale)) { rc = SZY_EINVAL; goto tile_cleanup; }
  if (szy_bw_f64le(out_tile, mu)) { rc = SZY_EINVAL; goto tile_cleanup; }
  if (szy_bw_f32le(out_tile, clip_rate)) { rc = SZY_EINVAL; goto tile_cleanup; }
  if (szy_bw_i16le(out_tile, bias)) { rc = SZY_EINVAL; goto tile_cleanup; }

  if (szy_bw_u8(out_tile, 0)) { rc = SZY_EINVAL; goto tile_cleanup; }

  {
    uint8_t tmp10[10];
    size_t vn = szy_varuint_encode((uint64_t)ne, tmp10);
    if (szy_bw_put(out_tile, tmp10, vn)) { rc = SZY_EINVAL; goto tile_cleanup; }
    vn = szy_varuint_encode((uint64_t)idx_bytes_len, tmp10);
    if (szy_bw_put(out_tile, tmp10, vn)) { rc = SZY_EINVAL; goto tile_cleanup; }
    if (idx_bytes_len && szy_bw_put(out_tile, idx_bytes, idx_bytes_len)) { rc = SZY_EINVAL; goto tile_cleanup; }
  }

  if (szy_bw_u8(out_tile, amp_mode)) { rc = SZY_EINVAL; goto tile_cleanup; }
  if (amp_blob_n && szy_bw_put(out_tile, amp_blob, amp_blob_n)) { rc = SZY_EINVAL; goto tile_cleanup; }

  {
    uint8_t tmp10[10];
    size_t vn = szy_varuint_encode((uint64_t)payload_n, tmp10);
    if (szy_bw_put(out_tile, tmp10, vn)) { rc = SZY_EINVAL; goto tile_cleanup; }
    if (payload_n && szy_bw_put(out_tile, payload, payload_n)) { rc = SZY_EINVAL; goto tile_cleanup; }
  }

tile_cleanup:
  free(idx_bytes);
  free(amp_blob);
  free(payload);
  return rc;
}
/* -------------------- PARALLEL TILE ENCODE -------------------- */

typedef struct {
  const double* x;
  int H, W, B, ntx, ntiles;
  const szy_enc_cfg_t* cfg;

  uint8_t** tile_ptrs;
  uint32_t* tile_lens;
  int* fallback_flags;  /* NEW: Track fallback per tile */

#ifdef _WIN32
  volatile LONG next_ti;
  volatile LONG err;
#else
  int next_ti;
  int err;
  pthread_mutex_t mu;
#endif
} szy_enc_tp_ctx_t;

#ifdef _WIN32
static DWORD WINAPI enc_worker_win(LPVOID arg) {
  szy_enc_tp_ctx_t* c = (szy_enc_tp_ctx_t*)arg;

  size_t tile_npix = (size_t)c->B * (size_t)c->B;
  double* tile_buf = (double*)malloc(tile_npix * sizeof(double));
  if (!tile_buf) {
    InterlockedCompareExchange(&c->err, (LONG)SZY_E_OOM, 0);
    return 0;
  }

  for (;;) {
    if (InterlockedCompareExchange(&c->err, 0, 0) != 0) break;

    LONG ti = InterlockedIncrement(&c->next_ti) - 1;

    if (InterlockedCompareExchange(&c->err, 0, 0) != 0) break;
    if (ti >= (LONG)c->ntiles) break;

    int ty = (int)ti / c->ntx;
    int tx = (int)ti % c->ntx;

    for (int i = 0; i < c->B; i++) {
      const double* src_row = c->x + (size_t)(ty * c->B + i) * (size_t)c->W + (size_t)(tx * c->B);
      memcpy(tile_buf + (size_t)i * (size_t)c->B, src_row, (size_t)c->B * sizeof(double));
    }

    szy_bw_t tile_rec;
    szy_bw_init(&tile_rec);
    
    int fallback_used = 0;
    int rc = pack_tile_with_fallback(tile_buf, c->B, c->cfg, &tile_rec, &fallback_used);
    
    if (rc != SZY_OK) {
      szy_bw_free(&tile_rec);
      InterlockedCompareExchange(&c->err, (LONG)rc, 0);
      break;
    }
    
    if (tile_rec.n == 0 || tile_rec.n > UINT32_MAX) {
      szy_bw_free(&tile_rec);
      InterlockedCompareExchange(&c->err, (LONG)SZY_E_OVERFLOW, 0);
      break;
    }

    c->tile_lens[(int)ti] = (uint32_t)tile_rec.n;
    c->tile_ptrs[(int)ti] = szy_bw_steal(&tile_rec, NULL);
    c->fallback_flags[(int)ti] = fallback_used;
    szy_bw_free(&tile_rec);

    if (!c->tile_ptrs[(int)ti] && c->tile_lens[(int)ti]) {
      InterlockedCompareExchange(&c->err, (LONG)SZY_E_OOM, 0);
      break;
    }
  }

  free(tile_buf);
  return 0;
}
#else
static void* enc_worker_posix(void* arg) {
  szy_enc_tp_ctx_t* c = (szy_enc_tp_ctx_t*)arg;

  size_t tile_npix = (size_t)c->B * (size_t)c->B;
  double* tile_buf = (double*)malloc(tile_npix * sizeof(double));
  if (!tile_buf) {
    pthread_mutex_lock(&c->mu);
    c->err = SZY_E_OOM;
    pthread_mutex_unlock(&c->mu);
    return NULL;
  }

  for (;;) {
    int ti;

    pthread_mutex_lock(&c->mu);
    if (c->err) { pthread_mutex_unlock(&c->mu); break; }
    ti = c->next_ti++;
    pthread_mutex_unlock(&c->mu);

    if (ti >= c->ntiles) break;

    int ty = ti / c->ntx;
    int tx = ti % c->ntx;

    for (int i = 0; i < c->B; i++) {
      const double* src_row = c->x + (size_t)(ty * c->B + i) * (size_t)c->W + (size_t)(tx * c->B);
      memcpy(tile_buf + (size_t)i * (size_t)c->B, src_row, (size_t)c->B * sizeof(double));
    }

    szy_bw_t tile_rec;
    szy_bw_init(&tile_rec);
    
    int fallback_used = 0;
    int rc = pack_tile_with_fallback(tile_buf, c->B, c->cfg, &tile_rec, &fallback_used);
    
    if (rc != SZY_OK) {
      szy_bw_free(&tile_rec);
      pthread_mutex_lock(&c->mu);
      c->err = rc;
      pthread_mutex_unlock(&c->mu);
      break;
    }
    
    if (tile_rec.n == 0 || tile_rec.n > UINT32_MAX) {
      szy_bw_free(&tile_rec);
      pthread_mutex_lock(&c->mu);
      c->err = SZY_E_OVERFLOW;
      pthread_mutex_unlock(&c->mu);
      break;
    }

    c->tile_lens[ti] = (uint32_t)tile_rec.n;
    c->tile_ptrs[ti] = szy_bw_steal(&tile_rec, NULL);
    c->fallback_flags[ti] = fallback_used;
    szy_bw_free(&tile_rec);

    if (!c->tile_ptrs[ti] && c->tile_lens[ti]) {
      pthread_mutex_lock(&c->mu);
      c->err = SZY_E_OOM;
      pthread_mutex_unlock(&c->mu);
      break;
    }
  }

  free(tile_buf);
  return NULL;
}
#endif

static int encode_tiles_parallel(
  const double* x, int H2, int W2, int B,
  int nty, int ntx, int ntiles,
  const szy_enc_cfg_t* cfg,
  int num_workers,
  uint32_t** out_tile_lengths,
  szy_bw_t* out_tiles_blob,
  int* out_fallback_count  /* NEW */
) {
  (void)H2;
  (void)nty;
  if (!out_tile_lengths || !out_tiles_blob) return SZY_EINVAL;
  *out_tile_lengths = NULL;

  uint8_t** tile_ptrs = (uint8_t**)calloc((size_t)ntiles, sizeof(uint8_t*));
  uint32_t* tile_lens = (uint32_t*)malloc((size_t)ntiles * sizeof(uint32_t));
  int* fallback_flags = (int*)calloc((size_t)ntiles, sizeof(int));
  
  if (!tile_ptrs || !tile_lens || !fallback_flags) { 
    free(tile_ptrs); free(tile_lens); free(fallback_flags); 
    return SZY_E_OOM; 
  }
  memset(tile_lens, 0, (size_t)ntiles * sizeof(uint32_t));

  szy_enc_tp_ctx_t ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.x = x;
  ctx.H = H2; ctx.W = W2; ctx.B = B;
  ctx.ntx = ntx;
  ctx.ntiles = ntiles;
  ctx.cfg = cfg;
  ctx.tile_ptrs = tile_ptrs;
  ctx.tile_lens = tile_lens;
  ctx.fallback_flags = fallback_flags;

#ifdef _WIN32
  ctx.next_ti = 0;
  ctx.err = 0;

  int nworkers = num_workers;
  if (nworkers > ntiles) nworkers = ntiles;
  if (nworkers < 1) nworkers = 1;

  HANDLE* threads = (HANDLE*)malloc((size_t)nworkers * sizeof(HANDLE));
  if (!threads) { free(tile_ptrs); free(tile_lens); free(fallback_flags); return SZY_E_OOM; }

  int created = 0;
  for (int i=0; i<nworkers; i++) {
    threads[i] = CreateThread(NULL, 0, enc_worker_win, &ctx, 0, NULL);
    if (!threads[i]) {
      InterlockedCompareExchange(&ctx.err, (LONG)SZY_E_TILE, 0);
      break;
    }
    created++;
  }

  if (created > 0) {
    WaitForMultipleObjects((DWORD)created, threads, TRUE, INFINITE);
    for (int i=0; i<created; i++) CloseHandle(threads[i]);
  }
  free(threads);

  int err = (ctx.err == 0) ? SZY_OK : (int)ctx.err;
  if (err != SZY_OK) {
    for (int i=0; i<ntiles; i++) free(tile_ptrs[i]);
    free(tile_ptrs);
    free(tile_lens);
    free(fallback_flags);
    return err;
  }

#else
  ctx.next_ti = 0;
  ctx.err = SZY_OK;
  pthread_mutex_init(&ctx.mu, NULL);

  int nworkers = num_workers;
  if (nworkers > ntiles) nworkers = ntiles;
  if (nworkers < 1) nworkers = 1;

  pthread_t* threads = (pthread_t*)malloc((size_t)nworkers * sizeof(pthread_t));
  if (!threads) {
    pthread_mutex_destroy(&ctx.mu);
    free(tile_ptrs); free(tile_lens); free(fallback_flags);
    return SZY_E_OOM;
  }

  int created = 0;
  for (int i=0; i<nworkers; i++) {
    if (pthread_create(&threads[i], NULL, enc_worker_posix, &ctx) != 0) {
      pthread_mutex_lock(&ctx.mu);
      ctx.err = SZY_E_TILE;
      pthread_mutex_unlock(&ctx.mu);
      break;
    }
    created++;
  }

  for (int i=0; i<created; i++) pthread_join(threads[i], NULL);
  free(threads);
  pthread_mutex_destroy(&ctx.mu);

  if (ctx.err != SZY_OK) {
    for (int i=0; i<ntiles; i++) free(tile_ptrs[i]);
    free(tile_ptrs);
    free(tile_lens);
    free(fallback_flags);
    return ctx.err;
  }
#endif

  /* Count fallbacks */
  int total_fallback = 0;
  for (int i=0; i<ntiles; i++) {
    if (fallback_flags[i] > 0) total_fallback++;
  }
  if (out_fallback_count) *out_fallback_count = total_fallback;

  /* Concatenate tiles */
  uint64_t sum = 0;
  for (int i=0; i<ntiles; i++) sum += (uint64_t)tile_lens[i];
  if (sum > SIZE_MAX) {
    for (int i=0; i<ntiles; i++) free(tile_ptrs[i]);
    free(tile_ptrs);
    free(tile_lens);
    free(fallback_flags);
    return SZY_E_OVERFLOW;
  }
  if (szy_bw_reserve(out_tiles_blob, (size_t)sum)) {
    for (int i=0; i<ntiles; i++) free(tile_ptrs[i]);
    free(tile_ptrs);
    free(tile_lens);
    free(fallback_flags);
    return SZY_E_OOM;
  }

  for (int i=0; i<ntiles; i++) {
    if (!tile_ptrs[i] || tile_lens[i] == 0) {
      for (int j=0; j<ntiles; j++) free(tile_ptrs[j]);
      free(tile_ptrs);
      free(tile_lens);
      free(fallback_flags);
      return SZY_E_TILE;
    }
    if (szy_bw_put(out_tiles_blob, tile_ptrs[i], (size_t)tile_lens[i])) {
      for (int j=0; j<ntiles; j++) free(tile_ptrs[j]);
      free(tile_ptrs);
      free(tile_lens);
      free(fallback_flags);
      return SZY_E_OOM;
    }
    free(tile_ptrs[i]);
  }

  free(tile_ptrs);
  free(fallback_flags);
  *out_tile_lengths = tile_lens;
  return SZY_OK;
}
int szy_encode_2d_f64_v8_ex(
  const double* x, int H, int W,
  const szy_enc_cfg_t* cfg,
  int num_workers,
  uint8_t** out_buf, size_t* out_n
) {
  if (!x || !cfg || !out_buf || !out_n) return SZY_EINVAL;
  *out_buf = NULL; *out_n = 0;

  if (!(cfg->qbits == 8 || cfg->qbits == 16)) return SZY_EINVAL;

  if (!(cfg->ent == SZY_ENT_NONE || cfg->ent == SZY_ENT_ZSTD || cfg->ent == SZY_ENT_ZLIB)) return SZY_EINVAL;

  /* Bitplane shuffle nie jest supportowane dla qbits=8 */
  if (cfg->use_bitplane_shuffle && cfg->qbits == 8) return SZY_EINVAL;
  
  /* Nie wspieramy jeszcze nieistniejących entropia modes */
  if (cfg->ent == SZY_ENT_LZMA || cfg->ent == SZY_ENT_AUTO_RATIO || cfg->ent == SZY_ENT_AUTO_FAST) return SZY_EINVAL;

  int B = cfg->block;
  if (B <= 0) return SZY_EINVAL;
  if (H <= 0 || W <= 0) return SZY_EINVAL;

  int H2 = (H / B) * B;
  int W2 = (W / B) * B;
  if (H2 <= 0 || W2 <= 0) return SZY_EINVAL;

  if (H2 > 65535 || W2 > 65535 || B > 65535) return SZY_E_DIMS;

  int nty = H2 / B;
  int ntx = W2 / B;
  int ntiles = nty * ntx;
  if (ntiles <= 0) return SZY_EINVAL;

  int workers = 1;
  int nrc = szy_normalize_num_workers_relaxed(num_workers, &workers);
  if (nrc != SZY_OK) return nrc;

  szy_bw_t tiles_blob;
  szy_bw_init(&tiles_blob);

  uint32_t* tile_lengths = NULL;
  int total_fallback_tiles = 0;

  int use_parallel = (workers > 1 && ntiles >= 16);

  if (use_parallel) {
    int rc = encode_tiles_parallel(x, H2, W2, B, nty, ntx, ntiles, cfg, workers, &tile_lengths, &tiles_blob, &total_fallback_tiles);
    if (rc != SZY_OK) {
      szy_bw_free(&tiles_blob);
      return rc;
    }
  } else {
    size_t tl_bytes = (size_t)ntiles * sizeof(uint32_t);
    if (ntiles > 0 && tl_bytes / sizeof(uint32_t) != (size_t)ntiles) {
      szy_bw_free(&tiles_blob);
      return SZY_E_OVERFLOW;
    }

    tile_lengths = (uint32_t*)malloc(tl_bytes);
    if (!tile_lengths) { szy_bw_free(&tiles_blob); return SZY_E_OOM; }

    size_t tile_npix = (size_t)B * (size_t)B;
    if (tile_npix == 0 || tile_npix > (SIZE_MAX / sizeof(double))) {
      free(tile_lengths);
      szy_bw_free(&tiles_blob);
      return SZY_E_OVERFLOW;
    }

    double* tile_buf = (double*)malloc(tile_npix * sizeof(double));
    if (!tile_buf) {
      free(tile_lengths);
      szy_bw_free(&tiles_blob);
      return SZY_E_OOM;
    }

    int ti = 0;
    for (int ty = 0; ty < nty; ty++) {
      for (int tx = 0; tx < ntx; tx++, ti++) {
        szy_bw_t tile_rec;
        szy_bw_init(&tile_rec);

        for (int i = 0; i < B; i++) {
          const double* src_row = x + (size_t)(ty * B + i) * (size_t)W + (size_t)(tx * B);
          memcpy(tile_buf + (size_t)i * (size_t)B, src_row, (size_t)B * sizeof(double));
        }

        int fallback_used = 0;
        int prc = pack_tile_with_fallback(tile_buf, B, cfg, &tile_rec, &fallback_used);
        
        if (prc != SZY_OK) {
          szy_bw_free(&tile_rec);
          free(tile_buf);
          free(tile_lengths);
          szy_bw_free(&tiles_blob);
          return prc;
        }
        
        if (fallback_used > 0) total_fallback_tiles++;

        if (tile_rec.n == 0) {
          szy_bw_free(&tile_rec);
          free(tile_buf);
          free(tile_lengths);
          szy_bw_free(&tiles_blob);
          return SZY_EINVAL;
        }
        if (tile_rec.n > UINT32_MAX) {
          szy_bw_free(&tile_rec);
          free(tile_buf);
          free(tile_lengths);
          szy_bw_free(&tiles_blob);
          return SZY_E_OVERFLOW;
        }

        tile_lengths[ti] = (uint32_t)tile_rec.n;

        if (szy_bw_put(&tiles_blob, tile_rec.p, tile_rec.n)) {
          szy_bw_free(&tile_rec);
          free(tile_buf);
          free(tile_lengths);
          szy_bw_free(&tiles_blob);
          return SZY_E_OOM;
        }

        szy_bw_free(&tile_rec);
      }
    }

    free(tile_buf);
  }

  /* Log fallback stats (optional) */
  if (total_fallback_tiles > 0) {
    /* Could log or add to metadata */
    (void)total_fallback_tiles;  /* Suppress unused warning for now */
  }

  /* Build index */
  szy_bw_t index_blob;
  szy_bw_init(&index_blob);
  {
    uint8_t tmp10[10];
    size_t vn = szy_varuint_encode((uint64_t)ntiles, tmp10);
    if (szy_bw_put(&index_blob, tmp10, vn)) goto fail_all;

    for (int i = 0; i < ntiles; i++) {
      vn = szy_varuint_encode((uint64_t)tile_lengths[i], tmp10);
      if (szy_bw_put(&index_blob, tmp10, vn)) goto fail_all;
    }
  }

  const char* meta_json = "{\"encoder\":\"szy-c-v9-m1\"}";
  size_t meta_len_sz = strlen(meta_json);
  if (meta_len_sz > 65535) goto fail_all;
  uint16_t meta_len = (uint16_t)meta_len_sz;

  if (index_blob.n > SIZE_MAX - tiles_blob.n) goto fail_all;
  size_t data_blob_n = index_blob.n + tiles_blob.n;

  uint8_t* data_blob = (uint8_t*)malloc(data_blob_n);
  if (!data_blob) goto fail_all;

  memcpy(data_blob, index_blob.p, index_blob.n);
  memcpy(data_blob + index_blob.n, tiles_blob.p, tiles_blob.n);

  uint8_t data_sha[32];
  szy_sha256(data_blob, data_blob_n, data_sha);

  uint8_t flags = 1;
  szy_bw_t out;
  szy_bw_init(&out);

  if (szy_bw_put(&out, MAGIC, 4)) goto fail_out;
  if (szy_bw_u8(&out, VERSION)) goto fail_out;
  if (szy_bw_u8(&out, flags)) goto fail_out;
  if (szy_bw_u16le(&out, (uint16_t)H2)) goto fail_out;
  if (szy_bw_u16le(&out, (uint16_t)W2)) goto fail_out;
  if (szy_bw_u16le(&out, (uint16_t)B)) goto fail_out;
  if (szy_bw_u16le(&out, meta_len)) goto fail_out;
  if (meta_len && szy_bw_put(&out, meta_json, meta_len)) goto fail_out;
  if (szy_bw_put(&out, data_sha, 32)) goto fail_out;

  if (data_blob_n && szy_bw_put(&out, data_blob, data_blob_n)) goto fail_out;

  uint8_t cont_sha[32];
  szy_sha256(out.p, out.n, cont_sha);
  if (szy_bw_put(&out, cont_sha, 32)) goto fail_out;

  *out_buf = szy_bw_steal(&out, out_n);

  free(data_blob);
  szy_bw_free(&index_blob);
  szy_bw_free(&tiles_blob);
  free(tile_lengths);
  return SZY_OK;

fail_out:
  szy_bw_free(&out);
  free(data_blob);
fail_all:
  szy_bw_free(&index_blob);
  szy_bw_free(&tiles_blob);
  free(tile_lengths);
  return SZY_EINVAL;
}

int szy_encode_2d_f64_v8(
  const double* x, int H, int W,
  const szy_enc_cfg_t* cfg,
  uint8_t** out_buf, size_t* out_n
) {
  return szy_encode_2d_f64_v8_ex(x, H, W, cfg, 1, out_buf, out_n);
}