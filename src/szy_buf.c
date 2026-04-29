#include "szy_buf.h"
#include "szy_endian.h"

#include <stdlib.h>
#include <string.h>
#include <limits.h>

void szy_bw_init(szy_bw_t* w) {
  if (!w) return;
  w->p = NULL; w->n = 0; w->cap = 0;
}

void szy_bw_free(szy_bw_t* w) {
  if (!w) return;
  free(w->p);
  w->p = NULL; w->n = 0; w->cap = 0;
}

int szy_bw_reserve(szy_bw_t* w, size_t extra) {
  if (!w) return -1;
  if (extra > (SIZE_MAX - w->n)) return -1;
  size_t need = w->n + extra;
  if (need <= w->cap) return 0;

  size_t newcap = (w->cap == 0) ? 256 : w->cap;
  while (newcap < need) {
    if (newcap > (SIZE_MAX / 2)) { newcap = need; break; }
    newcap *= 2;
  }

  uint8_t* np = (uint8_t*)realloc(w->p, newcap);
  if (!np) return -1;
  w->p = np;
  w->cap = newcap;
  return 0;
}

int szy_bw_put(szy_bw_t* w, const void* data, size_t len) {
  if (!w || (!data && len)) return -1;
  if (szy_bw_reserve(w, len)) return -1;
  memcpy(w->p + w->n, data, len);
  w->n += len;
  return 0;
}

int szy_bw_u8(szy_bw_t* w, uint8_t v) {
  return szy_bw_put(w, &v, 1);
}

int szy_bw_u16le(szy_bw_t* w, uint16_t v) {
  uint8_t b[2];
  szy_store_u16le(b, v);
  return szy_bw_put(w, b, 2);
}

int szy_bw_i16le(szy_bw_t* w, int16_t v) {
  return szy_bw_u16le(w, (uint16_t)v);
}

int szy_bw_f32le(szy_bw_t* w, float v) {
  uint8_t b[4];
  szy_store_f32le(b, v);
  return szy_bw_put(w, b, 4);
}

int szy_bw_f64le(szy_bw_t* w, double v) {
  uint8_t b[8];
  szy_store_f64le(b, v);
  return szy_bw_put(w, b, 8);
}

uint8_t* szy_bw_steal(szy_bw_t* w, size_t* out_len) {
  if (!w) return NULL;
  uint8_t* p = w->p;
  if (out_len) *out_len = w->n;
  w->p = NULL; w->n = 0; w->cap = 0;
  return p;
}