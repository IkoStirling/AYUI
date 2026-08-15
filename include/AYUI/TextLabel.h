#pragma once

#include "AYUI/LeafWidget.h"

namespace ayt::ui {

// R-3 (2026-07-17): TextLabel no longer extends Button. It now extends
// LeafWidget and owns its own _text field. The previous `TextLabel : Button`
// inheritance forced a `setEnabled(false); setOnClicked({});` hack to
// neutralize the inherited state machine — see design.md §15.2 B1.
class TextLabel : public LeafWidget {
public:
    TextLabel();
    ~TextLabel() override;

    // TextLabel now owns its own text (was inherited from Button pre-R-3).
    const std::wstring& getText() const { return _text; }
    void setText(const std::wstring& text) { _text = text; }

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
    HAlignment getHorizontalAlignment() const { return _hAlign; }
    VAlignment getVerticalAlignment() const { return _vAlign; }

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

    std::wstring _fontFamily;
    std::wstring _text;
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