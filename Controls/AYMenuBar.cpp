#include "AYUI/MenuBar.h"
#include "AYUI/Button.h"
#include "AYUI/UIManager.h"
#include "AYUI/IRenderBackend.h"
#include "AYMath/MathUtils.h"
#include <algorithm>

namespace ayt::ui {

MenuBar::MenuBar() {
    setSize(math::FVector2(kDefaultWidth, kDefaultHeight));
    // Polish (P3): register with the active UIManager so onKeyDown can
    // dispatch accelerator keys. We tryGet (rather than get) so a
    // MenuBar constructed WITHOUT a UIManager (pure unit-test fixtures)
    // is silently inactive — its findAccel path still works for direct
    // calls, just no global dispatch.
    if (UIManager* ui = UIManager::tryGet()) {
        ui->registerMenuBar(this);
    }
}

MenuBar::~MenuBar() {
    // Polish (P3): deregister from UIManager BEFORE destroying child
    // tree. If we unregistered AFTER, the child's destruction could
    // touch _menuBars (the registry) in some CompoundWidget destruction
    // path and find a dangling pointer to ourselves.
    if (UIManager* ui = UIManager::tryGet()) {
        ui->unregisterMenuBar(this);
    }

    // Open-menu lifetime fix (commit ay-ui.md code-review 2026-08-02 #5):
    // `Menu::open()` reparents the Menu onto the UIManager overlay
    // (NOT under us). When the editor destroys the MenuBar with an open
    // menu still on the overlay, the Menu is no longer in our
    // `_children` list — CompoundWidget's destructor wouldn't free it
    // and it leaks (and any submenu items with it). Worse, MenuBar's
    // findAccel() iterates `_menus[i].menu` and dereferences each —
    // if we cleared `_menus` without freeing the menus, an accelerator
    // hit on the orphan menu reads freed memory. Mirror ~DockArea's
    // snapshot-and-delete pattern.
    //
    // Order matters: close() FIRST (which reparents the menu back onto
    // us via _ownerHost so the standard child-tree destructor can free
    // it normally), THEN let CompoundWidget's destruction handle it
    // through `_children`. If close() fails to reparent (e.g. the
    // manager is already gone), fall back to an explicit delete.
    const std::vector<MenuEntry> snapshot = _menus;
    _menus.clear();
    // AYUI-Audit-2026-08-26 Bug2: UAF in base dtor. Each Menu was added
    // via addChildExternal() (see addMenu), so Menu* lives in this
    // MenuBar's `_children` list. We `delete e.menu` below, but THEN
    // ~Widget runs and walks `_children` writing `child->_parent =
    // nullptr` through the now-freed pointer — classic UAF.
    //
    // The fix: drop the deleted Menu entries from `_children` BEFORE
    // the delete loop. After this clear, ~Widget's child walk skips
    // the freed memory entirely. We use _children.clear() (rather than
    // selective removeChild) because MenuBar owns its menus via the
    // _menus snapshot above — the _children list contains only those
    // external menus, and we're about to delete every one of them.
    {
        const std::vector<Widget*> kidsSnapshot = _children;
        _children.clear();
        // Restore non-menu children so the base dtor can still clear
        // their back-pointers (e.g. anchor Buttons, which are added
        // via addChild owning and CompoundWidget destroys them on
        // delete). We only removed the externally-owned Menu entries.
        for (Widget* kid : kidsSnapshot) {
            if (kid == nullptr) continue;
            bool isMenu = false;
            for (const MenuEntry& e : snapshot) {
                if (e.menu == kid) { isMenu = true; break; }
            }
            if (!isMenu) {
                _children.push_back(kid);
            }
        }
    }
    for (const MenuEntry& e : snapshot) {
        if (e.menu == nullptr) continue;
        // Break the open-menu's _ownerHost back-pointer so its close()
        // / dismissFromManager() doesn't try to re-add itself to us
        // mid-dtor (we're already unwinding).
        e.menu->clearOwnerHost();
        // Never call close() from the MenuBar dtor — close() restores
        // focus via UIManager::setFocus, and during loadLayout/shutdown
        // the saved widget (or RTTI) may already be gone (Reload JSON
        // crash: MenuBar::~MenuBar → Menu::close → setFocus).
        e.menu->detachForHostDestruction();
        if (e.menu->getParent() != nullptr) {
            e.menu->detachFromParent();
        }
        delete e.menu;
    }
}

Menu* MenuBar::addMenu(const std::wstring& title) {
    auto* m = new Menu();
    // External: MenuBar owns via _menus and deletes in ~MenuBar.
    // addChild (owning) would let destroyWidgetTree free the Menu before
    // ~MenuBar runs, leaving _menus dangling (Reload/shutdown UAF).
    addChildExternal(m);
    MenuEntry e;
    // Code-review 2026-08-02 #21: removed the dead `title` field
    // assignment. Anchor button (below) owns the visible title via
    // setText(); MenuEntry only tracks Menu* + Button* anchors.
    e.menu = m;
    _menus.push_back(e);
    // Anchor button lives next to the menu (sibling-style).
    class Button* btn = new Button();
    btn->setText(title);
    // Respect the height assigned by a parent layout. The editor shell puts
    // MenuBar inside a 26px row with 2px vertical padding, so its live height
    // is 22px rather than the standalone 26px default.
    btn->setSize(math::FVector2(
        _anchorWidth, std::max(1.0f, getSize().y)));
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
    // Polish (P3) invariant: accelerator dispatch (and any other "menu
    // should dismiss" call) must close ANY menu currently in the open
    // state, not just the one tracked by _openIdx. Rationale: a host
    // (or a test) can open a Menu directly via `menu->open(&bar, pos)`
    // bypassing the anchor-click path, which leaves _openIdx < 0 even
    // though the menu is visually open. Walking _menus and closing any
    // that reports isOpen()==true is O(M) where M = number of menus
    // (~3-7 in practice) and matches the "any open menu should dismiss"
    // policy without requiring every caller to also update _openIdx.
    for (MenuEntry& e : _menus) {
        if (e.menu != nullptr && e.menu->isOpen()) {
            e.menu->close();
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

// ============================================================================
// Polish (P3) — accelerator lookup.
// ============================================================================
// We do NOT maintain a separate registry map. Instead, findAccel does a
// linear walk through every MenuItem in every Menu this MenuBar owns.
// Justification: a MenuBar typically has 3-7 top-level menus with 5-15
// items each (~50 items max in the worst case — IDE "Window" menu).
// 50 items × 2 int comparisons per onKeyDown call is negligible vs the
// alternative (a reverse-channel signal from MenuItem::setShortcut up
// to MenuBar, requiring MenuItem to track its owning MenuBar — and
// potentially dynamic_cast every setShortcut call). The lazy approach
// also means stale-item problems (item destroyed, registry still
// pointing at freed memory) are impossible: we read _items from the
// still-alive Menu each call.
//
// Collision policy: first match wins. addItem order is host's call
// order so the "first" item with a given (mods, key) is the one the
// host added first — predictable for debugging.
// ============================================================================

MenuItem* MenuBar::findAccel(uint8_t mods, int keyCode) const {
    if (keyCode == 0) return nullptr;
    for (const MenuEntry& e : _menus) {
        if (e.menu == nullptr) continue;
        const size_t n = e.menu->getItemCount();
        for (size_t i = 0; i < n; ++i) {
            MenuItem* it = e.menu->getItem(static_cast<int>(i));
            if (it == nullptr) continue;
            if (it->getAccelKey() != 0 &&
                it->getAccelKey() == keyCode &&
                it->getAccelMods() == mods) {
                return it;
            }
        }
    }
    return nullptr;
}

void MenuBar::onAnchorClicked(int index) {
    if (index < 0 || index >= static_cast<int>(_menus.size())) return;
    // PR-S1c: gate the toggle on the menu actually being open. Clicking
    // outside the menu closes it via UIManager → Menu::close() /
    // dismissFromManager(), which never updates _openIdx — with only the
    // index check, the next anchor click hit this toggle branch and
    // refused to reopen (menu stayed closed until a second click).
    if (_openIdx == index && _menus[index].menu->isOpen()) {
        // Same anchor + open → toggle.
        closeOpenMenu();
        return;
    }
    closeOpenMenu();
    Menu* m = _menus[index].menu;
    if (m == nullptr) return;
    if (auto* btn = _menus[index].anchor) {
        // Bottom-left of the anchor (VS Code / Win32 menu convention).
        // getMax() is bottom-right and made File menus open under the
        // wrong edge of the button.
        const math::FRectangle b = btn->getWorldBounds();
        const math::FVector2 anchorWorld(b.minX, b.maxY);
        m->open(this, anchorWorld);
    } else {
        m->open(this, math::FVector2(0.0f, getSize().y));
    }
    _openIdx = index;
}

void MenuBar::setAnchorSpacing(float spacing) {
    _anchorSpacing = std::max(0.0f, spacing);
    layoutAnchors();
}

void MenuBar::setAnchorWidth(float width) {
    _anchorWidth = std::max(kAnchorMinWidth, width);
    layoutAnchors();
}

void MenuBar::setAnchorAutoWidth(bool enabled) {
    _anchorAutoWidth = enabled;
    layoutAnchors();
}

void MenuBar::layoutAnchors() {
    float x = 0.0f;
    // Parent containers own the cross-axis size. Forcing the standalone
    // default here made the editor's 22px MenuBar overflow its padded 26px
    // chrome row and overlap the DockArea below by 2px.
    const float h = std::max(1.0f, getSize().y);
    bool first = true;
    for (size_t i = 0; i < _menus.size(); ++i) {
        if (_menus[i].anchor == nullptr) continue;
        if (!first) {
            x += _anchorSpacing;
        }
        first = false;
        Button* btn = _menus[i].anchor;
        const float w = _anchorAutoWidth
            ? std::max(kAnchorMinWidth, btn->getPreferredSize().x)
            : _anchorWidth;
        btn->setSize(math::FVector2(w, h));
        btn->setPosition(math::FVector2(x, 0.0f));
        x += w;
    }
    setSize(math::FVector2(std::max(x, 1.0f), h));
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
}

void MenuBar::renderChildren(IRenderBackend& renderer) {
    Widget::renderChildren(renderer);

    // Draw the rule after the anchors so it remains continuous instead of
    // surviving only as small fragments in the gaps between opaque buttons.
    // MenuBar shrinks to the live anchor cluster, so this line naturally ends
    // below Help rather than running to both window edges.
    const math::FRectangle b = getWorldBounds();
    if (b.maxX - b.minX <= 4.0f || b.maxY <= b.minY) return;
    renderer.drawRect(
        math::FRectangle(b.minX + 2.0f, b.maxY - 1.0f,
                         b.maxX - 2.0f, b.maxY),
        math::FVector4(0.38f, 0.40f, 0.45f, 0.82f));
}

Widget* createMenuBarWidget() { return new MenuBar(); }

} // namespace ayt::ui
