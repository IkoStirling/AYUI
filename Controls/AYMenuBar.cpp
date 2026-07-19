#include "AYMenuBar.h"
#include "AYButton.h"
#include "IAYRenderBackend.h"
#include "aymath/MathUtils.h"
#include <algorithm>

namespace ayt::ui {

MenuBar::MenuBar() {
    setSize(math::FVector2(kDefaultWidth, kDefaultHeight));
}

MenuBar::~MenuBar() {
    // Children include anchor buttons + open menus. CompoundWidget
    // destructor frees them all.
    _menus.clear();
}

Menu* MenuBar::addMenu(const std::wstring& title) {
    auto* m = new Menu();
    addChild(m);   // owning — MenuBar owns the menu
    MenuEntry e;
    e.title = "";   // UTF-8 left as-is for menu titles; Button's setText takes wstring
    e.menu = m;
    _menus.push_back(e);
    // Anchor button lives next to the menu (sibling-style).
    class Button* btn = new Button();
    btn->setText(title);
    btn->setSize(math::FVector2(80.0f, kDefaultHeight));
    btn->setLayoutPositionManaged(false);
    btn->setLayoutSizeManaged(false);
    addChild(btn);
    _menus.back().anchor = btn;
    // Wire click → open menu.
    const size_t idx = _menus.size() - 1;
    btn->setOnClicked([this, idx]() {
        onAnchorClicked(static_cast<int>(idx));
    });
    layoutAnchors();
    return m;
}

Menu* MenuBar::getMenu(size_t index) const {
    if (index >= _menus.size()) return nullptr;
    return _menus[index].menu;
}

const std::wstring& MenuBar::getMenuTitle(size_t index) const {
    static const std::wstring kEmpty;
    if (index >= _menus.size()) return kEmpty;
    return _menus[index].anchor ? _menus[index].anchor->getText() : kEmpty;
}

void MenuBar::closeOpenMenu() {
    if (_openIdx < 0) return;
    if (_openIdx < static_cast<int>(_menus.size())) {
        if (_menus[_openIdx].menu && _menus[_openIdx].menu->isOpen()) {
            _menus[_openIdx].menu->close();
        }
    }
    _openIdx = -1;
}

Widget* MenuBar::hitTest(const math::FVector2& worldPos) {
    // Always defer to CompoundWidget's default hit-test for our bounds +
    // anchors. When a menu is open, the menu itself is also a sibling
    // child and CompoundWidget walks children in order — back-to-front.
    return CompoundWidget::hitTest(worldPos);
}

void MenuBar::onAnchorClicked(int index) {
    if (index < 0 || index >= static_cast<int>(_menus.size())) return;
    if (_openIdx == index) {
        // Same anchor → toggle.
        closeOpenMenu();
        return;
    }
    closeOpenMenu();
    Menu* m = _menus[index].menu;
    if (m == nullptr) return;
    if (auto* btn = _menus[index].anchor) {
        const math::FVector2 anchorWorld = btn->getWorldBounds().getMax();
        m->open(this, anchorWorld);
    } else {
        m->open(this, math::FVector2(0.0f, kDefaultHeight));
    }
    _openIdx = index;
}

void MenuBar::layoutAnchors() {
    float x = 0.0f;
    const float h = kDefaultHeight;
    for (size_t i = 0; i < _menus.size(); ++i) {
        if (_menus[i].anchor == nullptr) continue;
        _menus[i].anchor->setPosition(math::FVector2(x, 0.0f));
        const float w = _menus[i].anchor->getSize().x;
        x += w;
    }
    setSize(math::FVector2(x, h));
}

void MenuBar::performLayout() {
    CompoundWidget::performLayout();
    layoutAnchors();
}

void MenuBar::onRender(IRenderBackend& renderer) {
    const math::FRectangle b = getWorldBounds();
    if (b.maxX <= b.minX || b.maxY <= b.minY) return;
    // Dark bar background.
    renderer.drawRect(b, math::FVector4(0.12f, 0.13f, 0.16f, 1.0f));
    // Bottom border.
    renderer.drawRect(
        math::FRectangle(b.minX, b.maxY - 1.0f, b.maxX, b.maxY),
        math::FVector4(0.30f, 0.30f, 0.34f, 1.0f));
}

Widget* createMenuBarWidget() { return new MenuBar(); }

} // namespace ayt::ui
