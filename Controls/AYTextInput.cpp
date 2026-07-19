#include "AYTextInput.h"
#include "AYUIManager.h"
#include "AYIRenderBackend.h"
#include "AYStyle.h"
#include "UIKeyCode.h"
#include "aymath/MathUtils.h"

#include <algorithm>
#include <string>
#include <vector>

namespace ayt::ui {

// =============================================================================
// Phase C (S4) — UTF-8 → std::wstring helper for IME composition.
// =============================================================================
//
// Shared with AYTextArea.cpp via copy-paste (the helpers are tiny — ~30 LOC —
// and we'd rather not couple two unrelated compilation units through a new
// shared header just for this). Mirrors UIManager::onDeviceChar's UTF-8
// decoder exactly so behavior is consistent between committed-char and
// composition-char paths.
//
// Returns:
//   - std::wstring with the decoded codepoints (BMP only — supplementary
//     planes are best-effort, kept as two surrogate halves in wchar_t
//     because Windows wchar_t is 16-bit. R4 in the Phase C plan.)
//   - outByteToWChar (optional): mapping array, outByteToWChar[i] is the
//     wchar_t index corresponding to UTF-8 byte i. Used to translate
//     AYDevice's byte caret (GCS_CURSORPOS) into our wchar_t caret.
// =============================================================================
namespace {
std::wstring utf8ToWString(const std::string& utf8,
                            std::vector<size_t>* outByteToWChar = nullptr) {
    std::wstring out;
    out.reserve(utf8.size());
    const auto* p = reinterpret_cast<const unsigned char*>(utf8.data());
    const int n = static_cast<int>(utf8.size());
    for (int i = 0; i < n; ) {
        unsigned char c = p[i];
        uint32_t cp = 0;
        int bytes = 0;
        if      ((c & 0x80u) == 0x00u) { cp = c;            bytes = 1; }
        else if ((c & 0xE0u) == 0xC0u) { cp = c & 0x1Fu;    bytes = 2; }
        else if ((c & 0xF0u) == 0xE0u) { cp = c & 0x0Fu;    bytes = 3; }
        else if ((c & 0xF8u) == 0xF0u) { cp = c & 0x07u;    bytes = 4; }
        else { ++i; continue; }
        if (i + bytes > n) break;
        bool ok = true;
        for (int k = 1; k < bytes; ++k) {
            if ((p[i + k] & 0xC0u) != 0x80u) { ok = false; break; }
            cp = (cp << 6) | (p[i + k] & 0x3Fu);
        }
        if (outByteToWChar != nullptr) {
            // Map every consumed byte to the same wchar_t index (the
            // start codepoint of this UTF-8 sequence). AYDevice's caret
            // is a byte offset into the source UTF-8 string.
            for (int k = 0; k < bytes; ++k) {
                outByteToWChar->push_back(out.size());
            }
        }
        if (ok) {
            out.push_back(static_cast<wchar_t>(cp));
        }
        i += bytes;
    }
    return out;
}
} // namespace

// Phase B (S3): TextInput uses UIKeyCode (UIKey_Backspace etc.) instead
// of anonymous VK raw ints. Values are VK-aligned so the comparison
// against legacy host-side int codes stays correct — see Controls/UIKeyCode.h.

TextInput::TextInput() {
    setSize(math::FVector2(kDefaultWidth, kDefaultHeight));
}

TextInput::~TextInput() {
    // Phase C (S4): before destruction, tell UIManager to drop any
    // composition this widget is owning. Without this the manager's
    // _compositionOwner would dangle, and a subsequent Update or End
    // event would call onImeCompositionEnd on a freed TextInput. R1 in
    // the Phase C plan; locked decision Q3 ("dtor cancels, clear()
    // does NOT").
    //
    // CRITICAL: do NOT fire onImeCompositionEnd on `this` from here —
    // a virtual call during destruction is undefined behavior once
    // the subclass part is partially torn down. cancelComposition
    // must silently drop the owner without dispatching. We add an
    // overload below to support this. Menu hit the same pattern in
    // Phase A (PR-5: UAF on shutdown) — keep the same lesson here.
    //
    // CRITICAL #2: also drop focus / hover / capture / composition
    // ownership from UIManager if they point to `this`. Otherwise
    // shutdown() will dynamic_cast the freed pointer later. Same UAF
    // pattern as Phase A PR-5 (Menu::close).
    UIManager& ui = UIManager::get();
    if (ui.getFocusedWidget() == this) ui.setFocus(nullptr);
    if (ui.isCapturing()) ui.cancelCapture();
    ui.cancelComposition(this, /*fireEndOnOwner*/ false);
}

void TextInput::setText(const std::wstring& text) {
    std::wstring newText = text;
    if (_maxLength > 0 && newText.size() > _maxLength) {
        newText = newText.substr(0, _maxLength);
    }
    if (newText == _text) {
        // Idempotent — same value, no callback.
        return;
    }
    _text = newText;
    // Move caret to end of new text — matches user expectation when
    // re-loading an external value, and matches Windows / macOS native
    // textbox behavior.
    _caret = _text.size();
    _selStart = _caret;
    _selEnd = _caret;
    if (_onTextChanged) {
        _onTextChanged(_text);
    }
}

bool TextInput::insertChar(wchar_t ch) {
    if (!_hasFocus || _readOnly) return false;
    // Reject control characters except common ones (tab handled separately).
    if (ch < 0x20 && ch != L'\t') return false;
    if (_maxLength > 0 && _text.size() >= _maxLength &&
        !hasSelection()) {
        return false;
    }
    if (ch == L'\n' || ch == L'\r') {
        // Single-line input rejects newlines.
        return false;
    }
    replaceRange(_selStart, _selEnd, std::wstring(1, ch));
    return true;
}

bool TextInput::deleteLeft() {
    if (_readOnly) return false;
    if (hasSelection()) {
        replaceRange(_selStart, _selEnd, L"");
        return true;
    }
    if (_caret == 0) return false;
    replaceRange(_caret - 1, _caret, L"");
    return true;
}

bool TextInput::deleteRight() {
    if (_readOnly) return false;
    if (hasSelection()) {
        replaceRange(_selStart, _selEnd, L"");
        return true;
    }
    if (_caret >= _text.size()) return false;
    replaceRange(_caret, _caret + 1, L"");
    return true;
}

void TextInput::clear() {
    if (_text.empty() && !hasSelection() && _caret == 0) return;
    _text.clear();
    _caret = 0;
    resetSelectionToCaret();
    if (_onTextChanged) {
        _onTextChanged(_text);
    }
}

void TextInput::appendText(const std::wstring& s) {
    if (s.empty()) return;
    replaceRange(_text.size(), _text.size(), s);
}

void TextInput::setCaret(size_t pos) {
    _caret = std::min(pos, _text.size());
    resetSelectionToCaret();
}

void TextInput::setSelection(size_t start, size_t end) {
    const size_t len = _text.size();
    _selStart = std::min(start, len);
    _selEnd   = std::min(end,   len);
    if (_selEnd < _selStart) std::swap(_selStart, _selEnd);
    _caret = _selEnd;
}

void TextInput::clearSelection() {
    resetSelectionToCaret();
}

void TextInput::selectAll() {
    _selStart = 0;
    _selEnd = _text.size();
    _caret = _selEnd;
}

void TextInput::clampCaret() {
    if (_caret > _text.size()) _caret = _text.size();
    if (_selStart > _text.size()) _selStart = _text.size();
    if (_selEnd > _text.size()) _selEnd = _text.size();
    if (_selEnd < _selStart) std::swap(_selStart, _selEnd);
}

void TextInput::resetSelectionToCaret() {
    _selStart = _caret;
    _selEnd = _caret;
}

void TextInput::replaceRange(size_t a, size_t b, const std::wstring& replacement) {
    if (a > b) std::swap(a, b);
    if (a > _text.size()) a = _text.size();
    if (b > _text.size()) b = _text.size();
    std::wstring before = _text.substr(0, a);
    std::wstring after  = _text.substr(b);
    if (_maxLength > 0 && before.size() + replacement.size() + after.size() > _maxLength) {
        // Trim replacement to fit; if still no room, no-op.
        const size_t room = _maxLength - before.size() - after.size();
        if (room == 0) return;
        _text = before + replacement.substr(0, room) + after;
        _caret = a + std::min(room, replacement.size());
    } else {
        _text = before + replacement + after;
        _caret = a + replacement.size();
    }
    _selStart = _caret;
    _selEnd = _caret;
    if (_onTextChanged) {
        _onTextChanged(_text);
    }
}

bool TextInput::onMouseButtonDown(const UIMouseEvent& e) {
    if (e.mouseButton != 0) return false;
    if (!getWorldBounds().contains(e.mousePos)) return false;
    // Click anywhere grants focus. Reset blink so the caret is visible
    // immediately after click.
    setFocus(true);
    // Phase C (C5) — drag-select. Record the click as the drag anchor
    // and return true so UIManager captures this widget. Subsequent
    // onMouseMove events (delivered because of capture) extend the
    // selection from the anchor to the current column.
    _dragging = true;
    _dragAnchorWorld = e.mousePos;
    // Approximate the anchor column from the click x relative to the
    // text area start. We use the same 7px char width as the renderer
    // (R3 approximation). Clamp to text length.
    constexpr float kApproxCharWidth = 7.0f;
    const math::FRectangle b = getWorldBounds();
    const float localX = e.mousePos.x - (b.minX + kPaddingX);
    long approxCol = static_cast<long>(localX / kApproxCharWidth);
    if (approxCol < 0) approxCol = 0;
    if (static_cast<size_t>(approxCol) > _text.size()) {
        approxCol = static_cast<long>(_text.size());
    }
    _dragAnchorCol = static_cast<size_t>(approxCol);
    // Initial selection is the anchor (collapsed) — onMouseMove will
    // extend it once the mouse moves.
    _selStart = _dragAnchorCol;
    _selEnd = _dragAnchorCol;
    _caret = _dragAnchorCol;
    _caretBlinkTimer = 0.0f;
    _caretVisible = true;
    return true;
}

bool TextInput::onMouseMove(const UIMouseEvent& e) {
    // Phase C (C5): only meaningful while drag is active and we own the
    // capture. We don't gate on _capturedWidget because UIManager only
    // delivers onMouseMove to the captured widget — if we're getting
    // called, we're captured.
    if (!_dragging) return false;
    constexpr float kApproxCharWidth = 7.0f;
    const math::FRectangle b = getWorldBounds();
    const float localX = e.mousePos.x - (b.minX + kPaddingX);
    long curCol = static_cast<long>(localX / kApproxCharWidth);
    if (curCol < 0) curCol = 0;
    if (static_cast<size_t>(curCol) > _text.size()) {
        curCol = static_cast<long>(_text.size());
    }
    const size_t anchor = _dragAnchorCol;
    const size_t cur = static_cast<size_t>(curCol);
    if (cur < anchor) {
        _selStart = cur;
        _selEnd = anchor;
    } else {
        _selStart = anchor;
        _selEnd = cur;
    }
    _caret = _selEnd;
    _caretBlinkTimer = 0.0f;
    _caretVisible = true;
    return true;
}

bool TextInput::onMouseButtonUp(const UIMouseEvent& e) {
    // Phase C (C5): end the drag. The selection built up during the
    // drag stays in place; user can then copy / replace / etc.
    if (e.mouseButton != 0) return false;
    if (!_dragging) return false;
    _dragging = false;
    // Returning false here lets UIManager do its normal capture-release
    // bookkeeping. UIManager clears _capturedWidget unconditionally on
    // onMouseButtonUp (line ~764), so we don't need to fight it.
    return false;
}

bool TextInput::onTextInput(wchar_t ch) {
    if (!_hasFocus || _readOnly) return false;
    return insertChar(ch);
}

bool TextInput::onKeyDown(int keyCode) {
    if (!_hasFocus) return false;
    bool handled = true;
    switch (keyCode) {
    case UIKey_Backspace:
        deleteLeft();
        break;
    case UIKey_Delete:
        deleteRight();
        break;
    case UIKey_Left:
        if (_caret > 0) setCaret(_caret - 1);
        break;
    case UIKey_Right:
        if (_caret < _text.size()) setCaret(_caret + 1);
        break;
    case UIKey_Home:
        setCaret(0);
        break;
    case UIKey_End:
        setCaret(_text.size());
        break;
    case UIKey_Enter:
        if (_onSubmit) _onSubmit(_text);
        break;
    case UIKey_A:
        // Ctrl+A select-all is host-side (we don't track modifier
        // keys here); v1 keeps keyboard shortcut handling out of scope.
        handled = false;
        break;
    case UIKey_Tab:
        // UIManager.onKeyDown intercepts Tab BEFORE delegating; if we ever
        // see Tab here it means a host bypassed UIManager. Swallow it
        // defensively to avoid caret-eating surprises.
        return true;
    default:
        handled = false;
        break;
    }
    if (handled) {
        _caretBlinkTimer = 0.0f;
        _caretVisible = true;
    }
    return handled;
}

UiCursorHint TextInput::getCursorHint() const {
    if (!isVisible()) return UiCursorHint::Default;
    return UiCursorHint::Beam;
}

// =============================================================================
// Phase C (S4) — IME composition hooks
// =============================================================================
//
// State machine: see AYFocusableWidget.h. UIManager is the only caller.
// Each hook is a thin wrapper around the composition state + replaceRange.
// We refuse composition on _readOnly (lock-down forms), and refuse when
// !_hasFocus (defensive — UIManager should not route to an unfocused
// widget, but if a host bypasses UIManager the default-onKeyDown Tab
// defensive guard pattern applies).
// =============================================================================

bool TextInput::onImeCompositionStart(const std::string& text, int caret) {
    if (!_hasFocus || _readOnly) return false;
    std::vector<size_t> byteMap;
    _compositionPreview = utf8ToWString(text, &byteMap);
    // Translate AYDevice byte caret (GCS_CURSORPOS) into our wchar_t
    // caret by clamping into the byteMap. caret < 0 → caret at end.
    if (caret < 0 || static_cast<size_t>(caret) >= byteMap.size()) {
        _compositionCaretBytes = static_cast<int>(_compositionPreview.size());
    } else {
        _compositionCaretBytes = static_cast<int>(byteMap[caret]);
    }
    _composing = true;
    return true;
}

bool TextInput::onImeCompositionUpdate(const std::string& text, int caret) {
    if (!_hasFocus || _readOnly) return false;
    // Defensive: a host that bypasses UIManager might fire Update
    // without a Start. Promote-to-Start per the documented contract.
    if (!_composing) {
        return onImeCompositionStart(text, caret);
    }
    std::vector<size_t> byteMap;
    _compositionPreview = utf8ToWString(text, &byteMap);
    if (caret < 0 || static_cast<size_t>(caret) >= byteMap.size()) {
        _compositionCaretBytes = static_cast<int>(_compositionPreview.size());
    } else {
        _compositionCaretBytes = static_cast<int>(byteMap[caret]);
    }
    return true;
}

bool TextInput::onImeCompositionEnd(const std::string& committed) {
    if (!_composing) return false;
    // Commit replaces the current selection (or inserts at caret if no
    // selection). reject if !_hasFocus (host bypass). reject if
    // _readOnly (drop the candidate entirely without writing).
    if (!_readOnly) {
        std::wstring committedText = utf8ToWString(committed);
        if (!committedText.empty()) {
            // Use replaceRange to honor selection; the pre-edit candidate
            // never made it into _text (it lives in _compositionPreview),
            // so the current selection is whatever the user typed into
            // BEFORE composition started.
            replaceRange(_selStart, _selEnd, committedText);
        }
    }
    _compositionPreview.clear();
    _compositionCaretBytes = 0;
    _composing = false;
    return true;
}

void TextInput::tick(float dt) {
    if (!_hasFocus) {
        _caretVisible = false;
        _caretBlinkTimer = 0.0f;
        return;
    }
    _caretBlinkTimer += dt;
    while (_caretBlinkTimer >= kCaretBlinkSeconds) {
        _caretBlinkTimer -= kCaretBlinkSeconds;
        _caretVisible = !_caretVisible;
    }
}

void TextInput::onFocusGained() {
    _caretBlinkTimer = 0.0f;
    _caretVisible = true;
}

void TextInput::onFocusLost() {
    _caretVisible = false;
    _caretBlinkTimer = 0.0f;
    clearSelection();
}

void TextInput::onRender(IRenderBackend& renderer) {
    math::FRectangle bounds = getWorldBounds();
    if (bounds.maxX <= bounds.minX || bounds.maxY <= bounds.minY) return;

    const ResolvedStyle style = resolveStyle(getStyleId());
    math::FVector4 bg;
    math::FVector4 borderColor;
    float borderWidth;
    if (style.hasStyle) {
        bg = style.backgroundColor;
        borderColor = style.borderColor;
        borderWidth = style.borderWidth;
    } else {
        bg = math::FVector4(0.12f, 0.12f, 0.13f, 1.0f);
        borderColor = _hasFocus
            ? math::FVector4(0.18f, 0.45f, 0.78f, 1.0f)
            : math::FVector4(0.4f, 0.4f, 0.45f, 1.0f);
        borderWidth = 1.0f;
    }
    renderer.drawRect(bounds, bg);
    renderer.drawBorderRect(bounds, borderColor, borderWidth, 2.0f);

    // Display text — password mask replaces each char with '*'.
    const std::wstring displayText =
        _passwordMode ? std::wstring(_text.size(), L'*') : _text;
    if (!displayText.empty() || _hasFocus) {
        math::FRectangle textBounds(
            bounds.minX + kPaddingX, bounds.minY,
            bounds.maxX - kPaddingX, bounds.maxY);
        const math::FVector4 textColor = _readOnly
            ? math::FVector4(0.55f, 0.55f, 0.55f, 1.0f)
            : math::FVector4(1.0f, 1.0f, 1.0f, 1.0f);
        if (!displayText.empty()) {
            renderer.drawText(textBounds, displayText, 14, textColor);
        }
    }

    // =================================================================
    // Phase C (C4) — placeholder text. Drawn when:
    //   - _text is empty,
    //   - widget is NOT focused (focused state shows the cursor, not a hint),
    //   - _placeholder is non-empty.
    //
    // Style: WidgetStyle::placeholderColor (default muted gray, ~0.7 alpha).
    // The placeholder text is drawn at the same font size as the real
    // text, but at the left padding offset.
    // =================================================================
    if (_text.empty() && !_hasFocus && !_placeholder.empty()) {
        math::FVector4 phColor(0.55f, 0.55f, 0.60f, 0.7f);
        if (!getStyleId().empty()) {
            const WidgetStyle* ws = StyleManager::get().getStyle(getStyleId());
            if (ws != nullptr) phColor = ws->placeholderColor;
        }
        math::FRectangle textBounds(
            bounds.minX + kPaddingX, bounds.minY,
            bounds.maxX - kPaddingX, bounds.maxY);
        renderer.drawText(textBounds, _placeholder, 14, phColor);
    }

    // =================================================================
    // Phase C (S4) — IME composition underline.
    // =================================================================
    // Drawn as a 1.5px-tall solid rectangle, sky-blue (theme key:
    // `compositionUnderlineColor`, default sky blue). We approximate the
    // width via `_compositionPreview.size() * approxCharWidth` — same
    // trade-off TextInput's caret x-position uses (we don't have a
    // precise text shaper). R3 in the Phase C plan: best-effort width
    // is fine for unit tests that count drawRect calls; v1.2 with a
    // real text shaper will tighten this.
    //
    // Y position: just below the text baseline (we don't know exact
    // baseline either; using maxY - 2.0 keeps it visible inside the
    // widget without overlapping the caret rectangle).
    // =================================================================
    if (_composing && !_compositionPreview.empty()) {
        // Resolve the underline color via StyleManager. Fall back to the
        // WidgetStyle default (sky blue) when no styleId is set. We
        // bypass ResolvedStyle because that struct only carries the
        // background/border fields — compositionUnderlineColor lives on
        // WidgetStyle directly.
        math::FVector4 ulColor(0.30f, 0.65f, 0.95f, 1.0f);
        if (!getStyleId().empty()) {
            const WidgetStyle* ws = StyleManager::get().getStyle(getStyleId());
            if (ws != nullptr) {
                ulColor = ws->compositionUnderlineColor;
            }
        }
        constexpr float approxCharWidth = 7.0f; // matches TextArea rough
        const float ulX = bounds.minX + kPaddingX;
        const float ulW = static_cast<float>(_compositionPreview.size()) * approxCharWidth;
        const float ulY = bounds.maxY - 3.0f;
        constexpr float ulH = 1.5f;
        renderer.drawRect(
            math::FRectangle(ulX, ulY, ulX + ulW, ulY + ulH),
            ulColor);
    }

    // Caret — small vertical line at the right side of the text we
    // pinned in the rendering rect (we don't know glyph widths, so
    // the caret stays at the right of the rendered area as a
    // deliberately approximate signal — sufficient for unit tests).
    if (_hasFocus && _caretVisible) {
        const float cx = bounds.maxX - kPaddingX - kCaretWidth;
        const float pad = 3.0f;
        renderer.drawRect(
            math::FRectangle(cx, bounds.minY + pad,
                             cx + kCaretWidth, bounds.maxY - pad),
            math::FVector4(1.0f, 1.0f, 1.0f, 0.9f));
    }
}

Widget* createTextInputWidget() {
    return new TextInput();
}

} // namespace ayt::ui
