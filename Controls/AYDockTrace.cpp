#include "AYUI/DockTrace.h"

#include <cstdio>

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace ayt::ui {

// Only the path resolution needs Win32. Kept out-of-line so DockTrace.h
// (a public header included by every widget TU) does not drag <windows.h>
// into consumer translation units — see audit B-1.
const char* dockTracePath() {
    static char path[512] = {};
    if (path[0] != '\0') {
        return path;
    }
#if defined(_WIN32)
    char tmp[MAX_PATH];
    const DWORD n = GetTempPathA(MAX_PATH, tmp);
    if (n == 0 || n >= MAX_PATH - 32) {
        std::snprintf(path, sizeof(path), "ay_dock_trace.log");
    } else {
        std::snprintf(path, sizeof(path), "%say_dock_trace.log", tmp);
    }
#else
    std::snprintf(path, sizeof(path), "/tmp/ay_dock_trace.log");
#endif
    return path;
}

} // namespace ayt::ui
