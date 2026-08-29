#pragma once

#include "AYUI/CompoundFocusableWidget.h"
#include "AYUI/Widget.h"
#include "AYUI/ListView.h"
#include "AYUI/TextLabel.h"
#include "AYUI/TypeaheadBuffer.h"
#include <functional>
#include <string>
#include <vector>

namespace ayt::ui {

// Single-selection popup picker. The display label is persistent and the
// ListView popup is allocated lazily. While open, UIManager mounts the popup
// on its overlay and enforces the single-active-dropdown invariant; popup
// placement flips/clamps against the viewport through PopupAnchor.
//
// ComboBox owns selection and keyboard/typeahead behavior. Up/Down navigate,
// Enter commits, Escape cancels, and printable keys search by prefix. The
// transient popup is not part of serialized state. Editable-combo mode, item
// icon columns and user-driven popup animation policy are not exposed yet.

class ComboBox : public CompoundFocusableWidget {
public:
    static constexpr float kDefaultWidth = 160.0f;
    static constexpr float kDefaultHeight = 28.0f;
    static constexpr int   kDefaultMaxPopupItems = 8;
    static constexpr float kPopupGap = 2.0f;
    static constexpr float kArrowWidth = 18.0f;
    static constexpr float kTextPadX = 8.0f;

    ComboBox();
    ~ComboBox() override;

    // Enable / disable click + popup. ComboBox is a CompoundWidget (not
    // InteractiveWidget) because it hosts the popup ListView as a child —
    // so we maintain a simple enabled flag here rather than inheriting
    // the whole InteractiveWidget state machine.
    void setEnabled(bool enabled);
    bool isEnabled() const { return _enabled; }

    // Data — wraps the inner ListView (popup) + a parallel vector kept
    // here so getSelectedItem works even when the popup is closed.
    void setItems(const std::vector<std::wstring>& items);
    void addItem(const std::wstring& item);
    void clearItems();
    size_t getItemCount() const { return _items.size(); }
    const std::wstring& getItem(size_t index) const;
    const std::vector<std::wstring>& getItemsRef() const { return _items; }

    int  getSelectedIndex() const { return _selectedIndex; }
    void setSelectedIndex(int index);
    const std::wstring& getSelectedItem() const;

    // Popup control.
    // Popup control. isPopupOpen() reports the LATCH state AND verifies
    // that the popup is still mounted — `_popupOpen` is set by openPopup
    // and cleared by closePopup. DropdownManager may close our popup out
    // from under us when another popup opens (single-active-popup
    // invariant). After that we want isPopupOpen() to also return false.
    // The `_popup->getParent() != nullptr` check detects that external-
    // close case: if the popup's parent was cleared by destroyWidgetTree,
    // the popup is no longer in the tree, so we report closed.
    bool isPopupOpen() const {
        if (!_popupOpen) return false;
        if (_popup == nullptr) return false;
        return _popup->getParent() != nullptr;
    }
    void openPopup();
    void closePopup();
    void togglePopup() { if (_popupOpen) closePopup(); else openPopup(); }

    // PR-B3 — exposes the popup ListView (may be nullptr before first
    // openPopup). Hosts and tests use this to query the popup's
    // scrollOffset / firstVisibleIndex without going through the
    // overlay tree directly. Returns the managed pointer; lifetime
    // is owned by ComboBox.
    ListView* getPopup() const { return _popup; }

    // Layout knobs.
    void setMaxPopupItems(int n) { _maxPopupItems = (n > 0 ? n : 1); }
    int  getMaxPopupItems() const { return _maxPopupItems; }

    // Callbacks.
    void setOnSelectionChanged(std::function<void(int)> cb) {
        _onSelectionChanged = std::move(cb);
    }

    // Called by UIManager::closePopup when the overlay dismisses our
    // ListView out from under us (single-active-popup, click-outside,
    // reload). MUST null `_popup` before the manager frees it — otherwise
    // isPopupOpen()'s `_popup->getParent()` reads freed memory.
    void onPopupDismissedByManager();

    // Hit-test + layout override.
    Widget* hitTest(const math::FVector2& worldPos) override;
    bool onMouseButtonUp(const UIMouseEvent& e) override;
    void onMouseLeave() override;

    // onMouseButtonDown grabs keyboard focus so the user
    // can arrow-cycle without an explicit click-then-Tab. The actual
    // click → toggle-popup path lives in onMouseButtonUp (returning
    // false here lets the event flow up unchanged).
    bool onMouseButtonDown(const UIMouseEvent& e) override;

    // Keyboard state machine. Owns Up/Down/Enter/Escape
    // — does NOT delegate to the popup ListView. Returns true when the key
    // was consumed.
    //
    // PR-C2: extended to consume UIKey_A..UIKey_Z (and digits 0-9) as
    // typeahead letters. See typeahead section in the cpp for the
    // matching algorithm + buffer semantics.
    bool onKeyDown(int keyCode) override;

    // PR-C2 — accumulates the typeahead-reset timer. Called by the
    // CompoundFocusableWidget::tick cascade when ComboBox is in the
    // _root subtree (the common case). Hosts that put a ComboBox on
    // the overlay can tick it directly; v1 doesn't expect that.
    void tick(float dt) override;

    void performLayout() override;
    void onRender(IRenderBackend& renderer) override;

protected:
    void layoutChildren() override;

private:
    void ensurePopupCreated();
    void syncPopupSelection();
    math::FRectangle computePopupBounds() const;

    std::vector<std::wstring> _items;
    int _selectedIndex = -1;
    int _maxPopupItems = kDefaultMaxPopupItems;
    bool _popupOpen = false;

    TextLabel* _display = nullptr;
    ListView*  _popup   = nullptr;

    bool _enabled = true;

    // PR-C2 → PR-TypeaheadBuffer: typeahead state is now encapsulated in
    // a small reusable struct shared with Menu. Prefix accumulator lives
    // on `_typeaheadBuffer`; timeout (0.5s) lives on the struct's
    // `kTimeout` constant.
    TypeaheadBuffer _typeaheadBuffer;

    // Mute flag for the popup's selection callback. When
    // onKeyDown's open-state path mirrors `_selectedIndex` into the popup
    // ListView, that sync would fire the popup's _onSelectionChanged
    // (wired in ensurePopupCreated to closePopup + fire host callback).
    // Keyboard-driven selection is owned by ComboBox — calling closePopup
    // here would dismiss the popup mid-arrows, and re-firing
    // _onSelectionChanged would commit on every keypress. We set this
    // guard before the sync and clear it after; the callback reads the
    // flag and returns early.
    bool _silentPopupSync = false;

    std::function<void(int)> _onSelectionChanged;
};

Widget* createComboBoxWidget();

} // namespace ayt::ui
