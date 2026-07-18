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
// DECISION 1: Menu is a non-FocusableWidget CompoundWidget. Up/Down/Home/
//   End keyboard nav is v1.1. Same precedent as ComboBox DECISION 3.
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

#include "AYWidget.h"
#include "AYMenuItem.h"
#include <functional>
#include <string>
#include <vector>

namespace ayt::ui {

class Menu : public CompoundWidget {
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

    // Hit-test override: when open, catches clicks anywhere inside Menu
    // bounds (which may extend past host's bounds).
    Widget* hitTest(const math::FVector2& worldPos) override;

    void performLayout() override;
    void onRender(IRenderBackend& renderer) override;

private:
    void layoutItems();
    void onItemClickedAny();

    std::vector<MenuItem*> _items;
    std::vector<Menu*>     _submenus;       // owned sub-menus
    bool _open = false;
    int  _lastActivatedIndex = -1;          // for the close callback

    std::function<void(int)> _onItemActivated;
    std::function<void()>    _onClose;
};

Widget* createMenuWidget();

} // namespace ayt::ui
