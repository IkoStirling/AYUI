#pragma once

#include "AYCompoundFocusableWidget.h"
#include "AYWidget.h"
#include "AYTabStrip.h"
#include "AYPanel.h"
#include <functional>
#include <string>
#include <vector>

namespace ayt::ui {

// =============================================================================
// C-9 TabControl: a tab strip + content area.
// =============================================================================
//
// Architecture (Phase D PR-3):
//   TabControl (CompoundFocusableWidget)
//     ├─ _tabStrip: TabStrip*   (Phase D4 — horizontal row of Button tabs
//                              with sky-blue accent underline; replaces
//                              the old vertical ListView header)
//     └─ _body:    Panel*      (current tab's content host; addChildExternal
//                              is what callers use to put widgets inside)
//
// Body is a Panel (C-1) so it can host arbitrary children and paint a styled
// background. The active tab's content is reparented (addChildExternal) into
// _body on selection change; previous tab's content is detached (not freed —
// callers own it; see destroy policy note below).
//
// Layout:
//   ┌──────────────────────────────────┐  ← TabControl bounds
//   │ ┌──────┐ ┌──────┐ ┌──────┐ ...  │  ← _tabStrip (height = _headerHeight)
//   ├──────────────────────────────────┤
//   │                                  │
//   │     active tab's content         │  ← _body (fills remaining height)
//   │                                  │
//   └──────────────────────────────────┘
//
// -----------------------------------------------------------------------------
// v1 design decisions + known limitations + v1.1 upgrade paths
// -----------------------------------------------------------------------------
//
// DECISION 1 (Phase D PR-3): Header IS a TabStrip (horizontal Button row),
//   not a vertical ListView. The old ListView header shipped in C-9
//   (2026-07-18) was visually wrong for production (one tall column of
//   text rows). D4 replaces it with a Phase D widget that lays a row of
//   plain `Button` (C-1) tabs out left-to-right with an accent underline
//   beneath the active tab. Q10: tabs are `Button : InteractiveWidget`,
//   selected-accent underline drawn by TabStrip::onRender.
//
// DECISION 2: Body is a Panel, NOT a slot-stack of pre-loaded contents.
//   Why: TabControl does not own the lifetime of tab content — hosts create
//   their content widgets (often from JSON via WidgetFactory) and pass them
//   in. addTab(label, content*) keeps a raw pointer; on selection change we
//   detach the previous content from _body and reparent the new one. We do
//   NOT free the previous content. This matches CompoundWidget's "parent
//   never deletes children" contract.
//   Consequence: if a host wants to destroy a tab's content it must call
//   removeTab(index) first, then destroyWidgetTree on its content. See
//   destroyWidgetTree usage in Test_TabControl for the canonical pattern.
//
// DECISION 3: Single-selection in v1. No multi-tab open / split views.
//   Deferred to v2+ alongside ListView's multi-selection upgrade.
//
// DECISION 4 (Phase B — B4, S3): TabControl extends
// CompoundFocusableWidget. Left/Right cycle _selectedIndex (wrap).
// onMouseButtonDown only grabs focus when the click is INSIDE the
// header rect — clicking on body content (a button, a text input, etc.)
// must NOT steal focus from the body widget.
//
// DECISION 5: No closeable tabs, no reorder-by-drag, no new-tab button.
//   All pure-visual v2+ features. Public API stays stable.
//
// DECISION 6 (Phase D PR-3 Q12): v1 clips overflow — tabs longer than
// the strip width draw past the right edge without scrolling. Hosts polling
// `getTabStrip()->isOverflown()` can decide to show a "more tabs" badge
// or defer the v1.1 ScrollView wrap until it ships.
//
// =============================================================================

class TabControl : public CompoundFocusableWidget {
public:
    // Header height in logical pixels. Default 28 — also the TabStrip
    // kDefaultTabHeight. Hosts that want a taller header should call
    // `setHeaderHeight(h)` (it forwards to _tabStrip->setTabHeight(h)).
    static constexpr float kDefaultHeaderHeight = 28.0f;

    TabControl();
    ~TabControl() override;

    // Tab data management. `content` is adopted by reference only — TabControl
    // does NOT delete it. Callers must keep the content alive and free it
    // via destroyWidgetTree after removing the tab.
    void addTab(const std::wstring& label, Widget* content);
    void removeTab(int index);
    void clearTabs();
    size_t getTabCount() const { return _tabs.size(); }
    const std::wstring& getTabLabel(int index) const;
    Widget* getTabContent(int index) const;

    // Selection. -1 = no tab selected (initial state until at least one tab
    // exists; when first tab is added, selection auto-moves to 0).
    int  getSelectedIndex() const { return _selectedIndex; }
    void setSelectedIndex(int index);
    const std::wstring& getSelectedLabel() const;

    // Access to the underlying TabStrip (header) and Panel (body). Hosts use
    // these to skin the header (setTabHeight, setSpacing) or pre-load the body.
    //
    // Phase D PR-3: getHeaderListView() REMOVED. Hosts that pre-D4 code
    // called `tc.getHeaderListView()->setItems(...)` etc. must migrate
    // to `tc.getTabStrip()->...` (the TabStrip API is different —
    // addTab(label) per-tab, no items vector). The 1 inline call site
    // inside Test_TabControl was rewritten.
    TabStrip* getTabStrip() const { return _tabStrip; }
    Panel*    getBodyPanel() const { return _body; }

    void setHeaderHeight(float h);
    float getHeaderHeight() const;

    void setOnSelectionChanged(std::function<void(int)> cb) {
        _onSelectionChanged = std::move(cb);
    }

    // Phase B (B4): onMouseButtonDown grabs focus only when the click
    // is within the header rect — clicking the body must NOT steal focus
    // from whatever focusable widget lives there. onKeyDown cycles
    // _selectedIndex Left/Right (wrap). D4 forwards Left/Right to the
    // inner TabStrip for the underline + button visuals to update too.
    bool onMouseButtonDown(const UIMouseEvent& e) override;
    bool onKeyDown(int keyCode) override;

    void performLayout() override;

protected:
    void layoutChildren() override;

private:
    struct TabEntry {
        std::wstring label;
        Widget* content = nullptr;   // not owned
    };

    void ensureStripAndBodyCreated();
    void handleStripSelectionChanged(int stripIndex);
    void remountBodyContent(int newIndex);

    std::vector<TabEntry> _tabs;
    int   _selectedIndex = -1;
    float _headerHeight = kDefaultHeaderHeight;

    TabStrip* _tabStrip = nullptr;
    Panel*    _body     = nullptr;

    std::function<void(int)> _onSelectionChanged;
};

Widget* createTabControlWidget();

} // namespace ayt::ui