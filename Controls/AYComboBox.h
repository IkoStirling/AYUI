#pragma once

#include "AYWidget.h"
#include "AYListView.h"
#include "AYTextLabel.h"
#include <functional>
#include <string>
#include <vector>

namespace ayt::ui {

// =============================================================================
// C-6 ComboBox: a single-selection popup picker.
// =============================================================================
//
// Architecture (v1):
//   ComboBox (CompoundWidget)
//     ├─ _display: TextLabel  (shows selected item text + arrow chevron)
//     └─ _popup:   ListView*  (created on first openPopup; lives as a
//                             CompoundWidget child while visible; hidden
//                             when closed; destroyed in ~ComboBox)
//
// The popup sits BELOW the main ComboBox bounds. Because popup bounds
// extend past ComboBox's own bounds, ComboBox::hitTest is overridden to
// check the popup first when it is open — otherwise CompoundWidget's
// default self-bounds check would reject clicks on the popup that lie
// outside the main ComboBox rect.
//
// Selection model: ComboBox owns its own _selectedIndex (-1 = nothing
// selected). Setting it programmatically updates both the display label
// and the popup's selection so they stay in sync. When the user picks
// from the popup, ComboBox's _onSelectionChanged fires AFTER the popup
// is closed (so listeners that dismiss further UI on selection change
// don't fight the popup).
//
// Popup height: bounded by _maxPopupItems * itemHeight. When item count
// is less than _maxPopupItems the popup shrinks accordingly.
//
// -----------------------------------------------------------------------------
// v1 design decisions + known limitations + v1.1 upgrade paths
// -----------------------------------------------------------------------------
// (This block is the "designer's note" for the next person who touches this
// file. The decisions below are deliberate v1 simplifications, not bugs.
// Each one has a known limitation that v1 tests do NOT cover, and a clear
// upgrade path that does not require changing the public API.)
//
// DECISION 1: ComboBox extends CompoundWidget, NOT InteractiveWidget.
//
//   Why: ComboBox hosts the popup ListView as a child widget. Inheriting
//   InteractiveWidget's hover/press state machine would force the popup
//   to also be hover-aware (which it shouldn't be — it's a transient
//   dropdown, not a clickable button).
//
//   Consequence: there is no visual hover/press feedback on ComboBox
//   itself. We carry a plain `bool _enabled` flag for setEnabled/isEnabled.
//
//   v1.1 upgrade: if C-11 MenuItem / ToolButton / any other "CompoundWidget
//   that acts like a clickable" appears, lift this flag + setOnClicked
//   into a new `ClickableWidget` base. ComboBox and MenuItem can both
//   extend it. Don't do this preemptively for v1 — wait for the 2nd
//   consumer so the abstraction earns its keep.
//
// DECISION 2: Popup is a child of ComboBox (popup-as-child), NOT mounted
// at the UIManager root.
//
//   Why: simplest ownership story — popup is destroyed when ComboBox is
//   destroyed via the existing destroyWidgetTree path; no extra inject
//   point needed.
//
//   Known limitations (NOT covered by v1 tests; see Test_ComboBox.cpp
//   top-of-file "known-not-covered" block for the regression-pinning
//   entry points):
//
//     L1. **ScrollView parent.** If ComboBox is hosted inside a
//         ScrollView, ScrollView's CompoundWidget::hitTest does NOT
//         descend past the ScrollView's content subtree, so clicks on
//         the part of the popup that overflows the ScrollView's content
//         bounds will miss the popup and hit nothing useful. Workaround:
//         don't put ComboBox inside a ScrollView in v1; or layer a
//         full-screen transparent click-catcher behind the popup.
//
//     L2. **Window drag.** UIManager clears `_capturedWidget` only on
//         shutdown / reload / explicit mouseup. If a Window containing a
//         ComboBox is destroyed mid-open-popup (the host frees the
//         Window, or hot-reload swaps the tree), the popup's rows
//         referenced by `_capturedWidget` are dangling until the next
//         mouse event. In practice this hasn't crashed because the
//         next mouse event re-picks via pickWidgetAt which finds null,
//         but it's a latent UAF.
//
//     L3. **Multi-popup coexistence.** Two ComboBox side-by-side: A's
//         openPopup calls ComboBox::bringToFront which raises both
//         ComboBox and its popup together. The popup stays on top within
//         the sibling order, but B's main area may be hit-tested through
//         if the popup overlaps it. Native OS dropdowns avoid this via a
//         global "only one popup open" manager.
//
//     L4. **No auto-flip / clamp against the viewport.** Popup always
//         sits below the main ComboBox rect. If the ComboBox is near
//         the bottom of the screen, the popup extends past the viewport
//         with no clamp or upward flip. Host must position the ComboBox
//         with enough room below.
//
//   v1.1 upgrade: introduce `ComboBox::setPopupParent(Widget* root)`
//   injection point. openPopup then addChildExternal's the popup to
//   `root` instead of `this`, and closePopup removes it. ComboBox
//   no longer owns the popup (root's destroyWidgetTree does). This
//   fixes L1 (popup becomes a top-level widget in the same tree as
//   the ScrollView, hit-tested normally) and L2 (root owns it across
//   Window destruction). L3 needs a separate `DropdownManager`
//   singleton — handle in that step. L4 needs viewport metrics from
//   the host (pass in via setViewportSize or compute in performLayout).
//
// DECISION 3: ComboBox does NOT extend FocusableWidget (no keyboard
// navigation in v1).
//
//   Why: TextInput/C-3 set the precedent that FocusableWidget is the
//   focus-routing base, but ComboBox-as-clickable-picker is a separate
//   interaction model from text editing. Conflating them would force
//   caret/IME plumbing onto ComboBox.
//
//   Consequence: Tab does not move focus into ComboBox; Up/Down/Home/
//   End do not change selection; Enter does not activate. Hosts that
//   need keyboard navigation (editor shell, any form UI) wire onKeyDown
//   externally and check `UIManager::getFocusedWidget() == combobox`.
//
//   v1.1 upgrade: make ComboBox inherit FocusableWidget INSTEAD OF
//   CompoundWidget (parallel base, not a vertical chain — same trick
//   TextInput uses). Override onKeyDown to translate Up/Down/Home/End
//   into setSelectedIndex deltas; override onKeyUp for Enter activation.
//   This does NOT change the public API; the only observable change is
//   Tab/arrows starting to work.
//
// DECISION 4: No typeahead, no icon column, no animation, no opens-UP.
//
//   Deferred to v2+. None block v1 — these are visible-only features.
//   When adding them, preserve the public API (setItems / setSelectedIndex
//   / openPopup / closePopup / setOnSelectionChanged) so existing call
//   sites don't break.
//
// =============================================================================

class ComboBox : public CompoundWidget {
public:
    static constexpr float kDefaultWidth = 160.0f;
    static constexpr float kDefaultHeight = 28.0f;
    static constexpr int   kDefaultMaxPopupItems = 8;
    static constexpr float kPopupGap = 2.0f;
    static constexpr float kArrowWidth = 18.0f;

    ComboBox();
    ~ComboBox() override;

    // Enable / disable click + popup. ComboBox is a CompoundWidget (not
    // InteractiveWidget) because it hosts the popup ListView as a child —
    // so we maintain a simple enabled flag here rather than inheriting
    // the whole InteractiveWidget state machine.
    void setEnabled(bool enabled) { _enabled = enabled; }
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
    bool isPopupOpen() const { return _popupOpen; }
    void openPopup();
    void closePopup();
    void togglePopup() { if (_popupOpen) closePopup(); else openPopup(); }

    // Layout knobs.
    void setMaxPopupItems(int n) { _maxPopupItems = (n > 0 ? n : 1); }
    int  getMaxPopupItems() const { return _maxPopupItems; }

    // Callbacks.
    void setOnSelectionChanged(std::function<void(int)> cb) {
        _onSelectionChanged = std::move(cb);
    }

    // Hit-test + layout override.
    Widget* hitTest(const math::FVector2& worldPos) override;
    bool onMouseButtonUp(const UIMouseEvent& e) override;
    void onMouseLeave() override;

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

    std::function<void(int)> _onSelectionChanged;
};

Widget* createComboBoxWidget();

} // namespace ayt::ui
