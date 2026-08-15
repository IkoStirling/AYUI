#include "AYInteractiveWidget.h"
#include "AYDockTrace.h"
#include "AYMath/MathUtils.h"

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
    if (ayuiTraceInputEnabled() && (_isMouseOver || _state != ButtonState::Normal)) {
        dockTrace("[LeaveTrace] LEAVE id=%s (was hover=%d state=%d)\n",
                  getStyleId().c_str(), _isMouseOver ? 1 : 0,
                  static_cast<int>(_state));
    }
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

// ---------------------------------------------------------------------------
// UI animation lane, cut 1 — color transitions.
// ---------------------------------------------------------------------------

void InteractiveWidget::setColor(const math::FVector4& c) {
    _colorInitialized = true;
    _colorAnim.snap(c);
    _color = c;
}

void InteractiveWidget::animateColorTo(const math::FVector4& to, float durationMs,
                                       AnimationCurve curve) {
    if (!_colorInitialized) {
        // Never rendered a color yet — snap to avoid a ghost tween from
        // the {1,1,1,1} placeholder.
        _colorInitialized = true;
        _colorAnim.snap(to);
        _color = to;
        return;
    }
    _colorAnim.start(_color, to, durationMs, curve);
}

void InteractiveWidget::tick(float dt) {
    Widget::tick(dt);
    const bool wasActive = _colorAnim.active;
    float t;
    if (_colorAnim.advance(dt, t)) {
        _color = tweenLerp(_colorAnim.from, _colorAnim.to, t);
    } else if (wasActive) {
        // Completed this frame — snap to the exact target.
        _color = _colorAnim.to;
    }
}

math::FVector4 InteractiveWidget::resolveTransitionColor(
    const math::FVector4& target) {
    // First render or tween disabled: identity — snap to the target so
    // pre-animation semantics are byte-identical.
    if (!_colorInitialized || _colorTweenMs <= 0.0f) {
        _colorInitialized = true;
        _colorAnim.snap(target);
        _color = target;
        return target;
    }
    if (_colorAnim.active) {
        if (target == _colorAnim.to) {
            // Still running toward the same target — return the current
            // lerped value.
            return _color;
        }
        // Target changed mid-flight — retarget from the current value.
        _colorAnim.start(_color, target, _colorTweenMs, AnimationCurve::EaseOut);
        return _color;
    }
    if (target != _color) {
        // Idle and the target moved — start the transition from here.
        _colorAnim.start(_color, target, _colorTweenMs, AnimationCurve::EaseOut);
    }
    return _color;
}

} // namespace ayt::ui