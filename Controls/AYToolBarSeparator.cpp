#include "AYUI/ToolBarSeparator.h"
#include "AYUI/IRenderBackend.h"

namespace ayt::ui {

ToolBarSeparator::ToolBarSeparator() {
    // G7 — default to vertical (toolbars group buttons horizontally
    // with vertical separators between groups). 1px wide × 24px tall
    // matches the typical ToolBar row height minus 2*padding. Hosts
    // can override via setSize / setThickness.
    setOrientation(Separator::Orientation::Vertical);
    setSize(math::FVector2(kDefaultBarWidth, kDefaultBarHeight));
    // Toolbar palette: slightly cooler than base Separator's neutral
    // gray (0.5/0.5/0.55) so the line reads against the standard
    // toolbar background (0.18-0.20 range).
    setColor(math::FVector4(0.42f, 0.42f, 0.48f, 0.9f));
    setThickness(1.0f);
    setInset(0.0f);
    setLayoutPositionManaged(false);
    setLayoutSizeManaged(false);
}

ToolBarSeparator::~ToolBarSeparator() = default;

void ToolBarSeparator::onRender(IRenderBackend& renderer) {
    // Same draw as base Separator: a single rect in the long-axis
    // direction. We override only to anchor the paint to the base
    // class's contract — G7's distinguishing feature is the
    // pre-styled defaults, not a custom draw.
    Separator::onRender(renderer);
}

Widget* createToolBarSeparatorWidget() {
    return new ToolBarSeparator();
}

} // namespace ayt::ui