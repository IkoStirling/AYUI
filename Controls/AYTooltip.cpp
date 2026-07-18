#include "AYTooltip.h"
#include "AYIRenderBackend.h"
#include <algorithm>

namespace ayt::ui {

Tooltip::Tooltip() {
    setSize(math::FVector2(0.0f, 0.0f));   // recomputed on layout
    setVisible(false);
    ensureLabelCreated();
}

// DECISION 3: tooltip does not steal mouse input. Override hitTest so
// pointer events pass through to the underlying target.
Widget* Tooltip::hitTest(const math::FVector2& /*worldPos*/) {
    return nullptr;
}

Tooltip::~Tooltip() {
    // CompoundWidget's destructor handles child cleanup.
}

Tooltip* Tooltip::attachTo(Widget* target) {
    if (target == nullptr) return nullptr;
    Tooltip* tip = new Tooltip();
    // Use the owning addChild path so when target is destroyed via
    // destroyWidgetTree, the tooltip is freed too (no manual cleanup).
    target->addChild(tip);
    tip->_target = target;
    tip->setVisible(false);
    return tip;
}

void Tooltip::ensureLabelCreated() {
    if (_label != nullptr) return;
    _label = new TextLabel();
    addChild(_label);   // owning
}

void Tooltip::setText(const std::wstring& text) {
    ensureLabelCreated();
    _label->setText(text);
}

const std::wstring& Tooltip::getText() const {
    static const std::wstring kEmpty;
    if (_label == nullptr) return kEmpty;
    return _label->getText();
}

void Tooltip::tick(float dt, const math::FVector2& mousePos,
                   const math::FVector2& viewportSize) {
    _viewportSize = viewportSize;
    if (_target == nullptr) return;
    const math::FRectangle tBounds = _target->getWorldBounds();

    const bool inside = tBounds.contains(mousePos);
    if (inside) {
        if (!_hovering) {
            _hovering = true;
            _hoverTime = 0.0f;
        }
        _hoverTime += dt;
        if (!_visible && _hoverTime >= _hoverDelay) {
            show();
        }
        if (_visible) {
            // Keep position synced in case the target moves or the
            // viewport changed while hovering.
            syncPosition();
        }
    } else {
        if (_hovering) {
            _hovering = false;
            _hoverTime = 0.0f;
            hide();
        }
    }
}

void Tooltip::show() {
    _visible = true;
    setVisible(true);
    syncPosition();
}

void Tooltip::hide() {
    _visible = false;
    setVisible(false);
}

void Tooltip::syncPosition() {
    if (_target == nullptr) return;
    const math::FRectangle tBounds = _target->getWorldBounds();
    // Force a layout pass to compute label size.
    performLayout();
    const math::FVector2 tipSize = getSize();
    // Default: below target.
    math::FVector2 pos(tBounds.minX, tBounds.maxY + kTipOffsetY);
    // Flip above if it would overflow the viewport bottom.
    if (_viewportSize.y > 0.0f &&
        pos.y + tipSize.y > _viewportSize.y) {
        pos.y = tBounds.minY - tipSize.y - kTipOffsetY;
        if (pos.y < 0.0f) pos.y = 0.0f;
    }
    setPosition(pos);
}

void Tooltip::performLayout() {
    CompoundWidget::performLayout();
    if (_label == nullptr) return;
    const math::FVector2 labelPad(kDefaultPadding * 2.0f, kDefaultPadding * 2.0f);
    // The label sets its own size based on text content; we just wrap it
    // with padding so the tooltip rectangle is comfortably larger than
    // the label's drawn rect.
    const math::FVector2 labelSize = _label->getSize();
    setSize(math::FVector2(labelSize.x + labelPad.x,
                            labelSize.y + labelPad.y));
    _label->setPosition(math::FVector2(kDefaultPadding, kDefaultPadding));
}

void Tooltip::onRender(IRenderBackend& renderer) {
    const math::FRectangle b = getWorldBounds();
    if (b.maxX <= b.minX || b.maxY <= b.minY) return;
    // Background plate with slight alpha.
    renderer.drawRect(b, math::FVector4(0.10f, 0.10f, 0.13f, 0.92f));
    // Subtle border in lighter shade.
    renderer.drawRect(
        math::FRectangle(b.minX, b.minY, b.maxX, b.minY + 1.0f),
        math::FVector4(0.45f, 0.45f, 0.50f, 1.0f));
    renderer.drawRect(
        math::FRectangle(b.minX, b.maxY - 1.0f, b.maxX, b.maxY),
        math::FVector4(0.45f, 0.45f, 0.50f, 1.0f));
}

Widget* createTooltipWidget() { return new Tooltip(); }

} // namespace ayt::ui
