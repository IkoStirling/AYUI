#pragma once

// =============================================================================
// C-11 MenuBar: top-level horizontal strip with menu anchors.
// =============================================================================
//
// Architecture (v1):
//   MenuBar (CompoundWidget)
//     └─ _anchors: HBox-style row of "MenuAnchor" buttons (each is a Button
//          with a label + onClick → open its Menu popup)
//     └─ _menus:   Menu* per anchor; one open at a time.
//
// Each anchor is a Button (clickable). The Menu popup is shown when its
// anchor is clicked; clicking another anchor switches menus; clicking
// outside any menu closes the currently-open menu.
//
// MenuBar is the parent of all open menus (popup-as-child, matches
// ComboBox DECISION 2). When MenuBar is destroyed, its open menus are
// freed through the standard destroyWidgetTree path.

#include "AYWidget.h"
#include "AYMenu.h"
#include "AYMenuItem.h"
#include <functional>
#include <string>
#include <vector>

namespace ayt::ui {

class MenuBar : public CompoundWidget {
public:
    static constexpr float kDefaultWidth = 600.0f;
    static constexpr float kDefaultHeight = 26.0f;

    MenuBar();
    ~MenuBar() override;

    // Append a top-level menu with given title. Returns the Menu so the
    // caller can populate items.
    Menu* addMenu(const std::wstring& title);
    size_t getMenuCount() const { return _menus.size(); }
    Menu* getMenu(size_t index) const;
    const std::wstring& getMenuTitle(size_t index) const;

    // Which menu is currently open (-1 = none).
    int getOpenMenuIndex() const { return _openIdx; }
    void closeOpenMenu();

    // Hit-test override: when a menu is open, clicks outside the menu
    // close it. We achieve this by extending hit-test to include the
    // bounds of the open menu while not letting the menu's own bounds
    // block other widgets — but Menu's own hitTest already handles this.
    Widget* hitTest(const math::FVector2& worldPos) override;

    void performLayout() override;
    void onRender(IRenderBackend& renderer) override;

private:
    void layoutAnchors();
    void onAnchorClicked(int index);

    struct MenuEntry {
        std::string title;
        Menu* menu;
        // Lazily-created Button for the anchor (laid out next to it).
        class Button* anchor = nullptr;
    };

    std::vector<MenuEntry> _menus;
    int _openIdx = -1;
};

Widget* createMenuBarWidget();

} // namespace ayt::ui
