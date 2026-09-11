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
    // AYUI-DirtyRect-2026-08-26: setText swaps the rendered glyph run —
    // must re-render this frame. The early-out on equal text is omitted
    // here deliberately: callers use setText as the canonical "label
    // changed, redraw it" signal even when (rarely) the new text equals
    // the old (e.g. i18n key resolved to the same string). The next
    // render() will clear the dirty flag, so the cost is one frame.
    void setText(const std::wstring& text) {
        _text = text;
        markDirty();
    }

    void setFontSize(int size) { _fontSize = size; markDirty(); }
    int getFontSize() const { return _fontSize; }

    void setTextColor(const math::FVector4& color) {
        _textR = color.x;
        _textG = color.y;
        _textB = color.z;
        _textA = color.w;
        markDirty();
    }

    math::FVector4 getTextColor() const {
        return math::FVector4(_textR, _textG, _textB, _textA);
    }

    void setFontFamily(const std::wstring& family) { _fontFamily = family; markDirty(); }
    const std::wstring& getFontFamily() const { return _fontFamily; }

    enum class HAlignment { Left, Center, Right };
    enum class VAlignment { Top, Center, Bottom };

    void setHorizontalAlignment(HAlignment align) { _hAlign = align; markDirty(); }
    void setVerticalAlignment(VAlignment align) { _vAlign = align; markDirty(); }
    HAlignment getHorizontalAlignment() const { return _hAlign; }
    VAlignment getVerticalAlignment() const { return _vAlign; }

    void setWordWrap(bool wrap) { _wordWrap = wrap; markDirty(); }
    bool getWordWrap() const { return _wordWrap; }

    void setWrapWidth(float width) { _wrapWidth = width; markDirty(); markBoundsDirty(); }

    void setBackgroundColor(const math::FVector4& color) {
        _bgR = color.x;
        _bgG = color.y;
        _bgB = color.z;
        _bgA = color.w;
        markDirty();
    }

    math::FVector4 getBackgroundColor() const {
        return math::FVector4(_bgR, _bgG, _bgB, _bgA);
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
