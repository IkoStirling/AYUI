#include "AYTextInput.h"
#include "AYIRenderBackend.h"
#include "AYStyle.h"
#include "UIKeyCode.h"
#include "aymath/MathUtils.h"

#include <algorithm>

namespace ayt::ui {

// Phase B (S3): TextInput uses UIKeyCode (UIKey_Backspace etc.) instead
// of anonymous VK raw ints. Values are VK-aligned so the comparison
// against legacy host-side int codes stays correct — see Controls/UIKeyCode.h.

TextInput::TextInput() {
    setSize(math::FVector2(kDefaultWidth, kDefaultHeight));
}

TextInput::~TextInput() = default;

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
    // Click anywhere grants focus. Selection-by-coordinate is out of
    // scope for v1 (we don't know glyph widths precisely).
    setFocus(true);
    clearSelection();
    // Reset blink so the caret is visible immediately after click.
    _caretBlinkTimer = 0.0f;
    _caretVisible = true;
    return true;
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
