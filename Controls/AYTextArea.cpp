#include "AYUI/TextArea.h"
#include "AYUI/ScrollBar.h"
#include "AYUI/UIManager.h"
#include "AYUI/Style.h"
#include "AYUI/TextMeasure.h"
#include "AYUI/UnicodeText.h"
#include "AYUI/Clipboard.h"
#include "AYUI/UIKeyCode.h"
#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

// MSVC C4172 fires on every `return buildVisualLines(...)` chain
// even when both functions return a member-field reference — the
// compiler cannot prove the lifetime of the chained return value
// extends past the call. Suppress for this translation unit: the
// chain is safe (buildVisualLines returns &this->_cachedVisualLines).
#pragma warning(push)
#pragma warning(disable : 4172)

namespace ayt::ui {

namespace {
float measureRangeWidth(const std::wstring& text, size_t start, size_t end,
                        int fontSize, IRenderBackend* backend = nullptr) {
    if (end <= start || start >= text.size()) return 0.0f;
    end = std::min(end, text.size());
    const std::wstring range = text.substr(start, end - start);
    return measurePrefixWidth(range, range.size(), backend, fontSize);
}

int columnFromRangeX(const std::wstring& text, int start, int end, float x,
                     int fontSize, IRenderBackend* backend = nullptr) {
    start = std::clamp(start, 0, static_cast<int>(text.size()));
    end = std::clamp(end, start, static_cast<int>(text.size()));
    if (x <= 0.0f || start == end) return start;
    const UnicodeTextAnalysis analysis = analyzeUnicodeText(text);
    int previous = start;
    float previousX = 0.0f;
    for (const UnicodeTextCluster& cluster : analysis.clusters) {
        const int clusterStart = static_cast<int>(cluster.textStart);
        const int boundary = static_cast<int>(cluster.textStart + cluster.textLength);
        if (boundary <= start) continue;
        if (clusterStart >= end) break;
        const int clampedBoundary = std::min(boundary, end);
        const float boundaryX = measureRangeWidth(
            text, static_cast<size_t>(start), static_cast<size_t>(clampedBoundary),
            fontSize, backend);
        if (x < (previousX + boundaryX) * 0.5f) return previous;
        previous = clampedBoundary;
        previousX = boundaryX;
    }
    return end;
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

    bool acceptsEditorTab() const {
        return _owner != nullptr && _owner->doesTabInsertIndent()
            && !_owner->isReadOnly();
    }

    bool onMouseButtonDown(const UIMouseEvent& e) override {
        if (e.mouseButton != 0) return false;
        if (UIManager* ui = UIManager::tryGet()) ui->setFocus(this);
        const math::FVector2 local = e.mousePos - getWorldBounds().getMin();
        int line = 0;
        int col = 0;
        _owner->hitTestDocumentPosition(local, line, col);
        if (_lastClickTime >= 0.0f && line == _lastClickLine
            && std::abs(col - _lastClickCol) <= 1) {
            _owner->selectWordAt(line, col);
            _lastClickTime = -1.0f;
            _dragging = false;
            return true;
        }
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
        _lastClickTime = 0.0f;
        _lastClickLine = line;
        _lastClickCol = col;
        return true;
    }

    bool onMouseMove(const UIMouseEvent& e) override {
        // Phase C (C5): extend selection from anchor to current position
        // while drag is active. We don't gate on _capturedWidget because
        // UIManager only routes onMouseMove to the captured widget.
        if (!_dragging) return false;
        const math::FVector2 local = e.mousePos - getWorldBounds().getMin();
        int line = 0;
        int col = 0;
        _owner->hitTestDocumentPosition(local, line, col);
        _owner->_selStartLine = _dragAnchorLine;
        _owner->_selStartCol = _dragAnchorCol;
        _owner->_selEndLine = line;
        _owner->_selEndCol = col;
        _owner->_caretLine = line;
        _owner->_caretCol = col;
        // A real drag is not the first half of a double-click. Keeping the
        // click candidate alive here made the next nearby click select a
        // word instead of collapsing the previous drag selection.
        if (line != _dragAnchorLine || col != _dragAnchorCol) {
            _lastClickTime = -1.0f;
        }
        _owner->invalidateDocument();
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

    void tick(float dt) override {
        Widget::tick(dt);
        if (_lastClickTime < 0.0f) return;
        _lastClickTime += dt;
        if (_lastClickTime > 0.45f) _lastClickTime = -1.0f;
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
        if (ctrl && keyCode == UIKey_A) {
            _owner->selectAll();
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
            _owner->deleteLeft();
            return true;
        }
        if (ctrl && keyCode == UIKey_V) {
            if (_owner->isReadOnly()) return true;
            std::wstring clip;
            if (!ayt::ui::getClipboard().getText(clip) || clip.empty()) return true;
            // Strip '\r' (Windows-paste leftover), keep '\n' for new lines.
            std::wstring cleaned;
            cleaned.reserve(clip.size());
            for (const wchar_t ch : clip) {
                if (ch == L'\r') continue;
                cleaned.push_back(ch);
            }
            _owner->insertText(cleaned);
            return true;
        }
        int line = _owner->_caretLine;
        int col  = _owner->_caretCol;
        const int fontSize = _owner->effectiveFontSize();
        const float availableWidth = std::max(
            1.0f, _owner->getSize().x - _owner->textStartX()
                - TextArea::kPaddingX);
        const std::vector<TextArea::VisualLine> visual =
            _owner->buildVisualLines(availableWidth);
        auto currentVisualIndex = [&]() -> int {
            for (size_t i = 0; i < visual.size(); ++i) {
                if (visual[i].logicalLine != line) continue;
                const bool lastForLogical = i + 1u == visual.size()
                    || visual[i + 1u].logicalLine != line;
                if (col >= visual[i].startCol
                    && (col < visual[i].endCol || lastForLogical)) {
                    return static_cast<int>(i);
                }
            }
            return visual.empty() ? -1 : 0;
        };
        switch (keyCode) {
        case UIKey_Left:
            if (col > 0) {
                col = static_cast<int>(previousGraphemeBoundary(
                    _owner->_lines[line], static_cast<size_t>(col)));
            } else if (line > 0) {
                --line;
                col = static_cast<int>(_owner->_lines[line].size());
            }
            break;
        case UIKey_Right:
            if (col < static_cast<int>(_owner->_lines[line].size())) {
                col = static_cast<int>(nextGraphemeBoundary(
                    _owner->_lines[line], static_cast<size_t>(col)));
            } else if (line + 1 < static_cast<int>(_owner->_lines.size())) {
                ++line;
                col = 0;
            }
            break;
        case UIKey_Up:
        case UIKey_Down: {
            const int current = currentVisualIndex();
            if (current >= 0) {
                const int target = std::clamp(current
                    + (keyCode == UIKey_Up ? -1 : 1),
                    0, static_cast<int>(visual.size()) - 1);
                const float x = measureRangeWidth(_owner->_lines[line],
                    static_cast<size_t>(visual[current].startCol),
                    static_cast<size_t>(col), fontSize);
                line = visual[target].logicalLine;
                col = columnFromRangeX(_owner->_lines[line],
                    visual[target].startCol, visual[target].endCol, x,
                    fontSize);
            }
            break;
        }
        case UIKey_Home: {
            const int current = currentVisualIndex();
            col = current >= 0 ? visual[current].startCol : 0;
            break;
        }
        case UIKey_End: {
            const int current = currentVisualIndex();
            col = current >= 0 ? visual[current].endCol
                               : static_cast<int>(_owner->_lines[line].size());
            break;
        }
        case UIKey_Backspace: _owner->deleteLeft(); return true;
        case UIKey_Delete:    _owner->deleteRight(); return true;
        case UIKey_Tab:
            return _owner->doesTabInsertIndent()
                ? _owner->applyIndent(shift) : true;
        default: return false;
        }
        if (shift) _owner->setCaretExtendingSelection(line, col);
        else _owner->setCaret(line, col);
        return true;
    }

    bool onTextInput(wchar_t ch) override {
        if (ch == L'\r') ch = L'\n';
        // Win32 can emit WM_CHAR('\t') after the already-consumed Tab
        // key-down. Code-editor TextAreas handle that key in applyIndent();
        // accepting the text event as well inserted a literal tab that the
        // font displayed as a tofu square.
        if (ch == L'\t' && acceptsEditorTab()) return true;
        return _owner->insertChar(ch);
    }

    bool onTextInputText(const std::wstring& text) override {
        std::wstring normalized;
        normalized.reserve(text.size());
        bool previousWasCr = false;
        for (wchar_t ch : text) {
            if (ch == L'\r') {
                normalized.push_back(L'\n');
                previousWasCr = true;
            } else {
                if (ch == L'\n' && previousWasCr) {
                    previousWasCr = false;
                    continue;
                }
                previousWasCr = false;
                if (ch == L'\t' && acceptsEditorTab()) {
                    // The matching key-down already inserted indentation.
                    // Ctrl+V bypasses this path and is normalized by
                    // TextArea::insertText, so pasted tabs are retained as
                    // indentation rather than dropped.
                    continue;
                }
                if (ch >= 0x20 || ch == L'\n' || ch == L'\t') {
                    normalized.push_back(ch);
                }
            }
        }
        return normalized.empty() ? true : _owner->insertText(normalized);
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
        _compositionPreview = decodeUtf8Text(text, &byteMap);
        _compositionCaretBytes = (caret < 0
            || static_cast<size_t>(caret) >= byteMap.size())
            ? static_cast<int>(_compositionPreview.size())
            : static_cast<int>(byteMap[caret]);
        _composing = true;
        markDirty();
        return true;
    }
    bool onImeCompositionUpdate(const std::string& text, int caret) override {
        if (!hasFocus() || _owner->isReadOnly()) return false;
        if (!_composing) return onImeCompositionStart(text, caret);
        std::vector<size_t> byteMap;
        _compositionPreview = decodeUtf8Text(text, &byteMap);
        _compositionCaretBytes = (caret < 0
            || static_cast<size_t>(caret) >= byteMap.size())
            ? static_cast<int>(_compositionPreview.size())
            : static_cast<int>(byteMap[caret]);
        markDirty();
        return true;
    }
    bool onImeCompositionEnd(const std::string& committed) override {
        if (!_composing) return false;
        if (!_owner->isReadOnly()) {
            std::wstring committedText = decodeUtf8Text(committed);
            if (!committedText.empty()) {
                _owner->insertText(committedText);
            }
        }
        _compositionPreview.clear();
        _compositionCaretBytes = 0;
        _composing = false;
        markDirty();
        return true;
    }

    void onRender(IRenderBackend& renderer) override {
        const auto& lines = _owner->_lines;
        const float lh = _owner->getLineHeight();
        const math::FVector2 origin = getWorldBounds().getMin();
        const int fontSize = _owner->effectiveFontSize();
        const float gutter = _owner->gutterWidth(&renderer);
        const float textOriginX = origin.x + _owner->textStartX(&renderer);
        const float availableWidth = std::max(
            1.0f, getSize().x - _owner->textStartX(&renderer)
                - TextArea::kPaddingX);
        const std::vector<TextArea::VisualLine> visual =
            _owner->buildVisualLines(availableWidth, &renderer);

        // PR-Container-Shared-Contract: pushClip(pushClip(getClientRect()))).
        // TextDocument has no chrome of its own so getClientRect() ==
        // getWorldBounds(), but the wrapper guarantees partial-line / caret
        // / IME underline at the document edge cannot paint outside the
        // document rect (latent bug when long lines or scrolled content
        // bleeds into neighbouring widgets).
        const math::FRectangle docBounds = getClientRect();
        renderer.pushClip(docBounds);

        // Code-editor gutter. It scrolls with the document vertically, while
        // remaining a distinct column with a one-pixel separator.
        if (_owner->areLineNumbersVisible() && gutter > 0.0f) {
            const float separatorX = origin.x + gutter - 1.0f;
            renderer.drawRect(
                math::FRectangle(origin.x, docBounds.minY,
                                 origin.x + gutter, docBounds.maxY),
                math::FVector4(0.105f, 0.108f, 0.12f, 1.0f));
            renderer.drawRect(
                math::FRectangle(separatorX, docBounds.minY,
                                 separatorX + 1.0f, docBounds.maxY),
                math::FVector4(0.25f, 0.27f, 0.31f, 1.0f));
        }

        // Selection highlight, projected across every affected visual row.
        if (_owner->hasSelection()) {
            int sl = _owner->_selStartLine;
            int sc = _owner->_selStartCol;
            int el = _owner->_selEndLine;
            int ec = _owner->_selEndCol;
            // Order so start <= end.
            if (sl > el || (sl == el && sc > ec)) {
                std::swap(sl, el);
                std::swap(sc, ec);
            }
            for (size_t row = 0; row < visual.size(); ++row) {
                const TextArea::VisualLine& segment = visual[row];
                if (segment.logicalLine < sl || segment.logicalLine > el) continue;
                const int selectionStart = segment.logicalLine == sl ? sc : 0;
                const int selectionEnd = segment.logicalLine == el
                    ? ec : static_cast<int>(lines[segment.logicalLine].size());
                const int a = std::max(segment.startCol, selectionStart);
                const int b = std::min(segment.endCol, selectionEnd);
                if (a >= b) continue;
                const float x0 = textOriginX
                    + measureRangeWidth(lines[segment.logicalLine],
                        static_cast<size_t>(segment.startCol), static_cast<size_t>(a),
                        fontSize, &renderer);
                const float x1 = textOriginX
                    + measureRangeWidth(lines[segment.logicalLine],
                        static_cast<size_t>(segment.startCol), static_cast<size_t>(b),
                        fontSize, &renderer);
                const float y = origin.y + TextArea::kPaddingY
                    + static_cast<float>(row) * lh;
                // FRectangle's two-vector constructor is (min, max), not
                // (position, size). Passing width/height as the second vector
                // inverted every translated selection rectangle.
                renderer.drawRect(
                    math::FRectangle(x0, y, x1, y + lh),
                                  math::FVector4(0.30f, 0.45f, 0.78f, 0.35f));
            }
        }

        // Visual lines. Soft wraps never mutate the logical line buffer.
        for (size_t i = 0; i < visual.size(); ++i) {
            const TextArea::VisualLine& segment = visual[i];
            math::FVector2 p(textOriginX,
                             origin.y + static_cast<float>(i) * lh
                                 + TextArea::kPaddingY);
            const bool firstVisualRow = i == 0u
                || visual[i - 1u].logicalLine != segment.logicalLine;
            if (_owner->areLineNumbersVisible() && firstVisualRow) {
                const std::wstring number = std::to_wstring(
                    static_cast<unsigned long long>(segment.logicalLine + 1));
                const float numberWidth = measurePrefixWidth(
                    number, number.size(), &renderer, fontSize);
                const float right = origin.x + gutter - 7.0f;
                renderer.drawText(
                    math::FRectangle(right - numberWidth, p.y,
                                     right, p.y + lh),
                    number, fontSize,
                    math::FVector4(0.48f, 0.51f, 0.58f, 1.0f));
            }
            if (segment.endCol > segment.startCol) {
                const std::wstring& line = lines[segment.logicalLine];
                const math::FVector4 defaultColor(
                    0.92f, 0.92f, 0.94f, 1.0f);
                std::vector<TextArea::SyntaxSpan> spans;
                if (_owner->_syntaxHighlighter) {
                    spans = _owner->_syntaxHighlighter(
                        static_cast<size_t>(segment.logicalLine), line);
                    std::stable_sort(spans.begin(), spans.end(),
                        [](const TextArea::SyntaxSpan& a,
                           const TextArea::SyntaxSpan& b) {
                            return a.start < b.start;
                        });
                }
                const size_t rowStart = static_cast<size_t>(segment.startCol);
                const size_t rowEnd = static_cast<size_t>(segment.endCol);
                size_t cursor = rowStart;
                const auto drawRange = [&](size_t start, size_t end,
                                           const math::FVector4& color) {
                    if (end <= start) return;
                    const float x = p.x + measureRangeWidth(
                        line, rowStart, start, fontSize, &renderer);
                    const float width = measureRangeWidth(
                        line, start, end, fontSize, &renderer);
                    renderer.drawText(
                        math::FRectangle(x, p.y, x + width, p.y + lh),
                        line.substr(start, end - start), fontSize, color);
                };
                for (const TextArea::SyntaxSpan& span : spans) {
                    const size_t spanEnd = span.start + span.length;
                    if (span.length == 0u || spanEnd <= rowStart
                        || span.start >= rowEnd) {
                        continue;
                    }
                    const size_t a = std::max({rowStart, cursor, span.start});
                    const size_t b = std::min(rowEnd, spanEnd);
                    if (a > cursor) drawRange(cursor, a, defaultColor);
                    if (b > a) {
                        drawRange(a, b, span.color);
                        cursor = b;
                    }
                }
                if (cursor < rowEnd) drawRange(cursor, rowEnd, defaultColor);
            }
        }

        // Caret.
        if (hasFocus()) {
            size_t caretRow = 0;
            for (size_t i = 0; i < visual.size(); ++i) {
                if (visual[i].logicalLine != _owner->_caretLine) continue;
                const bool lastForLogical = i + 1u == visual.size()
                    || visual[i + 1u].logicalLine != _owner->_caretLine;
                if (_owner->_caretCol >= visual[i].startCol
                    && (_owner->_caretCol < visual[i].endCol || lastForLogical)) {
                    caretRow = i;
                    break;
                }
            }
            const TextArea::VisualLine& segment = visual[caretRow];
            const float x = textOriginX
                + measureRangeWidth(lines[segment.logicalLine],
                    static_cast<size_t>(segment.startCol),
                    static_cast<size_t>(_owner->_caretCol), fontSize, &renderer);
            const float y = origin.y + TextArea::kPaddingY
                + static_cast<float>(caretRow) * lh + 1.0f;
            renderer.drawRect(
                math::FRectangle(x, y, x + 1.0f, y + lh - 2.0f),
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
            size_t caretRow = 0;
            for (size_t i = 0; i < visual.size(); ++i) {
                if (visual[i].logicalLine != _owner->_caretLine) continue;
                const bool lastForLogical = i + 1u == visual.size()
                    || visual[i + 1u].logicalLine != _owner->_caretLine;
                if (_owner->_caretCol >= visual[i].startCol
                    && (_owner->_caretCol < visual[i].endCol || lastForLogical)) {
                    caretRow = i;
                    break;
                }
            }
            const TextArea::VisualLine& caretSegment = visual[caretRow];
            const float caretX = measureRangeWidth(
                lines[caretSegment.logicalLine],
                static_cast<size_t>(caretSegment.startCol),
                static_cast<size_t>(_owner->_caretCol), fontSize, &renderer);
            const float ulW = measurePrefixWidth(_compositionPreview,
                                                 _compositionPreview.size(),
                                                 &renderer, fontSize);
            const float ulX = textOriginX + caretX;
            const float ulY = origin.y + TextArea::kPaddingY
                + static_cast<float>(caretRow) * lh + lh - 3.0f;
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
    float _lastClickTime = -1.0f;
    int _lastClickLine = 0;
    int _lastClickCol = 0;
};

bool TextArea::focusedDocumentAcceptsTab(const Widget* focused) {
    const auto* document = dynamic_cast<const TextDocument*>(focused);
    return document != nullptr && document->acceptsEditorTab();
}

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
    // AYUI-Audit-2026-08-26 Bug1 (rebase fix): lifetime audit caught
    // that the original comment was misleading — _document is NOT a
    // child of TextArea via addChild(). ensureChildrenCreated() hands
    // it to ScrollView::setContent() which uses addChildExternal
    // (host-owned lifetime). destroyWidgetTree skips externally-owned
    // children, so the TextDocument leaked per loader-built TextArea.
    //
    // Destruction ordering (destroyWidgetTree path):
    //   1. destroyWidgetTree(_scrollView) runs first — _scrollView and
    //      its bars (also externally owned) are torn down BEFORE we get
    //      here. So by the time ~TextArea body runs, _document's parent
    //      pointer is dangling but _scrollView's dtor chain is done.
    //   2. delete _document is therefore safe at the START of this body.
    //      We delete FIRST, then null out the raw pointer slot, then
    //      null _scrollView (defensive, in case a derived class ever
    //      re-enters during destruction).
    //
    // Stack-scoped TextArea (host-delete path without destroyWidgetTree)
    // is unchanged: both _scrollView and _document leak, but that's
    // the UI-OWN-1 contract — Widget never owns its children. The
    // audit specifically flagged the loader path (always uses
    // destroyWidgetTree); that's now closed.
    delete _document;
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
    std::wstring normalized;
    normalized.reserve(text.size());
    size_t lineColumn = 0u;
    const size_t tabWidth = std::max<size_t>(1u, _tabWidth);
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\r') {
            normalized.push_back(L'\n');
            lineColumn = 0u;
            if (i + 1u < text.size() && text[i + 1u] == L'\n') ++i;
        } else if (text[i] == L'\n') {
            normalized.push_back(L'\n');
            lineColumn = 0u;
        } else if (text[i] == L'\t' && _tabInsertsIndent) {
            const size_t spaces = tabWidth - (lineColumn % tabWidth);
            normalized.append(spaces, L' ');
            lineColumn += spaces;
        } else {
            normalized.push_back(text[i]);
            ++lineColumn;
        }
    }
    if (_maxLength > 0 && normalized.size() > _maxLength) {
        normalized.resize(floorGraphemeBoundary(normalized, _maxLength));
    }
    _lines.clear();
    size_t start = 0;
    for (size_t i = 0; i <= normalized.size(); ++i) {
        if (i == normalized.size() || normalized[i] == L'\n') {
            _lines.push_back(normalized.substr(start, i - start));
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
    invalidateDocument();
}

bool TextArea::insertChar(wchar_t ch) {
    return insertText(std::wstring(1, ch));
}

bool TextArea::insertText(const std::wstring& text) {
    if (_readOnly || text.empty()) return false;
    const bool replacingSelection = hasSelection();
    int insertionColumn = _caretCol;
    if (replacingSelection) {
        int startLine = _selStartLine;
        int startCol = _selStartCol;
        int endLine = _selEndLine;
        int endCol = _selEndCol;
        if (startLine > endLine || (startLine == endLine && startCol > endCol)) {
            std::swap(startLine, endLine);
            std::swap(startCol, endCol);
        }
        insertionColumn = startCol;
    }
    std::wstring normalized;
    normalized.reserve(text.size());
    size_t lineColumn = static_cast<size_t>(std::max(0, insertionColumn));
    const size_t tabWidth = std::max<size_t>(1u, _tabWidth);
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\r') {
            normalized.push_back(L'\n');
            lineColumn = 0u;
            if (i + 1u < text.size() && text[i + 1u] == L'\n') ++i;
        } else if (text[i] == L'\n') {
            normalized.push_back(L'\n');
            lineColumn = 0u;
        } else if (text[i] == L'\t' && _tabInsertsIndent) {
            const size_t spaces = tabWidth - (lineColumn % tabWidth);
            normalized.append(spaces, L' ');
            lineColumn += spaces;
        } else {
            normalized.push_back(text[i]);
            ++lineColumn;
        }
    }

    if (_maxLength > 0) {
        const size_t selectedLength = replacingSelection ? getSelectedText().size() : 0u;
        const size_t retainedLength = getText().size() - selectedLength;
        const size_t room = retainedLength < _maxLength
            ? _maxLength - retainedLength : 0u;
        if (normalized.size() > room) {
            normalized.resize(floorGraphemeBoundary(normalized, room));
        }
        if (normalized.empty() && !replacingSelection) return false;
    }

    pushUndo();
    deleteSelectionWithoutHistory();
    size_t start = 0;
    for (size_t i = 0; i <= normalized.size(); ++i) {
        if (i != normalized.size() && normalized[i] != L'\n') continue;
        if (i > start) {
            const std::wstring span = normalized.substr(start, i - start);
            _lines[_caretLine].insert(static_cast<size_t>(_caretCol), span);
            _caretCol += static_cast<int>(span.size());
        }
        if (i < normalized.size()) {
            std::wstring tail = _lines[_caretLine].substr(static_cast<size_t>(_caretCol));
            _lines[_caretLine].erase(static_cast<size_t>(_caretCol));
            _lines.insert(_lines.begin() + _caretLine + 1, std::move(tail));
            ++_caretLine;
            _caretCol = 0;
        }
        start = i + 1u;
    }
    clearSelection();
    syncDocumentSizeToContent();
    _textCacheDirty = true;
    fireTextChanged();
    invalidateDocument();
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
        deleteSelectionWithoutHistory();
        syncDocumentSizeToContent();
        _textCacheDirty = true;
        fireTextChanged();
        invalidateDocument();
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
        const size_t previous = previousGraphemeBoundary(
            _lines[_caretLine], static_cast<size_t>(_caretCol));
        _lines[_caretLine].erase(previous,
            static_cast<size_t>(_caretCol) - previous);
        _caretCol = static_cast<int>(previous);
    }
    syncDocumentSizeToContent();
    _textCacheDirty = true;
    fireTextChanged();
    invalidateDocument();
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
        const size_t next = nextGraphemeBoundary(
            _lines[_caretLine], static_cast<size_t>(_caretCol));
        _lines[_caretLine].erase(static_cast<size_t>(_caretCol),
            next - static_cast<size_t>(_caretCol));
    }
    syncDocumentSizeToContent();
    _textCacheDirty = true;
    fireTextChanged();
    invalidateDocument();
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
    invalidateDocument();
}

void TextArea::setCaret(int line, int col) {
    line = std::clamp(line, 0, static_cast<int>(_lines.size()) - 1);
    if (line < 0) line = 0;
    col = static_cast<int>(floorGraphemeBoundary(
        _lines[line], static_cast<size_t>(std::max(0, col))));
    _caretLine = line;
    _caretCol = col;
    clearSelection();
    invalidateDocument();
}

int TextArea::getCaretLine() const { return _caretLine; }
int TextArea::getCaretCol()  const { return _caretCol; }

void TextArea::setSelection(int sl, int sc, int el, int ec) {
    sl = std::clamp(sl, 0, static_cast<int>(_lines.size()) - 1);
    el = std::clamp(el, 0, static_cast<int>(_lines.size()) - 1);
    sc = static_cast<int>(floorGraphemeBoundary(
        _lines[sl], static_cast<size_t>(std::max(0, sc))));
    ec = static_cast<int>(floorGraphemeBoundary(
        _lines[el], static_cast<size_t>(std::max(0, ec))));
    _selStartLine = sl; _selStartCol = sc;
    _selEndLine = el;   _selEndCol = ec;
    _caretLine = el;
    _caretCol = ec;
    invalidateDocument();
}

void TextArea::clearSelection() {
    _selStartLine = _caretLine; _selStartCol = _caretCol;
    _selEndLine = _caretLine;   _selEndCol = _caretCol;
    invalidateDocument();
}

void TextArea::selectAll() {
    _selStartLine = 0; _selStartCol = 0;
    _selEndLine = static_cast<int>(_lines.size()) - 1;
    _selEndCol = static_cast<int>(_lines.back().size());
    _caretLine = _selEndLine;
    _caretCol = _selEndCol;
    invalidateDocument();
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
    invalidateDocument();
}

void TextArea::setLineNumbersVisible(bool visible) {
    if (_showLineNumbers == visible) return;
    _showLineNumbers = visible;
    syncDocumentSizeToContent();
    markBoundsDirty();
    invalidateDocument();
}

void TextArea::setSyntaxHighlighter(SyntaxHighlighter highlighter) {
    _syntaxHighlighter = std::move(highlighter);
    invalidateDocument();
}

void TextArea::setTabInsertsIndent(bool enabled) {
    _tabInsertsIndent = enabled;
}

void TextArea::setTabWidth(size_t spaces) {
    _tabWidth = std::clamp<size_t>(spaces, 1u, 16u);
}

float TextArea::gutterWidth(IRenderBackend* backend) const {
    if (!_showLineNumbers) return 0.0f;
    const std::wstring largest = std::to_wstring(
        static_cast<unsigned long long>(std::max<size_t>(1u, _lines.size())));
    const float digits = measurePrefixWidth(
        largest, largest.size(), backend, effectiveFontSize());
    return std::max(32.0f, digits + 14.0f);
}

float TextArea::textStartX(IRenderBackend* backend) const {
    return gutterWidth(backend) + kPaddingX;
}

bool TextArea::applyIndent(bool unindent) {
    if (_readOnly || !_tabInsertsIndent) return true;
    const int width = static_cast<int>(std::max<size_t>(1u, _tabWidth));

    if (!hasSelection()) {
        if (!unindent) {
            const int count = width - (_caretCol % width);
            return insertText(std::wstring(static_cast<size_t>(count), L' '));
        }
        std::wstring& line = _lines[static_cast<size_t>(_caretLine)];
        int remove = 0;
        if (!line.empty() && line.front() == L'\t') {
            remove = 1;
        } else {
            while (remove < width
                   && remove < static_cast<int>(line.size())
                   && line[static_cast<size_t>(remove)] == L' ') {
                ++remove;
            }
        }
        if (remove == 0) return true;
        pushUndo();
        line.erase(0u, static_cast<size_t>(remove));
        _caretCol = std::max(0, _caretCol - remove);
        clearSelection();
        syncDocumentSizeToContent();
        _textCacheDirty = true;
        fireTextChanged();
        invalidateDocument();
        return true;
    }

    int first = _selStartLine;
    int last = _selEndLine;
    int normalizedEndCol = _selEndCol;
    if (first > last || (first == last && _selStartCol > _selEndCol)) {
        std::swap(first, last);
        normalizedEndCol = _selStartCol;
    }
    // A selection ending at column zero conventionally excludes that final
    // line from block indentation.
    if (last > first && normalizedEndCol == 0) --last;

    std::vector<int> deltas(_lines.size(), 0);
    size_t added = 0u;
    for (int lineIndex = first; lineIndex <= last; ++lineIndex) {
        const std::wstring& line = _lines[static_cast<size_t>(lineIndex)];
        if (!unindent) {
            deltas[static_cast<size_t>(lineIndex)] = width;
            added += static_cast<size_t>(width);
            continue;
        }
        int remove = 0;
        if (!line.empty() && line.front() == L'\t') {
            remove = 1;
        } else {
            while (remove < width
                   && remove < static_cast<int>(line.size())
                   && line[static_cast<size_t>(remove)] == L' ') {
                ++remove;
            }
        }
        deltas[static_cast<size_t>(lineIndex)] = -remove;
    }
    if (!unindent && _maxLength > 0
        && getText().size() + added > _maxLength) {
        return true;
    }
    if (unindent && std::none_of(
            deltas.begin(), deltas.end(), [](int delta) { return delta != 0; })) {
        return true;
    }

    pushUndo();
    const std::wstring indent(static_cast<size_t>(width), L' ');
    for (int lineIndex = first; lineIndex <= last; ++lineIndex) {
        std::wstring& line = _lines[static_cast<size_t>(lineIndex)];
        const int delta = deltas[static_cast<size_t>(lineIndex)];
        if (delta > 0) {
            line.insert(0u, indent);
        } else if (delta < 0) {
            line.erase(0u, static_cast<size_t>(-delta));
        }
    }
    const auto adjust = [&deltas](int line, int col) {
        if (line < 0 || line >= static_cast<int>(deltas.size())) return col;
        return std::max(0, col + deltas[static_cast<size_t>(line)]);
    };
    _selStartCol = adjust(_selStartLine, _selStartCol);
    _selEndCol = adjust(_selEndLine, _selEndCol);
    _caretLine = _selEndLine;
    _caretCol = _selEndCol;
    syncDocumentSizeToContent();
    _textCacheDirty = true;
    fireTextChanged();
    invalidateDocument();
    return true;
}

std::vector<TextArea::VisualLine>& TextArea::buildVisualLines(
    float availableWidth, IRenderBackend* backend) const {
    availableWidth = std::max(1.0f, availableWidth);

    // Audit H-R-1..3: pre-fix, this method rebuilt every visual line on
    // every call, including the render path which fires every frame.
    // Memoize keyed on (text content hash, availableWidth rounded to a
    // pixel, wordWrap flag, line height). invalidateDocument() bumps the
    // dirty flag whenever _lines change. Width changes (resize, gutter
    // toggle, font size change) are caught by the availableWidth +
    // lineHeight comparison below.
    const float widthKey = std::floor(availableWidth);
    const int lineHeightKey = static_cast<int>(std::floor(_lineHeight));
    auto recomputeHash = [&]() -> size_t {
        size_t h = static_cast<size_t>(_wordWrap ? 1 : 0)
            * 1315423911u + static_cast<size_t>(lineHeightKey) * 2654435769u;
        for (const auto& l : _lines) {
            h ^= std::hash<std::wstring>{}(l) + 0x9e3779b9u
                + (h << 6) + (h >> 2);
        }
        return h;
    };
    const size_t currentHash = recomputeHash();

    if (_visualLinesDirty || _visualLinesWidth != widthKey
        || _visualLinesContentHash != currentHash) {
        _visualLinesContentHash = currentHash;
        _visualLinesWidth = widthKey;
        _cachedVisualLines.clear();
        std::vector<VisualLine>& result = _cachedVisualLines;
        const int fontSize = effectiveFontSize();
        for (size_t logical = 0; logical < _lines.size(); ++logical) {
            const std::wstring& line = _lines[logical];
            if (!_wordWrap || line.empty()) {
                result.push_back(VisualLine{
                    static_cast<int>(logical), 0,
                    static_cast<int>(line.size())});
                continue;
            }

            const UnicodeTextAnalysis analysis = analyzeUnicodeText(line);
            size_t start = 0;
            while (start < line.size()) {
                size_t end = start;
                size_t lastSoftBreak = start;
                for (const UnicodeTextCluster& cluster : analysis.clusters) {
                    const size_t clusterEnd = cluster.textStart + cluster.textLength;
                    if (clusterEnd <= start) continue;
                    const float candidateWidth = measureRangeWidth(
                        line, start, clusterEnd, fontSize, backend);
                    if (candidateWidth > availableWidth && end > start) break;
                    end = clusterEnd;
                    if (cluster.softBreakAfter) lastSoftBreak = clusterEnd;
                    if (candidateWidth > availableWidth) break;
                }
                if (end <= start) end = nextGraphemeBoundary(line, start);
                if (end < line.size() && lastSoftBreak > start) end = lastSoftBreak;
                result.push_back(VisualLine{
                    static_cast<int>(logical), static_cast<int>(start),
                    static_cast<int>(end)});
                start = end;
            }
        }
        if (result.empty()) result.push_back(VisualLine{});
        _visualLinesDirty = false;
    }
    return _cachedVisualLines;
}

void TextArea::hitTestDocumentPosition(const math::FVector2& local,
                                       int& line, int& col) const {
    const float availableWidth = std::max(
        1.0f, getSize().x - textStartX() - kPaddingX);
    const std::vector<VisualLine> visual = buildVisualLines(availableWidth);
    int row = static_cast<int>((local.y - kPaddingY) / _lineHeight);
    row = std::clamp(row, 0, static_cast<int>(visual.size()) - 1);
    const VisualLine& segment = visual[static_cast<size_t>(row)];
    line = segment.logicalLine;
    col = columnFromRangeX(_lines[line], segment.startCol, segment.endCol,
                           local.x - textStartX(), effectiveFontSize());
}

int TextArea::effectiveFontSize() const noexcept {
    // Keep the historical line-height-to-font-size mapping used by drawing,
    // but make it the single source of truth for rendering, wrapping,
    // hit-testing, selection, caret placement and content sizing.
    return std::max(1, static_cast<int>(_lineHeight - 4.0f));
}

void TextArea::setCaretExtendingSelection(int line, int col) {
    line = std::clamp(line, 0, static_cast<int>(_lines.size()) - 1);
    col = static_cast<int>(floorGraphemeBoundary(
        _lines[line], static_cast<size_t>(std::max(0, col))));
    if (!hasSelection()) {
        _selStartLine = _caretLine;
        _selStartCol = _caretCol;
    }
    _selEndLine = line;
    _selEndCol = col;
    _caretLine = line;
    _caretCol = col;
    invalidateDocument();
}

void TextArea::selectWordAt(int line, int col) {
    line = std::clamp(line, 0, static_cast<int>(_lines.size()) - 1);
    std::wstring& text = _lines[line];
    col = static_cast<int>(floorGraphemeBoundary(
        text, static_cast<size_t>(std::max(0, col))));
    auto isWordChar = [](wchar_t ch) {
        return ch == L'_' || std::iswalnum(static_cast<wint_t>(ch)) != 0;
    };
    int start = col;
    while (start > 0 && isWordChar(text[static_cast<size_t>(start - 1)])) --start;
    int end = col;
    while (end < static_cast<int>(text.size())
           && isWordChar(text[static_cast<size_t>(end)])) ++end;
    start = static_cast<int>(floorGraphemeBoundary(text, static_cast<size_t>(start)));
    end = static_cast<int>(ceilGraphemeBoundary(text, static_cast<size_t>(end)));
    if (start == end) setCaret(line, col);
    else setSelection(line, start, line, end);
}

void TextArea::deleteSelectionWithoutHistory() {
    if (!hasSelection()) return;
    int sl = _selStartLine, sc = _selStartCol;
    int el = _selEndLine, ec = _selEndCol;
    if (sl > el || (sl == el && sc > ec)) {
        std::swap(sl, el);
        std::swap(sc, ec);
    }
    if (sl == el) {
        _lines[sl].erase(static_cast<size_t>(sc), static_cast<size_t>(ec - sc));
    } else {
        _lines[sl].erase(static_cast<size_t>(sc));
        _lines[sl].append(_lines[el].substr(static_cast<size_t>(ec)));
        _lines.erase(_lines.begin() + sl + 1, _lines.begin() + el + 1);
    }
    _caretLine = sl;
    _caretCol = sc;
    _selStartLine = sl;
    _selStartCol = sc;
    _selEndLine = sl;
    _selEndCol = sc;
}

void TextArea::invalidateDocument() {
    _visualLinesDirty = true;
    if (_document != nullptr) _document->markDirty();
}

void TextArea::syncDocumentSizeToContent() {
    if (_document == nullptr) return;
    const float wrapWidthPx = std::max(
        1.0f, getSize().x - textStartX() - kPaddingX);
    const std::vector<VisualLine> visual = buildVisualLines(wrapWidthPx);
    const float h = static_cast<float>(visual.size()) * _lineHeight
        + 2.0f * kPaddingY;
    // Width is set by ScrollView's layout pass — we provide a minimum
    // hint that allows the longest line to fit if hbar were enabled.
    float maxLineW = 0.0f;
    for (const auto& l : _lines) {
        const float w = measurePrefixWidth(
            l, l.size(), nullptr, effectiveFontSize());
        maxLineW = std::max(maxLineW, w + textStartX() + kPaddingX);
    }
    const float contentWidth = _wordWrap ? getSize().x
                                         : std::max(maxLineW, getSize().x);
    _document->setSize(math::FVector2(contentWidth, h));
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
    invalidateDocument();
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
