#include "AYTextLabel.h"
#include "IAYRenderBackend.h"

namespace ayt::ui {

Widget* createTextLabelWidget() {
    return new TextLabel();
}

TextLabel::TextLabel()
    : _fontSize(14)
    , _textR(1.0f)
    , _textG(1.0f)
    , _textB(1.0f)
    , _textA(1.0f)
    , _bgR(0.0f)
    , _bgG(0.0f)
    , _bgB(0.0f)
    , _bgA(0.0f)
    , _hAlign(HAlignment::Left)
    , _vAlign(VAlignment::Top)
    , _wordWrap(false)
    , _wrapWidth(0.0f)
{
    // R-3 (2026-07-17): no longer calls setEnabled(false) / setOnClicked({}).
    // TextLabel extends LeafWidget which extends Widget — neither exists.
}

TextLabel::~TextLabel() = default;

void TextLabel::onRender(IRenderBackend& renderer) {
    if (getText().empty()) {
        return;
    }

    if (_bgA > 0.0f) {
        renderer.drawRect(getWorldBounds(),
                          math::FVector4(_bgR, _bgG, _bgB, _bgA));
    }

    renderer.drawText(getWorldBounds(), getText(), _fontSize,
                      math::FVector4(_textR, _textG, _textB, _textA));
}

} // namespace ayt::ui