#include "AYUI/Separator.h"
#include "AYUI/IRenderBackend.h"
#include "AYUI/Style.h"

namespace ayt::ui {

Separator::Separator() {
    setSize(math::FVector2(100.0f, 1.0f));   // horizontal default
}

Separator::~Separator() = default;

void Separator::onRender(IRenderBackend& renderer) {
    const math::FRectangle bounds = getWorldBounds();
    if (bounds.maxX <= bounds.minX || bounds.maxY <= bounds.minY) return;

    // G9 — follow the resolveStyle() pattern (R-5). When a style is wired
    // AND its bg is not the makeDefault sentinel, the line uses
    // style.borderColor (semantically: a separator IS a border). Otherwise
    // the stored _color wins (so ToolBarSeparator's pre-styled palette
    // and host-setColor() overrides both keep working — they only get
    // overridden when a styleId is set AND the style has a real color).
    const ResolvedStyle style = resolveStyle(getStyleId());
    math::FVector4 lineColor = _color;
    if (style.hasStyle) {
        lineColor = style.borderColor;
    }

    math::FRectangle line;
    if (_orientation == Orientation::Horizontal) {
        // Center the rule vertically; apply side insets on the long axis.
        float cy = (bounds.minY + bounds.maxY) * 0.5f - _thickness * 0.5f;
        line = math::FRectangle(
            bounds.minX + _inset, cy,
            bounds.maxX - _inset, cy + _thickness);
    } else {
        float cx = (bounds.minX + bounds.maxX) * 0.5f - _thickness * 0.5f;
        line = math::FRectangle(
            cx, bounds.minY + _inset,
            cx + _thickness, bounds.maxY - _inset);
    }
    renderer.drawRect(line, lineColor);
}

Widget* createSeparatorWidget() { return new Separator(); }

} // namespace ayt::ui
