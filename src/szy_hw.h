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
#include "szy_api.h"
#include "szy_rc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Returns number of online hardware threads, at least 1.
   Exported (engine-level helper), useful for clients (C/C++/Python).
*/
SZY_API int szy_hw_threads(void);

/* requested:
     0  -> auto (hw threads)
    >0  -> use requested
    <0  -> invalid

   Relaxed: if requested > hw -> clamp to hw (good for legacy APIs).
*/
int szy_normalize_num_workers_relaxed(int requested, int* out_num);

/* Strict: if requested > hw -> return SZY_E_THREADS (good for *_ex APIs). */
int szy_normalize_num_workers_strict(int requested, int* out_num);

#ifdef __cplusplus
}
#endif
