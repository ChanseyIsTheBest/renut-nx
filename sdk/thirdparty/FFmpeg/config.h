#if defined(WIN32) || defined(_WIN32)
#if defined(__aarch64__) || defined(_M_ARM64)
#include "config_windows_aarch64.h"
#else
#include "config_windows_x86_64.h"
#endif
#elif defined(__APPLE__) && defined(__MACH__)
#if defined(__aarch64__) || defined(__arm64__)
#include "config_macos_aarch64.h"
#else
#include "config_macos_x86_64.h"
#endif
#elif defined(__SWITCH__)
/* Nintendo Switch (devkitA64 + libnx), as in nfsmw-nx. */
#include "config_switch_aarch64.h"
#elif defined(__gnu_linux__)
#if defined(__aarch64__)
#include "config_linux_aarch64.h"
#else
#include "config_linux_x86_64.h"
#endif
#elif defined(__ANDROID__)
#if defined(__aarch64__)
#include "config_android_aarch64.h"
#elif defined(__amd64__) || defined(__x86_64__)
#include "config_android_x86_64.h"
#else
#error "android arch unsupported"
#endif
#else
#error "no config"
#endif

/* renut-ultimate (2026-11-23, Katamari's movies: app/src/cpu/katamari_wmv_ffmpeg.cpp): the fork addition's WMV3 /
   VC-1 decoders (config_masseffect_wmv3.h), whose sources sdk/thirdparty/CMakeLists.txt always compiled; culdcept-nx,
   banjo-nx, vivapinata-nx and nights-nx include it here the same way. */
#include "config_masseffect_wmv3.h"
