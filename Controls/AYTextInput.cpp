#include "AYTextInput.h"
#include "AYUIManager.h"
#include "IAYRenderBackend.h"
#include "AYStyle.h"
#include "AYTextMeasure.h"
#include "AYClipboard.h"
#include "UIKeyCode.h"
#include "aymath/MathUtils.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <string>
#include <vector>

namespace ayt::ui {

namespace {

constexpr int kTextFontSize = ayt::ui::kDefaultTextFontSize;

size_t columnFromLocalX(const std::wstring& text, float localX) {
    if (localX <= 0.0f || text.empty()) return 0;
    size_t lo = 0;
    size_t hi = text.size();
    while (lo < hi) {
        const size_t mid = (lo + hi + 1) / 2;
        if (measurePrefixWidth(text, mid) <= localX) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    return lo;
}

} // namespace

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
    // Phase D §5.3 patch — R3 landmine cleanup. setFocus(nullptr) does
    // dynamic_cast<FocusableWidget*>(prev) and fires virtual setFocus(false)
    // on the prev widget, undefined behavior during destruction because the
    // derived-class vtable / RTTI info is partially torn down. The Phase D
    // PR-2 fix introduced clearFocusNoDispatch for exactly this reason —
    // ~Modal and the Manager teardown paths use it; TextInput ~ had been
    // missed (Phase C only handled composition cleanup). Mirror the same
    // R3 landmine avoidance here.
    // Prefer tryGet(): after UIManager::shutdown(), get() would hit the
    // static fallback and could stash `this` (about to die) as focus /
    // composition owner — batch SEGV at process exit / next suite.
    if (UIManager* ui = UIManager::tryGet()) {
        if (ui->getFocusedWidget() == this) {
            ui->clearFocusNoDispatch(this);
        }
        ui->clearCaptureNoDispatch(this);
        ui->clearHoverNoDispatch(this);
        ui->cancelComposition(this, /*fireEndOnOwner*/ false);
    }
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
    // PR-A3: setText mutates _text directly (bypasses replaceRange),
    // so it must push its own undo snapshot. Idempotent guard above
    // ensures we don't push when nothing actually changes.
    pushUndo();
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
    // PR-A3: clear() mutates _text directly (bypasses replaceRange),
    // so it must push its own undo snapshot. The idempotent guard
    // above ensures we don't push when the buffer is already empty.
    pushUndo();
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
    // PR-A3: detect no-op before pushing undo — a no-op replace (a==b
    // and replacement empty) shouldn't bloat the history stack. The
    // maxLength-trim case below also has a no-op fast path (room==0)
    // which returns without mutating; that one returns BEFORE
    // capturing the snapshot so we don't push on a no-op.
    const bool isNoOp = (a == b && replacement.empty());
    if (isNoOp) return;
    pushUndo();
    std::wstring before = _text.substr(0, a);
    std::wstring after  = _text.substr(b);
    if (_maxLength > 0 && before.size() + replacement.size() + after.size() > _maxLength) {
        // Trim replacement to fit; if still no room, no-op. We must
        // pop the snapshot we just pushed since the text didn't change
        // after all.
        const size_t room = _maxLength - before.size() - after.size();
        if (room == 0) {
            _undoStack.pop_back();
            return;
        }
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
    const bool gainingFocus = !_hasFocus;
    // Register with UIManager so onTextInput / IME route here. Local
    // setFocus(true) alone leaves UIManager::_focusedWidget null and
    // typed characters are dropped.
    if (UIManager* ui = UIManager::tryGet()) {
        ui->setFocus(this);
    } else {
        setFocus(true);
    }
    // First click that grants focus: select-all so default/demo text is
    // easy to replace (common single-line field UX). Subsequent clicks
    // place a caret / start a drag-select.
    if (gainingFocus && !_text.empty()) {
        selectAll();
        _dragging = true;
        _dragAnchorCol = 0;
        _dragAnchorWorld = e.mousePos;
        _caretBlinkTimer = 0.0f;
        _caretVisible = true;
        return true;
    }
    // Resolve the column for this click — same path as the drag-start
    // branch below. We need `_dragAnchorCol` populated BEFORE the
    // double-click check so selectWordAt can scan from the right
    // position.
    const math::FRectangle b = getWorldBounds();
    const float localX = e.mousePos.x - (b.minX + kPaddingX);
    const std::wstring& display = _passwordMode
        ? std::wstring(_text.size(), L'*') : _text;
    const size_t colAtClick = columnFromLocalX(display, localX);
    // PR-A3: double-click word select. We only fire this on a SECOND
    // click within kDoubleClickSeconds AND within ±kDoubleClickColSlack
    // columns of the previous click. A first-click that would also
    // match (e.g. _lastClickTime == -1) is treated as a normal single
    // click — only the second one in the pair gets the word-select
    // semantics. Reset _lastClickTime to -1 after a successful
    // double-click so a third click doesn't immediately re-fire.
    //
    // We compute `_lastClickTime` via tick(dt)-accumulated float, so
    // a test that wants to fire a double-click just calls
    // `ti.tick(0.1f); ti.onMouseButtonDown(...);` between the two
    // clicks.
    if (_lastClickTime >= 0.0f
        && std::abs(static_cast<int>(colAtClick)
                    - static_cast<int>(_lastClickCol)) <= kDoubleClickColSlack) {
        // Double-click detected — select the word under `colAtClick`
        // and reset the click timer so a third click is treated as a
        // new first-click.
        selectWordAt(colAtClick);
        _lastClickTime = -1.0f;
        _dragging = false;     // double-click supersedes drag-start
        _caretBlinkTimer = 0.0f;
        _caretVisible = true;
        return true;
    }
    // Not a double-click: record this click for the next time and
    // proceed with the normal drag-select branch.
    _dragging = true;
    _dragAnchorWorld = e.mousePos;
    _dragAnchorCol = colAtClick;
    _selStart = colAtClick;
    _selEnd = colAtClick;
    _caret = colAtClick;
    _lastClickTime = 0.0f;   // zero; tick(dt) will advance it
    _lastClickCol = colAtClick;
    _caretBlinkTimer = 0.0f;
    _caretVisible = true;
    return true;
}

bool TextInput::onMouseMove(const UIMouseEvent& e) {
    if (!_dragging) return false;
    const math::FRectangle b = getWorldBounds();
    const float localX = e.mousePos.x - (b.minX + kPaddingX);
    const std::wstring& display = _passwordMode
        ? std::wstring(_text.size(), L'*') : _text;
    const size_t cur = columnFromLocalX(display, localX);
    const size_t anchor = _dragAnchorCol;
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
    // Drop C0 controls (Ctrl+C → 0x03 etc.); shortcuts go through onKeyDown.
    if (ch < 0x20 && ch != L'\t') return true;
    return insertChar(ch);
}

bool TextInput::onKeyDown(int keyCode) {
    if (!_hasFocus) return false;

    UIManager* ui = UIManager::tryGet();
    const uint32_t mods = ui ? ui->getModifiers() : 0u;
    const bool ctrl  = (mods & (1u << (UIKey_Control - UIKey_Shift))) != 0u;
    const bool shift = (mods & (1u << (UIKey_Shift   - UIKey_Shift))) != 0u;

    if (ctrl) {
        switch (keyCode) {
        case UIKey_A:
            selectAll();
            _caretBlinkTimer = 0.0f;
            _caretVisible = true;
            return true;
        case UIKey_Z:
            // PR-A3: undo. Ctrl+Shift+Z is the macOS redo; honor it.
            if (shift) {
                redo();
            } else {
                undo();
            }
            _caretBlinkTimer = 0.0f;
            _caretVisible = true;
            return true;
        case UIKey_Y:
            // PR-A3: redo.
            redo();
            _caretBlinkTimer = 0.0f;
            _caretVisible = true;
            return true;
        case UIKey_C: {
            if (!hasSelection()) return true;
            size_t a = _selStart;
            size_t b = _selEnd;
            if (b < a) std::swap(a, b);
            (void)ayt::ui::getClipboard().setText(_text.substr(a, b - a));
            return true;
        }
        case UIKey_X: {
            if (_readOnly || !hasSelection()) return true;
            size_t a = _selStart;
            size_t b = _selEnd;
            if (b < a) std::swap(a, b);
            if (ayt::ui::getClipboard().setText(_text.substr(a, b - a))) {
                replaceRange(a, b, L"");
            }
            _caretBlinkTimer = 0.0f;
            _caretVisible = true;
            return true;
        }
        case UIKey_V: {
            if (_readOnly) return true;
            std::wstring clip;
            if (!ayt::ui::getClipboard().getText(clip) || clip.empty()) return true;
            // Single-line: strip CR/LF from paste.
            clip.erase(std::remove(clip.begin(), clip.end(), L'\r'), clip.end());
            clip.erase(std::remove(clip.begin(), clip.end(), L'\n'), clip.end());
            if (clip.empty()) return true;
            replaceRange(_selStart, _selEnd, clip);
            _caretBlinkTimer = 0.0f;
            _caretVisible = true;
            return true;
        }
        default:
            break;
        }
    }

    bool handled = true;
    switch (keyCode) {
    case UIKey_Backspace:
        // PR-A3: shift+Backspace is plain Backspace. Selection delete
        // path already handles the no-selection case via deleteLeft.
        deleteLeft();
        break;
    case UIKey_Delete:
        deleteRight();
        break;
    case UIKey_Left:
        // PR-A3: Shift+arrow extends selection rather than collapsing.
        if (shift) {
            const size_t newCaret = (_caret > 0) ? _caret - 1 : 0;
            setCaretExtendingSelection(newCaret);
        } else if (_caret > 0) {
            setCaret(_caret - 1);
        }
        break;
    case UIKey_Right:
        if (shift) {
            const size_t newCaret = std::min(_caret + 1, _text.size());
            setCaretExtendingSelection(newCaret);
        } else if (_caret < _text.size()) {
            setCaret(_caret + 1);
        }
        break;
    case UIKey_Home:
        if (shift) {
            setCaretExtendingSelection(0);
        } else {
            setCaret(0);
        }
        break;
    case UIKey_End:
        if (shift) {
            setCaretExtendingSelection(_text.size());
        } else {
            setCaret(_text.size());
        }
        break;
    case UIKey_Enter:
        if (_onSubmit) _onSubmit(_text);
        break;
    case UIKey_Tab:
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
    // PR-A3: advance the double-click timer. The timer only runs while
    // the widget is focused (consistent with onMouseButtonDown only
    // firing when focused) and only while pending (_lastClickTime >= 0).
    // After a successful double-click, onMouseButtonDown resets
    // _lastClickTime to -1 to disable the timer; on the next click we
    // set it back to 0 and start a new window.
    if (_lastClickTime >= 0.0f) {
        _lastClickTime += dt;
        if (_lastClickTime > kDoubleClickSeconds) {
            // Window expired — clear so a future click is treated as
            // a first click.
            _lastClickTime = -1.0f;
        }
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
    // PR-A3: a focus drop invalidates the pending double-click — a
    // click after regaining focus should be treated as a fresh
    // first click, not the second of a stale pair.
    _lastClickTime = -1.0f;
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
    // B3: rounded fill matches the 2px rounded border.
    renderer.drawRoundedRect(bounds, bg, 2.0f);
    renderer.drawBorderRect(bounds, borderColor, borderWidth, 2.0f);

    // Display text — password mask replaces each char with '*'.
    const std::wstring displayText =
        _passwordMode ? std::wstring(_text.size(), L'*') : _text;
    const float textW = measurePrefixWidth(displayText, displayText.size());
    const float innerW = (bounds.maxX - kPaddingX) - (bounds.minX + kPaddingX);
    float textMinX = bounds.minX + kPaddingX;
    if (_hAlign == HAlign::Right && textW < innerW) {
        textMinX = bounds.maxX - kPaddingX - textW;
    } else if (_hAlign == HAlign::Center && textW < innerW) {
        textMinX = bounds.minX + kPaddingX + (innerW - textW) * 0.5f;
    }

    if (!displayText.empty() || _hasFocus) {
        math::FRectangle textBounds(
            textMinX, bounds.minY,
            bounds.maxX - kPaddingX, bounds.maxY);
        if (hasSelection()) {
            size_t a = _selStart;
            size_t bsel = _selEnd;
            if (bsel < a) std::swap(a, bsel);
            if (a > displayText.size()) a = displayText.size();
            if (bsel > displayText.size()) bsel = displayText.size();
            const float selX0 = textMinX + measurePrefixWidth(displayText, a);
            const float selX1 = textMinX + measurePrefixWidth(displayText, bsel);
            const float pad = 2.0f;
            renderer.drawRect(
                math::FRectangle(selX0, bounds.minY + pad,
                                 selX1, bounds.maxY - pad),
                math::FVector4(0.18f, 0.45f, 0.78f, 0.45f));
        }
        const math::FVector4 textColor = _readOnly
            ? math::FVector4(0.55f, 0.55f, 0.55f, 1.0f)
            : math::FVector4(1.0f, 1.0f, 1.0f, 1.0f);
        if (!displayText.empty()) {
            renderer.drawText(textBounds, displayText, kTextFontSize, textColor);
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
        const float phW = measurePrefixWidth(_placeholder, _placeholder.size());
        float phMinX = bounds.minX + kPaddingX;
        if (_hAlign == HAlign::Right && phW < innerW) {
            phMinX = bounds.maxX - kPaddingX - phW;
        } else if (_hAlign == HAlign::Center && phW < innerW) {
            phMinX = bounds.minX + kPaddingX + (innerW - phW) * 0.5f;
        }
        math::FRectangle textBounds(
            phMinX, bounds.minY,
            bounds.maxX - kPaddingX, bounds.maxY);
        renderer.drawText(textBounds, _placeholder, kTextFontSize, phColor);
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
        math::FVector4 ulColor(0.30f, 0.65f, 0.95f, 1.0f);
        if (!getStyleId().empty()) {
            const WidgetStyle* ws = StyleManager::get().getStyle(getStyleId());
            if (ws != nullptr) {
                ulColor = ws->compositionUnderlineColor;
            }
        }
        const float ulX = textMinX + measurePrefixWidth(displayText, _caret);
        const float ulW = measurePrefixWidth(_compositionPreview,
                                             _compositionPreview.size());
        const float ulY = bounds.maxY - 3.0f;
        constexpr float ulH = 1.5f;
        renderer.drawRect(
            math::FRectangle(ulX, ulY, ulX + ulW, ulY + ulH),
            ulColor);
    }

    // Caret — vertical bar at the glyph edge after `_caret` characters.
    if (_hasFocus && _caretVisible) {
        float cx = textMinX + measurePrefixWidth(displayText, _caret);
        const float maxCx = bounds.maxX - kPaddingX - kCaretWidth;
        if (cx > maxCx) cx = maxCx;
        if (cx < bounds.minX + kPaddingX) cx = bounds.minX + kPaddingX;
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

// =============================================================================
// PR-A3 — shift+arrow / double-click / undo helpers.
// =============================================================================
//
// `setCaretExtendingSelection` is the shift+arrow primitive: it moves
// the caret to `newCaret` while keeping `_selStart` fixed as the
// anchor and updating `_selEnd` to follow the caret. When the
// selection is currently collapsed (_selStart == _selEnd), the
// previous caret position becomes the anchor — matching how Windows
// TextBox / macOS NSTextField handles the first shift+arrow in a run.
// =============================================================================

void TextInput::setCaretExtendingSelection(size_t newCaret) {
    if (newCaret > _text.size()) newCaret = _text.size();
    // If the selection is currently collapsed, the previous caret
    // position becomes the anchor for the new selection. We achieve
    // that by ensuring _selStart stays put (which is the same value
    // as _caret pre-call) and only _selEnd / _caret move.
    _selEnd = newCaret;
    _caret  = newCaret;
    // _selStart is intentionally NOT changed here — the caller
    // guarantees it holds the original anchor.
    // Note: setCaret() (used by the non-shift branch) calls
    // resetSelectionToCaret which collapses _selStart to _caret.
    // That's the opposite behavior of THIS function, by design.
}

void TextInput::selectWordAt(size_t col) {
    if (col > _text.size()) col = _text.size();
    // std::iswalnum is true for CJK ideographs in the C locale; that
    // matches Windows TextBox behavior (double-clicking a CJK run
    // selects the whole run). We additionally treat '_' as a word
    // char since identifiers commonly use it.
    auto isWordChar = [](wchar_t c) {
        return c == L'_' || std::iswalnum(static_cast<wint_t>(c)) != 0;
    };
    size_t start = col;
    while (start > 0 && isWordChar(_text[start - 1])) --start;
    size_t end = col;
    while (end < _text.size() && isWordChar(_text[end])) ++end;
    if (start == end) {
        // No word under the click — collapse to a single caret.
        // Matches Windows TextBox: clicking on whitespace / punctuation
        // does not extend selection.
        setCaret(col);
    } else {
        setSelection(start, end);
    }
}

// =============================================================================
// PR-A3 — undo / redo infrastructure.
// =============================================================================
// Mirrors TextArea::captureSnapshot / restoreSnapshot / pushUndo / undo /
// redo (see AYTextArea.cpp around line 774-838 for the 2D equivalent).
// Key invariants:
//
//   1. pushUndo is the single source of truth for adding to the undo
//      stack. insertChar / deleteLeft / deleteRight all funnel through
//      replaceRange which calls pushUndo at the top. setText and
//      clear push directly because they mutate _text without going
//      through replaceRange.
//
//   2. pushUndo clears `_redoStack` — standard linear-history
//      semantics. Any "fresh" edit invalidates the redo path.
//
//   3. pushUndo is called BEFORE the change, not after. The snapshot
//      therefore represents "the state I'm leaving" — undo pops and
//      restores to that state.
//
//   4. restoreSnapshot does NOT fire _onTextChanged. Undo/redo are
//      user-visible actions; the host can already observe them via
//      canUndo/canRedo and getText. Re-firing onTextChanged would
//      double-count the same edit (the cause of the original P1
//      regression in TextArea before this guard was added there).
//
//   5. The stack is capped at kMaxHistoryEntries=100 to bound memory
//      in long sessions. Oldest entries are dropped from the front;
//      newest wins. Mirrors TextArea's `erase(begin())` policy.
// =============================================================================

TextInput::TextEditSnapshot TextInput::captureSnapshot() const {
    TextEditSnapshot s;
    s.text         = _text;
    s.caret        = _caret;
    s.selStart     = _selStart;
    s.selEnd       = _selEnd;
    s.hasSelection = hasSelection();
    return s;
}

void TextInput::restoreSnapshot(const TextEditSnapshot& s) {
    _text     = s.text;
    _caret    = std::min(s.caret, _text.size());
    _selStart = std::min(s.selStart, _text.size());
    _selEnd   = std::min(s.selEnd, _text.size());
    if (_selEnd < _selStart) std::swap(_selStart, _selEnd);
    // Do NOT fire _onTextChanged — see invariant 4 above.
}

void TextInput::pushUndo() {
    _undoStack.push_back(captureSnapshot());
    if (_undoStack.size() > kMaxHistoryEntries) {
        _undoStack.erase(_undoStack.begin());
    }
    _redoStack.clear();
}

void TextInput::undo() {
    if (_undoStack.empty()) return;
    // Push the current state onto redo so redo can return here.
    _redoStack.push_back(captureSnapshot());
    // Pop the most recent undo entry and restore it.
    TextEditSnapshot s = _undoStack.back();
    _undoStack.pop_back();
    restoreSnapshot(s);
}

void TextInput::redo() {
    if (_redoStack.empty()) return;
    // Symmetric: push current state onto undo so the user can come
    // back here with another undo.
    _undoStack.push_back(captureSnapshot());
    TextEditSnapshot s = _redoStack.back();
    _redoStack.pop_back();
    restoreSnapshot(s);
}

} // namespace ayt::ui
