#pragma once

#include "AYUI/InteractiveWidget.h"

#include <memory>

namespace ayt::ui {

class SvgDocument;

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

    // Optional vector icon slot. The document is immutable/shareable, so a
    // toolbar can load one SVG once and bind it to multiple buttons without
    // texture ownership or backend handles leaking into AYUI.
    void setIconDocument(std::shared_ptr<const SvgDocument> document);
    const std::shared_ptr<const SvgDocument>& getIconDocument() const {
        return _iconDocument;
    }
    void setIconSize(float size);
    float getIconSize() const { return _iconSize; }
    void setIconGap(float gap);
    float getIconGap() const { return _iconGap; }
    void setIconColor(const math::FVector4& color);
    const math::FVector4& getIconColor() const { return _iconColor; }

    // Optional override for the unstyled (fallback) Hovered fill. Default
    // palette stays a restrained grey lift; Gallery's Animation demo pins
    // accent-blue on anim_btn* only. No-op on the styled draw path.
    void setFallbackHoverColor(const math::FVector4& c) {
        _fallbackHover = c;
        _hasFallbackHover = true;
        markDirty();
    }
    void clearFallbackHoverColor() { _hasFallbackHover = false; markDirty(); }

    // Preferred-size estimate used by TabStrip layout. This query has no
    // renderer parameter, so width uses a deterministic character estimate;
    // final text drawing and hit geometry still use backend shaping.
    math::FVector2 getPreferredSize() const;

protected:
    void onRender(IRenderBackend& renderer) override;
    math::FRectangle getTextBounds() const;

    std::wstring _text;
    math::FVector4 _padding{8.0f, 4.0f, 8.0f, 4.0f};
    std::shared_ptr<const SvgDocument> _iconDocument;
    float _iconSize = 16.0f;
    float _iconGap = 5.0f;
    math::FVector4 _iconColor{0.92f, 0.92f, 0.94f, 1.0f};
    bool _hasFallbackHover = false;
    math::FVector4 _fallbackHover{0.0f, 0.0f, 0.0f, 1.0f};
};

} // namespace ayt::ui
