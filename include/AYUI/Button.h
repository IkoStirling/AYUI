#pragma once

#include "AYUI/InteractiveWidget.h"

namespace ayt::ui {

// Button is now a thin text+padding wrapper on top of InteractiveWidget's
// state machine. R-1 refactor (2026-07-17) lifted ButtonState + mouse handlers
// + cursor hint + enable/click into the new base.
class Button : public InteractiveWidget {
public:
    Button();
    virtual ~Button();

    const std::wstring& getText() const { return _text; }
    // AYUI-DirtyRect-2026-08-26: setText swaps the rendered glyph run.
    // No early-out — the next render() clears the dirty flag.
    void setText(const std::wstring& text) {
        _text = text;
        markDirty();
    }

    void setPadding(float left, float top, float right, float bottom);

    // Optional override for the unstyled (fallback) Hovered fill. Default
    // palette stays a restrained grey lift; Gallery's Animation demo pins
    // accent-blue on anim_btn* only. No-op on the styled draw path.
    void setFallbackHoverColor(const math::FVector4& c) {
        _fallbackHover = c;
        _hasFallbackHover = true;
    }
    void clearFallbackHoverColor() { _hasFallbackHover = false; }

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
    bool _hasFallbackHover = false;
    math::FVector4 _fallbackHover{0.0f, 0.0f, 0.0f, 1.0f};
};

} // namespace ayt::ui