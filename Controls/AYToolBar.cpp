#include "AYToolBar.h"
#include "AYButton.h"
#include "AYSeparator.h"
#include "AYIRenderBackend.h"
#include <algorithm>

namespace ayt::ui {

ToolBar::ToolBar() {
    setSize(math::FVector2(kDefaultWidth, kDefaultHeight));
}

ToolBar::~ToolBar() {
    _items.clear();
}

class Button* ToolBar::addButton(const std::wstring& text,
                                  std::function<void()> onClick) {
    auto* btn = new Button();
    btn->setText(text);
    btn->setSize(math::FVector2(80.0f, kDefaultHeight - 2.0f * kPadding));
    btn->setLayoutPositionManaged(false);
    btn->setLayoutSizeManaged(false);
    addChild(btn);
    if (onClick) {
        btn->setOnClicked(onClick);
    }
    _items.push_back({ItemRecord::Kind::Button, btn});
    performLayout();
    return btn;
}

void ToolBar::addSeparator() {
    auto* sep = new Separator();
    sep->setOrientation(Separator::Orientation::Vertical);
    sep->setSize(math::FVector2(1.0f, kDefaultHeight - 2.0f * kPadding));
    sep->setLayoutPositionManaged(false);
    sep->setLayoutSizeManaged(false);
    addChild(sep);
    _items.push_back({ItemRecord::Kind::Separator, sep});
    performLayout();
}

Widget* ToolBar::getItem(size_t index) const {
    if (index >= _items.size()) return nullptr;
    return _items[index].widget;
}

void ToolBar::clearItems() {
    for (auto& it : _items) {
        if (it.widget != nullptr) {
            removeChild(it.widget);
            delete it.widget;
        }
    }
    _items.clear();
}

void ToolBar::performLayout() {
    CompoundWidget::performLayout();
    float x = kPadding;
    const float y = kPadding;
    const float h = kDefaultHeight - 2.0f * kPadding;
    for (size_t i = 0; i < _items.size(); ++i) {
        Widget* w = _items[i].widget;
        if (w == nullptr) continue;
        const float ww = w->getSize().x;
        if (_items[i].kind == ItemRecord::Kind::Separator) {
            // Separators are slim + tall.
            w->setPosition(math::FVector2(x + 2.0f, y));
            w->setSize(math::FVector2(2.0f, h));
            x += 4.0f + kItemSpacing;
        } else {
            w->setPosition(math::FVector2(x, y));
            w->setSize(math::FVector2(ww, h));
            x += ww + kItemSpacing;
        }
    }
    setSize(math::FVector2(x + kPadding, kDefaultHeight));
}

void ToolBar::onRender(IRenderBackend& renderer) {
    const math::FRectangle b = getWorldBounds();
    if (b.maxX <= b.minX || b.maxY <= b.minY) return;
    // Subtle tool-strip background.
    renderer.drawRect(b, math::FVector4(0.20f, 0.22f, 0.27f, 1.0f));
    // Bottom rule.
    renderer.drawRect(
        math::FRectangle(b.minX, b.maxY - 1.0f, b.maxX, b.maxY),
        math::FVector4(0.36f, 0.36f, 0.40f, 1.0f));
}

Widget* createToolBarWidget() { return new ToolBar(); }

} // namespace ayt::ui
