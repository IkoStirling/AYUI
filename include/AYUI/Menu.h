#pragma once

// Vertical overlay popup of MenuItems. Opening saves focus, mounts the Menu on
// UIManager's overlay, and participates in the single-active-dropdown rule;
// closing or outside-click dismissal restores focus when the target remains
// valid. Up/Down/Enter/Escape and prefix typeahead are handled by Menu itself.
// Submenus open after the hover delay. Menu owns its items and attached
// submenus; a MenuBar keeps the top-level Menu pointer alive across soft close.

#include "AYUI/CompoundFocusableWidget.h"
#include "AYUI/Widget.h"
#include "AYUI/MenuItem.h"
#include "AYUI/ScrollBar.h"
#include "AYUI/ScrollableWidget.h"
#include "AYUI/TypeaheadBuffer.h"
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

    // PR-C3 — accessor for the keyboard-driven highlight index. Lets
    // tests (and hosts) inspect typeahead state without depending on
    // the Enter activation side effect.
    int  getHoveredIndex() const { return _hoveredIndex; }

    // Submenu arrow on the given item — sets MenuItem::setSubmenu and
    // adds `sub` to our owned-children list so it ships with us.
    void attachSubmenu(MenuItem* item, Menu* sub);

    // Visibility. close() soft-unmounts (MenuBar keeps Menu* alive).
    bool isOpen() const { return _open; }
    void open(Widget* host, const math::FVector2& anchorPos);
    void close();

    // Owner-host break path for dtor order safety. ~MenuBar calls this
    // BEFORE close() so the reparent-on-close logic doesn't try to add
    // us back to a MenuBar that's already unwinding. Idempotent.
    void clearOwnerHost() { _ownerHost = nullptr; }

    // Soft-dismiss used by ~MenuBar / tree teardown. Clears open state and
    // unmounts from the overlay WITHOUT restoring focus via setFocus
    // (the saved focus target may already be destroyed).
    void detachForHostDestruction();

    // Soft-dismissal path used by UIManager when closing one dropdown
    // before opening another (or when the user clicks outside the menu).
    // Same durable-Menu* contract as close(): MenuBar keeps the pointer.
    void dismissFromManager();

    // UI animation lane: called by UIManager when a fade-out close
    // completes. Hides the plate and reparents back under the owning
    // MenuBar (the fade-out kept the menu mounted on the overlay to
    // render; bookkeeping was already done at beginPopupFadeOut time).
    void onPopupFadeOutCompleted();

    // Hit-test override: when open, catches clicks anywhere inside Menu
    // bounds (which may extend past host's bounds).
    Widget* hitTest(const math::FVector2& worldPos) override;

    // onMouseButtonDown grabs focus + onKeyDown handles
    // Up/Down/Enter/Escape. Focus save/restore on open/close is the
    // critical R3 detail — when the menu opens we must remember who
    // had focus and restore it on close so Tab traversal is correct.
    bool onMouseButtonDown(const UIMouseEvent& e) override;
    bool onKeyDown(int keyCode) override;
    bool onMouseWheel(const UIMouseWheelEvent& e) override;

    const math::FVector2& getScrollOffset() const {
        return _scrollState.getScrollOffset();
    }
    void setScrollOffset(const math::FVector2& offset);
    ScrollBar* getVerticalScrollBar() const { return _vbar; }

    // Callbacks. setOnItemActivated fires with the index of whichever
    // item was activated (mouse click on a MenuItem OR keyboard Enter
    // on the highlighted row — both routes go through activateItem()).
    // setOnClose fires when the menu closes for any reason (item pick,
    // Escape, click-outside — the latter is the host's responsibility
    // via DropdownManager).
    //
    // PR-C3 — setOnHoverChanged fires whenever _hoveredIndex changes
    // (mouse hover, keyboard Up/Down, typeahead letter jump). The
    // Gallery uses this to surface typeahead feedback in the status
    // label — without it the user has no visible signal that pressing
    // 'A' actually highlighted "Apple" instead of just sitting there.
    void setOnItemActivated(std::function<void(int)> cb) {
        _onItemActivated = std::move(cb);
    }
    void setOnClose(std::function<void()> cb) {
        _onClose = std::move(cb);
    }
    void setOnHoverChanged(std::function<void(int)> cb) {
        _onHoverChanged = std::move(cb);
    }

    void performLayout() override;
    void onRender(IRenderBackend& renderer) override;
    void renderChildren(IRenderBackend& renderer) override;
    math::FRectangle getClientRect() const override;

    // PR-C3 — first-letter typeahead timer accumulator. Driven by the
    // CompoundFocusableWidget::tick cascade when Menu is in the tree.
    void tick(float dt) override;

private:
    void layoutItems();
    void ensureScrollBar();
    void syncScrollBar();
    void syncItemPositions();
    void scrollItemIntoView(int index);
    bool scrollBy(float deltaY);
    math::FVector2 getScrollViewportSize() const;
    void onItemClickedAny();
    // Invoke the same "row was activated" path that
    // MenuItem's onMouseButtonUp triggers — record index, fire
    // _onItemActivated, close the menu. Lives here so the keyboard
    // Enter path doesn't need to call MenuItem's protected handleClick.
    void activateItem(int index);

    // PR-C3 — set _hoveredIndex and fire _onHoverChanged if it changed.
    // Single source of truth for every path that moves the highlight
    // (Up/Down, typeahead letter, mouse hover).
    void setHoveredIndex(int index);
    // PR-S3 — typeahead-aware variant. Same as setHoveredIndex but also
    // fires _onHoverChanged when the new index equals the current one,
    // so a typeahead letter that resolves to the already-hovered item
    // still surfaces feedback (status label, etc). Internal — callers
    // outside the typeahead path should keep using setHoveredIndex.
    void setHoveredIndexFromTypeahead(int index);

    std::vector<MenuItem*> _items;
    std::vector<Menu*>     _submenus;       // owned sub-menus
    bool _open = false;
    int  _lastActivatedIndex = -1;          // for the close callback

    // Keyboard-driven hover index. Rendered as the
    // "highlighted row" via the same row-flag ListView uses.
    // PR-S3 fix: default is -1 (no item highlighted yet) so first-letter
    // typeahead from a fresh menu includes idx 0 in the search. The
    // previous default of 0 caused 'A' on {Apple, Apricot, ...} to jump
    // straight to Apricot (skipping Apple) and 'R' on {Red, Green, Blue}
    // to appear unresponsive (Red was already at idx 0). Up/Down arrow
    // paths check `<= 0` so both -1 and 0 wrap to the last/first item
    // respectively — no regression in arrow navigation.
    int _hoveredIndex = -1;

    // The widget that had keyboard focus BEFORE the
    // menu opened. Saved in Menu::open() so Menu::close() can restore it
    // — without this, opening a menu leaves focus stuck on the overlay
    // after the menu closes, which breaks Tab-out semantics.
    Widget* _focusedWidgetBefore = nullptr;

    // MenuBar (or other host) that owns this Menu for the session.
    // open() reparents onto the overlay; close() reparents back so the
    // host CompoundWidget still destroys us.
    Widget* _ownerHost = nullptr;

    // PR-TypeaheadBuffer: typeahead state now encapsulated in the shared
// struct (same as ComboBox). Menu retains the single-letter wrap
// (Windows listbox convention) as caller logic since ComboBox does
// not use it.
    TypeaheadBuffer _typeaheadBuffer;

    // Long-menu overflow state. The bar is created lazily and remains a
    // child across opens; Auto visibility reserves no gutter while hidden.
    ScrollBar* _vbar = nullptr;
    ScrollableWidget _scrollState;
    float _contentHeight = 0.0f;

    std::function<void(int)> _onItemActivated;
    std::function<void()>    _onClose;
    std::function<void(int)> _onHoverChanged;   // PR-C3 feedback
};

Widget* createMenuWidget();

} // namespace ayt::ui
