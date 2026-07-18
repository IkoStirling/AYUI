#pragma once

#include "AYFocusableWidget.h"
#include <functional>
#include <string>

namespace ayt::ui {

// C-3 TextInput: a single-line text input with caret + selection +
// password mask. NOT multi-line (that's C-10 TextArea, scoped to
// C-4 + C-3 reuse).
//
// Why extends FocusableWidget (not InteractiveWidget, not Widget):
//   - FocusableWidget owns the focus lifecycle and gives us a single
//     state ('_hasFocus') that determines cursor-hint (Beam) and which
//     overrides UIManager routes key/text input to.
//   - We do NOT need InteractiveWidget's hover state machine — a
//     focused TextInput the cursor has moved away from remains
//     focused; the cursor continues blinking. Hover is layered on
//     top via the base Widget's getCursorHint interaction with the
//     hovered Widget tree (UIManager already merges focus + hover
//     hint precedence).
//
// Text payload uses AYTextContent's TextContent-equivalent fields
// inline (NOT the full struct) — adopting TextContent fully would
// force TextInput to share Button's text-alignment logic, but
// TextInput has stricter caret-driven horizontal positioning that
// differs from Button's centered label. The free helper text-typing
// API uses std::wstring directly. Promote when ScrollView/ListView
// need similar text containers.
//
// v1 semantics covered:
//   - single-line input via setText / getText / insertChar / deleteLeft /
//     deleteRight / clear / appendText
//   - selection (start, end) caret positions; range selection via
//     setSelection / clearSelection / selectAll
//   - password mask: when _passwordMode is true, renders '*' chars
//     without changing underlying text
//   - caret blink (driven by a tick() callback — blinks every 0.5s;
//     default-on when focused)
//   - readonly mode (setReadOnly) blocks text edit but still allows
//     selection / cursor moves
//
// v1 NOT covered (deferred):
//   - IME / composition
//   - placeholder text
//   - text alignment beyond Left (no Center / Right)
//   - drag-to-select

class TextInput : public FocusableWidget {
public:
    static constexpr float kDefaultWidth  = 200.0f;
    static constexpr float kDefaultHeight = 24.0f;
    static constexpr float kPaddingX      = 6.0f;
    static constexpr float kCaretWidth    = 1.0f;
    static constexpr float kCaretBlinkSeconds = 0.5f;

    TextInput();
    ~TextInput() override;

    // Text payload.
    const std::wstring& getText() const { return _text; }
    void setText(const std::wstring& text);

    // Insert a single char at the caret (or replace current selection).
    // Returns true if text changed.
    bool insertChar(wchar_t ch);

    // Delete the char to the left of the caret (or selection range).
    // Returns true if text changed.
    bool deleteLeft();

    // Delete the char to the right of the caret (or selection range).
    bool deleteRight();

    void clear();
    void appendText(const std::wstring& s);

    // Caret position is a 0..len index. setCaret clamps; clearSelection
    // collapses selection to caret (single-position).
    void  setCaret(size_t pos);
    size_t getCaret() const { return _caret; }

    // Selection is half-open [start, end). setSelection(start, end) sets
    // the range and moves caret to end. clearSelection collapses to
    // caret.
    void setSelection(size_t start, size_t end);
    void clearSelection();
    void selectAll();
    bool hasSelection() const { return _selStart != _selEnd; }
    size_t getSelectionStart() const { return _selStart; }
    size_t getSelectionEnd() const { return _selEnd; }

    // Length cap (0 = unlimited). Default unlimited.
    void   setMaxLength(size_t n) { _maxLength = n; }
    size_t getMaxLength() const { return _maxLength; }

    void setReadOnly(bool ro) { _readOnly = ro; }
    bool isReadOnly() const { return _readOnly; }

    void setPasswordMode(bool p) {
        _passwordMode = p;
        if (p) clearSelection();
    }
    bool isPasswordMode() const { return _passwordMode; }

    void setOnTextChanged(std::function<void(const std::wstring&)> cb) {
        _onTextChanged = std::move(cb);
    }
    void setOnSubmit(std::function<void(const std::wstring&)> cb) {
        _onSubmit = std::move(cb);
    }

    bool onMouseButtonDown(const UIMouseEvent& e) override;
    bool onTextInput(wchar_t ch) override;
    bool onKeyDown(int keyCode) override;

    UiCursorHint getCursorHint() const override;
    void tick(float dt) override;

protected:
    void onRender(IRenderBackend& renderer) override;
    void onFocusGained() override;
    void onFocusLost() override;

    // Internal: replaces [a, b) in _text with `replacement`. Updates
    // caret / selection / fires _onTextChanged.
    void replaceRange(size_t a, size_t b, const std::wstring& replacement);
    void clampCaret();
    void resetSelectionToCaret();

    std::wstring _text;
    size_t _caret = 0;
    size_t _selStart = 0;
    size_t _selEnd = 0;
    size_t _maxLength = 0;     // 0 = unlimited
    bool _readOnly = false;
    bool _passwordMode = false;
    // Caret blink: toggled each kCaretBlinkSeconds when focused.
    float _caretBlinkTimer = 0.0f;
    bool _caretVisible = true;
    std::function<void(const std::wstring&)> _onTextChanged;
    std::function<void(const std::wstring&)> _onSubmit;
};

Widget* createTextInputWidget();

} // namespace ayt::ui
