#pragma once

#include "AYWidget.h"
#include "aymath/MathTypes.h"

namespace ayt::ui {

// C-4 ScrollableWidget: a thin helper base for any widget that owns
// "more content than is visible" — ScrollView (the headline user),
// later C-5 ListView, and the C-9 TabControl content area.
//
// Provides:
//   - _contentSize: the virtual size of the content (may exceed the
//     widget's own bounds). Defaults to (0, 0) — subclasses set this
//     when they adopt layout.
//   - _scrollOffset: the (x, y) pixel offset into _contentSize the
//     widget is currently showing at. Top-left of the widget maps
//     to (_scrollOffset.x, _scrollOffset.y) on the virtual content.
//   - getMaxScrollOffset(): clamped max so content doesn't go past
//     its bounds.
//   - scrollBy(dx, dy): clamps and applies. Fires _onScroll only on
//     real changes.
//
// Why not a polymorphic base at v1 (mirrors AYValueWidget comment):
//   - The base would only have ScrollView (C-4) and maybe later
//     ListView / TabControl as consumers. With one consumer in v1,
//     the abstraction is premature.
//   - ScrollView implements most of the API itself; this file
//     documents the contract for when the 2nd consumer appears.
//
// Subclass contract:
//   - In layout pass, set _contentSize to the virtual bounds.
//   - When rendering children, offset each child by -_scrollOffset.
//   - When the user drags a ScrollBar, call scrollBy() and let the
//     helper clamp.

class ScrollableWidget {
public:
    ScrollableWidget() = default;
    virtual ~ScrollableWidget() = default;

    void setContentSize(const math::FVector2& size) { _contentSize = size; }
    const math::FVector2& getContentSize() const { return _contentSize; }

    void setScrollOffset(const math::FVector2& offset);
    const math::FVector2& getScrollOffset() const { return _scrollOffset; }

    math::FVector2 getMaxScrollOffset(const math::FVector2& viewportSize) const;

    // Returns true if the offset changed after clamping.
    bool scrollBy(const math::FVector2& delta, const math::FVector2& viewportSize);

protected:
    // Subclass overrides; ScrollableWidget's default impl fires the
    // callback if attached.
    virtual void onScrollChanged() {}

    math::FVector2 _contentSize{0.0f, 0.0f};
    math::FVector2 _scrollOffset{0.0f, 0.0f};
};

} // namespace ayt::ui
