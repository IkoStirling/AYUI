#include "AYScrollableWidget.h"

#include <algorithm>

namespace ayt::ui {

void ScrollableWidget::setScrollOffset(const math::FVector2& offset) {
    _scrollOffset = offset;
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
    math::FVector2 newOff(
        _scrollOffset.x + delta.x,
        _scrollOffset.y + delta.y);
    if (newOff.x < 0.0f) newOff.x = 0.0f;
    if (newOff.x > maxOff.x) newOff.x = maxOff.x;
    if (newOff.y < 0.0f) newOff.y = 0.0f;
    if (newOff.y > maxOff.y) newOff.y = maxOff.y;
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

} // namespace ayt::ui
