// AYClipboard.cpp — interface + active-impl dispatch
// Platform implementations live in AYClipboard_Win32.cpp and
// AYClipboard_Posix.cpp. Keeping dispatch here avoids leaking platform
// headers into widgets that only consume IClipboard.

#include "AYUI/Clipboard.h"

#include <memory>
#include <string>

namespace ayt::ui {

namespace {

// Pointer to the active impl. Lazy-initialised on first getClipboard()
// call. Owned by `std::unique_ptr` so destruction in dtor is clean.
std::unique_ptr<IClipboard> g_clipboard;

}  // namespace

void setClipboardImpl(IClipboard* impl) {
    g_clipboard.reset(impl);
}

IClipboard& getClipboard() {
    if (g_clipboard == nullptr) {
#if defined(_WIN32)
        // Lazy include via the Win32 impl TU (which already pulls
        // <Windows.h> behind a guard). We avoid a separate .cpp to
        // keep the impl translation unit singular.
        extern IClipboard* createWin32Clipboard();
        g_clipboard.reset(createWin32Clipboard());
#else
        extern IClipboard* createPosixClipboard();
        g_clipboard.reset(createPosixClipboard());
#endif
    }
    return *g_clipboard;
}

}  // namespace ayt::ui
