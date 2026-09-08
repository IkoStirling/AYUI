#pragma once

#include "AYUI/Panel.h"

namespace ayt::ui {

// Editor-only view node. Authored widgets keep document-space geometry while
// this node maps their rendering and hit testing through a local pan/zoom.
class LayoutCanvasViewport final : public Panel {
public:
    LayoutCanvasViewport();

    void setView(float zoom, const math::FVector2& pan);
    float getViewZoom() const { return _zoom; }
    const math::FVector2& getViewPan() const { return _pan; }

    math::FVector2 documentToScreen(const math::FVector2& point) const;
    math::FVector2 screenToDocument(const math::FVector2& point) const;
    math::FRectangle documentToScreen(const math::FRectangle& bounds) const;

    Widget* hitTest(const math::FVector2& worldPos) override;
    void renderChildren(IRenderBackend& renderer) override;

private:
    float _zoom = 1.0f;
    math::FVector2 _pan{0.0f, 0.0f};
};

} // namespace ayt::ui
