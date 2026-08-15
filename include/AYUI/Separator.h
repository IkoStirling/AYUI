#pragma once

// =============================================================================
// C-11 Separator: a leaf widget that draws a 1-pixel rule (horizontal or
// vertical) used inside menus, toolbars, status bars, etc.
// =============================================================================
//
// Architecture (v1):
//   Separator : LeafWidget
//     - orientation: Horizontal (default) | Vertical
//     - thickness: 1 px (line) | N px (block)        — default 1
//     - color: 0.5 / 0.5 / 0.55 / 1.0                — overridable
//     - inset: 0 px on each cross axis               — padding from edge
//
// Why a leaf: Separator has no children, no focus, no input. It is just
// a render-time visual primitive — same role as Image / TextLabel.
// Layout hint: a horizontal separator with default size renders as a
// 16 px tall, 100 px wide thin rule. Hosts typically constrain one axis
// to match the parent's stretching logic (e.g. toolbar height for
// horizontal rules; menu item width for vertical rules).

#include "AYUI/LeafWidget.h"
#include "AYMath/MathTypes.h"

namespace ayt::ui {

class Separator : public LeafWidget {
public:
    enum class Orientation {
        Horizontal,  // ──── (long axis = width)
        Vertical,    // |||| (long axis = height)
    };

    Separator();
    ~Separator() override;

    Orientation getOrientation() const { return _orientation; }
    void setOrientation(Orientation o) { _orientation = o; markBoundsDirty(); }

    float getThickness() const { return _thickness; }
    void setThickness(float t) { _thickness = (t < 1.0f ? 1.0f : t); markBoundsDirty(); }

    const math::FVector4& getColor() const { return _color; }
    void setColor(const math::FVector4& c) { _color = c; markBoundsDirty(); }

    float getInset() const { return _inset; }
    void setInset(float i) { _inset = (i < 0.0f ? 0.0f : i); markBoundsDirty(); }

    void onRender(IRenderBackend& renderer) override;

private:
    Orientation _orientation = Orientation::Horizontal;
    float _thickness = 1.0f;
    float _inset     = 0.0f;
    math::FVector4 _color{0.5f, 0.5f, 0.55f, 1.0f};
};

Widget* createSeparatorWidget();

} // namespace ayt::ui
