#pragma once

// Hover-delayed text popup mounted on UIManager's overlay. UIManager drives
// attached tooltips from its update path; placement defaults below the target
// and flips near the viewport edge. Tooltip is pick-through and never steals
// mouse input.
//
// Tooltip lifetime is independent of its non-owning target pointer. Call
// detach() or destroy the tooltip before destroying the target; the caller
// owns the Tooltip returned by attachTo().

#include "AYUI/Widget.h"
#include "AYUI/TextLabel.h"
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
