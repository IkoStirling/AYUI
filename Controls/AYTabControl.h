#pragma once

#include "AYWidget.h"
#include "AYListView.h"
#include "AYPanel.h"
#include <functional>
#include <string>
#include <vector>

namespace ayt::ui {

// =============================================================================
// C-9 TabControl: a tab strip + content area.
// =============================================================================
//
// Architecture (v1):
//   TabControl (CompoundWidget)
//     ├─ _header: ListView* (single-row header; each tab = one list row)
//     └─ _body:   Panel*   (current tab's content host; addChildExternal
//                          is what callers use to put widgets inside)
//
// Header reuses C-5 ListView directly — picking a tab is exactly a single-
// selection list. The list's _onSelectionChanged is forwarded to TabControl's
// _onSelectionChanged, and additionally remounts the body content for the new
// tab (see TabControl::handleHeaderSelectionChanged).
//
// Body is a Panel (C-1) so it can host arbitrary children and paint a styled
// background. The active tab's content is reparented (addChildExternal) into
// _body on selection change; previous tab's content is detached (not freed —
// callers own it; see destroy policy note below).
//
// Layout (v1):
//   ┌──────────────────────────────────┐  ← TabControl bounds
//   │ ┌──────────┐ ┌────┐ ┌────┐ ... │  ← _header (height = _headerHeight)
//   ├──────────────────────────────────┤
//   │                                  │
//   │     active tab's content         │  ← _body (fills remaining height)
//   │                                  │
//   └──────────────────────────────────┘
//
// The header is forced to its one visible row; the ListView's vertical
// scrollbar is hidden via setScrollEnabled(false) on the scroll state (v1:
// ListView's scrollbar is always created — the host can hide it by setting
// _vbar->setVisible(false) if visible-vbar becomes noisy).
//
// -----------------------------------------------------------------------------
// v1 design decisions + known limitations + v1.1 upgrade paths
// -----------------------------------------------------------------------------
//
// DECISION 1: Header IS a ListView (not a horizontal strip of buttons).
//   Why: list reuse gives us single-selection semantics, click handling, and
//   visual styling for free. We trade the natural "horizontal tab strip"
//   look for code simplicity — v1 keeps the header as a one-row vertical
//   ListView (each tab is a row). A future v1.1 horizontal header can be a
//   separate `TabStrip` widget that re-uses ListView under the hood but
//   rotates its layout. The public API does not change.
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
// DECISION 4: No keyboard nav (Tab to enter, Left/Right to switch, Enter to
// activate). ListView v1 does not override onKeyDown either; ComboBox
// already pinned this as a shared v1.1 task. TabControl inherits the same
// deferral.
//
// DECISION 5: No closeable tabs, no reorder-by-drag, no new-tab button.
//   All pure-visual v2+ features. Public API stays stable.
//
// DECISION 6: HeaderListView vbar visible by default (ListView always
// creates one). v1 does not auto-hide it because list viewports are tiny
// for tabs (1-2 rows fit). When the host adds >_visibleRows tabs the vbar
// becomes visible and looks ugly — hosts should call
// `tc.getHeaderListView()->getVerticalScrollBar()->setVisible(false)`
// before showing. We document this instead of auto-hiding because auto-hide
// would also need to clamp _visibleRows (otherwise content past the visible
// row is unreachable). v1.1 fix: add ListView::setVisibleRowCount(n) +
// auto-hide vbar when items <= n.
// =============================================================================

class TabControl : public CompoundWidget {
public:
    // Header height in logical pixels. Default 28. ListView row height also
    // defaults to 24 — hosts that want a taller header should call
    // `getHeaderListView()->setItemHeight(headerHeight)` AND
    // `setHeaderHeight(headerHeight)`.
    static constexpr float kDefaultHeaderHeight = 28.0f;
    static constexpr float kDefaultRowHeight    = 28.0f;

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

    // Access to the underlying ListView (header) and Panel (body). Hosts use
    // these to skin the header (font, itemHeight) or pre-load the body.
    ListView* getHeaderListView() const { return _header; }
    Panel*    getBodyPanel() const      { return _body; }

    void setHeaderHeight(float h) { _headerHeight = h; }
    float getHeaderHeight() const { return _headerHeight; }

    void setOnSelectionChanged(std::function<void(int)> cb) {
        _onSelectionChanged = std::move(cb);
    }

    void performLayout() override;

protected:
    void layoutChildren() override;

private:
    struct TabEntry {
        std::wstring label;
        Widget* content = nullptr;   // not owned
    };

    void ensureHeaderAndBodyCreated();
    void syncHeaderItems();
    void remountBodyContent(int newIndex);
    void handleHeaderSelectionChanged(int listIndex);

    std::vector<TabEntry> _tabs;
    int  _selectedIndex = -1;
    float _headerHeight = kDefaultHeaderHeight;

    ListView* _header = nullptr;
    Panel*    _body   = nullptr;

    std::function<void(int)> _onSelectionChanged;
};

Widget* createTabControlWidget();

} // namespace ayt::ui