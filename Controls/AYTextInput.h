#pragma once

#include "AYFocusableWidget.h"
#include <functional>
#include <string>
#include <vector>

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
    // PR-A3: double-click threshold for word-select. Windows default 0.4s;
    // macOS is 0.5s but we standardize on 0.4 for consistency.
    static constexpr float kDoubleClickSeconds = 0.4f;
    static constexpr int   kDoubleClickColSlack = 2;  // ±2 col distance still counts as double-click
    // PR-A3: undo history cap. Mirrors TextArea::kMaxHistoryEntries=100.
    // 100 entries × O(few KB) per snapshot stays well within memory budget
    // even for 10k+ character typing sessions.
    static constexpr size_t kMaxHistoryEntries = 100;

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

    // =================================================================
    // Phase C (C4) — placeholder text. Drawn when the buffer is empty
    // AND the widget is not focused. Style override via
    // `placeholderColor` (WidgetStyle key added in PR-1). Default muted
    // gray, semitransparent.
    // =================================================================
    void setPlaceholder(const std::wstring& text) { _placeholder = text; }
    const std::wstring& getPlaceholder() const { return _placeholder; }

    bool onMouseButtonDown(const UIMouseEvent& e) override;
    bool onMouseButtonUp(const UIMouseEvent& e) override;
    bool onTextInput(wchar_t ch) override;
    bool onKeyDown(int keyCode) override;

    // =================================================================
    // Phase C (S4): IME composition hooks. See AYFocusableWidget.h for
    // the state-machine contract. We override here because TextInput is
    // the canonical single-line recipient of IME composition.
    //
    // Byte caret (from AYDevice / GCS_CURSORPOS) is converted to a
    // wchar_t-codepoint caret internally so replaceRange and selection
    // operations work on _text directly. R4 in the Phase C plan: we
    // accept that surrogate-pair codepoints arrive as two UTF-16 halves
    // on Windows wchar_t.
    // =================================================================
    bool onImeCompositionStart(const std::string& text, int caret) override;
    bool onImeCompositionUpdate(const std::string& text, int caret) override;
    bool onImeCompositionEnd(const std::string& committed) override;

    // Phase C: state-query helper for tests + UI hints. True between
    // onImeCompositionStart and the matching onImeCompositionEnd.
    bool isComposing() const { return _composing; }

    // =================================================================
    // Phase C (C5) — mouse drag-to-select.
    // =================================================================
    // onMouseButtonDown returns true (so UIManager captures the widget)
    // and records the click as the drag anchor. onMouseMove (when this
    // is the captured widget) extends the selection from anchor to
    // current position. onMouseButtonUp clears _dragging.
    //
    // Width approximation: we don't have a precise text shaper, so the
    // caret x position uses a 7px-per-char approximation (same as
    // TextArea's rendering). R3 applies here too.
    // =================================================================
    bool onMouseMove(const UIMouseEvent& e) override;

    bool isDragging() const { return _dragging; }

    // Phase C: a TextInput IS a text-editing widget per the
    // UIManager::isTextEditing() helper. AYDevice::TextInput gate is
    // flipped when this widget gains/loses focus.
    bool isTextEditingWidget() const override { return true; }

    // =================================================================
    // G6 — text horizontal alignment. v1 was always Left (v1.1 adds
    // Center + Right). Affects the rendered textBounds.minX only;
    // caret stays at the right padding edge as a deliberately
    // approximate signal (we don't have a precise text shaper; v1.2
    // will tighten). Default Left preserves v1 contract — every
    // existing test that pins drawRect bounds stays green.
    // =================================================================
    enum class HAlign { Left, Center, Right };
    void  setHAlign(HAlign a) { _hAlign = a; }
    HAlign getHAlign() const   { return _hAlign; }

    UiCursorHint getCursorHint() const override;
    void tick(float dt) override;

    // =================================================================
    // PR-A3 — undo / redo.
    // =================================================================
    // Mirrors TextArea's P1 polish: each mutating op (insertChar /
    // deleteLeft / deleteRight / setText / clear / replaceRange) pushes
    // a TextEditSnapshot onto `_undoStack` BEFORE the change. Ctrl+Z
    // pops `_undoStack` and pushes the current state onto `_redoStack`;
    // Ctrl+Y (or Ctrl+Shift+Z) is the inverse. After undo, the next
    // mutation clears `_redoStack` — standard linear-history semantics.
    //
    // We intentionally do NOT fire `_onTextChanged` from
    // `restoreSnapshot` — undo/redo are user-visible actions; the host
    // already observes them through canUndo/canRedo/getText, and
    // re-firing onTextChanged would double-count the same change
    // (which is what caused the Phase C P1 baseline regression in
    // TextArea before the equivalent guard was added).
    // =================================================================
    bool canUndo() const { return !_undoStack.empty(); }
    bool canRedo() const { return !_redoStack.empty(); }
    void undo();
    void redo();
    size_t getUndoStackSize() const { return _undoStack.size(); }
    size_t getRedoStackSize() const { return _redoStack.size(); }

protected:
    void onRender(IRenderBackend& renderer) override;
    void onFocusGained() override;
    void onFocusLost() override;

    // Internal: replaces [a, b) in _text with `replacement`. Updates
    // caret / selection / fires _onTextChanged.
    void replaceRange(size_t a, size_t b, const std::wstring& replacement);
    void clampCaret();
    void resetSelectionToCaret();

    // PR-A3: shift+arrow helper. Moves caret to `newCaret` while keeping
    // the existing anchor side of the selection stable. Used by
    // onKeyDown's Shift+Left/Right/Home/End branches.
    //
    // Convention: `_caret` is the *active* end, `_selStart` is the
    // fixed *anchor* end. When the user starts shift-extending from a
    // collapsed selection, the anchor becomes the original caret
    // position (set by resetSelectionToCaret). On the next shift+arrow,
    // `_selStart` no longer moves — only `_caret` and `_selEnd` do.
    void setCaretExtendingSelection(size_t newCaret);

    // PR-A3: double-click word-select. Walks left/right from `col`
    // while the character is a word char (alnum or underscore) per
    // std::iswalnum. If neither side yields a word (clicked on
    // whitespace / punctuation), collapses selection to a single
    // caret at `col` — matches Windows TextBox behavior.
    void selectWordAt(size_t col);

    // PR-A3: undo snapshot helpers. captureSnapshot reads the current
    // editing state; restoreSnapshot writes a snapshot back WITHOUT
    // firing _onTextChanged. pushUndo appends to `_undoStack` and
    // clears `_redoStack`. replaceRange is the single source of truth
    // for push calls — it fires for insertChar / deleteLeft /
    // deleteRight (all three call replaceRange). setText / clear push
    // directly because they bypass replaceRange.
    struct TextEditSnapshot {
        std::wstring text;
        size_t caret     = 0;
        size_t selStart  = 0;
        size_t selEnd    = 0;
        bool   hasSelection = false;
    };
    TextEditSnapshot captureSnapshot() const;
    void restoreSnapshot(const TextEditSnapshot& s);
    void pushUndo();

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

    // =================================================================
    // Phase C (S4): IME composition state.
    // =================================================================
    // _compositionPreview holds the IME's pre-edit string (UTF-16
    // decoded from AYDevice's UTF-8 chunk). It's NOT merged into _text
    // until End fires — render shows it visually distinct (underline)
    // so users can see what's still being composed.
    //
    // _compositionCaretBytes is the byte offset from AYDevice (Win32
    // GCS_CURSORPOS). We translate to wchar_t-codepoint offset when we
    // actually need it (selection / replaceRange). Storing the byte
    // offset avoids re-decoding on every Update.
    //
    // _composing is the flag UIManager's state machine flips on
    // Start/End. Render uses it to decide whether to draw the
    // underline.
    std::wstring _compositionPreview;
    int          _compositionCaretBytes = 0;
    bool         _composing = false;

    // Phase C (C4) placeholder.
    std::wstring _placeholder;

    // =================================================================
    // Phase C (C5) — drag-select state.
    // =================================================================
    // _dragging is set by onMouseButtonDown (returns true so UIManager
    // captures) and cleared by onMouseButtonUp. While _dragging is true,
    // onMouseMove extends _selStart.._selEnd from _dragAnchorCol to the
    // current approximate column.
    //
    // _dragAnchorWorld is the world-space mouse position at button-down.
    // We translate to a column via the same 7px approximation used in
    // the renderer. C5 of Phase C; mirrors Phase B's B1 onKeyDown
    // approximation philosophy.
    bool           _dragging = false;
    math::FVector2 _dragAnchorWorld = math::FVector2(0.0f, 0.0f);
    size_t         _dragAnchorCol = 0;

    // G6 — text alignment. Default Left preserves v1 contract.
    HAlign _hAlign = HAlign::Left;

    // =================================================================
    // PR-A3 — double-click detection state.
    // =================================================================
    // `_lastClickTime` is the elapsed-time value (in seconds, advanced
    // by tick(dt)) at which the most recent click landed. Negative
    // value means "no click pending" (initial state, or after a
    // successful double-click reset). On the second click within
    // kDoubleClickSeconds of the first AND within
    // kDoubleClickColSlack columns, onMouseButtonDown treats the click
    // as a double-click and calls selectWordAt.
    //
    // Using a tick-accumulated float rather than wall-clock avoids
    // pulling in a system-time API just for a single UX nicety. Tests
    // can drive the timer with explicit `tick(dt)` calls.
    // =================================================================
    float  _lastClickTime  = -1.0f;
    size_t _lastClickCol   = 0;

    // =================================================================
    // PR-A3 — undo / redo history.
    // =================================================================
    // Two stacks implementing linear history: every new edit clears
    // `_redoStack` (standard editor semantics). Bounded by
    // kMaxHistoryEntries to keep memory predictable in long sessions.
    // =================================================================
    std::vector<TextEditSnapshot> _undoStack;
    std::vector<TextEditSnapshot> _redoStack;
};

Widget* createTextInputWidget();

} // namespace ayt::ui
