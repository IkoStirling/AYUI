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
//
// -----------------------------------------------------------------------------
// Polish (P3) — accelerator registry.
// -----------------------------------------------------------------------------
// Every MenuItem in any owned Menu whose setShortcut parses to a
// (mods, key) tuple is auto-registered into _accelRegistry. UIManager's
// onKeyDown dispatches query this registry first; on hit, the matched
// item's onActivate callback fires and the active menu closes.
// -----------------------------------------------------------------------------

#include "AYWidget.h"
#include "AYMenu.h"
#include "AYMenuItem.h"
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ayt::ui {

class MenuBar : public CompoundWidget {
public:
    static constexpr float kDefaultWidth = 600.0f;
    static constexpr float kDefaultHeight = 26.0f;
    // Default anchor chrome: fixed width (not text-fit) + 1px gap between
    // items. Text-fit is opt-in via setAnchorAutoWidth(true).
    static constexpr float kDefaultAnchorWidth = 80.0f;
    static constexpr float kDefaultAnchorSpacing = 1.0f;
    static constexpr float kAnchorMinWidth = 40.0f;

    MenuBar();
    ~MenuBar() override;

    // Append a top-level menu with given title. Returns the Menu so the
    // caller can populate items.
    Menu* addMenu(const std::wstring& title);
    size_t getMenuCount() const { return _menus.size(); }
    Menu* getMenu(size_t index) const;
    const std::wstring& getMenuTitle(size_t index) const;

    // Anchor layout knobs (MenuBar lays out its own row — not an HBox).
    // Mirrors BoxBase::setSpacing for the bar's top-level buttons.
    void setAnchorSpacing(float spacing);
    float getAnchorSpacing() const { return _anchorSpacing; }

    void setAnchorWidth(float width);
    float getAnchorWidth() const { return _anchorWidth; }

    // When true, each anchor width = max(min, getPreferredSize().x).
    // Default false → fixed getAnchorWidth() for every item.
    void setAnchorAutoWidth(bool enabled);
    bool isAnchorAutoWidth() const { return _anchorAutoWidth; }

    // Which menu is currently open (-1 = none).
    int getOpenMenuIndex() const { return _openIdx; }
    void closeOpenMenu();

    // Polish (P3): look up an accelerator in the registry. If a
    // MenuItem in this MenuBar has a parsed (mods, key), returns it.
    // Called by UIManager::onKeyDown at the top of dispatch — exposed
    // here so the registry stays co-located with its owner and not
    // smeared across UIManager.
    MenuItem* findAccel(uint8_t mods, int keyCode) const;

    // Polish (P3): number of registered accelerators. Public so tests
    // can pin the registry lifecycle (items removed via clearItems or
    // Menu destruction must shrink the count).
    size_t accelCount() const { return _accelRegistry.size(); }

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
        // Code-review 2026-08-02 #21: removed dead `title` field.
        // addMenu() always set it to "" (empty placeholder) and
        // getMenuTitle() reads from `anchor->getText()` instead.
        // Keeping the field around invited type confusion (was
        // std::string while the public getter returned std::wstring).
        Menu* menu;
        // Lazily-created Button for the anchor (laid out next to it).
        class Button* anchor = nullptr;
    };

    std::vector<MenuEntry> _menus;
    int _openIdx = -1;

    float _anchorSpacing = kDefaultAnchorSpacing;
    float _anchorWidth = kDefaultAnchorWidth;
    bool  _anchorAutoWidth = false;

    // Polish (P3): accelerator registry — (mods << 9) | keyCode maps to
    // the MenuItem. 9-bit shift because UIKeyCode values fit in 9 bits
    // (VK_A..VK_Z + extensions up to PageDown = ~92). Mods use 3 bits
    // (bit 0/1/2 Shift/Control/Alt) so the upper bits stay free for
    // collision-avoidance. Collisions on the same (mods, keyCode) keep
    // the FIRST item registered (last-write would be confusing because
    // the order depends on addItem call order, which is implementation-
    // detail from the host's perspective).
    std::unordered_map<int, MenuItem*> _accelRegistry;
};

Widget* createMenuBarWidget();

} // namespace ayt::ui
