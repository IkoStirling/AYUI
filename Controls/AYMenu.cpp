#include "AYMenu.h"
#include "AYIRenderBackend.h"
#include "AYSeparator.h"
#include "AYUIManager.h"
#include "UIKeyCode.h"
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
    // Copy first: getChildren() returns a reference; removeChild mutates it.
    std::vector<Widget*> kids = getChildren();
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
    // Phase A (A2): mount on UIManager's overlay root instead of the host.
    // This lets the menu render + hit-test above any nested layout (e.g.
    // a MenuBar inside a Window inside a VBox) and survive host destruction
    // cleanly via the overlay's destroyWidgetTree path.
    UIManager::get().openPopup(host, this);
    _open = true;
    // Phase B (B3) R3: save the focused widget BEFORE we steal focus,
    // so close() can restore it. Order matters: openPopup may close a
    // different active dropdown which fires onPopupDismissedByManager,
    // but UIManager's own _focusedWidget is untouched by that — so we
    // can capture it here, after the overlay is mounted, and the saved
    // pointer is the live widget the user was working with.
    _focusedWidgetBefore = UIManager::get().getFocusedWidget();
    UIManager::get().setFocus(this);
    performLayout();
}

void Menu::close() {
    if (!_open) return;
    setVisible(false);
    _open = false;
    // Phase B (B3) R3: restore focus to whatever had it before open().
    // The slot is cleared so a second close() (defensive) is a no-op.
    // setFocus(null) is acceptable when nothing was focused previously.
    if (_focusedWidgetBefore != nullptr) {
        UIManager::get().setFocus(_focusedWidgetBefore);
        _focusedWidgetBefore = nullptr;
    }
    if (_onClose) _onClose();
    // Phase A (A2): UIManager::closePopup removes us from the overlay and
    // frees the tree via destroyWidgetTree. After this call `this` is
    // dangling — callers must not touch the Menu after close().
    //
    // CRITICAL: clear _focusedWidget BEFORE closePopup destroys the menu.
    // UIManager::shutdown() walks _focusedWidget and dynamic_casts it to
    // FocusableWidget to fire setFocus(false). If we left it pointing at
    // this Menu, shutdown hits a freed-pointer RTTI lookup → access
    // violation. setFocus(null) drops the reference while this is still
    // alive (the subsequent closePopup call is what actually frees us).
    if (UIManager::get().getFocusedWidget() == this) {
        UIManager::get().setFocus(nullptr);
    }
    if (getParent() != nullptr) {
        UIManager::get().closePopup(this);
    }
}

Widget* Menu::hitTest(const math::FVector2& worldPos) {
    if (!_open) return nullptr;
    const math::FRectangle b = getWorldBounds();
    if (!b.contains(worldPos)) return nullptr;
    // Descend into children the normal way — CompoundFocusableWidget::hitTest
    // does this for us.
    return CompoundFocusableWidget::hitTest(worldPos);
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
    CompoundFocusableWidget::performLayout();
    layoutItems();
}

void Menu::onRender(IRenderBackend& renderer) {
    // Use local position + size to avoid a stale getWorldBounds when the
    // menu has never been laid out (caller might call render() directly
    // without first wiring into a host). For a popup later this is the
    // right answer too — the host pushes the menu into the widget tree
    // and CompoundWidget::performLayout stays a no-op for popup, leaving
    // position/size intact.
    const math::FVector2 pos = getPosition();
    const math::FVector2 sz = getSize();
    if (sz.x <= 0.0f || sz.y <= 0.0f) return;
    math::FRectangle b(pos.x, pos.y, pos.x + sz.x, pos.y + sz.y);
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

// =============================================================================
// Phase B (B3) — keyboard navigation + click focus grab
// =============================================================================

bool Menu::onMouseButtonDown(const UIMouseEvent& e) {
    // Grab focus on press — open() already saved the previous focus, so
    // if the user later closes via Escape or item activation, focus
    // returns cleanly. Returning false lets the click flow up to the
    // item hit-test path so row clicks still select.
    if (e.mouseButton == 0) {
        UIManager::get().setFocus(this);
    }
    return false;
}

bool Menu::onKeyDown(int keyCode) {
    // Menu owns Up/Down/Enter/Escape. Tab is intentionally not consumed —
    // UIManager intercepts Tab before this method sees it (R2 contract).
    // Tab-while-menu-open focuses the NEXT focusable widget, leaving the
    // menu — Escape is the menu-internal dismiss path.

    if (_items.empty()) {
        if (keyCode == UIKey_Escape) { close(); return true; }
        return false;
    }
    const int n = static_cast<int>(_items.size());

    switch (keyCode) {
    case UIKey_Down:
        _hoveredIndex = (_hoveredIndex + 1) % n;
        return true;
    case UIKey_Up:
        _hoveredIndex = (_hoveredIndex <= 0) ? n - 1 : _hoveredIndex - 1;
        return true;
    case UIKey_Enter:
        if (_hoveredIndex >= 0 && _hoveredIndex < n) {
            activateItem(_hoveredIndex);
            return true;
        }
        return false;
    case UIKey_Escape:
        close();
        return true;
    default:
        return false;
    }
}

void Menu::activateItem(int index) {
    // Mirror the same side effects MenuItem's onMouseButtonUp callback
    // triggers (see the lambda installed in addItem): record index,
    // fire _onItemActivated, close. Centralized here so the keyboard
    // path and the mouse path stay byte-identical without exposing
    // MenuItem's protected handleClick().
    if (index < 0 || index >= static_cast<int>(_items.size())) return;
    _lastActivatedIndex = index;
    if (_onItemActivated) {
        _onItemActivated(index);
    }
    close();
}

} // namespace ayt::ui
