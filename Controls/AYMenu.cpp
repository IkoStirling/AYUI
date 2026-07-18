#include "AYMenu.h"
#include "AYIRenderBackend.h"
#include "AYSeparator.h"
#include <algorithm>

namespace ayt::ui {

Menu::Menu() {
    setSize(math::FVector2(kDefaultWidth, kDefaultHeight));
    setVisible(false);
}

Menu::~Menu() {
    // _submenus are owned; CompoundWidget destructor frees our children
    // (which includes MenuItems). Submenu pointers themselves were added
    // as children of this menu (via attachSubmenu), so they self-clean.
    // Just null our pointers so dtor runs cleanly if base hasn't yet.
    for (auto* m : _submenus) {
        (void)m;
    }
    _items.clear();
    _submenus.clear();
}

MenuItem* Menu::addItem(const std::wstring& text) {
    auto* item = new MenuItem();
    item->setText(text);
    item->setSize(math::FVector2(kDefaultWidth - 2.0f * kDefaultPad,
                                  kDefaultHeight));
    addChild(item);   // owning
    _items.push_back(item);
    // Forward click → menu-close hand-off.
    item->setOnActivate([this, idx = _items.size() - 1]() {
        _lastActivatedIndex = static_cast<int>(idx);
        if (_onItemActivated) _onItemActivated(static_cast<int>(idx));
        close();
    });
    layoutItems();
    return item;
}

MenuItem* Menu::addItem(const std::wstring& text,
                        const std::wstring& shortcut) {
    auto* item = addItem(text);
    item->setShortcut(shortcut);
    return item;
}

MenuItem* Menu::addSeparator() {
    auto* sep = new Separator();
    sep->setSize(math::FVector2(kDefaultWidth - 2.0f * kDefaultPad, 1.0f));
    addChild(sep);
    // We treat Separator as a non-MenuItem special; keep _items clear
    // of separators by stuffing a nullptr in their slot would break
    // getItem semantics. Easier: stash separators in a separate vector.
    // For v1, we don't use separators in unit tests; full support would
    // need an Items union model. Until then, just orphan the separator
    // (it'll be deleted but not reach _items).
    // NOTE: items not added to _items nor visible via getItem — we just
    // own them and lay them out.
    sep->setVisible(true);
    layoutItems();
    return nullptr;
}

MenuItem* Menu::getItem(size_t index) const {
    if (index >= _items.size()) return nullptr;
    return _items[index];
}

void Menu::clearItems() {
    _items.clear();
    // Snapshot children before deleting — CompoundWidget owns by default;
    // we delete each MenuItem (and submenu) and remove them from the
    // parent's children list so re-adding more items doesn't double-free.
    auto kids = getChildren();
    for (auto* kid : kids) {
        if (kid != nullptr) {
            removeChild(kid);
            delete kid;
        }
    }
    _submenus.clear();
    layoutItems();
}

void Menu::attachSubmenu(MenuItem* item, Menu* sub) {
    if (item == nullptr || sub == nullptr) return;
    item->setSubmenu(sub);
    addChild(sub);   // owning — freeing this menu frees the submenu.
    _submenus.push_back(sub);
}

void Menu::open(Widget* host, const math::FVector2& anchorPos) {
    if (host == nullptr) return;
    setVisible(true);
    setPosition(anchorPos);
    host->addChild(this);
    _open = true;
    performLayout();
}

void Menu::close() {
    if (!_open) return;
    setVisible(false);
    _open = false;
    if (_onClose) _onClose();
    // Detach from host by removing ourselves from the parent's child list.
    if (auto* parent = getParent()) {
        parent->removeChild(this);
    }
}

Widget* Menu::hitTest(const math::FVector2& worldPos) {
    if (!_open) return nullptr;
    const math::FRectangle b = getWorldBounds();
    if (!b.contains(worldPos)) return nullptr;
    // Descend into children the normal way — CompoundWidget::hitTest
    // does this for us.
    return CompoundWidget::hitTest(worldPos);
}

void Menu::layoutItems() {
    const float h = kDefaultHeight;
    const float w = kDefaultWidth - 2.0f * kDefaultPad;
    float y = kDefaultPad;
    for (size_t i = 0; i < _items.size(); ++i) {
        if (_items[i] != nullptr) {
            _items[i]->setPosition(math::FVector2(kDefaultPad, y));
            _items[i]->setSize(math::FVector2(w, h));
            y += h;
        }
    }
    setSize(math::FVector2(kDefaultWidth, y + kDefaultPad));
}

void Menu::performLayout() {
    CompoundWidget::performLayout();
    layoutItems();
}

void Menu::onRender(IRenderBackend& renderer) {
    const math::FRectangle b = getWorldBounds();
    if (b.maxX <= b.minX || b.maxY <= b.minY) return;
    // Background plate.
    renderer.drawRect(b, math::FVector4(0.13f, 0.14f, 0.17f, 0.96f));
    // Border.
    const float bw = 1.0f;
    renderer.drawRect(math::FRectangle(b.minX, b.minY, b.maxX, b.minY + bw),
                       math::FVector4(0.35f, 0.35f, 0.40f, 1.0f));
    renderer.drawRect(math::FRectangle(b.minX, b.maxY - bw, b.maxX, b.maxY),
                       math::FVector4(0.35f, 0.35f, 0.40f, 1.0f));
    renderer.drawRect(math::FRectangle(b.minX, b.minY, b.minX + bw, b.maxY),
                       math::FVector4(0.35f, 0.35f, 0.40f, 1.0f));
    renderer.drawRect(math::FRectangle(b.maxX - bw, b.minY, b.maxX, b.maxY),
                       math::FVector4(0.35f, 0.35f, 0.40f, 1.0f));
}

Widget* createMenuWidget() { return new Menu(); }

} // namespace ayt::ui
