#pragma once

#include "AYUI/CompoundFocusableWidget.h"
#include "AYUI/Widget.h"
#include "AYUI/TabStrip.h"
#include "AYUI/Panel.h"
#include <functional>
#include <string>
#include <vector>

namespace ayt::ui {

// TabStrip header plus Panel content host. The selected tab's content is
// mounted into the body and the previous content is detached. addTab() keeps
// host ownership; addTabOwned() transfers ownership for loader/serializer
// generated subtrees.
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
// Left/Right cycles selection only when focus is in the header; clicks in body
// content do not steal focus. Overflow behavior is delegated to TabStrip's
// Scroll/Compress/Clip modes. Multi-pane tabs, close buttons, drag reorder and
// a new-tab affordance remain host-level features.

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
    // Owning counterpart for loader/serializer-created tab contents.
    // removeTab(), clearTabs(), and destruction release owned content.
    void addTabOwned(const std::wstring& label, Widget* content);
    bool setTabLabel(int index, const std::wstring& label);
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
    // Header and body accessors for styling and overflow configuration.
    TabStrip* getTabStrip() const { return _tabStrip; }
    Panel*    getBodyPanel() const { return _body; }

    void setHeaderHeight(float h);
    float getHeaderHeight() const;

    void setOnSelectionChanged(std::function<void(int)> cb) {
        _onSelectionChanged = std::move(cb);
    }

    // onMouseButtonDown grabs focus only when the click
    // is within the header rect — clicking the body must NOT steal focus
    // from whatever focusable widget lives there. onKeyDown cycles
    // _selectedIndex Left/Right (wrap) and keeps the inner TabStrip's
    // indicator and Button visuals synchronized.
    bool onMouseButtonDown(const UIMouseEvent& e) override;
    bool onKeyDown(int keyCode) override;

    void performLayout() override;

protected:
    void layoutChildren() override;

private:
    struct TabEntry {
        std::wstring label;
        Widget* content = nullptr;
        bool owned = false;
    };

    void addTabImpl(const std::wstring& label, Widget* content, bool owned);
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
