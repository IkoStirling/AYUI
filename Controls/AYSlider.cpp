#include "AYUI/Slider.h"
#include "AYUI/ValueWidget.h"
#include "AYUI/IRenderBackend.h"
#include "AYUI/Style.h"
#include "AYUI/UIManager.h"
#include "AYMath/MathUtils.h"

namespace ayt::ui {

Slider::Slider() {
    setSize(math::FVector2(200.0f, 24.0f));
    // Leaf widget — InteractiveWidget -> Widget -> no-op performLayout.
}

Slider::~Slider() {
    // H-MEM-3: a Slider can be captured by UIManager while the user is
    // dragging the handle (see Slider::onMouseButtonDown, where the
    // dimmer/UIManager captures the pointer to track subsequent moves).
    // If a host deletes the Slider mid-drag, _capturedWidget dangles and
    // the next onMouseMove dispatches into freed memory. Scrub the
    // transient pointers BEFORE the base dtor unwinds.
    if (UIManager* ui = UIManager::tryGet()) {
        ui->clearTransientStateForSubtree(this);
    }
}

void Slider::setMin(float v) {
    if (v > _max - kMinMaxEpsilon) {
        v = _max - kMinMaxEpsilon;
    }
    if (fabsf(v - _min) < kMinMaxEpsilon) {
        return;
    }
    _min = v;
    // Range changes alter the normalized handle position even when the
    // current value remains valid, so invalidate the retained paint list.
    markDirty();
    // Re-clamp value into the new range — setValue fires _onValueChanged
    // only if the post-clamp value actually changed.
    setValue(_value);
}

void Slider::setMax(float v) {
    if (v < _min + kMinMaxEpsilon) {
        v = _min + kMinMaxEpsilon;
    }
    if (fabsf(v - _max) < kMinMaxEpsilon) {
        return;
    }
    _max = v;
    markDirty();
    setValue(_value);
}

void Slider::setValueRange(float minVal, float maxVal) {
    // Normalize so max >= min + epsilon regardless of caller ordering.
    if (maxVal < minVal + kMinMaxEpsilon) {
        maxVal = minVal + kMinMaxEpsilon;
    }
    const bool minChanged = fabsf(minVal - _min) >= kMinMaxEpsilon;
    const bool maxChanged = fabsf(maxVal - _max) >= kMinMaxEpsilon;
    _min = minVal;
    _max = maxVal;
    if (minChanged || maxChanged) {
        markDirty();
    }
    // Reset _value to the new min if the current value now exceeds the
    // new range; otherwise leave it for setValue to no-op on identity.
    if (_value < _min) {
        setValue(_min);
    } else if (_value > _max) {
        setValue(_max);
    } else if (minChanged || maxChanged) {
        // Range changed but value still in range — still surface a value
        // callback because the SAME value in a different range represents
        // a different normalized position. The cheap guard is: a no-op
        // setValue(_value) below won't fire (setValue guards on equality).
        // Intentional: don't synthesize a fake event here. Observers that
        // care about the range shift can compare getMin/getMax.
        (void)0;
    }
}

void Slider::setValue(float v) {
    const float clamped = clampValueToRange(v, _min, _max);
    if (fabsf(clamped - _value) < kMinMaxEpsilon) {
        return;
    }
    _value = clamped;
    // Slider geometry is value-derived. Retained display lists must be
    // rebuilt before this frame is replayed or the handle appears frozen.
    markDirty();
    if (_onValueChanged) {
        _onValueChanged(_value);
    }
}

float Slider::getNormalized() const {
    return clampValueToRange((_value - _min) / (_max - _min), 0.0f, 1.0f);
}

math::FRectangle Slider::getTrackRect() const {
    math::FRectangle bounds = getWorldBounds();
    const float cy = (bounds.minY + bounds.maxY) * 0.5f;
    return math::FRectangle(
        bounds.minX,
        cy - kTrackHeight * 0.5f,
        bounds.maxX,
        cy + kTrackHeight * 0.5f);
}

math::FRectangle Slider::getHandleRect() const {
    math::FRectangle bounds = getWorldBounds();
    const float cy = (bounds.minY + bounds.maxY) * 0.5f;
    const float nx = getNormalized();
    const float cx = bounds.minX + nx * (bounds.maxX - bounds.minX);
    return math::FRectangle(
        cx - kHandleWidth * 0.5f,
        cy - kHandleHeight * 0.5f,
        cx + kHandleWidth * 0.5f,
        cy + kHandleHeight * 0.5f);
}

UiCursorHint Slider::getCursorHint() const {
    if (!_enabled) {
        return UiCursorHint::Default;
    }
    return UiCursorHint::SizeHorizontal;
}

bool Slider::onMouseButtonDown(const UIMouseEvent& e) {
    if (!_enabled) {
        return false;
    }
    if (e.mouseButton != 0) {
        return false;
    }
    const math::FRectangle bounds = getWorldBounds();
    if (!bounds.contains(e.mousePos)) {
        return false;
    }
    _dragging = true;
    // Click anywhere on the slider jumps the value to that point. The
    // drag updates flow through onMouseMove — but a single-click without
    // a subsequent move should still move the handle, so update here.
    const float trackW = getTrackRect().maxX - getTrackRect().minX;
    if (trackW > 0.0f) {
        const float nx = clampValueToRange(
            (e.mousePos.x - getTrackRect().minX) / trackW, 0.0f, 1.0f);
        applyNormalized(nx);
    } else {
        applyNormalized(0.5f);
    }
    // State transitions: base state machine doesn't know about dragging,
    // so we leave state Normal (pointer is over the slider; base state
    // machine will flip to Hovered via its own onMouseMove if applicable).
    _state = ButtonState::Pressed;
    markDirty();
    return true;
}

bool Slider::onMouseMove(const UIMouseEvent& e) {
    // Base state-machine hover update happens FIRST (covers the path
    // where the user is dragging and the cursor briefly leaves the
    // handle — the track still reads as hovered).
    InteractiveWidget::onMouseMove(e);
    if (!_enabled || !_dragging) {
        return InteractiveWidget::onMouseMove(e) || _dragging;
    }
    const math::FRectangle track = getTrackRect();
    const float trackW = track.maxX - track.minX;
    if (trackW <= 0.0f) {
        return true;
    }
    const float nx = clampValueToRange(
        (e.mousePos.x - track.minX) / trackW, 0.0f, 1.0f);
    applyNormalized(nx);
    return true;
}

bool Slider::onMouseButtonUp(const UIMouseEvent& e) {
    if (!_enabled) {
        return false;
    }
    if (e.mouseButton != 0) {
        return false;
    }
    if (!_dragging) {
        return false;
    }
    (void)e;
    _dragging = false;
    // No state transition needed — base state machine will recompute on
    // the next onMouseMove. We DO need to make sure drag-end doesn't
    // leave a stale _isPressed=true (the base class doesn't know about
    // drag; we mirror it via _state).
    if (_isPressed) {
        _isPressed = false;
    }
    _state = (_isMouseOver ? ButtonState::Hovered : ButtonState::Normal);
    markDirty();
    return true;
}

void Slider::onMouseLeave() {
    InteractiveWidget::onMouseLeave();
    // onMouseLeave from the base class clears _isMouseOver + _isPressed
    // and demotes state to Normal. We don't need to clear _dragging
    // here because the UIManager keeps the captured mouse on us and
    // the drag ends only on a real mouse-up; onMouseLeave alone doesn't
    // end the drag, but UIManager::endStuckSplitterDrags / future
    // analogous recovery handles capture-loss cases (out of scope for
    // Slider v1 — captured mouse + drag-end is the standard path).
}

void Slider::applyNormalized(float nx) {
    if (_max <= _min) {
        return;
    }
    setValue(_min + nx * (_max - _min));
}

void Slider::onRender(IRenderBackend& renderer) {
    math::FRectangle bounds = getWorldBounds();
    if (bounds.maxX <= bounds.minX || bounds.maxY <= bounds.minY) {
        return;
    }

    math::FRectangle track = getTrackRect();

    // Track background — resolveStyle() pattern. Disabled fallback is
    // a flat darker grey.
    const ResolvedStyle style = resolveStyle(getStyleId(), this);
    math::FVector4 trackBg = math::FVector4(0.18f, 0.18f, 0.20f, 1.0f);
    math::FVector4 trackBorder = math::FVector4(0.45f, 0.45f, 0.5f, 1.0f);
    float trackBorderWidth = 1.0f;
    if (style.hasStyle) {
        trackBg = style.backgroundColor;
        trackBorder = style.borderColor;
        trackBorderWidth = style.borderWidth;
    }
    if (!isEnabled()) {
        trackBg = math::FVector4(0.14f, 0.14f, 0.16f, 1.0f);
    }

    // Filled portion — from track leading edge to handle center x.
    const math::FRectangle handle = getHandleRect();
    const float fillMaxX = (handle.minX + handle.maxX) * 0.5f;
    math::FRectangle filled = track;
    filled.maxX = fillMaxX;
    if (filled.maxX > filled.minX) {
        renderer.drawRect(filled,
            resolveAccentColor(1.0f));
    }
    // Unfilled portion — from fillMaxX to track trailing edge.
    math::FRectangle unfilled = track;
    unfilled.minX = fillMaxX;
    if (unfilled.maxX > unfilled.minX) {
        renderer.drawRect(unfilled, trackBg);
    }
    // Outline
    renderer.drawBorderRect(track, trackBorder, trackBorderWidth, 2.0f);

    // Handle
    math::FVector4 handleColor = math::FVector4(0.85f, 0.88f, 0.92f, 1.0f);
    math::FVector4 handleBorder = math::FVector4(0.4f, 0.4f, 0.45f, 1.0f);
    // G9 — when a style is wired AND its bg is not the makeDefault
    // sentinel, derive the handle fill from the style's backgroundColor
    // brightened toward white (so the handle reads as "lighter than the
    // track"). State-aware overrides (disabled / pressed / hover) take
    // precedence — they signal interaction and must win over the theme.
    if (style.hasStyle) {
        const auto& bg = style.backgroundColor;
        // Mix toward white by 0.55 — keeps the style hue but lifts the
        // value so the handle reads against a mid-grey track.
        handleColor.x = bg.x + (1.0f - bg.x) * 0.55f;
        handleColor.y = bg.y + (1.0f - bg.y) * 0.55f;
        handleColor.z = bg.z + (1.0f - bg.z) * 0.55f;
        handleColor.w = bg.w;
        // Border reuses the style border for theme consistency.
        handleBorder = style.borderColor;
    }
    if (!isEnabled()) {
        handleColor = math::FVector4(0.5f, 0.5f, 0.52f, 1.0f);
    } else if (_dragging || isPressed()) {
        handleColor = math::FVector4(0.55f, 0.75f, 1.0f, 1.0f);
    } else if (isMouseOver()) {
        handleColor = math::FVector4(0.95f, 0.96f, 0.98f, 1.0f);
    }
    handleColor = resolveTransitionColor(handleColor);
    // B3: rounded handle fill matches the 2px rounded handle border.
    // (Track halves stay square on purpose — two rounded segments would
    // leave a notch at the fill seam.)
    renderer.drawRoundedRect(handle, handleColor, 2.0f);
    renderer.drawBorderRect(handle, handleBorder, 1.0f, 2.0f);
}

Widget* createSliderWidget() {
    return new Slider();
}

} // namespace ayt::ui
