#include "AYScrollBar.h"
#include "AYValueWidget.h"
#include "IAYRenderBackend.h"
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
    const float content = _max - _min;
    if (tl <= 0.0f || content <= 0.0f || _viewportSize >= content) {
        // No scrolling needed: thumb fills the track.
        return track;
    }
    float ratio = _viewportSize / content;
    if (ratio > 1.0f) ratio = 1.0f;
    float thumbLength = tl * ratio;
    if (thumbLength < kMinThumbLength) thumbLength = kMinThumbLength;
    if (thumbLength > tl) thumbLength = tl;

    // Thumb travels over (track - thumb); value is scroll offset in
    // [min, min + maxScroll] where maxScroll = content - viewport.
    const float maxScroll = content - _viewportSize;
    float t = (maxScroll > 1e-5f)
        ? ((_value - _min) / maxScroll)
        : 0.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    float thumbStart;
    if (_orientation == Orientation::Horizontal) {
        thumbStart = track.minX + t * (tl - thumbLength);
        return math::FRectangle(thumbStart, track.minY,
                                thumbStart + thumbLength, track.maxY);
    }
    thumbStart = track.minY + t * (tl - thumbLength);
    return math::FRectangle(track.minX, thumbStart,
                            track.maxX, thumbStart + thumbLength);
}

void ScrollBar::applyThumbStart(float thumbStartAlong) {
    const float content = _max - _min;
    if (content <= 0.0f || _viewportSize >= content) {
        setValue(_min);
        return;
    }
    const float tl = trackLength();
    float ratio = _viewportSize / content;
    if (ratio > 1.0f) ratio = 1.0f;
    float thumbLength = (tl > 0.0f) ? (tl * ratio) : 0.0f;
    if (thumbLength < kMinThumbLength) thumbLength = kMinThumbLength;
    if (thumbLength > tl) thumbLength = tl;
    const float travel = tl - thumbLength;
    const float maxScroll = content - _viewportSize;
    // thumbStartAlong is the thumb's leading edge in track space;
    // offset in [0, maxScroll] follows [0, travel].
    float t = (travel > 1e-5f) ? (thumbStartAlong / travel) : 0.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    setValue(_min + t * maxScroll);
}

void ScrollBar::applyNormalized(float nx) {
    const float tl = trackLength();
    const float content = _max - _min;
    float ratio = (content > 0.0f) ? (_viewportSize / content) : 1.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    float thumbLength = (tl > 0.0f) ? (tl * ratio) : 0.0f;
    if (thumbLength < kMinThumbLength) thumbLength = kMinThumbLength;
    if (thumbLength > tl) thumbLength = tl;
    // nx is mouse position along the full track; map so thumb center
    // under the cursor → offset in [0, maxScroll].
    applyThumbStart(nx * tl - thumbLength * 0.5f);
}

UiCursorHint ScrollBar::getCursorHint() const {
    // PR-S5c: scrollbars keep the arrow cursor. Native UIs never change
    // the pointer over a scrollbar; the Size* hints are reserved for the
    // window rim / slider / splitter handle.
    return UiCursorHint::Default;
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
        // PR-S1b: pressing the thumb arms a pure drag — keep the
        // pressed cursor's offset from the thumb's leading edge so the
        // thumb does NOT jump (clicking the thumb edge no longer snaps
        // the page to the cursor).
        _dragOffset = (_orientation == Orientation::Horizontal)
            ? (e.mousePos.x - thumb.minX)
            : (e.mousePos.y - thumb.minY);
    } else {
        // Track press = click-to-jump: thumb center lands under the
        // cursor. Keep the thumb centered while dragging from a track
        // press (matches the pre-PR-S1b drag behavior).
        const float thumbLen = (_orientation == Orientation::Horizontal)
            ? (thumb.maxX - thumb.minX)
            : (thumb.maxY - thumb.minY);
        _dragOffset = thumbLen * 0.5f;
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
    // Drag maps the thumb's leading edge to (cursor - pressed offset)
    // along the track. applyThumbStart clamps to [0, travel].
    const float along = (_orientation == Orientation::Horizontal)
        ? (e.mousePos.x - getTrackRect().minX)
        : (e.mousePos.y - getTrackRect().minY);
    applyThumbStart(along - _dragOffset);
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
    // Track background — B3: rounded fill matches the 2px rounded outline.
    renderer.drawRoundedRect(bounds,
        isEnabled() ? math::FVector4(0.10f, 0.10f, 0.12f, 1.0f)
                    : math::FVector4(0.07f, 0.07f, 0.08f, 1.0f),
        2.0f);

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
    // B3: rounded thumb fill matches the 2px rounded thumb border.
    renderer.drawRoundedRect(thumb, thumbColor, 2.0f);
    renderer.drawBorderRect(thumb, thumbBorder, 1.0f, 2.0f);
}

Widget* createScrollBarWidget() {
    return new ScrollBar();
}

} // namespace ayt::ui
