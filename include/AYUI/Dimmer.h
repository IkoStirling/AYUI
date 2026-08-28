#pragma once

#include "AYUI/Widget.h"

namespace ayt::ui {

// =============================================================================
// Phase D (D2) — Dimmer
// =============================================================================
//
// A full-viewport semi-transparent black scrim that blocks input from
// reaching widgets underneath it. The Dimmer itself does NOT decide whether
// to close its modal — it just fires `_onDismiss` on click and swallows the
// event. The owning Modal widget owns the policy (Q8 — _dismissOnDimmerClick).
//
// Inheritance: plain `Widget` (not `CompoundWidget`). Mirrors `_overlayRoot`'s
// design choice in UIManager (Phase A, R-9) — CompoundWidget::hitTest would
// self-match the dimmer at every cursor position, which would require extra
// filtering in `pickTopmostWidget`. Plain Widget::hitTest only returns `this`
// when the cursor is inside the dimmer's bounds, so the funnel handles it
// naturally.
//
// Visibility / sizing: in v1 the dimmer is sized to the full viewport by the
// owning Modal at open time. Modal::openModal sets size to
// `UIManager::getClientSize()` and re-parents the dimmer onto `_overlayRoot`,
// then re-parents the Modal content widget on top of the dimmer (children are
// ordered: dimmer first, modal content last; pickTopmostWidget reverse-iterates
// overlay children, so modal content wins by-pick-order).
// =============================================================================
class Dimmer : public Widget {
public:
    Dimmer();
    ~Dimmer() override;

    // Q8 — sink called on click. The owning Modal subscribes; the dimmer
    // itself does not know whether to close (the Modal decides based on
    // `_dismissOnDimmerClick`).
    using DismissCallback = std::function<void()>;
    void setOnDismiss(DismissCallback cb) { _onDismiss = std::move(cb); }

    // Color of the scrim. Defaults to (0, 0, 0, 0.5) — 50% black, a common
    // industry value. Hosts that want a lighter or darker overlay can adjust
    // per-modal at open time.
    void setScrimColor(const math::FVector4& color) {
        _scrimColor = color;
        markDirty();
    }
    const math::FVector4& getScrimColor() const { return _scrimColor; }

    // Standard Widget overrides. hitTest returns `this` for any point
    // inside the dimmer's bounds (which is the entire viewport by design).
    // onMouseButtonDown / onMouseButtonUp fire the dismiss sink and return
    // `true` so UIManager records the capture (and cancels it on release).
    Widget* hitTest(const math::FVector2& worldPos) override;
    bool onMouseButtonDown(const UIMouseEvent& e) override;
    bool onMouseButtonUp(const UIMouseEvent& e) override;
    void onRender(IRenderBackend& renderer) override;

private:
    DismissCallback _onDismiss;
    math::FVector4 _scrimColor;
};

} // namespace ayt::ui
