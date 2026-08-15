#include "AYUI/RadioButton.h"
#include "AYUI/IRenderBackend.h"
#include "AYUI/Style.h"
#include "AYMath/MathUtils.h"
#include <algorithm>

namespace ayt::ui {

RadioButton::RadioButton() {
    setSize(math::FVector2(140.0f, 24.0f));
    // Leaf widget — InteractiveWidget -> Widget -> no-op performLayout.
    // R-6: hitTest inherited default (self-only), correct for leaf.
}

RadioButton::~RadioButton() = default;

void RadioButton::setChecked(bool checked) {
    if (_checked == checked) {
        return;
    }
    const bool becameSelected = checked && !_checked;
    _checked = checked;
    if (becameSelected && _onSelected) {
        _onSelected();
    }
    if (_onToggled) {
        _onToggled(_checked);
    }
}

math::FRectangle RadioButton::getCircleRect() const {
    math::FRectangle bounds = getWorldBounds();
    const float height = bounds.maxY - bounds.minY;
    const float y = bounds.minY + (height - kCircleSize) * 0.5f;
    return math::FRectangle(
        bounds.minX + kCirclePadding,
        y,
        bounds.minX + kCirclePadding + kCircleSize,
        y + kCircleSize);
}

bool RadioButton::onMouseButtonUp(const UIMouseEvent& e) {
    if (!_enabled) {
        return false;
    }
    if (e.mouseButton != 0) {
        return false;
    }
    if (!_isPressed) {
        return false;
    }

    _isPressed = false;
    const bool stillOver = getWorldBounds().contains(e.mousePos);
    if (stillOver) {
        // Radio buttons are idempotent under click — clicking an already-
        // selected RadioButton keeps it selected and does NOT fire
        // _onSelected / _onToggled again. Only an uncheck -> check
        // transition is observable through the host's mutex lambda.
        const bool becameSelected = !_checked;
        _checked = true;
        _state = ButtonState::Hovered;
        if (becameSelected && _onSelected) {
            _onSelected();
        }
        if (becameSelected && _onToggled) {
            _onToggled(true);
        }
        if (_onClicked) {
            _onClicked();
        }
        return true;
    }
    _state = ButtonState::Normal;
    return false;
}

void RadioButton::onRender(IRenderBackend& renderer) {
    math::FRectangle bounds = getWorldBounds();
    if (bounds.maxX <= bounds.minX || bounds.maxY <= bounds.minY) {
        return;
    }

    math::FRectangle circle = getCircleRect();

    // Outer circle — uses resolveStyle() pattern. Without a wired
    // stylesheet, falls back to state-aware dark grey with a slightly
    // lighter border (the "outline" of the radio button).
    const ResolvedStyle style = resolveStyle(getStyleId());
    math::FVector4 outerBg;
    math::FVector4 outerBorderColor;
    float outerBorderWidth;
    if (style.hasStyle) {
        outerBg = style.backgroundColor;
        outerBorderColor = style.borderColor;
        outerBorderWidth = style.borderWidth;
    } else {
        outerBg = math::FVector4(0.22f, 0.22f, 0.24f, 1.0f);
        if (isPressed() && isMouseOver()) {
            outerBg = math::FVector4(0.34f, 0.34f, 0.36f, 1.0f);
        } else if (isMouseOver() && isEnabled()) {
            outerBg = math::FVector4(0.28f, 0.28f, 0.30f, 1.0f);
        } else if (!isEnabled()) {
            outerBg = math::FVector4(0.18f, 0.18f, 0.18f, 1.0f);
        }
        // UI animation lane: hover/press circle fill transitions (90ms
        // default). The selected dot below is NOT tweened — selection is
        // not a hover response.
        outerBg = resolveTransitionColor(outerBg);
        outerBorderColor = math::FVector4(0.5f, 0.5f, 0.55f, 1.0f);
        outerBorderWidth = 1.0f;
    }
    // Circle approximation — draw as a rounded square (cornerRadius =
    // half width) using drawBorderRect. The MockRenderer fans this out
    // into N rect fills; for visual purposes the round-enough shape is
    // what the user sees. Tests pin the draw-call count and accent color.
    // B3: the fill must be rounded to the same radius, or its square
    // corners poke out beyond the circle ring.
    renderer.drawRoundedRect(circle, outerBg, kCircleSize * 0.5f);
    renderer.drawBorderRect(circle, outerBorderColor, outerBorderWidth,
                            kCircleSize * 0.5f);

    // Selected dot — inset 4px on each side, accent fill. Same pattern
    // as CheckBox's check accent: when a style is wired AND its bg is
    // not the makeDefault sentinel, the dot reuses a brightened variant
    // of the style's backgroundColor. Otherwise the v1 fallback blue
    // matches Button's Pressed fill for visual coherence.
    if (_checked) {
        math::FVector4 accent(0.18f, 0.45f, 0.78f, 1.0f);
        if (style.hasStyle) {
            accent.x = std::min(1.0f, style.backgroundColor.x * 1.6f);
            accent.y = std::min(1.0f, style.backgroundColor.y * 1.6f);
            accent.z = std::min(1.0f, style.backgroundColor.z * 1.6f);
            accent.w = style.backgroundColor.w;
        }
        const float inset = 4.0f;
        renderer.drawRect(
            math::FRectangle(circle.minX + inset,
                             circle.minY + inset,
                             circle.maxX - inset,
                             circle.maxY - inset),
            accent);
    }

    // Label
    if (!_text.empty()) {
        const float textX = circle.maxX + kCircleToLabelGap;
        math::FRectangle labelBounds(
            textX, bounds.minY,
            bounds.maxX, bounds.maxY);
        const math::FVector4 textColor = isEnabled()
            ? math::FVector4(1.0f, 1.0f, 1.0f, 1.0f)
            : math::FVector4(0.55f, 0.55f, 0.55f, 1.0f);
        renderer.drawText(labelBounds, _text, 14, textColor);
    }
}

Widget* createRadioButtonWidget() {
    return new RadioButton();
}

} // namespace ayt::ui
