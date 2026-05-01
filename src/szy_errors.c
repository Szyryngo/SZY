/*
 * SZY_v8 Compressor
 * Copyright (C) 2026 Robert Szyryngo
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * ---
 * COMMERCIAL LICENSE
 * If you want to use this software in proprietary, closed-source projects
 * or in any other way that does not comply with the AGPLv3 license,
 * a commercial license is required. Contact robert.szyryngo@gmail.com for pricing.
 */
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
