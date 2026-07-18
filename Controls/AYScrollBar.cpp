#include "AYScrollBar.h"
#include "AYValueWidget.h"
#include "AYIRenderBackend.h"
#include "aymath/MathUtils.h"
#include <algorithm>

namespace ayt::ui {

ScrollBar::ScrollBar() {
    setSize(math::FVector2(kDefaultBarWidth, 100.0f));
}

ScrollBar::~ScrollBar() = default;

void ScrollBar::setRange(float minV, float maxV) {
    if (maxV < minV + kMinMaxEpsilon) maxV = minV + kMinMaxEpsilon;
    _min = minV;
    _max = maxV;
    // Re-clamp value.
    setValue(clampValueToRange(_value, _min, _max));
}

void ScrollBar::setValue(float v) {
    const float clamped = clampValueToRange(v, _min, _max);
    if (fabsf(clamped - _value) < kMinMaxEpsilon) return;
    _value = clamped;
    if (_onValueChanged) _onValueChanged(_value);
}

float ScrollBar::trackLength() const {
    math::FRectangle b = getWorldBounds();
    return (_orientation == Orientation::Horizontal)
        ? (b.maxX - b.minX)
        : (b.maxY - b.minY);
}

math::FRectangle ScrollBar::getTrackRect() const {
    return getWorldBounds();
}

math::FRectangle ScrollBar::getThumbRect() const {
    math::FRectangle track = getTrackRect();
    const float tl = trackLength();
    if (tl <= 0.0f || _max <= _min || _viewportSize >= (_max - _min)) {
        // No scrolling needed: thumb fills the track.
        return track;
    }
    float ratio = _viewportSize / (_max - _min);
    if (ratio > 1.0f) ratio = 1.0f;
    float thumbLength = tl * ratio;
    if (thumbLength < kMinThumbLength) thumbLength = kMinThumbLength;
    if (thumbLength > tl) thumbLength = tl;

    float thumbStart;
    if (_orientation == Orientation::Horizontal) {
        thumbStart = track.minX + (_value - _min) / (_max - _min)
                                * (tl - thumbLength);
        return math::FRectangle(thumbStart, track.minY,
                                thumbStart + thumbLength, track.maxY);
    }
    thumbStart = track.minY + (_value - _min) / (_max - _min)
                                * (tl - thumbLength);
    return math::FRectangle(track.minX, thumbStart,
                            track.maxX, thumbStart + thumbLength);
}

void ScrollBar::applyNormalized(float nx) {
    setValue(_min + nx * (_max - _min));
}

UiCursorHint ScrollBar::getCursorHint() const {
    if (!_enabled) return UiCursorHint::Default;
    return (_orientation == Orientation::Vertical)
        ? UiCursorHint::SizeVertical
        : UiCursorHint::Default;
}

bool ScrollBar::onMouseButtonDown(const UIMouseEvent& e) {
    if (!_enabled || e.mouseButton != 0) return false;
    if (!getWorldBounds().contains(e.mousePos)) return false;
    _dragging = true;
    const math::FRectangle thumb = getThumbRect();
    bool onThumb;
    float nx;
    const float tl = trackLength();
    if (_orientation == Orientation::Horizontal) {
        onThumb = (e.mousePos.x >= thumb.minX && e.mousePos.x <= thumb.maxX);
        nx = (tl > 0.0f) ? ((e.mousePos.x - getTrackRect().minX) / tl) : 0.5f;
    } else {
        onThumb = (e.mousePos.y >= thumb.minY && e.mousePos.y <= thumb.maxY);
        nx = (tl > 0.0f) ? ((e.mousePos.y - getTrackRect().minY) / tl) : 0.5f;
    }
    nx = clampValueToRange(nx, 0.0f, 1.0f);
    if (onThumb) {
        // Continuous tracking — apply normalized position.
        applyNormalized(nx);
    } else {
        // Page-jump — center the click on the thumb.
        const float ratio = _viewportSize / (_max - _min);
        nx -= ratio * 0.5f;
        applyNormalized(nx);
    }
    _state = ButtonState::Pressed;
    return true;
}

bool ScrollBar::onMouseMove(const UIMouseEvent& e) {
    InteractiveWidget::onMouseMove(e);
    if (!_enabled || !_dragging) return false;
    const float tl = trackLength();
    if (tl <= 0.0f) return true;
    float nx;
    if (_orientation == Orientation::Horizontal) {
        nx = (e.mousePos.x - getTrackRect().minX) / tl;
    } else {
        nx = (e.mousePos.y - getTrackRect().minY) / tl;
    }
    nx = clampValueToRange(nx, 0.0f, 1.0f);
    applyNormalized(nx);
    return true;
}

bool ScrollBar::onMouseButtonUp(const UIMouseEvent& e) {
    if (!_enabled || e.mouseButton != 0) return false;
    if (!_dragging) return false;
    (void)e;
    _dragging = false;
    if (_isPressed) _isPressed = false;
    _state = (_isMouseOver ? ButtonState::Hovered : ButtonState::Normal);
    return true;
}

void ScrollBar::onRender(IRenderBackend& renderer) {
    math::FRectangle bounds = getWorldBounds();
    if (bounds.maxX <= bounds.minX || bounds.maxY <= bounds.minY) return;
    // Track background
    renderer.drawRect(bounds,
        isEnabled() ? math::FVector4(0.10f, 0.10f, 0.12f, 1.0f)
                    : math::FVector4(0.07f, 0.07f, 0.08f, 1.0f));

    if (_max <= _min || _viewportSize >= (_max - _min)) {
        // No need for thumb; render just the track + outline.
        renderer.drawBorderRect(bounds,
            math::FVector4(0.30f, 0.30f, 0.34f, 1.0f), 1.0f, 2.0f);
        return;
    }

    // Thumb
    math::FRectangle thumb = getThumbRect();
    math::FVector4 thumbColor = math::FVector4(0.5f, 0.5f, 0.55f, 1.0f);
    math::FVector4 thumbBorder = math::FVector4(0.35f, 0.35f, 0.4f, 1.0f);
    if (!isEnabled()) {
        thumbColor = math::FVector4(0.3f, 0.3f, 0.32f, 1.0f);
    } else if (_dragging || isPressed()) {
        thumbColor = math::FVector4(0.65f, 0.65f, 0.7f, 1.0f);
    } else if (isMouseOver()) {
        thumbColor = math::FVector4(0.58f, 0.58f, 0.62f, 1.0f);
    }
    renderer.drawRect(thumb, thumbColor);
    renderer.drawBorderRect(thumb, thumbBorder, 1.0f, 2.0f);
}

Widget* createScrollBarWidget() {
    return new ScrollBar();
}

} // namespace ayt::ui
