#include "szy_hw.h"

#ifdef _WIN32
  #include <windows.h>
  #ifndef ALL_PROCESSOR_GROUPS
    #define ALL_PROCESSOR_GROUPS 0xFFFF
  #endif
#else
  #include <unistd.h>
#endif

SZY_API int szy_hw_threads(void) {
#ifdef _WIN32
  /* POPRAWKA #8: Fallback dla Windows XP/Vista */
  typedef DWORD (WINAPI *GetActiveProcessorCountFunc)(WORD);
  HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
  if (kernel32) {
    GetActiveProcessorCountFunc func = (GetActiveProcessorCountFunc)
        GetProcAddress(kernel32, "GetActiveProcessorCount");
    if (func) {
      DWORD n = func(ALL_PROCESSOR_GROUPS);
      return (n > 0) ? (int)n : 1;
    }
  }
  /* Fallback: użyj starej metody
     WARNING: Na systemach HyperThreading zwraca liczbę rdzeni logicznych (2x fizycznych) */
  SYSTEM_INFO sysinfo;
  GetSystemInfo(&sysinfo);
  return (int)sysinfo.dwNumberOfProcessors;
#else
  long n = sysconf(_SC_NPROCESSORS_ONLN);
  return (n > 0) ? (int)n : 1;
#endif
}

static int normalize_impl(int requested, int* out_num, int strict) {
  if (!out_num) return SZY_EINVAL;
  if (requested < 0) return SZY_EINVAL;

  int hw = szy_hw_threads();
  if (hw < 1) hw = 1;

  int use = (requested == 0) ? hw : requested;
  if (use < 1) use = 1;

  if (use > hw) {
    if (strict) return SZY_E_THREADS;
    use = hw; /* relaxed clamp */
  }

  *out_num = use;
  return SZY_OK;
}

int szy_normalize_num_workers_relaxed(int requested, int* out_num) {
  return normalize_impl(requested, out_num, 0);
}

int szy_normalize_num_workers_strict(int requested, int* out_num) {
  return normalize_impl(requested, out_num, 1);
}