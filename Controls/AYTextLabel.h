#pragma once

#include "AYButton.h"

namespace ayt::ui {

class TextLabel : public Button {
public:
    TextLabel();
    ~TextLabel() override;

    void setFontSize(int size) { _fontSize = size; }
    int getFontSize() const { return _fontSize; }

    void setTextColor(const math::FVector4& color) {
        _textR = color.x;
        _textG = color.y;
        _textB = color.z;
        _textA = color.w;
    }

    math::FVector4 getTextColor() const {
        return math::FVector4(_textR, _textG, _textB, _textA);
    }

    void setFontFamily(const std::wstring& family) { _fontFamily = family; }
    const std::wstring& getFontFamily() const { return _fontFamily; }

    enum class HAlignment { Left, Center, Right };
    enum class VAlignment { Top, Center, Bottom };

    void setHorizontalAlignment(HAlignment align) { _hAlign = align; }
    void setVerticalAlignment(VAlignment align) { _vAlign = align; }

    void setWordWrap(bool wrap) { _wordWrap = wrap; }
    bool getWordWrap() const { return _wordWrap; }

    void setWrapWidth(float width) { _wrapWidth = width; }

    void setBackgroundColor(const math::FVector4& color) {
        _bgR = color.x;
        _bgG = color.y;
        _bgB = color.z;
        _bgA = color.w;
    }

protected:
    void onRender(IRenderBackend& renderer) override;

    bool onMouseMove(const UIMouseEvent& e) override;
    bool onMouseButtonDown(const UIMouseEvent& e) override;
    bool onMouseButtonUp(const UIMouseEvent& e) override;

    std::wstring _fontFamily;
    int _fontSize;
    float _textR;
    float _textG;
    float _textB;
    float _textA;
    float _bgR;
    float _bgG;
    float _bgB;
    float _bgA;
    HAlignment _hAlign;
    VAlignment _vAlign;
    bool _wordWrap;
    float _wrapWidth;
};

Widget* createTextLabelWidget();

} // namespace ayt::ui
