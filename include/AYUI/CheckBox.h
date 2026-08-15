#pragma once

#include "AYUI/InteractiveWidget.h"
#include <functional>

namespace ayt::ui {

// C-2: CheckBox is the first widget after R-1 to compose InteractiveWidget's
// state machine with new toggling semantics. It reuses:
//   - the state machine (Normal / Hovered / Pressed / Disabled)
//   - cursor hint (Hand when hovered + enabled)
//   - setEnabled + getState (clears hover / press on disable — fixes B2 path)
//   - the captured-mouse contract from UIManager (CheckBox is a hit target,
//     so UIManager routes mouse-down to it via pickWidgetAt, captures it,
//     and routes mouse-up)
//
// Visual: small 16x16 box on the leading edge + label text on the right.
// When checked, the box interior is filled with the accent color.
//
// CheckBox DOES NOT extend Button — the rendering model is a checkbox, not a
// button. It overrides onMouseButtonUp so the click toggles _checked BEFORE
// the inherited _onClicked fires; this means observers that read the widget
// state from inside their _onClicked callback always see the post-toggle
// value. A richer setOnToggled(bool) callback also fires for observers that
// just want the new boolean.
class CheckBox : public InteractiveWidget {
public:
    static constexpr float kBoxSize = 16.0f;
    static constexpr float kBoxPadding = 4.0f;
    static constexpr float kBoxToLabelGap = 6.0f;

    CheckBox();
    ~CheckBox() override;

    bool isChecked() const { return _checked; }
    void setChecked(bool checked);

    const std::wstring& getText() const { return _text; }
    void setText(const std::wstring& text) { _text = text; }

    // Toggle-specific callback: fires with the NEW value.
    // Distinct from _onClicked (which still fires as well — matches Button's
    // contract so existing callback-style consumers don't need to learn a
    // new API).
    void setOnToggled(std::function<void(bool)> cb) { _onToggled = std::move(cb); }

    // Layout-shape computation exposed for tests + render.
    math::FRectangle getBoxRect() const;

    bool onMouseButtonUp(const UIMouseEvent& e) override;

protected:
    void onRender(IRenderBackend& renderer) override;

    std::wstring _text;
    bool _checked = false;
    std::function<void(bool)> _onToggled;
};

Widget* createCheckBoxWidget();

} // namespace ayt::ui
