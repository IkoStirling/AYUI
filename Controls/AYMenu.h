#pragma once

// =============================================================================
// C-11 Menu: a vertical popup of MenuItems.
// =============================================================================
//
// Architecture (v1):
//   Menu (CompoundWidget)
//     └─ _items: MenuItem* x N
//     └─ _hitTest override extends past Menu bounds so Menu can stay
//        visible even when its position extends past its parent bounds.
//
// Lifecycle: Menu is owned and reparented similar to ComboBox's popup.
// When shown, Menu adds itself as a child of the MenuBar's host window
// root (call via open(menuHost, anchorWidget)). When closed, it removes
// itself. The Menu remains alive while MenuBar keeps a pointer to it.
//
// -----------------------------------------------------------------------------
// v1 design decisions + known limitations + v1.1 upgrade paths
// -----------------------------------------------------------------------------
//
// DECISION 1 (Phase B — B3, S3): Menu extends CompoundFocusableWidget.
//   Up/Down/Enter/Escape are owned by Menu (NOT delegated to items).
//   First-letter jump is out of scope (deferred to v2).
//
//   On open: save UIManager::get().getFocusedWidget() to
//   _focusedWidgetBefore, then call UIManager::get().setFocus(this) so
//   keys route to the menu. On close: restore focus to
//   _focusedWidgetBefore and null the slot.
//
//   Up/Down mutate _hoveredIndex (keyboard-driven highlight, rendered as
//   the selected row). Enter invokes MenuItem::activate on the hovered
//   item — that triggers the existing item callback (which fires
//   _onItemActivated + closes the menu). Escape closes without
//   committing.
//
// DECISION 2: only one Menu is open at a time inside a MenuBar. Opening
//   a top-level menu closes any currently open sibling.
//
// DECISION 3: click-outside dismisses. Menu's hitTest override only
//   catches clicks on its own bounds; the host (MenuBar) reactively
//   closes open menus on mouse-down outside Menu bounds.
//
// DECISION 4: sub-menus nest on hover with a small delay (300 ms). The
//   sub-menu pointer on MenuItem is non-owning; parent Menu destroys
//   sub-menus in its destructor.
//
// DECISION 5: items can be added at any time. addItem rebuilds positions.

#include "AYCompoundFocusableWidget.h"
#include "AYWidget.h"
#include "AYMenuItem.h"
#include <functional>
#include <string>
#include <vector>

namespace ayt::ui {

class Menu : public CompoundFocusableWidget {
public:
    static constexpr float kDefaultWidth = 220.0f;
    static constexpr float kDefaultHeight = 32.0f;        // per-item height
    static constexpr float kDefaultPad = 4.0f;

    Menu();
    ~Menu() override;

    // Items.
    MenuItem* addItem(const std::wstring& text);
    MenuItem* addItem(const std::wstring& text,
                      const std::wstring& shortcut);
    MenuItem* addSeparator();
    size_t getItemCount() const { return _items.size(); }
    MenuItem* getItem(size_t index) const;
    void clearItems();

    // Submenu arrow on the given item — sets MenuItem::setSubmenu and
    // adds `sub` to our owned-children list so it ships with us.
    void attachSubmenu(MenuItem* item, Menu* sub);

    // Visibility.
    bool isOpen() const { return _open; }
    void open(Widget* host, const math::FVector2& anchorPos);
    void close();

    // Soft-dismissal path used by UIManager when closing one dropdown
    // before opening another (or when the user clicks outside the menu
    // area but still inside the host bounds). Unlike close() this
    // leaves the Menu* durable so MenuBar can re-open it; it just
    // detaches from _activeDropdown + restores prior focus.
    void dismissFromManager();

    // Hit-test override: when open, catches clicks anywhere inside Menu
    // bounds (which may extend past host's bounds).
    Widget* hitTest(const math::FVector2& worldPos) override;

    // Phase B (B3): onMouseButtonDown grabs focus + onKeyDown handles
    // Up/Down/Enter/Escape. Focus save/restore on open/close is the
    // critical R3 detail — when the menu opens we must remember who
    // had focus and restore it on close so Tab traversal is correct.
    bool onMouseButtonDown(const UIMouseEvent& e) override;
    bool onKeyDown(int keyCode) override;

    // Callbacks. setOnItemActivated fires with the index of whichever
    // item was activated (mouse click on a MenuItem OR keyboard Enter
    // on the highlighted row — both routes go through activateItem()).
    // setOnClose fires when the menu closes for any reason (item pick,
    // Escape, click-outside — the latter is the host's responsibility
    // via DropdownManager).
    void setOnItemActivated(std::function<void(int)> cb) {
        _onItemActivated = std::move(cb);
    }
    void setOnClose(std::function<void()> cb) {
        _onClose = std::move(cb);
    }

    void performLayout() override;
    void onRender(IRenderBackend& renderer) override;

private:
    void layoutItems();
    void onItemClickedAny();
    // Phase B (B3): invoke the same "row was activated" path that
    // MenuItem's onMouseButtonUp triggers — record index, fire
    // _onItemActivated, close the menu. Lives here so the keyboard
    // Enter path doesn't need to call MenuItem's protected handleClick.
    void activateItem(int index);

    std::vector<MenuItem*> _items;
    std::vector<Menu*>     _submenus;       // owned sub-menus
    bool _open = false;
    int  _lastActivatedIndex = -1;          // for the close callback

    // Phase B (B3): keyboard-driven hover index. Rendered as the
    // "highlighted row" via the same row-flag ListView uses. Starts at 0
    // so Up arrow wraps to the last item on first press.
    int _hoveredIndex = 0;

    // Phase B (B3) R3: the widget that had keyboard focus BEFORE the
    // menu opened. Saved in Menu::open() so Menu::close() can restore it
    // — without this, opening a menu leaves focus stuck on the overlay
    // after the menu closes, which breaks Tab-out semantics.
    Widget* _focusedWidgetBefore = nullptr;

    std::function<void(int)> _onItemActivated;
    std::function<void()>    _onClose;
};

Widget* createMenuWidget();

} // namespace ayt::ui
