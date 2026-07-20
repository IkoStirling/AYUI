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
//
// -----------------------------------------------------------------------------
// Polish (P3) — Menu shortcut accelerator dispatch.
// -----------------------------------------------------------------------------
// v1 stored `_shortcut` purely as a display string. P3 parses the string at
// setShortcut time into (mods, keyCode) and exposes it via
// getAccelMods / getAccelKey (keyCode is UIKeyCode value, 0 = no parse).
// UIManager::onKeyDown consults MenuBar's registry of (mods, key) →
// MenuItem* at the top of dispatch: if the pressed (mods, key) matches
// a registered item, fire its _onActivate callback and close any open
// menu, then return true so the key does NOT continue down to the
// focused widget (avoids weird over-trigger).
//
// This keeps the host API zero-config: callers already do
//     item->setShortcut(L"Ctrl+S");
// — no separate `bindAccelerator` call required. The string is the
// source of truth, parse is automatic.
//
// Modifiers: Ctrl / Shift / Alt (case-insensitive in the string). `+` is
// the separator. Examples: "Ctrl+S", "Ctrl+Shift+Z", "Alt+F4".
// Unparseable strings (typos, F1, ⌘) are silently unparseable
// (keyCode=0) — still display correctly, just not dispatched.
// =============================================================================

#include "AYSelectableWidget.h"
#include <cstdint>
#include <functional>
#include <string>

namespace ayt::ui {

class Menu;   // forward — used for submenu pointer.

class MenuItem : public SelectableWidget {
public:
    static constexpr float kDefaultWidth = 200.0f;
    static constexpr float kDefaultHeight = 24.0f;

    // Polish (P3): accelerator modifier flags. Matches the bit layout
    // UIManager::getModifiers() uses internally (bit 0 Shift, bit 1
    // Control, bit 2 Alt). Keeping identical layout means the dispatch
    // path can compare bitmask values directly.
    static constexpr uint8_t kAccelShift   = 0x01u;
    static constexpr uint8_t kAccelControl = 0x02u;
    static constexpr uint8_t kAccelAlt     = 0x04u;

    MenuItem();
    ~MenuItem() override;

    const std::wstring& getText() const { return _text; }
    void setText(const std::wstring& text) { _text = text; markBoundsDirty(); }

    const std::wstring& getShortcut() const { return _shortcut; }
    // Polish (P3): setShortcut parses the string into (mods, keyCode).
    // Re-callable: passing a different string updates the dispatch
    // binding too.
    void setShortcut(const std::wstring& s);

    // Polish (P3): parsed accelerator. If the displayed string is not
    // parseable (typo, unsupported F1 etc.) _accelKey remains 0 and
    // nothing is dispatched on this item — but the string still shows.
    uint8_t getAccelMods() const { return _accelMods; }
    int     getAccelKey()  const { return _accelKey;  }
    bool    hasAccel()     const { return _accelKey != 0; }

    // Polish (P3): parse a shortcut string into (mods, keyCode). Pure
    // function — exposed so tests + hosts can pre-validate or build
    // ad-hoc mappings without going through setShortcut.
    // Returns true if at least one key token was recognized; mods is
    // populated even if the key was unparseable (the modifier prefix
    // might still be useful).
    static bool parseShortcut(const std::wstring& s,
                              uint8_t& outMods,
                              int& outKey);

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

    // Polish (P3): cached parse result for _shortcut. _accelKey = 0
    // means unparseable — setShortcut failed to recognize a key token.
    uint8_t _accelMods = 0;
    int     _accelKey  = 0;
};

Widget* createMenuItemWidget();

} // namespace ayt::ui
