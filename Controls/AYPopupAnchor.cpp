#include "AYUI/PopupAnchor.h"

#include <algorithm>

namespace ayt::ui {

math::FVector2 popupAnchorPlacement(const math::FVector2& belowPos,
                                    const math::FVector2& abovePos,
                                    const math::FVector2& size,
                                    const math::FVector2& viewport) {
    math::FVector2 pos = belowPos;
    // 1. Flip decision: prefer "below" unless it overflows the bottom edge
    //    AND "above" still fits. Matches Windows native tooltip/combobox
    //    convention.
    if (viewport.y > 0.0f && pos.y + size.y > viewport.y && abovePos.y >= 0.0f) {
        pos = abovePos;
    }
    // 2. Clamp y inside [0, viewport.y - size.y]. Anchor may have been
    //    positioned outside the viewport if target is near the edge.
    if (viewport.y > 0.0f) {
        if (pos.y < 0.0f) pos.y = 0.0f;
        if (pos.y + size.y > viewport.y) {
            pos.y = std::max(0.0f, viewport.y - size.y);
        }
    }
    // 3. Clamp x inside [0, viewport.x - size.x]. Tooltip previously
    //    SKIPPED this clamp (latent bug — anchor at minX=10 with width
    //    500 in an 800-wide viewport overflows on the right).
    if (viewport.x > 0.0f) {
        if (pos.x < 0.0f) pos.x = 0.0f;
        if (pos.x + size.x > viewport.x) {
            pos.x = std::max(0.0f, viewport.x - size.x);
        }
    }
    return pos;
}

} // namespace ayt::ui