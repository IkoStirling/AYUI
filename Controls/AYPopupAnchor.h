#pragma once

// =============================================================================
// PR-Container-Shared-Contract: shared popup-anchor placement helper.
//
// Tooltip and ComboBox used to inline their own flip+clamp math, drifting
// apart — Tooltip missed the x-clamp entirely, ComboBox had the x-clamp
// but its formula differed in subtle ways. One function, one contract.
//
// API:
//   belowPos  : candidate position if the popup anchors BELOW the target.
//   abovePos  : candidate position if the popup anchors ABOVE the target.
//   size      : popup's own size in pixels (w, h).
//   viewport  : client area size (w, h). Sentinel (0, 0) is accepted and
//               means "no clamp" (used during initial layout before the
//               UIManager has reported client size).
//
// Returns the final position to setPosition() on the popup widget.
// =============================================================================

#include "aymath/MathTypes.h"

namespace ayt::ui {

math::FVector2 popupAnchorPlacement(const math::FVector2& belowPos,
                                    const math::FVector2& abovePos,
                                    const math::FVector2& size,
                                    const math::FVector2& viewport);

} // namespace ayt::ui