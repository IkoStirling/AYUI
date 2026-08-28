#pragma once

// =============================================================================
// AYClipboard — cross-widget clipboard abstraction
// -----------------------------------------------------------------------------
// PR-A2: Pre-PR, TextInput's Win32 clipboard code lived in an anonymous
// namespace inside AYTextInput.cpp (clipboardSetText / clipboardGetText).
// TextArea had no clipboard support at all. PR-A2 lifts the interface so
//   1. TextInput can reuse it (no duplicate Win32 boilerplate)
//   2. TextArea::TextDocument can wire Ctrl+C/X/V without re-implementing
//      the OpenClipboard / GlobalAlloc dance
//   3. Tests can inject a mock IClipboard (so we can assert multi-line
//      paste behaviour in TextArea without touching the real Windows
//      clipboard between cases)
//
// API surface:
//   IClipboard        — virtual interface
//   getClipboard()    — returns the active impl (lazy static fallback)
//   setClipboardImpl()— host/test swaps the impl; pass nullptr to reset
//
// The Win32 implementation lives in AYClipboard_Win32.cpp (out-of-line
// because <Windows.h> would otherwise pollute every translation unit that
// pulls in this header).
// =============================================================================

#include <string>

namespace ayt::ui {

class IClipboard {
public:
    virtual ~IClipboard() = default;

    // Set the OS clipboard text. Returns true on success, false on
    // platform / allocation error. Pre-PR behaviour for TextInput was
    // silent on failure (the helper returned false and TextInput
    // ignored it); PR-A2 keeps that contract.
    virtual bool setText(const std::wstring& text) = 0;

    // Get the OS clipboard text into `out`. Returns true on success,
    // false on empty / non-text clipboard / platform error. On false
    // `out` is left empty.
    virtual bool getText(std::wstring& out) = 0;
};

// Returns the currently-active clipboard impl. The first call lazily
// initialises a platform-default impl (Win32, macOS pbcopy/pbpaste,
// Wayland wl-clipboard, or X11 xclip/xsel).
IClipboard& getClipboard();

// Replaces the active impl. Pass nullptr to clear back to the lazy
// default (next getClipboard() call will re-create a default impl).
// Ownership transfers to AYUI. Pass a heap object; setClipboardImpl(nullptr)
// destroys it and restores lazy platform selection on the next access.
void setClipboardImpl(IClipboard* impl);

}  // namespace ayt::ui
