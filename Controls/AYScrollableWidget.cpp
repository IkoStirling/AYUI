#include "AYUI/ScrollableWidget.h"

#include <algorithm>
#include <cmath>

namespace ayt::ui {

namespace {

// 1px SDF borders / separators AA against subpixel positions. Keep the
// scroll offset on whole pixels so every scrolled container edge moves
// in lockstep instead of shimmering while the fractional part churns.
math::FVector2 snapScrollPixels(const math::FVector2& v)
{
    return math::FVector2(std::round(v.x), std::round(v.y));
}

// UI-anim cut 2 — momentum tuning. v *= pow(friction, dt) gives an
// exponential decay with a ~0.23s half-life; below kStopSpeed px/s an
// axis is parked (no sub-pixel drift forever).
constexpr float kMomentumFriction = 0.02f;
constexpr float kMomentumStopSpeed = 2.0f;

} // namespace

void ScrollableWidget::setScrollOffset(const math::FVector2& offset) {
    _scrollOffset = snapScrollPixels(offset);
    onScrollChanged();
}

math::FVector2 ScrollableWidget::getMaxScrollOffset(const math::FVector2& viewportSize) const {
    const float maxX = (_contentSize.x > viewportSize.x)
        ? (_contentSize.x - viewportSize.x) : 0.0f;
    const float maxY = (_contentSize.y > viewportSize.y)
        ? (_contentSize.y - viewportSize.y) : 0.0f;
    return math::FVector2(maxX, maxY);
}

bool ScrollableWidget::scrollBy(const math::FVector2& delta, const math::FVector2& viewportSize) {
    const math::FVector2 maxOff = getMaxScrollOffset(viewportSize);
    math::FVector2 newOff = snapScrollPixels(math::FVector2(
        _scrollOffset.x + delta.x,
        _scrollOffset.y + delta.y));
    if (newOff.x < 0.0f) newOff.x = 0.0f;
    if (newOff.x > maxOff.x) newOff.x = maxOff.x;
    if (newOff.y < 0.0f) newOff.y = 0.0f;
    if (newOff.y > maxOff.y) newOff.y = maxOff.y;
    // Re-snap after clamp: maxOff can be fractional when content/viewport
    // sizes are not integers; stay on whole pixels except when parked
    // exactly on a fractional max (prefer not exceeding content).
    if (newOff.x < maxOff.x) newOff.x = std::round(newOff.x);
    if (newOff.y < maxOff.y) newOff.y = std::round(newOff.y);
    if (fabsf(newOff.x - _scrollOffset.x) < 1e-5f &&
        fabsf(newOff.y - _scrollOffset.y) < 1e-5f) {
        return false;
    }
    _scrollOffset = newOff;
    onScrollChanged();
    return true;
}

// PR-Container-Shared-Contract: pure-function clamp. Window body has its
// own _scrollY field and only needs the math, not the state mutation.
math::FVector2 ScrollableWidget::clampScrollOffset(const math::FVector2& target,
                                                   const math::FVector2& viewportSize,
                                                   const math::FVector2& contentSize) {
    const float maxX = (contentSize.x > viewportSize.x)
        ? (contentSize.x - viewportSize.x) : 0.0f;
    const float maxY = (contentSize.y > viewportSize.y)
        ? (contentSize.y - viewportSize.y) : 0.0f;
    return math::FVector2(
        std::clamp(target.x, 0.0f, maxX),
        std::clamp(target.y, 0.0f, maxY));
}

bool ScrollableWidget::applyWheel(const math::FVector2& delta,
                                  const math::FVector2& viewportSize) {
    // Immediate scroll first (unchanged pre-momentum semantics), then
    // seed the glider from the snapped true offset.
    const bool changed = scrollBy(delta, viewportSize);
    _momentumOffset = _scrollOffset;
    _velocity = delta;
    _momentumActive = true;
    return changed;
}

bool ScrollableWidget::advanceMomentum(float dt,
                                       const math::FVector2& viewportSize,
                                       math::FVector2& outDelta) {
    outDelta = math::FVector2(0.0f, 0.0f);
    if (!_momentumActive) return false;

    // Exponential decay; park axes that have slowed below the stop speed.
    const float decay = std::pow(kMomentumFriction, dt);
    _velocity.x *= decay;
    _velocity.y *= decay;
    if (std::fabs(_velocity.x) < kMomentumStopSpeed) _velocity.x = 0.0f;
    if (std::fabs(_velocity.y) < kMomentumStopSpeed) _velocity.y = 0.0f;
    if (_velocity.x == 0.0f && _velocity.y == 0.0f) {
        _momentumActive = false;
        return false;
    }

    _momentumOffset += _velocity * dt;

    // Boundary: hard stop an axis that ran into the edge (no bounce in
    // v1; a fling past the end just parks at the limit).
    const math::FVector2 maxOff = getMaxScrollOffset(viewportSize);
    if (_momentumOffset.x < 0.0f) {
        _momentumOffset.x = 0.0f;
        _velocity.x = 0.0f;
    } else if (_momentumOffset.x > maxOff.x) {
        _momentumOffset.x = maxOff.x;
        _velocity.x = 0.0f;
    }
    if (_momentumOffset.y < 0.0f) {
        _momentumOffset.y = 0.0f;
        _velocity.y = 0.0f;
    } else if (_momentumOffset.y > maxOff.y) {
        _momentumOffset.y = maxOff.y;
        _velocity.y = 0.0f;
    }

    outDelta = snapScrollPixels(_momentumOffset) - _scrollOffset;
    return (std::fabs(outDelta.x) > 1e-5f || std::fabs(outDelta.y) > 1e-5f);
}

void ScrollableWidget::clearMomentum() {
    _momentumActive = false;
    _velocity = math::FVector2(0.0f, 0.0f);
}

} // namespace ayt::ui
