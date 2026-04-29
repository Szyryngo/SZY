#pragma once
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  SZY_OK           = 0,
  SZY_EINVAL       = -1,
  SZY_E_SHA_CONT   = -2,
  SZY_E_SHA_DATA   = -3,
  SZY_E_INDEX      = -4,
  SZY_E_DIMS       = -5,
  SZY_E_BLOCK      = -6,
  SZY_E_TILECOUNT  = -7,
  SZY_E_OVERFLOW   = -8,
  SZY_E_OOM        = -9,
  SZY_E_TILE       = -10,
  SZY_E_THREADS    = -11,
  SZY_E_BOUNDS     = -12
} szy_rc_t;

#ifdef __cplusplus
}
#endif