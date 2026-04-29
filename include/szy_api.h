#pragma once

#ifdef _WIN32
  #ifdef SZY_BUILD_DLL
    #define SZY_API __declspec(dllexport)
  #else
    #define SZY_API __declspec(dllimport)
  #endif
#else
  #define SZY_API
#endif