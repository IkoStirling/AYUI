#pragma once

// =============================================================================
// C-11 MenuItem: a single line inside a Menu.
// =============================================================================
//
// Architecture (v1):
//   MenuItem : SelectableWidget
//     - _text: std::wstring                // display label (left-justified)
//     - _shortcut: std::wstring            // right-justified hint (e.g. "Ctrl+S")
//     - _hasSubmenu: bool                  // shows a "›" chevron on the right
//     - _submenu: Menu*                    // optional owned sub-popup
//     - onActivated fires the click callback
//
// Click semantics: unlike ListView::Row, MenuItem does NOT toggle
// selection. Hovering highlights the row; click fires _onActivate (one
// shot, then the menu typically closes). v1.1: keep-open semantics
// (checkbox menu items) via _keepMenuOpenOnActivate flag.

#include "AYSelectableWidget.h"
#include <functional>
#include <string>

namespace ayt::ui {

class Menu;   // forward — used for submenu pointer.

class MenuItem : public SelectableWidget {
public:
    static constexpr float kDefaultWidth = 200.0f;
    static constexpr float kDefaultHeight = 24.0f;

    MenuItem();
    ~MenuItem() override;

    const std::wstring& getText() const { return _text; }
    void setText(const std::wstring& text) { _text = text; markBoundsDirty(); }

    const std::wstring& getShortcut() const { return _shortcut; }
    void setShortcut(const std::wstring& s) { _shortcut = s; markBoundsDirty(); }

    bool hasSubmenu() const { return _submenu != nullptr; }
    void setSubmenu(Menu* m) { _submenu = m; markBoundsDirty(); }

    // Activate callback. Fires on click after the menu typically closes.
    void setOnActivate(std::function<void()> cb) { _onActivate = std::move(cb); }

    // Override click handling — MENUS DON'T TOGGLE SELECTION. They just
    // fire the activate callback and let the menu close.
    bool handleClick() override;
    bool onMouseButtonUp(const UIMouseEvent& e) override;

    void onRender(IRenderBackend& renderer) override;

private:
    std::wstring _text;
    std::wstring _shortcut;
    Menu*        _submenu = nullptr;
    std::function<void()> _onActivate;
};

Widget* createMenuItemWidget();

} // namespace ayt::ui
