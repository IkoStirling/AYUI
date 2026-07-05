#include "AYButton.h"
#include "AYIRenderBackend.h"
#include "AYMathUtils.h"

namespace ayt::ui {

Button::Button()
    : _state(ButtonState::Normal)
    , _enabled(true)
    , _isMouseOver(false)
    , _isPressed(false)
    , _padding(8.0f, 4.0f, 8.0f, 4.0f)
{
    setSize(math::FVector2(100.0f, 32.0f));
}

Button::~Button() {
}

void Button::setPadding(float left, float top, float right, float bottom) {
    _padding = math::FVector4(left, top, right, bottom);
}

bool Button::onMouseMove(const UIMouseEvent& e) {
    if (!_enabled) {
        _state = ButtonState::Disabled;
        return false;
    }

    math::FRectangle bounds = getWorldBounds();
    bool isOver = bounds.contains(e.mousePos);

    if (isOver != _isMouseOver) {
        _isMouseOver = isOver;
        _state = isOver ? ButtonState::Hovered : ButtonState::Normal;
    }

    return isOver;
}

bool Button::onMouseButtonDown(const UIMouseEvent& e) {
    if (!_enabled) return false;
    if (e.mouseButton != 0) return false;

    math::FRectangle bounds = getWorldBounds();
    if (bounds.contains(e.mousePos)) {
        _isPressed = true;
        _state = ButtonState::Pressed;
        return true;
    }
    return false;
}

bool Button::onMouseButtonUp(const UIMouseEvent& e) {
    if (!_enabled) return false;
    if (e.mouseButton != 0) return false;

    if (_isPressed) {
        _isPressed = false;

        math::FRectangle bounds = getWorldBounds();
        if (bounds.contains(e.mousePos)) {
            _state = ButtonState::Hovered;
            if (_onClicked) {
                _onClicked();
            }
            return true;
        } else {
            _state = ButtonState::Normal;
        }
    }
    return false;
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

void Button::onMouseLeave() {
    _isMouseOver = false;
    _isPressed = false;
    if (_state == ButtonState::Hovered || _state == ButtonState::Pressed) {
        _state = ButtonState::Normal;
    }
}

UiCursorHint Button::getCursorHint() const {
    if (_enabled && _isMouseOver) {
        return UiCursorHint::Hand;
    }
    return UiCursorHint::Default;
}

void Button::onRender(IRenderBackend& renderer) {
    math::FRectangle bounds = getWorldBounds();
    math::FVector4 bg(0.28f, 0.28f, 0.30f, 1.0f);

    switch (_state) {
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
        math::FVector4 textColor = _enabled ? math::FVector4(1.0f, 1.0f, 1.0f, 1.0f)
                                          : math::FVector4(0.55f, 0.55f, 0.55f, 1.0f);
        renderer.drawText(getTextBounds(), _text, 14, textColor);
    }
}

} // namespace ayt::ui