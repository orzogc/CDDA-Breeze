#include "get_version.h" // IWYU pragma: associated

#if defined(BREEZE_ANDROID_VERSION)

#define STRINGIFY_IMPL(x) #x
#define STRINGIFY(x) STRINGIFY_IMPL(x)
#define VERSION STRINGIFY(BREEZE_ANDROID_VERSION)

#elif (defined(_WIN32) || defined(MINGW)) && !defined(GIT_VERSION) && !defined(CROSS_LINUX) && !defined(_MSC_VER)

#ifndef VERSION
#define VERSION "CDDA-Breeze 12.0"
#endif

#else

#include "version.h"

#endif

const char *getVersionString()
{
    return VERSION;
}
