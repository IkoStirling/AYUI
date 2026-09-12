#pragma once

#include "AYUI/CompoundFocusableWidget.h"
#include "AYUI/UnicodeText.h"

#include <cstddef>
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace ayt::ui {

enum class RichTextWrapMode { NoWrap, Word, Character };
enum class RichTextAlignment { Left, Center, Right, Justify };
enum class RichTextVerticalAlignment { Top, Center, Bottom };
enum class RichTextOverflow { Clip, Ellipsis };
enum class RichInlineKind { None, Image, Widget };

struct RichRun {
    std::wstring text;
    math::FVector4 color = math::FVector4(1.0f, 1.0f, 1.0f, 1.0f);
    int fontSize = 14;
    std::wstring fontFamily;
    int fontWeight = 400;
    std::string language;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikethrough = false;
    float letterSpacing = 0.0f;
    float baselineShift = 0.0f;
    bool isImage = false;
    RichInlineKind inlineKind = RichInlineKind::None;
    math::FVector2 inlineSize = math::FVector2(16.0f, 16.0f);
    float inlineBaseline = 0.0f;
    std::wstring inlineAltText;
    void* inlineTexture = nullptr; // runtime-only; never serialized
    math::FRectangle inlineUv = math::FRectangle(0, 0, 1, 1);
    Widget* inlineWidget = nullptr; // runtime-only; externally owned
};

struct RichTextFragment {
    size_t runIndex = 0;
    size_t runTextStart = 0;
    size_t textLength = 0;
    size_t documentTextStart = 0;
    size_t lineIndex = 0;
    uint8_t bidiLevel = 0;
    bool rightToLeft = false;
    bool inlineObject = false;
    math::FRectangle bounds;
    // Visual caret stops. Text indices are document std::wstring offsets;
    // x values are world-space and never split an extended grapheme cluster.
    std::vector<size_t> caretTextIndices;
    std::vector<float> caretX;
};

struct RichTextLine {
    size_t firstFragment = 0;
    size_t fragmentCount = 0;
    float width = 0.0f;
    float height = 0.0f;
    float y = 0.0f;
};

struct RichTextLayout {
    std::vector<RichTextFragment> fragments;
    std::vector<RichTextLine> lines;
    math::FVector2 contentSize;
    bool truncated = false;
};

class RichText : public CompoundFocusableWidget {
public:
    RichText();
    ~RichText() override;

    void addRun(const std::wstring& text, const math::FVector4& color, int fontSize);
    void addRun(const RichRun& run);
    void insertRun(size_t index, const RichRun& run);
    bool setRun(size_t index, const RichRun& run);
    bool removeRun(size_t index);
    bool moveRun(size_t fromIndex, size_t toIndex);
    void clearRuns();
    size_t getRunCount() const { return _runs.size(); }
    const RichRun& getRun(size_t i) const { return _runs[i]; }
    std::wstring getPlainText() const;
    void setPlainText(const std::wstring& text);

    void addInlineImage(void* textureHandle, const math::FVector2& size,
                        const std::wstring& altText = {},
                        const math::FRectangle& uv = math::FRectangle(0, 0, 1, 1));
    void addInlineWidget(Widget* widget, const math::FVector2& size,
                         const std::wstring& altText = {});

    void setDefaultColor(const math::FVector4& color) { _defaultColor = color; markDirty(); }
    math::FVector4 getDefaultColor() const { return _defaultColor; }
    void setDefaultFontSize(int size) { _defaultFontSize = size > 0 ? size : 14; markDirty(); }
    int getDefaultFontSize() const { return _defaultFontSize; }

    void setWrapWidth(float width);
    float getWrapWidth() const { return _wrapWidth; }
    void setWrapMode(RichTextWrapMode mode) { _wrapMode = mode; markDirty(); }
    RichTextWrapMode getWrapMode() const { return _wrapMode; }
    void setAlignment(RichTextAlignment alignment) { _alignment = alignment; markDirty(); }
    RichTextAlignment getAlignment() const { return _alignment; }
    void setVerticalAlignment(RichTextVerticalAlignment alignment) {
        _verticalAlignment = alignment; markDirty();
    }
    RichTextVerticalAlignment getVerticalAlignment() const { return _verticalAlignment; }
    void setOverflow(RichTextOverflow overflow) { _overflow = overflow; markDirty(); }
    RichTextOverflow getOverflow() const { return _overflow; }
    void setLineHeight(float multiplier) {
        _lineHeight = multiplier > 0.0f ? multiplier : 1.2f; markDirty();
    }
    float getLineHeight() const { return _lineHeight; }
    void setLineSpacing(float spacing) { _lineSpacing = spacing; markDirty(); }
    float getLineSpacing() const { return _lineSpacing; }
    void setMaxLines(size_t lines) { _maxLines = lines; markDirty(); }
    size_t getMaxLines() const { return _maxLines; }
    void setTextDirection(TextDirection direction) { _textDirection = direction; markDirty(); }
    TextDirection getTextDirection() const { return _textDirection; }

    RichTextLayout layout(IRenderBackend& renderer) const;
    math::FVector2 measureContent(IRenderBackend& renderer) const;
    size_t hitTestTextIndex(IRenderBackend& renderer,
                            const math::FVector2& worldPoint) const;
    math::FRectangle getCaretRect(IRenderBackend& renderer, size_t textIndex) const;

    void setSelectable(bool selectable);
    bool isSelectable() const { return _selectable; }
    void setEditable(bool editable);
    bool isEditable() const { return _editable; }
    bool isTextEditingWidget() const override { return _editable; }

    void setSelection(size_t anchor, size_t caret);
    void clearSelection();
    void selectAll();
    bool hasSelection() const { return _selectionAnchor != _caret; }
    size_t getSelectionStart() const { return std::min(_selectionAnchor, _caret); }
    size_t getSelectionEnd() const { return std::max(_selectionAnchor, _caret); }
    size_t getCaretIndex() const { return _caret; }
    std::wstring getSelectedText() const;
    bool replaceSelection(const std::wstring& text);
    bool undo();
    bool redo();
    void setOnTextChanged(std::function<void(const std::wstring&)> callback) {
        _onTextChanged = std::move(callback);
    }

    void setSelectionColor(const math::FVector4& color) { _selectionColor = color; markDirty(); }
    void setCaretColor(const math::FVector4& color) { _caretColor = color; markDirty(); }

    bool onMouseButtonDown(const UIMouseEvent& e) override;
    bool onMouseMove(const UIMouseEvent& e) override;
    bool onMouseButtonUp(const UIMouseEvent& e) override;
    bool onKeyDown(int keyCode) override;
    bool onTextInput(wchar_t ch) override;
    bool onTextInputText(const std::wstring& text) override;
    bool onImeCompositionStart(const std::string& text, int caret) override;
    bool onImeCompositionUpdate(const std::string& text, int caret) override;
    bool onImeCompositionEnd(const std::string& committed) override;
    UiCursorHint getCursorHint() const override;
    void tick(float dt) override;
    void layoutChildren() override;

protected:
    void onRender(IRenderBackend& renderer) override;
    void renderChildren(IRenderBackend& renderer) override;
    void onFocusGained() override;
    void onFocusLost() override;

private:
    float measureTextWidth(IRenderBackend& renderer, const std::wstring& text,
                           const RichRun& run, TextDirection direction) const;
    void normalizeRun(RichRun& run) const;
    bool replaceRange(size_t start, size_t end, const std::wstring& text,
                      bool recordUndo);
    void moveCaret(size_t next, bool extend);
    RichRun insertionStyleAt(size_t index) const;
    void placeInlineWidgets(const RichTextLayout& result);
    void syncInlineWidgetChildren();

    struct EditSnapshot {
        std::vector<RichRun> runs;
        size_t anchor = 0;
        size_t caret = 0;
    };

    std::vector<RichRun> _runs;
    math::FVector4 _defaultColor = math::FVector4(1, 1, 1, 1);
    int _defaultFontSize = 14;
    float _wrapWidth = 0.0f;
    RichTextWrapMode _wrapMode = RichTextWrapMode::NoWrap;
    RichTextAlignment _alignment = RichTextAlignment::Left;
    RichTextVerticalAlignment _verticalAlignment = RichTextVerticalAlignment::Top;
    RichTextOverflow _overflow = RichTextOverflow::Clip;
    float _lineHeight = 1.2f;
    float _lineSpacing = 0.0f;
    size_t _maxLines = 0;
    TextDirection _textDirection = TextDirection::Auto;
    bool _selectable = true;
    bool _editable = false;
    bool _dragSelecting = false;
    size_t _selectionAnchor = 0;
    size_t _caret = 0;
    math::FVector4 _selectionColor = math::FVector4(0.18f, 0.46f, 0.82f, 0.42f);
    math::FVector4 _caretColor = math::FVector4(0.35f, 0.68f, 1.0f, 1.0f);
    float _caretBlink = 0.0f;
    bool _caretVisible = true;
    std::vector<EditSnapshot> _undoStack;
    std::vector<EditSnapshot> _redoStack;
    bool _composing = false;
    size_t _compositionStart = 0;
    size_t _compositionLength = 0;
    std::function<void(const std::wstring&)> _onTextChanged;
};

Widget* createRichTextWidget();

} // namespace ayt::ui
