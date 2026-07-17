#pragma once

#include "AYWidget.h"

namespace ayt::ui {

// Marker base for leaf renderers (TextLabel, Image, ProgressBar).
// Provides a performLayout() no-op override matching the previous Image
// pattern. R-3 (2026-07-17) extracted this so TextLabel stops inheriting
// Button. Future widgets that share a leaf rendering payload should also
// extend LeafWidget.
class LeafWidget : public Widget {
public:
    LeafWidget() = default;
    ~LeafWidget() override = default;

    void performLayout() override {
        // Leaf widget: size/position come from the parent layout pass.
    }
};

} // namespace ayt::ui