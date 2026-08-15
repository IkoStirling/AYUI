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
// DECISION 3 (Phase B — B2, S3): ComboBox extends
// CompoundFocusableWidget (single inheritance, parallel base — same
// trick CompoundFocusableWidget uses for ListView/Menu/TabControl).
//
//   Why the rename: CompoundFocusableWidget is the shared base for
//   "compound widget that owns keyboard focus". ComboBox-as-clickable-
//   picker is a separate interaction model from text editing, so we do
//   NOT inherit FocusableWidget directly (TextInput-style) — that would
//   drag caret/IME plumbing along. CompoundFocusableWidget inherits
//   FocusableWidget for us; ComboBox gets focus + Tab traversal + can
//   implement its own onKeyDown without inheriting FocusableWidget's
//   text-edit assumptions.
//
//   Phase B (B2) state machine (ComboBox::onKeyDown owns ALL its keys):
//
//     Closed state:
//       Down  → open popup, target = current selection (or 0 if -1)
//       Up    → open popup, target = last item (n - 1)
//       other → not consumed
//
//     Open state:
//       Up/Down → mutate _selectedIndex by ±1 (wraps), sync popup
//       Enter   → close popup + fire _onSelectionChanged (commits)
//       Escape  → close popup without firing _onSelectionChanged
//
//   Phase B does NOT delegate keys to the popup ListView — the popup's
//   rows are click-only; keyboard lives on ComboBox. This keeps the
//   key-to-state mapping in ONE place (no double-routing risk).
//
// DECISION 4: No typeahead, no icon column, no animation, no opens-UP.
//
//   Deferred to v2+. None block v1 — these are visible-only features.
//   When adding them, preserve the public API (setItems / setSelectedIndex
//   / openPopup / closePopup / setOnSelectionChanged) so existing call
//   sites don't break.
//
// =============================================================================

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

    // Phase B (B2): onMouseButtonDown grabs keyboard focus so the user
    // can arrow-cycle without an explicit click-then-Tab. The actual
    // click → toggle-popup path lives in onMouseButtonUp (returning
    // false here lets the event flow up unchanged).
    bool onMouseButtonDown(const UIMouseEvent& e) override;

    // Phase B (B2): keyboard state machine. Owns Up/Down/Enter/Escape
    // — does NOT delegate to the popup ListView. See DECISION 3 in the
    // header note above for the full state table. Returns true when the
    // key was consumed.
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

    // Phase B (B2): mute flag for the popup's selection callback. When
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
