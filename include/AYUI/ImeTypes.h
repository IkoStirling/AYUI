#pragma once

// =============================================================================
// AYImeTypes.h — Phase C (S4) IME composition event payload
// =============================================================================
//
// AYDevice delivers character + composition events as UTF-8 chunks
// (see AYDevice/include/AYDevice/TextInput.h:36-55). UIManager's device bridge
// (onDeviceChar / onDeviceCompositionStart|Update|End) translates those into
// widget-side calls (onTextInput / onImeCompositionStart|Update|End).
//
// `ImeComposition` is a small POD that bundles the three fields any
// composition update needs. We expose it as a struct rather than three
// loose parameters because (a) it groups related fields, (b) it documents
// the End sentinel via `isEnd()`, and (c) it lets future S4 expansions
// (e.g. selection range within the composition) be added without breaking
// existing call sites.
//
// Why UTF-8 + bytes (not std::wstring + codepoints):
//   - AYDevice's WM_IME_COMPOSITION path (AYWindowManager.cpp:887) decodes
//     WCHAR via WideCharToMultiByte into UTF-8 before forwarding. Round-
//     tripping to wstring + codepoints inside AYUI would require us to
//     guess whether the host is UTF-16 (Windows wchar_t=16) or UTF-32
//     (Linux wchar_t=32). Keeping bytes here until dispatch to the widget
//     lets us defer that decision to the conversion helper, which already
//     has to deal with surrogate pairs (R4 in the Phase C plan).
//
// Why a `bool isEnd()` sentinel instead of std::optional:
//   - Win32's `WM_IME_ENDCOMPOSITION` arrives as `onComposition(nullptr,
//     -1, 0)`. We need a clear "this is the End marker, ignore all other
//     fields" path. A negative `byteCount` is the convention used by the
//     Win32 IME chain (ImmGetCompositionStringW returns negative buffer
//     lengths on no-data); mirroring it in our payload keeps the call
//     sites obvious.
// =============================================================================

#include <string>

namespace ayt::ui {

struct ImeComposition {
    std::string text;       // UTF-8 chunk; empty == "no preview" (use isEnd() to distinguish)
    int caret = 0;          // byte offset of IME cursor within `text` (Win32 GCS_CURSORPOS)
    int byteCount = 0;      // >=0 real length; <0 == End sentinel (Win32 sends -1)

    // True when this payload signals End-of-composition rather than a real
    // update. Hosts should treat `text` / `caret` as undefined when true.
    // We accept two conditions because some hosts (Linux IBus) emit empty
    // text + cursor=0 to signal End, and we want to be tolerant — see
    // UIManager's state-machine logic for the dispatch precedence.
    bool isEnd() const { return byteCount < 0; }
};

} // namespace ayt::ui