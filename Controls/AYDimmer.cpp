#include "AYDimmer.h"
#include "IAYRenderBackend.h"

namespace ayt::ui {

Dimmer::Dimmer()
    : _scrimColor(0.0f, 0.0f, 0.0f, 0.5f)
{
}

Dimmer::~Dimmer() {
    // No virtual dispatch during destruction (Phase B landmine from PR-5
    // Menu + Phase C landmine from PR-2 TextInput). The dimmer has no
    // virtual hooks that fire on the lifecycle, so this is a true no-op.
}

Widget* Dimmer::hitTest(const math::FVector2& worldPos) {
    if (!_visible) return nullptr;
    const math::FRectangle bounds = getWorldBounds();
    if (!bounds.contains(worldPos)) return nullptr;
    // The dimmer swallows every point inside its viewport-sized bounds.
    // Children of the dimmer (none expected in v1) would still be tested
    // first by CompoundWidget::hitTest, but since Dimmer : Widget (NOT
    // CompoundWidget), there's nothing to descend into — dimmer itself
    // wins the pick.
    return this;
}

bool Dimmer::onMouseButtonDown(const UIMouseEvent& e) {
    // Eat every button-down inside the dimmer. We don't dispatch to the
    // widget under us by design — the modal policy owns whether to
    // close-on-click or not, and that policy lives on the Modal's
    // `_dismissOnDimmerClick`. The Modal wires `_onDismiss` to its
    // `closeModal()` so the call here is what actually triggers the
    // dismissal.
    if (e.mouseButton != 0) return false;
    if (_onDismiss) {
        _onDismiss();
    }
    return true;   // eaten → UIManager captures
}

bool Dimmer::onMouseButtonUp(const UIMouseEvent& /*e*/) {
    // Return false so UIManager does its normal capture-release bookkeeping;
    // we already fired _onDismiss on the down event.
    return false;
}

void Dimmer::onRender(IRenderBackend& renderer) {
    const math::FRectangle bounds = getWorldBounds();
    if (bounds.maxX <= bounds.minX || bounds.maxY <= bounds.minY) return;
    renderer.drawRect(bounds, _scrimColor);
}

} // namespace ayt::ui
