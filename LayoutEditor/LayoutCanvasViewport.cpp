#include "AYUI/LayoutEditor/LayoutCanvasViewport.h"

#include <cmath>

namespace ayt::ui {

LayoutCanvasViewport::LayoutCanvasViewport() {
    setId("__le_canvas_viewport");
    setBackgroundEnabled(false);
    setBorderEnabled(false);
    setPadding(0.0f, 0.0f, 0.0f, 0.0f);
    setLayoutPositionManaged(false);
    setLayoutSizeManaged(false);
}

void LayoutCanvasViewport::setView(float zoom, const math::FVector2& pan) {
    if (std::fabs(_zoom - zoom) < 0.0001f &&
        std::fabs(_pan.x - pan.x) < 0.0001f &&
        std::fabs(_pan.y - pan.y) < 0.0001f) {
        return;
    }
    _zoom = zoom;
    _pan = pan;
    markDirty();
}

math::FVector2 LayoutCanvasViewport::documentToScreen(
    const math::FVector2& point) const {
    const math::FRectangle bounds = getWorldBounds();
    return math::FVector2(
        bounds.minX + _pan.x + (point.x - bounds.minX) * _zoom,
        bounds.minY + _pan.y + (point.y - bounds.minY) * _zoom);
}

math::FVector2 LayoutCanvasViewport::screenToDocument(
    const math::FVector2& point) const {
    const math::FRectangle bounds = getWorldBounds();
    const float inverseZoom = _zoom > 0.0001f ? 1.0f / _zoom : 1.0f;
    return math::FVector2(
        bounds.minX + (point.x - bounds.minX - _pan.x) * inverseZoom,
        bounds.minY + (point.y - bounds.minY - _pan.y) * inverseZoom);
}

math::FRectangle LayoutCanvasViewport::documentToScreen(
    const math::FRectangle& source) const {
    const math::FVector2 min = documentToScreen(
        math::FVector2(source.minX, source.minY));
    const math::FVector2 max = documentToScreen(
        math::FVector2(source.maxX, source.maxY));
    return math::FRectangle(min.x, min.y, max.x, max.y);
}

Widget* LayoutCanvasViewport::hitTest(const math::FVector2& worldPos) {
    if (!isVisible() || !getWorldBounds().contains(worldPos)) {
        return nullptr;
    }
    const math::FVector2 documentPos = screenToDocument(worldPos);
    const std::vector<Widget*>& children = getChildren();
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        if (*it != nullptr) {
            if (Widget* hit = (*it)->hitTest(documentPos)) {
                return hit;
            }
        }
    }
    return this;
}

void LayoutCanvasViewport::renderChildren(IRenderBackend& renderer) {
    const math::FRectangle bounds = getWorldBounds();
    const float tx = bounds.minX + _pan.x - bounds.minX * _zoom;
    const float ty = bounds.minY + _pan.y - bounds.minY * _zoom;
    const math::Float4x4 transform(
        _zoom, 0.0f, 0.0f, tx,
        0.0f, _zoom, 0.0f, ty,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f);
    renderer.pushClip(bounds);
    renderer.pushTransform(transform);
    Widget::renderChildren(renderer);
    renderer.popTransform();
    renderer.popClip();
}

} // namespace ayt::ui
