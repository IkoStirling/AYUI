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
// which adds it as an OWNING child. When the target is destroyed via
// destroyWidgetTree, the tooltip goes with it. The tooltip starts hidden
// and becomes visible when the cursor hovers the target for at least
// _hoverDelay seconds.
//
// Why popup-as-child (matches ComboBox DECISION 2): simplest ownership
// story — tooltip lifetime tracks target lifetime via the existing
// destroyWidgetTree path.
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

    // Factory: create a tooltip attached as OWNING child of `target`.
    // Caller does NOT manually free the tooltip — it's owned by `target`.
    static Tooltip* attachTo(Widget* target);

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
    float _hoverTime = 0.0f;
    float _hoverDelay = kDefaultHoverDelay;

    math::FVector2 _viewportSize{0.0f, 0.0f};
};

Widget* createTooltipWidget();   // unused stand-alone factory hook

} // namespace ayt::ui
