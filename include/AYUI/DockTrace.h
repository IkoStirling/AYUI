#pragma once

// Lightweight DockArea drag/drop trace. Always appends to
// %TEMP%\ay_dock_trace.log (and stderr). Delete the file between repros.
//
// This header is PUBLIC (transitively included from AYUI.h via
// DockArea.h). It deliberately does NOT include <windows.h> so consumer
// TUs do not pick up HANDLE/HWND/DWORD into their global namespace —
// the Win32 path lookup lives in Controls/AYDockTrace.cpp (audit B-1).
// See Clipboard.h for the same discipline.

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace ayt::ui {

// PR-InputTrace: env-gated input trace switch. Set AYUI_TRACE_INPUT=1
// before launching (GUI apps launched from Git Bash lose stderr, so the
// callers pair this with dockTrace's file-backed sink). Cached per process.
inline bool ayuiTraceInputEnabled()
{
    static int cached = -1;
    if (cached < 0) {
        const char* env = std::getenv("AYUI_TRACE_INPUT");
        cached = (env != nullptr && env[0] != '\0' && env[0] != '0') ? 1 : 0;
    }
    return cached != 0;
}

// Resolves the absolute log file path (Win32: %TEMP%\ay_dock_trace.log,
// POSIX: /tmp/ay_dock_trace.log). Definition in Controls/AYDockTrace.cpp.
const char* dockTracePath();

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
