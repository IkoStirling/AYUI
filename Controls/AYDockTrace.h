#pragma once

// Lightweight DockArea drag/drop trace. Always appends to
// %TEMP%\ay_dock_trace.log (and stderr). Delete the file between repros.

#include <cstdarg>
#include <cstdio>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace ayt::ui {

inline const char* dockTracePath() {
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

inline void dockTrace(const char* fmt, ...) {
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    std::fprintf(stderr, "%s", line);
    if (FILE* f = std::fopen(dockTracePath(), "a")) {
        std::fputs(line, f);
        std::fclose(f);
    }
}

inline const char* dockSlotName(int slot) {
    switch (slot) {
        case 0: return "Left";
        case 1: return "Right";
        case 2: return "Top";
        case 3: return "Bottom";
        case 4: return "Center";
        default: return "Count/Invalid";
    }
}

} // namespace ayt::ui
