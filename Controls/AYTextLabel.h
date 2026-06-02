#pragma once

#include "AYWidget.h"
#include <string>

namespace ayt::ui {

class TextLabel : public CompoundWidget {
public:
    TextLabel();
    virtual ~TextLabel();

    const std::wstring& getText() const { return _text; }
    void setText(const std::wstring& text) { _text = text; }

    void setFontSize(int size) { _fontSize = size; }
    int getFontSize() const { return _fontSize; }

    void setTextColor(const math::FVector4& color) { _textColor = color; }
    const math::FVector4& getTextColor() const { return _textColor; }

    void setFontFamily(const std::wstring& family) { _fontFamily = family; }
    const std::wstring& getFontFamily() const { return _fontFamily; }

    enum class HAlignment { Left, Center, Right };
    enum class VAlignment { Top, Center, Bottom };

    void setHorizontalAlignment(HAlignment align) { _hAlign = align; }
    void setVerticalAlignment(VAlignment align) { _vAlign = align; }

    void setWordWrap(bool wrap) { _wordWrap = wrap; }
    bool getWordWrap() const { return _wordWrap; }

    void setWrapWidth(float width) { _wrapWidth = width; }

protected:
    void onRender(IRenderBackend& renderer) override;

    std::wstring _text;
    int _fontSize;
    math::FVector4 _textColor;
    std::wstring _fontFamily;
    HAlignment _hAlign;
    VAlignment _vAlign;
    bool _wordWrap;
    float _wrapWidth;
};

} // namespace ayt::ui