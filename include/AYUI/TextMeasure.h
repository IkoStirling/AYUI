#pragma once

// =============================================================================
// AYTextMeasure — header-only text width utilities shared by TextInput / TextArea
// -----------------------------------------------------------------------------
// PR-A1: TextInput already had `fallbackCharWidth` + `measurePrefixWidth` in its
// anonymous namespace. This header lifts them to the ayt::ui namespace so
// TextArea (and any future text-bearing widget) can use the same backend-aware
// pipeline instead of multiplying by a hardcoded `7.0f` per character.
//
// Priority: if the UIManager's active IRenderBackend returns a non-zero
// `TextMetrics.width` from `measureText`, use that. Otherwise fall back to a
// rough per-character width (`7.0f` for ASCII, `fontSize` for non-ASCII —
// matches the pre-PR estimate so behaviour without a real backend is
// unchanged).
//
// Why header-only: this is hot-path code (caret positioning, selection rects,
// IME underline width). Avoiding a .cpp + .lib round-trip keeps each call site
// inlinable; the helper is small enough to live in headers alongside
// AYThickness.h / AYTextContent.h.
// =============================================================================

#include "AYUI/UIManager.h"
#include "AYUI/IRenderBackend.h"

#include <string>

namespace ayt::ui {

// Default font size for text-bearing widgets. Mirrors TextInput's
// pre-extraction `kTextFontSize = 14`. TextArea's `fontSize` field is the same
// value (see AYTextArea.cpp:onRender).
constexpr int kDefaultTextFontSize = 14;

// Rough fallback used only when the backend's measureText returns 0 width
// (unit tests, MockRenderer, no active UIManager). Mirrors the pre-PR
// TextInput behaviour.
inline float fallbackCharWidth(wchar_t ch, int fontSize = kDefaultTextFontSize) {
    return (ch < 0x100) ? 7.0f : static_cast<float>(fontSize);
}

// Measure the width of `text[0..n)` (n clamped to text.size()).
//
// `backend` (optional) — if null, falls back to UIManager::tryGet()->backend().
// Tests can inject a custom backend here without going through the UIManager
// singleton.
//
// Returns 0.0f if text is empty or n is 0.
inline float measurePrefixWidth(const std::wstring& text, size_t n,
                                IRenderBackend* backend = nullptr,
                                int fontSize = kDefaultTextFontSize) {
    if (n == 0 || text.empty()) return 0.0f;
    if (n > text.size()) n = text.size();
    if (backend == nullptr) {
        if (UIManager* ui = UIManager::tryGet()) {
            backend = ui->backend();
        }
    }
    if (backend != nullptr) {
        const IRenderBackend::TextMetrics m =
            backend->measureText(text.substr(0, n), fontSize);
        if (m.width > 0.0f) {
            return m.width;
        }
    }
    float w = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        w += fallbackCharWidth(text[i], fontSize);
    }
    return w;
}

}  // namespace ayt::ui
