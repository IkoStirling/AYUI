#pragma once

// =============================================================================
// C-11 Tooltip: a hover-delayed text popup anchored to a target widget.
// =============================================================================
//
// Architecture (v1):
//   Tooltip (CompoundWidget, hidden by default)
//     └─ _label: TextLabel  (the body text; may be replaced by any Widget)
//
// Lifecycle: tooltip is attached to a target widget via `Tooltip::attachTo`
// which mounts it on the UIManager's overlay root (NOT as a child of the
// target — see Phase A A2 for the popup-as-overlay convention). The
// tooltip's lifetime is therefore INDEPENDENT of the target: when the
// target is destroyed the tooltip stays alive on the overlay, and the
// next tick() sees `_target` dangling.
//
// To prevent that UAF, callers must call detach() (or destroy the tooltip)
// BEFORE destroying the target, OR rely on the tick() guard that skips
// work when _target is no longer reachable. The Tooltip pointer returned
// from attachTo remains owned by the caller.
//
// -----------------------------------------------------------------------------
// v1 design decisions + known limitations + v1.1 upgrade paths
// -----------------------------------------------------------------------------
//
// DECISION 1: timer advances each call to `tick(dt, mousePos, viewport)`.
//   Hosts that don't call tick() won't see tooltips appear. UIManager's
//   default update() propagation through Widget::tick hits every attached
//   tooltip automatically.
//   v1.1: a passive timer driven by onMouseMove deltas would let tips
//   appear without an explicit tick driver.
//
// DECISION 2: tooltip appears below the target by default. If the target
//   is near the bottom of the viewport, the tooltip is flipped upward.
//   Hosts that don't supply a viewport see the tooltip always below.
//
// DECISION 3: tooltip does not steal mouse input. setPickable(false)
//   so it doesn't intercept click-through.

#include "AYWidget.h"
#include "AYTextLabel.h"
#include <functional>
#include <string>

namespace ayt::ui {

class Tooltip : public CompoundWidget {
public:
    static constexpr float kDefaultHoverDelay = 0.5f;   // seconds
    static constexpr float kDefaultPadding = 6.0f;
    static constexpr float kTipOffsetY = 6.0f;

    Tooltip();
    ~Tooltip() override;

    // Factory: create a tooltip attached as overlay popup via the active
    // UIManager. Lifetime is independent of `target`; caller must call
    // detach() (or destroy the tooltip) before destroying the target to
    // avoid a dangling _target reference. Returns nullptr if no active
    // UIManager.
    static Tooltip* attachTo(Widget* target);

    // Detach from the overlay (no-op if not mounted) and disarm _target
    // so subsequent tick() calls early-return without dereferencing a
    // possibly-destroyed anchor. Idempotent. Caller still owns the
    // Tooltip* and must destroyWidgetTree it (NOT `delete` — Tooltip owns
    // its TextLabel child via addChild; UI-OWN-1 invariant: ~Widget does
    // not free children. destroyWidgetTree walks the tree recursively).
    void detach();

    // Text payload.
    void setText(const std::wstring& text);
    const std::wstring& getText() const;

    // Hover tuning. Hover delay in seconds.
    void setHoverDelay(float seconds) { _hoverDelay = seconds; }
    float getHoverDelay() const { return _hoverDelay; }

    // Visibility / hover state.
    bool isShowing() const { return _visible; }
    bool isHovered() const { return _hovering; }

    // Access for tests.
    Widget* getTarget() const { return _target; }

    // Advance timer. Mouse pos + viewport size are forwarded so the
    // tooltip can flip upward near the bottom edge (DECISION 2).
    void tick(float dt, const math::FVector2& mousePos,
              const math::FVector2& viewportSize);

    // UI animation lane: drives the fade-out completion check. Driven by
    // UIManager's overlay cascade (compoundDescendTick), NOT the 3-arg
    // hover driver — that one pauses when _hasLastMouse is false or the
    // tip is unregistered, which would freeze a fade mid-air.
    void tick(float dt) override;

    void performLayout() override;
    void onRender(IRenderBackend& renderer) override;

    // Pick-through — DECISION 3. Clicks fall through to the underlying target.
    Widget* hitTest(const math::FVector2& worldPos) override;

private:
    void show();
    void hide();
    void syncPosition();
    void ensureLabelCreated();

    Widget*   _target = nullptr;
    TextLabel* _label = nullptr;

    bool  _hovering = false;
    bool  _visible  = false;
    bool  _hiding   = false;   // UI animation lane: fade-out in flight
    float _hoverTime = 0.0f;
    float _hoverDelay = kDefaultHoverDelay;

    math::FVector2 _viewportSize{0.0f, 0.0f};
};

Widget* createTooltipWidget();   // unused stand-alone factory hook

} // namespace ayt::ui
