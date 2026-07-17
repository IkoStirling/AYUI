#pragma once

#include "AYWidget.h"
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

protected:
    ButtonState _state = ButtonState::Normal;
    bool _enabled = true;
    bool _isMouseOver = false;
    bool _isPressed = false;
    std::function<void()> _onClicked;
};

} // namespace ayt::ui