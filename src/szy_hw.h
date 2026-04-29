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