#include "AYCheckBox.h"
#include "IAYRenderBackend.h"
#include "AYStyle.h"
#include "AYMath/MathUtils.h"
#include <algorithm>

namespace ayt::ui {

CheckBox::CheckBox() {
    setSize(math::FVector2(140.0f, 24.0f));
    // Leaf widget — InteractiveWidget -> Widget -> no-op performLayout.
    // No children, so R-6 hitTest default (self-only) is correct.
}

CheckBox::~CheckBox() = default;

void CheckBox::setChecked(bool checked) {
    if (_checked == checked) {
        return;
    }
    _checked = checked;
    if (_onToggled) {
        _onToggled(_checked);
    }
}

math::FRectangle CheckBox::getBoxRect() const {
    math::FRectangle bounds = getWorldBounds();
    const float height = bounds.maxY - bounds.minY;
    const float y = bounds.minY + (height - kBoxSize) * 0.5f;
    return math::FRectangle(
        bounds.minX + kBoxPadding,
        y,
        bounds.minX + kBoxPadding + kBoxSize,
        y + kBoxSize);
}

bool CheckBox::onMouseButtonUp(const UIMouseEvent& e) {
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
        _checked = !_checked;
        _state = ButtonState::Hovered;
        if (_onToggled) {
            _onToggled(_checked);
        }
        if (_onClicked) {
            _onClicked();
        }
        return true;
    }
    _state = ButtonState::Normal;
    return false;
}

void CheckBox::onRender(IRenderBackend& renderer) {
    math::FRectangle bounds = getWorldBounds();
    if (bounds.maxX <= bounds.minX || bounds.maxY <= bounds.minY) {
        return;
    }

    math::FRectangle box = getBoxRect();

    // Box background — follows resolveStyle() pattern (R-5). When style is
    // wired AND its bg is not the makeDefault sentinel, use it; otherwise
    // a state-aware hardcoded fallback wins.
    const ResolvedStyle style = resolveStyle(getStyleId());
    math::FVector4 boxBg;
    math::FVector4 boxBorderColor;
    float boxBorderWidth;
    if (style.hasStyle) {
        boxBg = style.backgroundColor;
        boxBorderColor = style.borderColor;
        boxBorderWidth = style.borderWidth;
    } else {
        boxBg = math::FVector4(0.22f, 0.22f, 0.24f, 1.0f);
        if (isPressed() && isMouseOver()) {
            boxBg = math::FVector4(0.34f, 0.34f, 0.36f, 1.0f);
        } else if (isMouseOver() && isEnabled()) {
            boxBg = math::FVector4(0.28f, 0.28f, 0.30f, 1.0f);
        } else if (!isEnabled()) {
            boxBg = math::FVector4(0.18f, 0.18f, 0.18f, 1.0f);
        }
        // UI animation lane: hover/press box fill transitions (90ms
        // default). The checked accent below is NOT tweened — it is a
        // selection state, not a hover response.
        boxBg = resolveTransitionColor(boxBg);
        boxBorderColor = math::FVector4(0.5f, 0.5f, 0.55f, 1.0f);
        boxBorderWidth = 1.0f;
    }
    // B3: rounded fill matches the 2px rounded border.
    renderer.drawRoundedRect(box, boxBg, 2.0f);
    renderer.drawBorderRect(box, boxBorderColor, boxBorderWidth, 2.0f);

    // Checkmark accent fill (inset 3px on each side). When a style
    // is wired AND its bg color is not the makeDefault sentinel, the
    // accent reuses the style's backgroundColor (the theme's "filled"
    // state). Otherwise the v1 fallback blue applies — same as Button
    // Pressed's accent for visual coherence.
    if (_checked) {
        math::FVector4 accent(0.18f, 0.45f, 0.78f, 1.0f);
        if (style.hasStyle) {
            // Use a brightened variant of the style bg so the accent
            // reads as "selected" against the box bg. Multiply each
            // channel by 1.6 (clamped to 1.0). Keeps the design intent
            // (theme drives color) without inventing a new AccentColor
            // field in WidgetStyle.
            accent.x = std::min(1.0f, style.backgroundColor.x * 1.6f);
            accent.y = std::min(1.0f, style.backgroundColor.y * 1.6f);
            accent.z = std::min(1.0f, style.backgroundColor.z * 1.6f);
            accent.w = style.backgroundColor.w;
        }
        const float ix = box.minX + 3.0f;
        const float iy = box.minY + 3.0f;
        renderer.drawRect(
            math::FRectangle(ix, iy, box.maxX - 3.0f, box.maxY - 3.0f),
            accent);
    }

    // Label
    if (!_text.empty()) {
        const float textX = box.maxX + kBoxToLabelGap;
        math::FRectangle labelBounds(
            textX, bounds.minY,
            bounds.maxX, bounds.maxY);
        const math::FVector4 textColor = isEnabled()
            ? math::FVector4(1.0f, 1.0f, 1.0f, 1.0f)
            : math::FVector4(0.55f, 0.55f, 0.55f, 1.0f);
        renderer.drawText(labelBounds, _text, 14, textColor);
    }
}

Widget* createCheckBoxWidget() {
    return new CheckBox();
}

} // namespace ayt::ui
