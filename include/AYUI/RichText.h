#pragma once

#include "AYUI/LeafWidget.h"

#include <cstddef>
#include <string>
#include <vector>

namespace ayt::ui {

enum class RichTextWrapMode { NoWrap, Word, Character };
enum class RichTextAlignment { Left, Center, Right, Justify };
enum class RichTextVerticalAlignment { Top, Center, Bottom };
enum class RichTextOverflow { Clip, Ellipsis };

struct RichRun {
    std::wstring text;
    math::FVector4 color = math::FVector4(1.0f, 1.0f, 1.0f, 1.0f);
    int fontSize = 14;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikethrough = false;
    float letterSpacing = 0.0f;
    float baselineShift = 0.0f;
    bool isImage = false;
};

struct RichTextFragment {
    size_t runIndex = 0;
    size_t runTextStart = 0;
    size_t textLength = 0;
    size_t documentTextStart = 0;
    size_t lineIndex = 0;
    math::FRectangle bounds;
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

class RichText : public LeafWidget {
public:
    RichText();
    ~RichText() override;

    void addRun(const std::wstring& text, const math::FVector4& color, int fontSize);
    void addRun(const RichRun& run);
    void clearRuns();
    size_t getRunCount() const { return _runs.size(); }
    const RichRun& getRun(size_t i) const { return _runs[i]; }
    std::wstring getPlainText() const;

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

    RichTextLayout layout(IRenderBackend& renderer) const;
    math::FVector2 measureContent(IRenderBackend& renderer) const;
    size_t hitTestTextIndex(IRenderBackend& renderer,
                            const math::FVector2& worldPoint) const;
    math::FRectangle getCaretRect(IRenderBackend& renderer, size_t textIndex) const;

protected:
    void onRender(IRenderBackend& renderer) override;

private:
    float measureTextWidth(IRenderBackend& renderer, const std::wstring& text,
                           int fontSize) const;

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
};

Widget* createRichTextWidget();

} // namespace ayt::ui
