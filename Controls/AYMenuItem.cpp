#include "AYMenuItem.h"
#include "AYIRenderBackend.h"
#include "AYMenu.h"

namespace ayt::ui {

MenuItem::MenuItem() {
    setSize(math::FVector2(kDefaultWidth, kDefaultHeight));
    setLayoutPositionManaged(false);   // Menu positions items itself
    setLayoutSizeManaged(false);
}

MenuItem::~MenuItem() = default;

bool MenuItem::handleClick() {
    // Fire the activate callback. We do NOT toggle selection — menus
    // are not persistent single-select; the row visually highlights while
    // hovered and disappears when the menu closes.
    if (_onActivate) _onActivate();
    return true;
}

bool MenuItem::onMouseButtonUp(const UIMouseEvent& e) {
    if (!isEnabled() || e.mouseButton != 0) return false;
    if (!getWorldBounds().contains(e.mousePos)) return false;
    return handleClick();
}

void MenuItem::onRender(IRenderBackend& renderer) {
    const math::FRectangle b = getWorldBounds();
    if (b.maxX <= b.minX || b.maxY <= b.minY) return;

    // Hover / press highlight.
    if (isMouseOver() && isEnabled()) {
        renderer.drawRect(b, math::FVector4(0.18f, 0.45f, 0.78f, 0.55f));
    }

    const float padL = 16.0f;
    const float padR = 12.0f;
    const math::FVector4 textColor = isEnabled()
        ? math::FVector4(1.0f, 1.0f, 1.0f, 1.0f)
        : math::FVector4(0.55f, 0.55f, 0.55f, 1.0f);

    if (!_text.empty()) {
        math::FRectangle textBounds(
            b.minX + padL, b.minY + 4.0f,
            b.maxX - padR, b.maxY - 4.0f);
        renderer.drawText(textBounds, _text, 14, textColor);
    }
    if (!_shortcut.empty()) {
        math::FRectangle scBounds(
            b.minX, b.minY + 4.0f,
            b.maxX - padR, b.maxY - 4.0f);
        const math::FVector4 scColor = isEnabled()
            ? math::FVector4(0.70f, 0.70f, 0.74f, 1.0f)
            : math::FVector4(0.40f, 0.40f, 0.42f, 1.0f);
        // Approximate right-align by drawing the text near the right edge.
        const float approxW = static_cast<float>(_shortcut.size()) * 7.0f;
        scBounds.minX = b.maxX - approxW - padR;
        scBounds.maxX = b.maxX - padR;
        renderer.drawText(scBounds, _shortcut, 13, scColor);
    }
    if (_submenu != nullptr) {
        // Submenu chevron.
        renderer.drawText(
            math::FRectangle(b.maxX - 14.0f, b.minY + 4.0f,
                              b.maxX - 4.0f,  b.maxY - 4.0f),
            L"›",     // ›
            14, textColor);
    }
}

Widget* createMenuItemWidget() { return new MenuItem(); }

} // namespace ayt::ui
