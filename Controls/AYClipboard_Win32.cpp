// AYClipboard_Win32.cpp — Windows clipboard implementation of IClipboard.
// Header-only <Windows.h> use; gated by _WIN32 to keep the rest of the
// codebase free of platform headers.

#include "AYUI/Clipboard.h"

#include <cstring>

#if defined(_WIN32)

#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <Windows.h>

namespace ayt::ui {

namespace {

class Win32Clipboard : public IClipboard {
public:
    bool setText(const std::wstring& text) override {
        if (!::OpenClipboard(nullptr)) return false;
        ::EmptyClipboard();
        const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL mem = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (mem == nullptr) {
            ::CloseClipboard();
            return false;
        }
        void* locked = ::GlobalLock(mem);
        if (locked == nullptr) {
            ::GlobalFree(mem);
            ::CloseClipboard();
            return false;
        }
        std::memcpy(locked, text.c_str(), bytes);
        ::GlobalUnlock(mem);
        // SetClipboardData takes ownership of `mem` on success. On
        // failure we must free it ourselves (Windows re-uses the
        // standard error code we just hit above).
        if (::SetClipboardData(CF_UNICODETEXT, mem) == nullptr) {
            ::GlobalFree(mem);
            ::CloseClipboard();
            return false;
        }
        ::CloseClipboard();
        return true;
    }

    bool getText(std::wstring& out) override {
        out.clear();
        if (!::OpenClipboard(nullptr)) return false;
        HANDLE mem = ::GetClipboardData(CF_UNICODETEXT);
        if (mem == nullptr) {
            ::CloseClipboard();
            return false;
        }
        const wchar_t* locked = static_cast<const wchar_t*>(::GlobalLock(mem));
        if (locked == nullptr) {
            ::CloseClipboard();
            return false;
        }
        out.assign(locked);
        ::GlobalUnlock(mem);
        ::CloseClipboard();
        return true;
    }
};

}  // namespace

// Factory called by getClipboard()'s lazy-init path.
IClipboard* createWin32Clipboard() {
    return new Win32Clipboard();
}

}  // namespace ayt::ui

#endif  // _WIN32
