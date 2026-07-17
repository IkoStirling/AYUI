#include "AYButton.h"
#include "AYIRenderBackend.h"
#include "aymath/MathUtils.h"

namespace ayt::ui {

Button::Button()
    : _padding(8.0f, 4.0f, 8.0f, 4.0f)
{
    setSize(math::FVector2(100.0f, 32.0f));
}

Button::~Button() {
}

void Button::setPadding(float left, float top, float right, float bottom) {
    _padding = math::FVector4(left, top, right, bottom);
}

math::FRectangle Button::getTextBounds() const {
    math::FRectangle bounds = getWorldBounds();
    return math::FRectangle(
        bounds.minX + _padding.x,
        bounds.minY + _padding.y,
        bounds.maxX - _padding.z,
        bounds.maxY - _padding.w
    );
}

void Button::onRender(IRenderBackend& renderer) {
    math::FRectangle bounds = getWorldBounds();
    math::FVector4 bg(0.28f, 0.28f, 0.30f, 1.0f);

    // Hardcoded fills kept for R-1 (button_render_preserves_hover_fill test
    // asserts exact values). R-5 will refactor these to come from StyleManager.
    switch (getState()) {
    case ButtonState::Hovered:
        bg = math::FVector4(0.36f, 0.38f, 0.42f, 1.0f);
        break;
    case ButtonState::Pressed:
        bg = math::FVector4(0.18f, 0.45f, 0.78f, 1.0f);
        break;
    case ButtonState::Disabled:
        bg = math::FVector4(0.20f, 0.20f, 0.20f, 1.0f);
        break;
    default:
        break;
    }

    renderer.drawRect(bounds, bg);
    renderer.drawBorderRect(bounds, math::FVector4(0.12f, 0.12f, 0.12f, 1.0f), 1.0f, 2.0f);

    if (!_text.empty()) {
        math::FVector4 textColor = isEnabled() ? math::FVector4(1.0f, 1.0f, 1.0f, 1.0f)
                                                : math::FVector4(0.55f, 0.55f, 0.55f, 1.0f);
        renderer.drawText(getTextBounds(), _text, 14, textColor);
    }
}

} // namespace ayt::ui