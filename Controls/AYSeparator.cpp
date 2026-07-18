#include "AYSeparator.h"
#include "AYIRenderBackend.h"

namespace ayt::ui {

Separator::Separator() {
    setSize(math::FVector2(100.0f, 1.0f));   // horizontal default
}

Separator::~Separator() = default;

void Separator::onRender(IRenderBackend& renderer) {
    const math::FRectangle bounds = getWorldBounds();
    if (bounds.maxX <= bounds.minX || bounds.maxY <= bounds.minY) return;

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
    renderer.drawRect(line, _color);
}

Widget* createSeparatorWidget() { return new Separator(); }

} // namespace ayt::ui
