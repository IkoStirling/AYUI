#pragma once

#include "AYUI/Widget.h"

namespace ayt::ui {

// R-10 (C-1): Panel is a CompoundWidget that paints a styled background
// (and optional border) for a group of children. The "container that draws
// its own background" pattern used to be open-coded via subclassing
// CompoundWidget and overriding onRender — Panel makes it a first-class
// widget so layouts can express "a tinted region grouping these controls"
// without writing C++.
//
// Panel inherits CompoundWidget so:
//   - it can host children (Container semantics, layout pass via
//     layoutChildren — Panel leaves that to the parent layout because
//     Panel does not impose a stacking direction; VBox/HBox inside is the
//     typical use)
//   - hitTest descends into children (R-6 invariant)
//   - onMouseLeave propagates to descendants (R-6 invariant)
//
// Panel's onRender uses StyleManager::getStyle(styleId) so users can switch
// its visual via setStyleId("panel_default") / stylesheet override. No
// style registered → falls back to a sensible neutral grey (see
// panel_default in StyleBuilder::makePanel).
class Panel : public CompoundWidget {
public:
    Panel();
    ~Panel() override;

    // Toggle the optional border. Border uses the resolved style's
    // border.color/border.width when true. Default: true.
    void setBorderEnabled(bool enabled) { _borderEnabled = enabled; }
    bool isBorderEnabled() const { return _borderEnabled; }

    // When false, onRender skips the opaque body fill (and border).
    // Editor Play composite uses this on `card_viewport` so PostProcess
    // 3D remains visible under the DockCard hole. Default: true.
    void setBackgroundEnabled(bool enabled) { _backgroundEnabled = enabled; }
    bool isBackgroundEnabled() const { return _backgroundEnabled; }

    // Padding inset (logical pixels) for child layout. Panel does not
    // lay out children itself — this is exposed as a property so a future
    // layout pass (C-2 helpers) can read it without re-fetching from a
    // style. Stored as FVector4 to match WidgetStyle::padding.
    void setPadding(const math::FVector4& padding) { _padding = padding; }
    void setPadding(float left, float top, float right, float bottom);
    const math::FVector4& getPadding() const { return _padding; }

    void onRender(IRenderBackend& renderer) override;

protected:
    bool _borderEnabled = true;
    bool _backgroundEnabled = true;
    math::FVector4 _padding{4.0f, 4.0f, 4.0f, 4.0f};
};

} // namespace ayt::ui