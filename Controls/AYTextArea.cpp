#include "AYUI/TextArea.h"
#include "AYUI/ScrollBar.h"
#include "AYUI/UIManager.h"
#include "AYUI/Style.h"
#include "AYUI/TextMeasure.h"
#include "AYUI/Clipboard.h"
#include "AYUI/UIKeyCode.h"
#include <algorithm>
#include <string>
#include <vector>

namespace ayt::ui {

// =============================================================================
// Phase C (S4) — UTF-8 → std::wstring helper.
// =============================================================================
// Mirrors AYTextInput.cpp's copy exactly. See AYTextInput.cpp for the
// rationale on byte-to-codepoint mapping and surrogate-pair handling
// (R4 in the Phase C plan). Keeping a parallel copy avoids a new shared
// header for two ~30-LOC helpers that we'd otherwise couple through.
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
            for (int k = 0; k < bytes; ++k) {
                outByteToWChar->push_back(out.size());
            }
        }
        if (ok) out.push_back(static_cast<wchar_t>(cp));
        i += bytes;
    }
    return out;
}
} // namespace

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
    ~TextDocument() override {
        // Phase C (S4): R1 dtor safety. Drop any composition this
        // document owns before destruction so UIManager's
        // _compositionOwner doesn't dangle. Mirrors TextInput's dtor —
        // and like TextInput we pass fireEndOnOwner=false because a
        // virtual dispatch on `this` during destruction is UB.
        //
        // CRITICAL: also drop focus / capture from UIManager if they
        // point to `this`. Otherwise shutdown() dynamic_casts a freed
        // pointer → "no RTTI data" AV. Same UAF pattern as Phase A
        // PR-5 (Menu::close).
        if (UIManager* ui = UIManager::tryGet()) {
            if (ui->getFocusedWidget() == this) {
                ui->clearFocusNoDispatch(this);
            }
            ui->clearCaptureNoDispatch(this);
            ui->clearHoverNoDispatch(this);
            ui->cancelComposition(this, /*fireEndOnOwner*/ false);
        }
    }

    UiCursorHint getCursorHint() const override {
        return UiCursorHint::Beam;
    }

    // Phase C: a TextDocument IS a text-editing widget so the focus
    // gate (UIManager::onTextEditingFocusChanged) flips when focus
    // shifts into / out of a TextArea's body.
    bool isTextEditingWidget() const override { return true; }

    // Phase C: state query — true between Start and End. Exposed so
    // TextArea::isComposing() can delegate; tests use the public
    // TextArea::isComposing() surface rather than poking here.
    bool isComposing() const { return _composing; }

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
        // Phase C (C5): start drag. Returning true from onMouseButtonDown
        // signals UIManager to capture this widget — subsequent
        // onMouseMove events get delivered to us even if the cursor
        // leaves the widget bounds, which is what enables drag-out
        // selection.
        _dragging = true;
        _dragAnchorLine = line;
        _dragAnchorCol = col;
        return true;
    }

    bool onMouseMove(const UIMouseEvent& e) override {
        // Phase C (C5): extend selection from anchor to current position
        // while drag is active. We don't gate on _capturedWidget because
        // UIManager only routes onMouseMove to the captured widget.
        if (!_dragging) return false;
        const math::FVector2 local = e.mousePos - getWorldBounds().getMin();
        const float lineHeight = _owner->getLineHeight();
        int line = static_cast<int>(local.y / lineHeight);
        int col = static_cast<int>(local.x - TextArea::kPaddingX);
        line = std::clamp(line, 0, static_cast<int>(_owner->_lines.size()) - 1);
        if (line < 0) line = 0;
        col = std::max(0, col);
        if (col > static_cast<int>(_owner->_lines[line].size())) {
            col = static_cast<int>(_owner->_lines[line].size());
        }
        // Order anchor / current so start <= end.
        int sl = _dragAnchorLine, sc = _dragAnchorCol;
        int el = line,             ec = col;
        if (sl > el || (sl == el && sc > ec)) {
            std::swap(sl, el);
            std::swap(sc, ec);
        }
        _owner->_selStartLine = sl;
        _owner->_selStartCol = sc;
        _owner->_selEndLine = el;
        _owner->_selEndCol = ec;
        _owner->_caretLine = el;
        _owner->_caretCol = ec;
        return true;
    }

    bool onMouseButtonUp(const UIMouseEvent& /*e*/) override {
        // Phase C (C5): end drag. UIManager clears _capturedWidget
        // automatically on button-up. Return false to let UIManager
        // do its normal capture-release path.
        if (!_dragging) return false;
        _dragging = false;
        return false;
    }

    bool onKeyDown(int keyCode) override {
        // Minimal key routing for v1:
        //   Left / Right → move col
        //   Up / Down → move line (preserve col, clamp to line length)
        //   Home / End → start / end of line
        //   Backspace / Delete → handled by TextArea (insert/delete API)
        //   Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z → undo / redo (P1 polish)
        //   Ctrl+C / Ctrl+X / Ctrl+V → clipboard (PR-A2)
        //
        // Polish (P1): Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z routing. Modifiers
        // are tracked by UIManager::_modifiers — we never see the
        // modifier key itself (intercepted earlier in
        // UIManager::onKeyDown). The bit layout is bit 0 = Shift,
        // bit 1 = Control, bit 2 = Alt.
        UIManager* ui = UIManager::tryGet();
        const uint32_t mods = ui ? ui->getModifiers() : 0u;
        const bool ctrl    = (mods & 0x02u) != 0u;
        const bool shift   = (mods & 0x01u) != 0u;
        if (ctrl && (keyCode == UIKey_Z || keyCode == UIKey_Y)) {
            if (keyCode == UIKey_Y) {
                _owner->redo();
            } else if (shift) {
                // Ctrl+Shift+Z is the macOS redo; honor it so a host
                // running on both platforms has consistent behavior.
                _owner->redo();
            } else {
                _owner->undo();
            }
            return true;
        }
        // PR-A2: clipboard routing. Multi-line paste is split on '\n'
        // (and '\r' stripped) and re-inserted character-by-character
        // through TextArea::insertChar so existing line-splitting +
        // undo-push + caret-clamp logic all fires automatically. There's
        // no public replaceRange on TextArea; we delete the existing
        // selection first via deleteLeft (which respects readOnly) then
        // stream the paste. On readOnly, Ctrl+V is a no-op.
        if (ctrl && keyCode == UIKey_C) {
            if (!_owner->hasSelection()) return true;
            (void)ayt::ui::getClipboard().setText(_owner->getSelectedText());
            return true;
        }
        if (ctrl && keyCode == UIKey_X) {
            if (_owner->isReadOnly() || !_owner->hasSelection()) return true;
            const std::wstring sel = _owner->getSelectedText();
            if (!ayt::ui::getClipboard().setText(sel)) return true;
            // Delete the selection. TextArea has no public replaceRange,
            // so we move caret to start, then backspace once per char
            // (deleteLeft handles selection-collapse + undo-push).
            // Simpler: while hasSelection, deleteLeft.
            while (_owner->hasSelection()) {
                _owner->deleteLeft();
            }
            return true;
        }
        if (ctrl && keyCode == UIKey_V) {
            if (_owner->isReadOnly()) return true;
            std::wstring clip;
            if (!ayt::ui::getClipboard().getText(clip) || clip.empty()) return true;
            // Drop existing selection first so the paste replaces it.
            while (_owner->hasSelection()) {
                _owner->deleteLeft();
            }
            // Strip '\r' (Windows-paste leftover), keep '\n' for new lines.
            std::wstring cleaned;
            cleaned.reserve(clip.size());
            for (const wchar_t ch : clip) {
                if (ch == L'\r') continue;
                cleaned.push_back(ch);
            }
            for (const wchar_t ch : cleaned) {
                _owner->insertChar(ch);
            }
            return true;
        }
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

    // =================================================================
    // Phase C (S4) — IME composition hooks for the document.
    // =================================================================
    // We mirror TextInput's logic but write into TextArea's `_lines`
    // model. `_compositionPreview` stores the candidate; the underline
    // is drawn on the active line in onRender.
    //
    // End splits the committed UTF-8 on '\n' so multi-line IME commits
    // (some CJK IMEs allow pasting multiple lines of candidates) insert
    // as multiple lines. Byte caret → wchar_t codepoint caret via the
    // same utf8ToWString helper.
    // =================================================================
    bool onImeCompositionStart(const std::string& text, int caret) override {
        if (!hasFocus() || _owner->isReadOnly()) return false;
        std::vector<size_t> byteMap;
        _compositionPreview = utf8ToWString(text, &byteMap);
        _compositionCaretBytes = (caret < 0
            || static_cast<size_t>(caret) >= byteMap.size())
            ? static_cast<int>(_compositionPreview.size())
            : static_cast<int>(byteMap[caret]);
        _composing = true;
        return true;
    }
    bool onImeCompositionUpdate(const std::string& text, int caret) override {
        if (!hasFocus() || _owner->isReadOnly()) return false;
        if (!_composing) return onImeCompositionStart(text, caret);
        std::vector<size_t> byteMap;
        _compositionPreview = utf8ToWString(text, &byteMap);
        _compositionCaretBytes = (caret < 0
            || static_cast<size_t>(caret) >= byteMap.size())
            ? static_cast<int>(_compositionPreview.size())
            : static_cast<int>(byteMap[caret]);
        return true;
    }
    bool onImeCompositionEnd(const std::string& committed) override {
        if (!_composing) return false;
        if (!_owner->isReadOnly()) {
            std::wstring committedText = utf8ToWString(committed);
            if (!committedText.empty()) {
                // Split on '\n' so multi-line IME commits become multi-
                // line inserts. TextArea::insertChar handles the caret
                // advance + new-line insertion.
                std::wstring buf;
                for (wchar_t ch : committedText) {
                    if (ch == L'\n' || ch == L'\r') {
                        if (!buf.empty()) {
                            for (wchar_t bc : buf) _owner->insertChar(bc);
                            buf.clear();
                        }
                        if (ch == L'\r') continue; // skip CR; \r\n → \n
                        _owner->insertChar(L'\n');
                    } else {
                        buf.push_back(ch);
                    }
                }
                if (!buf.empty()) {
                    for (wchar_t bc : buf) _owner->insertChar(bc);
                }
            }
        }
        _compositionPreview.clear();
        _compositionCaretBytes = 0;
        _composing = false;
        return true;
    }

    void onRender(IRenderBackend& renderer) override {
        const auto& lines = _owner->_lines;
        const float lh = _owner->getLineHeight();
        const math::FVector2 origin = getWorldBounds().getMin();
        const float fontSize = lh - 4.0f;     // rough visual mapping

        // PR-Container-Shared-Contract: pushClip(pushClip(getClientRect()))).
        // TextDocument has no chrome of its own so getClientRect() ==
        // getWorldBounds(), but the wrapper guarantees partial-line / caret
        // / IME underline at the document edge cannot paint outside the
        // document rect (latent bug when long lines or scrolled content
        // bleeds into neighbouring widgets).
        const math::FRectangle docBounds = getClientRect();
        renderer.pushClip(docBounds);

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
                float x0 = origin.x + TextArea::kPaddingX
                           + measurePrefixWidth(lines[static_cast<size_t>(sl)], static_cast<size_t>(sc));
                float x1 = origin.x + TextArea::kPaddingX
                           + measurePrefixWidth(lines[static_cast<size_t>(sl)], static_cast<size_t>(ec));
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
                const float lineW = measurePrefixWidth(lines[i], lines[i].size());
                math::FRectangle bounds{p, {lineW, lh}};
                renderer.drawText(bounds, lines[i], static_cast<int>(fontSize),
                                  math::FVector4(0.92f, 0.92f, 0.94f, 1.0f));
            }
        }

        // Caret.
        if (hasFocus()) {
            float x = origin.x + TextArea::kPaddingX
                      + measurePrefixWidth(lines[static_cast<size_t>(_owner->_caretLine)],
                                           static_cast<size_t>(_owner->_caretCol));
            float y = origin.y + static_cast<float>(_owner->_caretLine) * lh + 1.0f;
            renderer.drawRect({{x, y}, {1.0f, lh - 2.0f}},
                              math::FVector4(1.0f, 1.0f, 1.0f, 0.95f));
        }

        // Phase C (S4) — IME composition underline. Drawn under the
        // active line, anchored at the caret column. Same sky-blue
        // default as TextInput; honours WidgetStyle override via
        // StyleManager lookup. Width now uses measurePrefixWidth so
        // the underline tracks real glyph metrics on a wired backend.
        if (_composing && !_compositionPreview.empty()) {
            math::FVector4 ulColor(0.30f, 0.65f, 0.95f, 1.0f);
            const std::string sid = getStyleId();
            if (!sid.empty()) {
                const WidgetStyle* ws = StyleManager::get().getStyle(sid);
                if (ws != nullptr) ulColor = ws->compositionUnderlineColor;
            }
            const float caretX = measurePrefixWidth(
                lines[static_cast<size_t>(_owner->_caretLine)],
                static_cast<size_t>(_owner->_caretCol));
            const float ulW = measurePrefixWidth(_compositionPreview,
                                                 _compositionPreview.size());
            const float ulX = origin.x + TextArea::kPaddingX + caretX;
            const float ulY = origin.y + static_cast<float>(_owner->_caretLine) * lh + lh - 3.0f;
            constexpr float ulH = 1.5f;
            renderer.drawRect(math::FRectangle(ulX, ulY, ulX + ulW, ulY + ulH),
                              ulColor);
        }

        // PR-Container-Shared-Contract: matches pushClip(docBounds) above.
        renderer.popClip();
    }

private:
    TextArea* _owner;

    // Phase C (S4) composition state — same shape as TextInput.
    std::wstring _compositionPreview;
    int          _compositionCaretBytes = 0;
    bool         _composing = false;

    // Phase C (C5) drag-select state.
    bool _dragging = false;
    int  _dragAnchorLine = 0;
    int  _dragAnchorCol = 0;
};

// =============================================================================
// TextArea
// =============================================================================

bool TextArea::isComposing() const {
    // _document may be null between construction and ensureChildrenCreated.
    return _document != nullptr && _document->isComposing();
}

FocusableWidget* TextArea::getDocumentAsFocusable() const {
    // static_cast requires the inheritance chain to be visible at this
    // call site — TextDocument is defined in this same translation unit
    // (line ~63) so the cast is well-defined.
    return static_cast<FocusableWidget*>(_document);
}

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
    pushUndo();
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
    // AYUI-DirtyRect-2026-08-26: text buffer fully replaced; the whole
    // document (and its scroll viewport if any) must repaint.
    markDirty();
}

bool TextArea::insertChar(wchar_t ch) {
    if (_readOnly) return false;
    if (_maxLength > 0 && getText().size() + 1 > _maxLength) return false;
    pushUndo();
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
    if (hasSelection() ||
        _caretCol > 0 ||
        _caretLine > 0) {
        pushUndo();
    }
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
    if (_caretCol < static_cast<int>(_lines[_caretLine].size()) ||
        _caretLine + 1 < static_cast<int>(_lines.size())) {
        pushUndo();
    }
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
    pushUndo();
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

// PR-A2: get the currently-selected text as a single wstring (lines
// joined by '\n'). Selection is internal-ordered so start <= end per
// axis (setSelection enforces that). Used by TextDocument::onKeyDown's
// Ctrl+C / Ctrl+X paths. Pre-PR there was no public selection-accessor
// because TextArea had no clipboard support at all.
std::wstring TextArea::getSelectedText() const {
    if (!hasSelection()) return std::wstring();
    int sl = _selStartLine, sc = _selStartCol;
    int el = _selEndLine,   ec = _selEndCol;
    if (sl > el || (sl == el && sc > ec)) {
        std::swap(sl, el);
        std::swap(sc, ec);
    }
    if (sl == el) {
        return _lines[sl].substr(sc, ec - sc);
    }
    std::wstring out;
    out.append(_lines[sl].substr(sc));
    out.push_back(L'\n');
    for (int line = sl + 1; line < el; ++line) {
        out.append(_lines[line]);
        out.push_back(L'\n');
    }
    out.append(_lines[el].substr(0, ec));
    return out;
}

void TextArea::setReadOnly(bool ro) { _readOnly = ro; }
bool TextArea::isReadOnly() const { return _readOnly; }

void TextArea::setLineHeight(float h) {
    _lineHeight = std::max(1.0f, h);
    syncDocumentSizeToContent();
}

void TextArea::syncDocumentSizeToContent() {
    if (_document == nullptr) return;
    // Phase C (C6): when word-wrap is on, the visual line count is
    // larger than _lines.size() because long lines break into multiple
    // visual rows. We compute the wrapped count here so the document
    // height (and therefore the scrollbar range) reflects what the
    // user actually sees.
    int visualLines = 0;
    // Em-width proxy: measure a single "M" so wrap math uses real glyph width
    // when the backend is wired. Falls back to 7px per char via measurePrefixWidth
    // when no backend is active (unit tests, MockRenderer).
    const float emWidth = std::max(1.0f, measurePrefixWidth(L"M", 1));
    const float wrapWidthPx = std::max(1.0f, getSize().x - 2.0f * kPaddingX);
    const int wrapCols = static_cast<int>(wrapWidthPx / emWidth);
    for (const auto& l : _lines) {
        if (!_wordWrap || wrapCols <= 0) {
            visualLines += 1;
        } else {
            // Greedy wrap: ceil(length / wrapCols) visual rows.
            visualLines += std::max(1,
                static_cast<int>((l.size() + wrapCols - 1) / wrapCols));
        }
    }
    const float h = static_cast<float>(visualLines) * _lineHeight + 2.0f * kPaddingY;
    // Width is set by ScrollView's layout pass — we provide a minimum
    // hint that allows the longest line to fit if hbar were enabled.
    float maxLineW = 0.0f;
    for (const auto& l : _lines) {
        const float w = measurePrefixWidth(l, l.size());
        maxLineW = std::max(maxLineW, w + 2.0f * kPaddingX);
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

// ============================================================================
// Polish (P1) — undo / redo infrastructure.
// ============================================================================
// captureSnapshot is the single point where the editing state is copied
// into a TextEditSnapshot. We copy by value rather than share a buffer
// so that subsequent mutations don't invalidate stored snapshots — each
// undo step must remain independently restorable even after many other
// edits have happened.
//
// restoreSnapshot is the inverse: it pushes back into all six editing
// state fields and marks the text cache dirty. It does NOT call
// fireTextChanged because onTextChanged listeners would otherwise get a
// spurious callback for an undo (which is already a user-visible action
// the host should learn about via the canRedo + getText pair, not via
// onTextChanged). Listeners can observe undo via a future hook if needed.
//
// pushUndo is the bookkeeping: capture current state, append to undo,
// drop redo (linear-history semantics). Called at the TOP of each
// mutating op BEFORE the change is applied. The undo entry therefore
// represents "the state I'm leaving".
// ============================================================================

TextArea::TextEditSnapshot TextArea::captureSnapshot() const {
    TextEditSnapshot s;
    s.lines        = _lines;
    s.caretLine    = _caretLine;
    s.caretCol     = _caretCol;
    s.selStartLine = _selStartLine;
    s.selStartCol  = _selStartCol;
    s.selEndLine   = _selEndLine;
    s.selEndCol    = _selEndCol;
    s.hasSelection = hasSelection();
    return s;
}

void TextArea::restoreSnapshot(const TextEditSnapshot& s) {
    _lines         = s.lines;
    _caretLine     = s.caretLine;
    _caretCol      = s.caretCol;
    _selStartLine  = s.selStartLine;
    _selStartCol   = s.selStartCol;
    _selEndLine    = s.selEndLine;
    _selEndCol     = s.selEndCol;
    // Defensive clamp: a restored snapshot might reference an out-of-range
    // caret if the host cleared _lines externally between pushes (rare but
    // possible via setText('')). Clamp rather than trust.
    if (_lines.empty()) _lines.push_back(L"");
    _caretLine = std::clamp(_caretLine, 0, static_cast<int>(_lines.size()) - 1);
    if (_caretLine < 0) _caretLine = 0;
    _caretCol = std::clamp(_caretCol, 0,
        static_cast<int>(_lines[_caretLine].size()));
    _textCacheDirty = true;
    syncDocumentSizeToContent();
}

void TextArea::pushUndo() {
    _undoStack.push_back(captureSnapshot());
    if (_undoStack.size() > kMaxHistoryEntries) {
        // Drop oldest. Used to keep memory bounded in pathological long
        // sessions (10k+ character typing). 100 entries × 6 ints × ~50
        // chars/line ≈ O(few KB) — well within budget.
        _undoStack.erase(_undoStack.begin());
    }
    // Fresh edit invalidates redo path (standard editor semantics).
    _redoStack.clear();
}

bool TextArea::canUndo() const { return !_undoStack.empty(); }
bool TextArea::canRedo() const { return !_redoStack.empty(); }

void TextArea::undo() {
    if (_undoStack.empty()) return;
    // Push current state onto redo so redo can come back here.
    _redoStack.push_back(captureSnapshot());
    // Pop undo and restore.
    TextEditSnapshot s = _undoStack.back();
    _undoStack.pop_back();
    restoreSnapshot(s);
}

void TextArea::redo() {
    if (_redoStack.empty()) return;
    _undoStack.push_back(captureSnapshot());
    TextEditSnapshot s = _redoStack.back();
    _redoStack.pop_back();
    restoreSnapshot(s);
}

Widget* createTextAreaWidget() { return new TextArea(); }

} // namespace ayt::ui