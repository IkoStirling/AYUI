#include "AYUI/Button.h"
#include "AYUI/IRenderBackend.h"
#include "AYUI/Style.h"
#include "AYUI/TextMeasure.h"
#include "AYUI/DockTrace.h"
#include "AYUI/SvgIcon.h"
#include "AYMath/MathUtils.h"

namespace ayt::ui {

Button::Button()
    : _padding(8.0f, 4.0f, 8.0f, 4.0f)
{
    setSize(math::FVector2(100.0f, 32.0f));
}

Button::~Button() {
}

void Button::setPadding(float left, float top, float right, float bottom) {
    _padding = math::FVector4(left, top, right, bottom);
    markDirty();
}

void Button::setIconDocument(std::shared_ptr<const SvgDocument> document) {
    if (_iconDocument == document) return;
    _iconDocument = std::move(document);
    markDirty();
}

void Button::setIconSize(float size) {
    const float clamped = std::max(0.0f, size);
    if (_iconSize == clamped) return;
    _iconSize = clamped;
    markDirty();
}

void Button::setIconGap(float gap) {
    const float clamped = std::max(0.0f, gap);
    if (_iconGap == clamped) return;
    _iconGap = clamped;
    markDirty();
}

void Button::setIconColor(const math::FVector4& color) {
    if (_iconColor == color) return;
    _iconColor = color;
    markDirty();
}

math::FRectangle Button::getTextBounds() const {
    math::FRectangle bounds = getWorldBounds();
    const float iconAdvance = _iconDocument != nullptr && !_text.empty()
        ? _iconSize + _iconGap : 0.0f;
    return math::FRectangle(
        bounds.minX + _padding.x + iconAdvance,
        bounds.minY + _padding.y,
        bounds.maxX - _padding.z,
        bounds.maxY - _padding.w
    );
}

// Phase D (D4) — preferred-size heuristic for TabStrip-driven layout.
// PR-B2: now resolves the actual glyph run width via
// ayt::ui::measurePrefixWidth (PR-A1 header). The backend path (bgfx Font)
// produces a true em-tracked width; without a backend the helper falls
// back to a 7px-per-ASCII / fontSize-per-wide per-char estimate that
// matches the pre-PR behaviour. Height defaults to kMinButtonHeight if
// the widget hasn't been sized yet, otherwise reports the current height
// so vertical-grow containers don't keep extending it.
math::FVector2 Button::getPreferredSize() const {
    constexpr float kMinButtonHeight = 24.0f;
    const float textW = measurePrefixWidth(_text, _text.size());
    const float preferredH = std::max(kMinButtonHeight, getHeight());
    const float iconW = _iconDocument != nullptr
        ? _iconSize + (_text.empty() ? 0.0f : _iconGap) : 0.0f;
    return math::FVector2(textW + iconW + _padding.x + _padding.z, preferredH);
}

// R-5: style resolution is centralized in resolveStyle() (see AYStyle.h).
// When hasStyle is true the resolved colors drive the draw; otherwise the
// hardcoded fallback fills (state-aware) take over. This matches the
// button_render_preserves_hover_fill contract: tests without a wired
// StyleSheet keep the original visuals.
void Button::onRender(IRenderBackend& renderer) {
    math::FRectangle bounds = getWorldBounds();

    const ResolvedStyle style = resolveStyle(
        getStyleId(), this,
        interactionStyleState(isEnabled(), isMouseOver(), isPressed()));

    if (style.hasStyle) {
        // B1: rounded fill matches the rounded border — a plain drawRect
        // left background-colored corners poking out inside the SDF ring.
        math::FVector4 background = style.backgroundColor;
        if (style.backgroundTransition.enabled) {
            background = resolveTransitionColor(
                background, style.backgroundTransition.durationMs,
                style.backgroundTransition.curve);
        }
        renderer.drawRoundedRect(bounds, background, style.cornerRadius);
        renderer.drawBorderRect(bounds,
                                math::FVector4(0.12f, 0.12f, 0.12f, 1.0f),
                                style.borderWidth, style.cornerRadius);
    } else {
        // Hardcoded fallback (R-1 + pre-R-1 behavior). Hover is a restrained
        // grey lift; hosts that want a louder demo (Gallery anim_btn*) call
        // setFallbackHoverColor().
        math::FVector4 bg(0.28f, 0.28f, 0.30f, 1.0f);
        switch (getState()) {
        case ButtonState::Hovered:
            bg = _hasFallbackHover
                     ? _fallbackHover
                     : math::FVector4(0.36f, 0.38f, 0.42f, 1.0f);
            break;
        case ButtonState::Pressed:
            bg = resolveAccentColor();
            break;
        case ButtonState::Disabled:
            bg = math::FVector4(0.20f, 0.20f, 0.20f, 1.0f);
            break;
        default:
            break;
        }
        // UI animation lane: hover/press transitions tween the fill instead
        // of swapping instantly (90ms default; setColorTweenMs(0) restores
        // the instant swap). The styled path stays untouched — styles don't
        // know state, so the target is constant and the transition is a
        // no-op there.
        bg = resolveTransitionColor(bg);
        if (ayuiTraceInputEnabled()) {
            dockTrace("[BtnTrace] id=%s state=%d fill=(%.3f,%.3f,%.3f,%.3f)\n",
                      getId().c_str(), static_cast<int>(getState()),
                      static_cast<double>(bg.x), static_cast<double>(bg.y),
                      static_cast<double>(bg.z), static_cast<double>(bg.w));
        }
        // B1: 2px rounded fill under the 2px rounded border (fallback).
        renderer.drawRoundedRect(bounds, bg, 2.0f);
        renderer.drawBorderRect(bounds, math::FVector4(0.12f, 0.12f, 0.12f, 1.0f), 1.0f, 2.0f);
    }

    if (_iconDocument != nullptr && _iconSize > 0.0f) {
        const math::FRectangle content(
            bounds.minX + _padding.x, bounds.minY + _padding.y,
            bounds.maxX - _padding.z, bounds.maxY - _padding.w);
        const float availableW = std::max(0.0f, content.maxX - content.minX);
        const float availableH = std::max(0.0f, content.maxY - content.minY);
        const float size = std::min({_iconSize, availableW, availableH});
        if (size > 0.0f) {
            const float x = _text.empty()
                ? content.minX + (availableW - size) * 0.5f : content.minX;
            const float y = content.minY + (availableH - size) * 0.5f;
            math::FVector4 color = isEnabled()
                ? _iconColor
                : math::FVector4(_iconColor.x, _iconColor.y, _iconColor.z,
                                 _iconColor.w * 0.55f);
            _iconDocument->draw(renderer,
                math::FRectangle(x, y, x + size, y + size), color);
        }
    }

    if (!_text.empty()) {
        math::FVector4 textColor = isEnabled() ? math::FVector4(1.0f, 1.0f, 1.0f, 1.0f)
                                                : math::FVector4(0.55f, 0.55f, 0.55f, 1.0f);
        renderer.drawText(getTextBounds(), _text, 14, textColor);
    }
}

} // namespace ayt::ui
