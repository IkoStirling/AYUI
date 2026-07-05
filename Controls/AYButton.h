#pragma once

#include "AYWidget.h"

namespace ayt::ui {

enum class ButtonState {
    Normal,
    Hovered,
    Pressed,
    Disabled
};

class Button : public CompoundWidget {
public:
    Button();
    virtual ~Button();

    const std::wstring& getText() const { return _text; }
    void setText(const std::wstring& text) { _text = text; }

    ButtonState getState() const { return _state; }
    void setState(ButtonState state) { _state = state; }

    void setEnabled(bool enabled) { _enabled = enabled; }
    bool isEnabled() const { return _enabled; }

    void setOnClicked(std::function<void()> callback) { _onClicked = callback; }

    bool onMouseMove(const UIMouseEvent& e) override;
    bool onMouseButtonDown(const UIMouseEvent& e) override;
    bool onMouseButtonUp(const UIMouseEvent& e) override;
    void onMouseLeave() override;

    UiCursorHint getCursorHint() const override;

    void setPadding(float left, float top, float right, float bottom);

protected:
    std::wstring _text;
    ButtonState _state;
    bool _enabled;
    std::function<void()> _onClicked;

    math::FVector4 _padding;
    bool _isMouseOver;
    bool _isPressed;

    math::FRectangle getTextBounds() const;

    void onRender(IRenderBackend& renderer) override;
};

} // namespace ayt::ui