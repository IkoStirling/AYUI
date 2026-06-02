#include "AYTextLabel.h"
#include "AYIRenderBackend.h"

namespace ayt::ui {

TextLabel::TextLabel()
    : _fontSize(14)
    , _textColor(1.0f, 1.0f, 1.0f, 1.0f)
    , _fontFamily(L"Arial")
    , _hAlign(HAlignment::Left)
    , _vAlign(VAlignment::Top)
    , _wordWrap(false)
    , _wrapWidth(0.0f)
{
}

TextLabel::~TextLabel() {
}

void TextLabel::onRender(IRenderBackend& renderer) {
    if (_text.empty() || !_renderBackend) return;

    IRenderBackend::TextStyle style;
    style.color = _textColor;
    style.align = static_cast<IRenderBackend::TextStyle::Align>(_hAlign);

    renderer.drawText(getBounds(), _text, _fontSize, style);
}

} // namespace ayt::ui