#include "szy_api.h"
#include "szy_rc.h"

SZY_API const char* szy_strerror(int code) {
  switch (code) {
    case SZY_OK:          return "Success";
    case SZY_EINVAL:      return "Invalid argument or bad/corrupt format";
    case SZY_E_SHA_CONT:  return "Container SHA256 mismatch";
    case SZY_E_SHA_DATA:  return "Data SHA256 mismatch";
    case SZY_E_INDEX:     return "Index corrupted / tile lengths invalid";
    case SZY_E_DIMS:      return "Invalid dimensions (H/W/B)";
    case SZY_E_BLOCK:     return "Dimensions not divisible by block size";
    case SZY_E_TILECOUNT: return "Tile count mismatch";
    case SZY_E_OVERFLOW:  return "Size overflow";
    case SZY_E_OOM:       return "Out of memory";
    case SZY_E_TILE:      return "Tile decode error";
    case SZY_E_THREADS:   return "Requested thread count exceeds available hardware threads";
    case SZY_E_BOUNDS:    return "Requested bounded-error guarantee cannot be satisfied with current settings";
    default:              return "Unknown error";
  }
}