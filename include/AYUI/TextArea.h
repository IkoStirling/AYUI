#pragma once

#include "AYUI/FocusableWidget.h"
#include "AYUI/ScrollView.h"
#include <functional>
#include <string>
#include <vector>

namespace ayt::ui {

// =============================================================================
// C-10 TextArea: a multi-line plain-text editor.
// =============================================================================
//
// Architecture:
//   TextArea (CompoundWidget — not FocusableWidget; see DECISION 3)
//     └─ _scrollView: ScrollView* (vbar always on; hbar off by default)
//         └─ _document: TextDocument* (a Widget that owns the line buffer,
//                                     computes its own size from line count ×
//                                     line height, draws lines + caret +
//                                     selection highlight)
//
// TextDocument is a private nested widget of TextArea (not a public widget)
// because the line / caret / selection model is TextArea-specific. Exposing
// it would either require pulling the model up into a base class or letting
// callers poke into TextArea's internals — both undesirable. See
// Controls/AYTextArea.cpp for the impl.
//
// Line model: `vector<wstring> _lines`; the buffer is "\n"-joined for
// getText() and split on "\n" for setText(). Empty input becomes one empty
// line. Optional soft wrapping is a visual projection only: caret and
// selection remain logical-line/code-unit positions snapped to graphemes.
//
// The inner TextDocument receives focus and owns input routing. TextArea
// provides measured soft wrapping, grapheme-safe caret/selection/editing,
// drag and word selection, IME composition, clipboard shortcuts and bounded
// undo/redo history. One committed text event is one edit transaction.
// =============================================================================

class TextArea : public CompoundWidget {
public:
    // Inner document widget — FocusableWidget so UIManager can route
    // keyboard events to it (DECISION 3). Public only so the factory can
    // construct it; hosts should not interact with it directly.
    //
    // Forward-declared with its base class declared via the typedef
    // trick below so callers can implicitly convert TextDocument* to
    // Widget*/FocusableWidget* without dragging in the nested-class
    // definition. The actual definition lives in AYTextArea.cpp.
    class TextDocument;

    static constexpr float kDefaultWidth    = 320.0f;
    static constexpr float kDefaultHeight   = 160.0f;
    static constexpr float kDefaultLineHeight = 18.0f;
    static constexpr float kPaddingX        = 6.0f;
    static constexpr float kPaddingY        = 4.0f;

    TextArea();
    ~TextArea() override;

    // Text payload. setText replaces the buffer (split on '\n'). getText
    // joins with '\n'. The buffer is guaranteed to have at least one line
    // (empty string → one empty line).
    const std::wstring& getText() const;
    void setText(const std::wstring& text);

    // Edit. Returns true on real change. insertChar / deleteLeft /
    // deleteRight honor selection ranges (replace) and readOnly.
    bool insertChar(wchar_t ch);
    bool insertText(const std::wstring& text);
    bool deleteLeft();
    bool deleteRight();
    void clear();

    // Caret is a 2D position (line, col). setCaret clamps to valid ranges
    // and clears the selection.
    void setCaret(int line, int col);
    int  getCaretLine() const;
    int  getCaretCol()  const;

    // Selection is two caret positions (anchor + active). setSelection
    // preserves their direction and moves the caret to `active`; edit and
    // rendering paths normalize a copy when they need ordered endpoints.
    void setSelection(int startLine, int startCol, int endLine, int endCol);
    void clearSelection();
    void selectAll();
    bool hasSelection() const;
    // PR-A2: read the currently-selected text as a single wstring, with
    // line boundaries joined by '\n'. Returns empty when no selection
    // is active. Used by TextDocument::onKeyDown's Ctrl+C / Ctrl+X paths.
    std::wstring getSelectedText() const;

    void setReadOnly(bool ro);
    bool isReadOnly() const;

    void setMaxLength(size_t n) { _maxLength = n; }
    size_t getMaxLength() const { return _maxLength; }

    void setLineHeight(float h);
    float getLineHeight() const { return _lineHeight; }

    // =================================================================
    // Word wrapping is a measured visual projection. It never inserts soft
    // newlines into the logical buffer; hit testing, selection and caret
    // placement share the same VisualLine projection used by rendering.
    void setWordWrap(bool w) {
        if (_wordWrap == w) return;
        _wordWrap = w;
        syncDocumentSizeToContent();
        markBoundsDirty();
        invalidateDocument();
        markDirty();
    }
    bool isWordWrap() const { return _wordWrap; }

    void setOnTextChanged(std::function<void(const std::wstring&)> cb) {
        _onTextChanged = std::move(cb);
    }

    // Phase C (S4): delegates to TextDocument::isComposing(). Tests
    // observe the composition state without poking at the private
    // inner widget. Implementation lives in the .cpp because
    // TextDocument is an incomplete type at this point in the header.
    bool isComposing() const;

    // Re-expose sub-widgets for hosts / tests that want to skin or hook.
    ScrollView*  getScrollView() const { return _scrollView; }
    TextDocument* getDocument()  const { return _document; }

    // Phase C (S4): FocusableWidget* view of the inner document. Defined
    // in the .cpp because the static_cast<FocusableWidget*>(TextDocument*)
    // requires TextDocument's inheritance to be visible at the call site.
    // NULL when not yet created.
    FocusableWidget* getDocumentAsFocusable() const;

    // =================================================================
    // Polish (P1) — undo / redo.
    // =================================================================
    // Each mutating op (insertChar / deleteLeft / deleteRight / setText)
    // pushes a TextEditSnapshot onto an internal double-stack
    // (`_undoStack` / `_redoStack`) BEFORE applying the change. Ctrl+Z
    // pops `_undoStack` and pushes the current state onto `_redoStack`;
    // Ctrl+Y (or Ctrl+Shift+Z) is the inverse. After undo, the next
    // mutation clears `_redoStack` — standard linear-history semantics.
    //
    // Snapshot captures _lines + caret + selection. Coalesce strategy
    // is intentionally minimal in v1 polish: each op pushes once. A
    // future v1.2 could group consecutive inserts within a 500ms window
    // into one snapshot for "word-level undo" (Word-style); not in
    // scope here.
    //
    // The history is owned by TextArea, not the inner TextDocument, so
    // TabControl / Modal close paths that destroy the document don't
    // leak history. TextDocument's onKeyDown dispatches Ctrl+Z/Y to
    // TextArea::undo() / redo() via its `_owner` pointer.
    // =================================================================
    bool canUndo() const;
    bool canRedo() const;
    void undo();
    void redo();
    size_t getUndoStackSize() const { return _undoStack.size(); }
    size_t getRedoStackSize() const { return _redoStack.size(); }

    void performLayout() override;

    // Polish (P1) — public so tests + hosts can size against the cap.
    static constexpr size_t kMaxHistoryEntries = 100;

private:
    struct VisualLine {
        int logicalLine = 0;
        int startCol = 0;
        int endCol = 0;
        float width = 0.0f;
    };

    // One snapshot of the editing state at a moment in time. Captured
    // BEFORE a mutation so the mutation can be reversed by restore()
    // back to this exact state. Held by value in the history stacks.
    struct TextEditSnapshot {
        std::vector<std::wstring> lines;
        int caretLine   = 0;
        int caretCol    = 0;
        int selStartLine = 0;
        int selStartCol = 0;
        int selEndLine   = 0;
        int selEndCol    = 0;
        bool hasSelection = false;
    };

    void ensureChildrenCreated();
    void syncDocumentSizeToContent();
    void syncTextToDocument();
    void fireTextChanged();
    std::vector<VisualLine> buildVisualLines(float availableWidth) const;
    void hitTestDocumentPosition(const math::FVector2& local,
                                 int& line, int& col) const;
    void setCaretExtendingSelection(int line, int col);
    void selectWordAt(int line, int col);
    void deleteSelectionWithoutHistory();
    void invalidateDocument();

    // Polish (P1): capture current state into a snapshot. Called at
    // the top of every mutating op; _redoStack is cleared at the same
    // point so a fresh-edit-after-undo forgets the redo path.
    TextEditSnapshot captureSnapshot() const;
    void restoreSnapshot(const TextEditSnapshot& s);
    void pushUndo();

    std::vector<TextEditSnapshot> _undoStack;
    std::vector<TextEditSnapshot> _redoStack;

    ScrollView*  _scrollView = nullptr;
    TextDocument* _document  = nullptr;

    // Joined text cache (rebuilt when lines change). Empty cache means
    // caller must rebuild via getText().
    mutable std::wstring _textCache;
    mutable bool _textCacheDirty = true;
    std::vector<std::wstring> _lines;

    int _caretLine = 0;
    int _caretCol  = 0;

    int _selStartLine = 0;
    int _selStartCol  = 0;
    int _selEndLine   = 0;
    int _selEndCol    = 0;

    size_t _maxLength = 0;        // 0 = unlimited (combined text size)
    bool _readOnly = false;
    float _lineHeight = kDefaultLineHeight;
    bool _wordWrap = false;       // Phase C (C6)

    std::function<void(const std::wstring&)> _onTextChanged;
};

Widget* createTextAreaWidget();

} // namespace ayt::ui
