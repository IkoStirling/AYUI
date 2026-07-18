#include "AYTextArea.h"
#include "AYScrollBar.h"
#include "UIKeyCode.h"
#include <algorithm>

namespace ayt::ui {

// =============================================================================
// TextDocument — the inner FocusableWidget that owns the line buffer.
// =============================================================================
//
// We keep the document class implementation inline (here) rather than in
// AYTextDocument.h so the multi-line text editing model is fully encapsulated
// in TextArea. The document is NOT registered with WidgetFactory (no public
// factory); TextArea always creates + owns it.

class TextArea::TextDocument : public FocusableWidget {
public:
    TextDocument(TextArea* owner) : _owner(owner) {}

    UiCursorHint getCursorHint() const override {
        return UiCursorHint::Beam;
    }

    bool onMouseButtonDown(const UIMouseEvent& e) override {
        if (e.mouseButton != 0) return false;
        // Map world position to (line, col).
        const math::FVector2 local = e.mousePos - getWorldBounds().getMin();
        const float lineHeight = _owner->getLineHeight();
        int line = static_cast<int>(local.y / lineHeight);
        int col = static_cast<int>(local.x - TextArea::kPaddingX);
        line = std::clamp(line, 0, static_cast<int>(_owner->_lines.size()) - 1);
        if (line < 0) line = 0;
        col = std::max(0, col);
        col = std::min(col, static_cast<int>(_owner->_lines[line].size()));
        _owner->setCaret(line, col);
        _owner->_selStartLine = line;
        _owner->_selStartCol = col;
        _owner->_selEndLine = line;
        _owner->_selEndCol = col;
        return true;
    }

    bool onKeyDown(int keyCode) override {
        // Minimal key routing for v1:
        //   Left / Right → move col
        //   Up / Down → move line (preserve col, clamp to line length)
        //   Home / End → start / end of line
        //   Backspace / Delete → handled by TextArea (insert/delete API)
        int line = _owner->_caretLine;
        int col  = _owner->_caretCol;
        switch (keyCode) {
        case UIKey_Left:      --col; break;
        case UIKey_Right:     ++col; break;
        case UIKey_Up:        --line; break;
        case UIKey_Down:      ++line; break;
        case UIKey_Home:      col = 0; break;
        case UIKey_End:       col = static_cast<int>(_owner->_lines[line].size()); break;
        case UIKey_Backspace: _owner->deleteLeft(); return true;
        case UIKey_Delete:    _owner->deleteRight(); return true;
        case UIKey_Tab:
            // UIManager.onKeyDown intercepts Tab BEFORE delegating; swallow
            // defensively if a host bypassed UIManager so caret does not
            // jump unexpectedly.
            return true;
        default: return false;
        }
        _owner->setCaret(line, col);
        return true;
    }

    bool onTextInput(wchar_t ch) override {
        if (ch == L'\r') ch = L'\n';
        return _owner->insertChar(ch);
    }

    void onRender(IRenderBackend& renderer) override {
        const auto& lines = _owner->_lines;
        const float lh = _owner->getLineHeight();
        const math::FVector2 origin = getWorldBounds().getMin();
        const float fontSize = lh - 4.0f;     // rough visual mapping

        // Selection highlight (single rectangular block for v1).
        if (_owner->hasSelection()) {
            // Compute selection rect. v1 simplification: only highlight
            // when selection is within a single line. Multi-line selection
            // is supported in the data model but the highlight is rendered
            // as the active-line range only — visual upgrade is v1.1.
            int sl = _owner->_selStartLine;
            int sc = _owner->_selStartCol;
            int el = _owner->_selEndLine;
            int ec = _owner->_selEndCol;
            // Order so start <= end.
            if (sl > el || (sl == el && sc > ec)) {
                std::swap(sl, el);
                std::swap(sc, ec);
            }
            if (sl == el) {
                float x0 = origin.x + TextArea::kPaddingX + static_cast<float>(sc) * 7.0f;  // rough char width
                float x1 = origin.x + TextArea::kPaddingX + static_cast<float>(ec) * 7.0f;
                float y  = origin.y + static_cast<float>(sl) * lh;
                renderer.drawRect({{x0, y}, {x1 - x0, lh}},
                                  math::FVector4(0.30f, 0.45f, 0.78f, 0.35f));
            }
        }

        // Lines.
        for (size_t i = 0; i < lines.size(); ++i) {
            math::FVector2 p(origin.x + TextArea::kPaddingX,
                             origin.y + static_cast<float>(i) * lh + TextArea::kPaddingY);
            if (!lines[i].empty()) {
                math::FRectangle bounds{p, {static_cast<float>(lines[i].size()) * 7.0f, lh}};
                renderer.drawText(bounds, lines[i], static_cast<int>(fontSize),
                                  math::FVector4(0.92f, 0.92f, 0.94f, 1.0f));
            }
        }

        // Caret.
        if (hasFocus()) {
            float x = origin.x + TextArea::kPaddingX + static_cast<float>(_owner->_caretCol) * 7.0f;
            float y = origin.y + static_cast<float>(_owner->_caretLine) * lh + 1.0f;
            renderer.drawRect({{x, y}, {1.0f, lh - 2.0f}},
                              math::FVector4(1.0f, 1.0f, 1.0f, 0.95f));
        }
    }

private:
    TextArea* _owner;
};

// =============================================================================
// TextArea
// =============================================================================

TextArea::TextArea() {
    setSize(math::FVector2(kDefaultWidth, kDefaultHeight));
    ensureChildrenCreated();
}

TextArea::~TextArea() {
    // _scrollView and _document are children of TextArea via addChild()
    // (the regular delete-owning path), so ~CompoundWidget will delete
    // them. We just null out our own pointers so dtor ordering is safe
    // in case the base class dtor hasn't run yet (it has, in practice).
    _document = nullptr;
    _scrollView = nullptr;
}

void TextArea::ensureChildrenCreated() {
    if (_scrollView != nullptr) return;
    _scrollView = new ScrollView();
    _scrollView->setVerticalScrollBarEnabled(true);
    _scrollView->setHorizontalScrollBarEnabled(false);   // DECISION 1
    // Seed ScrollView with a non-zero size so its viewport math doesn't
    // divide by zero when setContentSize is called before performLayout.
    _scrollView->setSize(math::FVector2(kDefaultWidth, kDefaultHeight));
    addChild(_scrollView);   // owns children → ~CompoundWidget deletes it

    _document = new TextDocument(this);
    // Force a non-empty line buffer so getSize doesn't collapse.
    _lines.push_back(L"");
    syncDocumentSizeToContent();
    _scrollView->setContent(_document);
}

const std::wstring& TextArea::getText() const {
    if (_textCacheDirty) {
        _textCache.clear();
        for (size_t i = 0; i < _lines.size(); ++i) {
            if (i > 0) _textCache.push_back(L'\n');
            _textCache.append(_lines[i]);
        }
        _textCacheDirty = false;
    }
    return _textCache;
}

void TextArea::setText(const std::wstring& text) {
    _lines.clear();
    size_t start = 0;
    for (size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == L'\n') {
            _lines.push_back(text.substr(start, i - start));
            start = i + 1;
        }
    }
    if (_lines.empty()) _lines.push_back(L"");
    // Park caret at end of buffer (matches TextInput::setText + native
    // textbox behavior: loading text moves caret to where the user would
    // resume typing).
    _caretLine = static_cast<int>(_lines.size()) - 1;
    _caretCol = static_cast<int>(_lines.back().size());
    clearSelection();
    syncDocumentSizeToContent();
    _textCacheDirty = true;
    fireTextChanged();
}

bool TextArea::insertChar(wchar_t ch) {
    if (_readOnly) return false;
    if (_maxLength > 0 && getText().size() + 1 > _maxLength) return false;
    // Replace selection if any.
    if (hasSelection()) {
        // Delete selection first (no-op if collapse).
        int sl = _selStartLine, sc = _selStartCol;
        int el = _selEndLine,   ec = _selEndCol;
        if (sl > el || (sl == el && sc > ec)) { std::swap(sl, el); std::swap(sc, ec); }
        // Splice [sl/sc, el/ec) out of the buffer.
        if (sl == el) {
            _lines[sl].erase(sc, ec - sc);
        } else {
            _lines[sl].erase(sc);
            _lines[sl].append(_lines[el].substr(ec));
            _lines.erase(_lines.begin() + sl + 1, _lines.begin() + el + 1);
        }
        _caretLine = sl;
        _caretCol = sc;
        clearSelection();
    }
    if (ch == L'\n') {
        std::wstring tail = _lines[_caretLine].substr(_caretCol);
        _lines[_caretLine].erase(_caretCol);
        _lines.insert(_lines.begin() + _caretLine + 1, tail);
        ++_caretLine;
        _caretCol = 0;
    } else {
        _lines[_caretLine].insert(_caretLine == 0 && false ? 0 : _caretCol, 1, ch);
        ++_caretCol;
    }
    syncDocumentSizeToContent();
    _textCacheDirty = true;
    fireTextChanged();
    return true;
}

bool TextArea::deleteLeft() {
    if (_readOnly) return false;
    if (hasSelection()) {
        // Reuse insertChar's selection-delete branch by simulating an
        // empty insert — simpler: do it inline.
        int sl = _selStartLine, sc = _selStartCol;
        int el = _selEndLine,   ec = _selEndCol;
        if (sl > el || (sl == el && sc > ec)) { std::swap(sl, el); std::swap(sc, ec); }
        if (sl == el) {
            _lines[sl].erase(sc, ec - sc);
        } else {
            _lines[sl].erase(sc);
            _lines[sl].append(_lines[el].substr(ec));
            _lines.erase(_lines.begin() + sl + 1, _lines.begin() + el + 1);
        }
        _caretLine = sl;
        _caretCol = sc;
        clearSelection();
        syncDocumentSizeToContent();
        _textCacheDirty = true;
        fireTextChanged();
        return true;
    }
    if (_caretCol == 0 && _caretLine == 0) return false;
    if (_caretCol == 0) {
        // Join with previous line.
        --_caretLine;
        _caretCol = static_cast<int>(_lines[_caretLine].size());
        _lines[_caretLine].append(_lines[_caretLine + 1]);
        _lines.erase(_lines.begin() + _caretLine + 1);
    } else {
        _lines[_caretLine].erase(_caretCol - 1, 1);
        --_caretCol;
    }
    syncDocumentSizeToContent();
    _textCacheDirty = true;
    fireTextChanged();
    return true;
}

bool TextArea::deleteRight() {
    if (_readOnly) return false;
    if (hasSelection()) return deleteLeft();
    if (_caretLine >= static_cast<int>(_lines.size())) return false;
    if (_caretCol >= static_cast<int>(_lines[_caretLine].size())) {
        if (_caretLine + 1 >= static_cast<int>(_lines.size())) return false;
        _lines[_caretLine].append(_lines[_caretLine + 1]);
        _lines.erase(_lines.begin() + _caretLine + 1);
    } else {
        _lines[_caretLine].erase(_caretCol, 1);
    }
    syncDocumentSizeToContent();
    _textCacheDirty = true;
    fireTextChanged();
    return true;
}

void TextArea::clear() {
    _lines.clear();
    _lines.push_back(L"");
    _caretLine = 0;
    _caretCol = 0;
    clearSelection();
    syncDocumentSizeToContent();
    _textCacheDirty = true;
    fireTextChanged();
}

void TextArea::setCaret(int line, int col) {
    line = std::clamp(line, 0, static_cast<int>(_lines.size()) - 1);
    if (line < 0) line = 0;
    col = std::max(0, col);
    if (col > static_cast<int>(_lines[line].size())) {
        col = static_cast<int>(_lines[line].size());
    }
    _caretLine = line;
    _caretCol = col;
    clearSelection();
}

int TextArea::getCaretLine() const { return _caretLine; }
int TextArea::getCaretCol()  const { return _caretCol; }

void TextArea::setSelection(int sl, int sc, int el, int ec) {
    _selStartLine = sl; _selStartCol = sc;
    _selEndLine = el;   _selEndCol = ec;
    _caretLine = el;
    _caretCol = ec;
}

void TextArea::clearSelection() {
    _selStartLine = _caretLine; _selStartCol = _caretCol;
    _selEndLine = _caretLine;   _selEndCol = _caretCol;
}

void TextArea::selectAll() {
    _selStartLine = 0; _selStartCol = 0;
    _selEndLine = static_cast<int>(_lines.size()) - 1;
    _selEndCol = static_cast<int>(_lines.back().size());
    _caretLine = _selEndLine;
    _caretCol = _selEndCol;
}

bool TextArea::hasSelection() const {
    return _selStartLine != _selEndLine || _selStartCol != _selEndCol;
}

void TextArea::setReadOnly(bool ro) { _readOnly = ro; }
bool TextArea::isReadOnly() const { return _readOnly; }

void TextArea::setLineHeight(float h) {
    _lineHeight = std::max(1.0f, h);
    syncDocumentSizeToContent();
}

void TextArea::syncDocumentSizeToContent() {
    if (_document == nullptr) return;
    const float h = static_cast<float>(_lines.size()) * _lineHeight + 2.0f * kPaddingY;
    // Width is set by ScrollView's layout pass — we provide a minimum
    // hint that allows the longest line to fit if hbar were enabled.
    float maxLineW = 0.0f;
    for (const auto& l : _lines) {
        maxLineW = std::max(maxLineW, static_cast<float>(l.size()) * 7.0f + 2.0f * kPaddingX);
    }
    _document->setSize(math::FVector2(std::max(maxLineW, getSize().x), h));
    if (_scrollView != nullptr) {
        _scrollView->setContentSize(_document->getSize());
    }
}

void TextArea::performLayout() {
    CompoundWidget::performLayout();
    if (_scrollView != nullptr) {
        _scrollView->setSize(getSize());
        _scrollView->setPosition(math::FVector2(0.0f, 0.0f));
    }
    syncDocumentSizeToContent();
}

void TextArea::fireTextChanged() {
    if (_onTextChanged) _onTextChanged(getText());
}

Widget* createTextAreaWidget() { return new TextArea(); }

} // namespace ayt::ui