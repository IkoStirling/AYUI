#pragma once

#include "AYUI/InteractiveWidget.h"
#include <functional>

namespace ayt::ui {

// C-4 ScrollBar: horizontal or vertical scrollbar. Always lives
// inside a ScrollView's frame as a sibling; the ScrollView drives
// ScrollBar's value via setValue/setRange when content dimensions
// change. ScrollBar in turn drives scrollBy on its owning ScrollView
// when the user drags.
//
// Orientation:
//   kHorizontal -> height = kDefaultBarWidth (thin)
//   kVertical   -> width  = kDefaultBarWidth
// The thumb dimensions are computed from the visible-vs-content ratio:
//   thumbLength = (viewport / content) * trackLength
//   thumbPosition = (offset / (content - viewport)) * (trackLength - thumbLength)
//
// Cursor hint overrides:
//   Horizontal -> SizeVertical (per VSCode's scheme: ↕ while hovering
//                horizontal bar — actually we keep it SimpleVertical
//                since the user uses scroll wheel for the dominant
//                motion; thumb-drag cursor is what SizeHorizontal
//                would imply, but VSCode uses default in horizontal
//                bar — we return Default for horizontal)
//   Vertical   -> SizeVertical when hovered
//
// Why InteractiveWidget: bar needs hover/press state machine; the
// drag thumb-portion works the same as a Slider drag.

class ScrollBar : public InteractiveWidget {
public:
    enum class Orientation { Horizontal, Vertical };
    static constexpr float kDefaultBarWidth = 12.0f;
    static constexpr float kMinThumbLength = 16.0f;
    static constexpr float kMinMaxEpsilon = 1e-5f;

    ScrollBar();
    ~ScrollBar() override;

    void setOrientation(Orientation o) { _orientation = o; }
    Orientation getOrientation() const { return _orientation; }

    // Range / value mirroring the host ScrollView's exposed API.
    void  setRange(float minV, float maxV);
    void  setValue(float v);
    float getValue() const { return _value; }

    // Visible viewport size — bar computes thumb ratio from this vs
    // (maxV - minV).
    void  setViewportSize(float vs) { _viewportSize = vs; markBoundsDirty(); }
    float getViewportSize() const { return _viewportSize; }

    void setOnValueChanged(std::function<void(float)> cb) {
        _onValueChanged = std::move(cb);
    }

    UiCursorHint getCursorHint() const override;
    bool onMouseButtonDown(const UIMouseEvent& e) override;
    bool onMouseMove(const UIMouseEvent& e) override;
    bool onMouseButtonUp(const UIMouseEvent& e) override;

    math::FRectangle getTrackRect() const;
    math::FRectangle getThumbRect() const;

protected:
    void onRender(IRenderBackend& renderer) override;

    void applyNormalized(float nx);
    // Maps an absolute thumb-start position along the track (in track
    // space, 0 = track start) to a scroll offset. Shared by click-to-jump
    // (thumb centered under cursor) and thumb drag (pressed offset kept).
    void applyThumbStart(float thumbStartAlong);
    float trackLength() const;

    Orientation _orientation = Orientation::Vertical;
    float _min = 0.0f;
    float _max = 1.0f;
    float _value = 0.0f;
    // Viewport-size 0 by default (= the entire content is visible, so
    // scrollable range equals full range). The pre-existing tests
    // assume this; defaulting to 1.0 collapses the last unit
    // (scrollable = content - 1) and breaks setRange/setValue clamp.
    float _viewportSize = 0.0f;
    bool _dragging = false;
    // PR-S1b: offset from the pressed cursor to the thumb's leading edge
    // at mouse-down. Thumb press keeps the offset fixed while dragging
    // (thumb travels with the cursor, no jump); track press uses
    // thumbLen/2 so drag keeps the thumb centered under the cursor.
    float _dragOffset = 0.0f;
    std::function<void(float)> _onValueChanged;
};

Widget* createScrollBarWidget();

} // namespace ayt::ui
