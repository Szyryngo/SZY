#include "szy_bytes.h"
#include "szy_endian.h"

#include <string.h>

static int rd_need(szy_rd_t* r, size_t k) {
  return (r && r->pos + k <= r->n) ? 0 : -1;
}

int szy_rd_init(szy_rd_t* r, const uint8_t* p, size_t n, size_t pos0) {
  if (!r || !p) return -1;
  if (pos0 > n) return -1;
  r->p = p;
  r->n = n;
  r->pos = pos0;
  return 0;
}

int szy_rd_u8(szy_rd_t* r, uint8_t* out) {
  if (rd_need(r, 1)) return -1;
  if (out) *out = r->p[r->pos];
  r->pos += 1;
  return 0;
}

int szy_rd_u16le(szy_rd_t* r, uint16_t* out) {
  if (rd_need(r, 2)) return -1;
  const uint8_t* q = r->p + r->pos;
  if (out) *out = szy_load_u16le(q);
  r->pos += 2;
  return 0;
}

int szy_rd_i16le(szy_rd_t* r, int16_t* out) {
  uint16_t u;
  if (szy_rd_u16le(r, &u)) return -1;
  if (out) *out = (int16_t)u;
  return 0;
}

int szy_rd_u32le(szy_rd_t* r, uint32_t* out) {
  if (rd_need(r, 4)) return -1;
  const uint8_t* q = r->p + r->pos;
  if (out) *out = szy_load_u32le(q);
  r->pos += 4;
  return 0;
}

int szy_rd_f32le(szy_rd_t* r, float* out) {
  if (rd_need(r, 4)) return -1;
  const uint8_t* q = r->p + r->pos;
  float v = szy_load_f32le(q);
  if (out) *out = v;
  r->pos += 4;
  return 0;
}

int szy_rd_f64le(szy_rd_t* r, double* out) {
  if (rd_need(r, 8)) return -1;
  const uint8_t* q = r->p + r->pos;
  double v = szy_load_f64le(q);
  if (out) *out = v;
  r->pos += 8;
  return 0;
}

int szy_rd_bytes(szy_rd_t* r, size_t k, const uint8_t** out) {
  if (rd_need(r, k)) return -1;
  if (out) *out = r->p + r->pos;
  r->pos += k;
  return 0;
}

int szy_rd_skip(szy_rd_t* r, size_t k) {
  if (rd_need(r, k)) return -1;
  r->pos += k;
  return 0;
}