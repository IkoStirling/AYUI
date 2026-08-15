#pragma once

#include "AYUI/InteractiveWidget.h"
#include <functional>

namespace ayt::ui {

// C-7 Slider. A horizontal slider that drags to set a continuous value
// in [_min, _max]. Default range [0, 1], default value 0.
//
// Why extends InteractiveWidget:
//   - Slider needs the Hovered/Pressed/Disabled state machine (so the
//     track can darken on hover, the handle can shrink during press,
//     and a disabled Slider is visibly non-interactive).
//   - Cursor hint: Slider wants SizeHorizontal — Override getCursorHint
//     below so the host (AYEditor / window manager) shows a horizontal
//     resize arrow over the slider band.
//   - Captured-mouse contract: click+drag is captured by UIManager via
//     pickWidgetAt + onMouseButtonDown. Slider rides that path; no
//     custom capture needed.
//
// Drag-state ownership:
//   _dragging = true after onMouseButtonDown. onMouseMove updates _value
//   only while _dragging is true. onMouseButtonUp clears _dragging and
//   synthesizes a final value-change emit (the mouse-up's pixel location
//   might be slightly past the value-clamped position; we re-clamp and
//   emit the post-clamp value so observers see the stable final state).
//
// Value contract (see AYValueWidget.h comments) lives on Slider verbatim;
// ProgressBar mirrors it. Future consolidation into a polymorphic base
// when a 3rd consumer appears.

class Slider : public InteractiveWidget {
public:
    static constexpr float kTrackHeight = 6.0f;
    static constexpr float kHandleWidth = 12.0f;
    static constexpr float kHandleHeight = 18.0f;
    static constexpr float kMinMaxEpsilon = 1e-5f;

    Slider();
    ~Slider() override;

    void  setMin(float v);
    float getMin() const { return _min; }

    void  setMax(float v);
    float getMax() const { return _max; }

    // Resets _value to the new minimum when the new range excludes the
    // current value (so a "shrink range" doesn't leave the value dangling
    // past max). If the current value still fits in the new range, it's
    // preserved. Fires _onValueChanged on the reset.
    void setValueRange(float minVal, float maxVal);

    void  setValue(float v);
    float getValue() const { return _value; }

    // Fraction (0..1, clamped) — convenient for render + tests.
    float getNormalized() const;

    void setOnValueChanged(std::function<void(float)> cb) {
        _onValueChanged = std::move(cb);
    }

    bool isDragging() const { return _dragging; }

    // Hit / render helpers exposed for tests + render.
    math::FRectangle getTrackRect() const;
    math::FRectangle getHandleRect() const;

    UiCursorHint getCursorHint() const override;
    bool onMouseButtonDown(const UIMouseEvent& e) override;
    bool onMouseMove(const UIMouseEvent& e) override;
    bool onMouseButtonUp(const UIMouseEvent& e) override;
    void onMouseLeave() override;

protected:
    void onRender(IRenderBackend& renderer) override;

    // Apply a normalized (0..1) click position on the track, clamping and
    // updating _value. No-op when _min == _max (degenerate range).
    void applyNormalized(float nx);

    float _min = 0.0f;
    float _max = 1.0f;
    float _value = 0.5f;

    bool _dragging = false;
    std::function<void(float)> _onValueChanged;
};

Widget* createSliderWidget();

} // namespace ayt::ui
