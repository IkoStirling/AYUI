#pragma once

#include "AYUI/Widget.h"
#include <functional>

namespace ayt::ui {

// State machine shared by Button / CheckBox / RadioButton / ToolButton / MenuItem.
// Lives here (not in Button) so future widgets can extend InteractiveWidget
// without inheriting Button's text/render payload.
enum class ButtonState {
    Normal,
    Hovered,
    Pressed,
    Disabled,
};

class InteractiveWidget : public Widget {
public:
    InteractiveWidget();
    ~InteractiveWidget() override;

    // State queries
    ButtonState getState() const { return _state; }

    // Enable / disable. Disabling clears hover/press and pins state to Disabled.
    // Re-enabling recomputes Normal/Hovered from _isMouseOver.
    void setEnabled(bool enabled);
    bool isEnabled() const { return _enabled; }

    // Click callback — owned at base so all interactive widgets can fire it.
    // Subclasses that need richer event data (CheckBox toggle, RadioButton
    // group change) override the mouse handlers and ignore this; or keep and
    // forward. Default semantics: fires on mouse-up while still over the widget.
    void setOnClicked(std::function<void()> callback) { _onClicked = std::move(callback); }

    // Default mouse state machine. Subclasses override ONLY if they need
    // different semantics (CheckBox toggles on click, etc.); for Button /
    // ToolButton the base is sufficient.
    bool onMouseMove(const UIMouseEvent& e) override;
    bool onMouseButtonDown(const UIMouseEvent& e) override;
    bool onMouseButtonUp(const UIMouseEvent& e) override;
    void onMouseLeave() override;

    // Default cursor hint — Hand when hovered and enabled, else Default.
    // Window / SplitterHandle override for Move / SizeHorizontal cursors.
    UiCursorHint getCursorHint() const override;

    // Transient hover/press flags. Public so tests (and any external logic
    // that needs to inspect pointer state without subscribing to events) can
    // query them. Subclasses should mutate via the mouse handlers above.
    bool isMouseOver() const { return _isMouseOver; }
    bool isPressed() const { return _isPressed; }

    // ---------------------------------------------------------------------
    // Color transitions (UI animation lane, cut 1). State colors in the
    // fallback render paths normally swap instantly on hover/press; with
    // the color tween on (default 90ms) they interpolate instead. The
    // tween is *render-driven*: subclasses compute the target color in
    // onRender and pass it through resolveTransitionColor(), which starts
    // the tween when the target differs from the current color. That
    // covers every state-change path (mouse handlers, setEnabled) without
    // touching the state machine. setColorTweenMs(0) restores instant
    // swap semantics (byte-identical to pre-animation rendering).
    // ---------------------------------------------------------------------
    void setColor(const math::FVector4& c);   // snap + cancel any tween
    math::FVector4 getColor() const { return _color; }
    void animateColorTo(const math::FVector4& to, float durationMs,
                        AnimationCurve curve = AnimationCurve::EaseOut);
    void setColorTweenMs(float ms) { _colorTweenMs = ms; }
    float getColorTweenMs() const { return _colorTweenMs; }

    // Advances the color tween; widgets whose tick is driven through
    // compoundDescendTick (virtual dispatch) hit this override.
    void tick(float dt) override;

protected:
    // onRender consumption point: returns the color to draw for `target`,
    // starting / retargeting the tween as needed. Never modifies `target`
    // semantics — with the tween disabled it is the identity function.
    math::FVector4 resolveTransitionColor(const math::FVector4& target);

    ButtonState _state = ButtonState::Normal;
    bool _enabled = true;
    bool _isMouseOver = false;
    bool _isPressed = false;
    std::function<void()> _onClicked;

    // UI animation lane: color transition state. _color is the currently
    // drawn color (written by tick); _colorAnim is idle until a transition
    // starts. _colorInitialized guards the first render — the initial
    // {1,1,1,1} is a placeholder, not a color to tween FROM.
    AnimState<math::FVector4> _colorAnim;
    math::FVector4 _color{1.0f, 1.0f, 1.0f, 1.0f};
    float _colorTweenMs = 90.0f;
    bool _colorInitialized = false;
};

} // namespace ayt::ui