#pragma once

#include "AYInteractiveWidget.h"
#include <functional>

namespace ayt::ui {

// C-2b: RadioButton is the mutually-exclusive sibling of CheckBox. The two
// share most of the architecture:
//   - extends InteractiveWidget (Hovered/Pressed/Disabled state, Hand cursor)
//   - leaf widget (self-only hitTest, no children, R-6 invariant)
//   - onMouseButtonUp is overridden so toggling happens BEFORE _onClicked
//     fires (callbacks reading widget state see the post-toggle value)
//   - resolveStyle() pattern for visual theming
//
// Where they diverge:
//   - Visual: a small circle (drawn as a rounded rect via drawRect + the
//     accent dot inside when selected) instead of a square
//   - Mutex: RadioButton carries a `_groupId` (int, default 0). Equality of
//     group ids defines the exclusive group; v1 deliberately does NOT
//     introduce a RadioGroup class — the host wires a lambda on each
//     button's setOnToggled/setOnClicked to deselect other buttons with
//     the same group id when one is selected. setChecked() remains public
//     so the host's arbitration lambda can flip the rest, but typical
//     user code should drive selection through clicking, not setChecked().
//   - Callback: in addition to _onClicked (Button-style) and _onToggled
//     (CheckBox-style), setOnSelected fires only when this button
//     transitions from unchecked to checked (i.e. just-selected events).
class RadioButton : public InteractiveWidget {
public:
    static constexpr float kCircleSize = 16.0f;
    static constexpr float kCirclePadding = 4.0f;
    static constexpr float kCircleToLabelGap = 6.0f;
    static constexpr int kDefaultGroupId = 0;

    RadioButton();
    ~RadioButton() override;

    // Group identity. Two RadioButtons with the same groupId are mutually
    // exclusive (the host enforces this via the arbitration lambda).
    int  getGroupId() const { return _groupId; }
    void setGroupId(int id) { _groupId = id; }

    bool isChecked() const { return _checked; }
    void setChecked(bool checked);

    const std::wstring& getText() const { return _text; }
    void setText(const std::wstring& text) { _text = text; }

    // Fires with the NEW value (true = selected, false = deselected).
    void setOnToggled(std::function<void(bool)> cb) { _onToggled = std::move(cb); }

    // Fires only on the uncheck -> check transition. Useful for
    // "selection-changed" observers that don't care about deselects.
    void setOnSelected(std::function<void()> cb) { _onSelected = std::move(cb); }

    math::FRectangle getCircleRect() const;

    bool onMouseButtonUp(const UIMouseEvent& e) override;

protected:
    void onRender(IRenderBackend& renderer) override;

    int _groupId = kDefaultGroupId;
    std::wstring _text;
    bool _checked = false;
    std::function<void(bool)> _onToggled;
    std::function<void()> _onSelected;
};

Widget* createRadioButtonWidget();

} // namespace ayt::ui
