#pragma once

#include "AYInteractiveWidget.h"

namespace ayt::ui {

// Button is now a thin text+padding wrapper on top of InteractiveWidget's
// state machine. R-1 refactor (2026-07-17) lifted ButtonState + mouse handlers
// + cursor hint + enable/click into the new base.
class Button : public InteractiveWidget {
public:
    Button();
    virtual ~Button();

    const std::wstring& getText() const { return _text; }
    void setText(const std::wstring& text) { _text = text; }

    void setPadding(float left, float top, float right, float bottom);

    // Phase D (D4) — preferred-size heuristic used by TabStrip when laying
    // out tab buttons. Each char ~8px + horizontal padding on each side.
    // This is intentionally rough; v1.1 swaps in a real text shaper. The
    // height is the larger of the current widget height and kMinButtonHeight.
    math::FVector2 getPreferredSize() const;

protected:
    void onRender(IRenderBackend& renderer) override;
    math::FRectangle getTextBounds() const;

    std::wstring _text;
    math::FVector4 _padding{8.0f, 4.0f, 8.0f, 4.0f};
};

} // namespace ayt::ui