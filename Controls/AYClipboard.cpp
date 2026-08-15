// AYClipboard.cpp — interface + active-impl dispatch
// Implementation lives in AYClipboard_Win32.cpp (out-of-line <Windows.h>).
// POSIX / Linux stub lives in this file so callers always link.

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
        // POSIX stub — getText returns false, setText returns false.
        // Pre-PR TextInput on Linux had the same behaviour via the
        // #else branch in AYTextInput.cpp's anonymous namespace.
        class StubClipboard : public IClipboard {
        public:
            bool setText(const std::wstring&) override { return false; }
            bool getText(std::wstring& out) override {
                out.clear();
                return false;
            }
        };
        g_clipboard.reset(new StubClipboard());
#endif
    }
    return *g_clipboard;
}

}  // namespace ayt::ui
