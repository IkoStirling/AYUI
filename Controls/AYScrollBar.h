#pragma once

#include "AYInteractiveWidget.h"
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
    float trackLength() const;

    Orientation _orientation = Orientation::Vertical;
    float _min = 0.0f;
    float _max = 1.0f;
    float _value = 0.0f;
    float _viewportSize = 1.0f;
    bool _dragging = false;
    std::function<void(float)> _onValueChanged;
};

Widget* createScrollBarWidget();

} // namespace ayt::ui
