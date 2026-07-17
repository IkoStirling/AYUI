#include "AYInteractiveWidget.h"
#include "aymath/MathUtils.h"

namespace ayt::ui {

InteractiveWidget::InteractiveWidget() = default;

InteractiveWidget::~InteractiveWidget() = default;

void InteractiveWidget::setEnabled(bool enabled) {
    if (_enabled == enabled) {
        return;
    }
    _enabled = enabled;

    if (!_enabled) {
        // Disable: clear transient flags, pin to Disabled so callers reading
        // getState() see the new reality immediately (fixes B2 — previously
        // _isMouseOver stayed true after setEnabled(false) so re-enable after
        // a hover-leave left state stuck at Hovered).
        _isMouseOver = false;
        _isPressed = false;
        _state = ButtonState::Disabled;
    } else {
        // Re-enable: derive state from current hover. Pressed cannot persist
        // across a disable — capture would have been released by the OS / UI
        // manager, and a stale _isPressed would fire _onClicked on the next
        // mouse-up with no down event.
        _state = _isMouseOver ? ButtonState::Hovered : ButtonState::Normal;
    }
}

bool InteractiveWidget::onMouseMove(const UIMouseEvent& e) {
    if (!_enabled) {
        return false;
    }

    math::FRectangle bounds = getWorldBounds();
    bool isOver = bounds.contains(e.mousePos);

    if (isOver != _isMouseOver) {
        _isMouseOver = isOver;
        if (_isPressed) {
            _state = ButtonState::Pressed;
        } else {
            _state = isOver ? ButtonState::Hovered : ButtonState::Normal;
        }
    }

    return isOver;
}

bool InteractiveWidget::onMouseButtonDown(const UIMouseEvent& e) {
    if (!_enabled) {
        return false;
    }
    if (e.mouseButton != 0) {
        return false;
    }

    math::FRectangle bounds = getWorldBounds();
    if (bounds.contains(e.mousePos)) {
        _isPressed = true;
        _state = ButtonState::Pressed;
        return true;
    }
    return false;
}

bool InteractiveWidget::onMouseButtonUp(const UIMouseEvent& e) {
    if (!_enabled) {
        return false;
    }
    if (e.mouseButton != 0) {
        return false;
    }

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

void InteractiveWidget::onMouseLeave() {
    _isMouseOver = false;
    _isPressed = false;
    if (_state == ButtonState::Hovered || _state == ButtonState::Pressed) {
        _state = ButtonState::Normal;
    }
}

UiCursorHint InteractiveWidget::getCursorHint() const {
    if (_enabled && _isMouseOver) {
        return UiCursorHint::Hand;
    }
    return UiCursorHint::Default;
}

} // namespace ayt::ui