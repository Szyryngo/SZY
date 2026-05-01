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
