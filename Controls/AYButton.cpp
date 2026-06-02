#include "AYButton.h"
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

} // namespace ayt::ui