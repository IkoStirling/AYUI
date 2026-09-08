#include "AYUI/TabControl.h"
#include "AYUI/UIManager.h"
#include "AYUI/UIKeyCode.h"
#include <algorithm>

namespace ayt::ui {

// ----------------------------------------------------------------------------
// Construction / destruction
// ----------------------------------------------------------------------------

TabControl::TabControl() {
    setSize(math::FVector2(320.0f, 200.0f));
    ensureStripAndBodyCreated();
}

TabControl::~TabControl() {
    // Never dereference external tab aliases here: legacy hosts may have
    // already destroyed them after detaching from the body. Owned contents
    // have the stronger hand-off contract and are released exactly once.
    for (auto& tab : _tabs) {
        if (tab.owned && tab.content != nullptr) {
            destroyWidgetTree(tab.content);
        }
    }
    _tabs.clear();
    _selectedIndex = -1;

    // These implementation widgets are allocated by TabControl itself. An
    // outer destroyWidgetTree() detaches external children before entering
    // this destructor but deliberately does not delete them, so both direct
    // delete and recursive tree teardown converge here exactly once.
    if (_tabStrip != nullptr) {
        _tabStrip->setOnSelectionChanged(nullptr);
        _tabStrip->clearTabs();
        _tabStrip->detachFromParent();
        delete _tabStrip;
        _tabStrip = nullptr;
    }
    if (_body != nullptr) {
        _body->detachFromParent();
        delete _body;
        _body = nullptr;
    }
}

void TabControl::setHeaderHeight(float h) {
    _headerHeight = h;
    if (_tabStrip != nullptr) _tabStrip->setTabHeight(h);
}

float TabControl::getHeaderHeight() const {
    return _headerHeight;
}

void TabControl::ensureStripAndBodyCreated() {
    if (_tabStrip == nullptr) {
        _tabStrip = new TabStrip();
        _tabStrip->setTabHeight(_headerHeight);
        _tabStrip->setSize(math::FVector2(getSize().x, _headerHeight));
        // Capture-by-this — handleStripSelectionChanged reads
        // _selectedIndex + remounts body content + fires our callback.
        _tabStrip->setOnSelectionChanged(
            [this](int idx) { handleStripSelectionChanged(idx); });
        addChildExternal(_tabStrip);
    }
    if (_body == nullptr) {
        _body = new Panel();
        _body->setBorderEnabled(true);
        addChildExternal(_body);
    }
}

// ----------------------------------------------------------------------------
// Tab data management
// ----------------------------------------------------------------------------

void TabControl::addTab(const std::wstring& label, Widget* content) {
    addTabImpl(label, content, false);
}

void TabControl::addTabOwned(const std::wstring& label, Widget* content) {
    addTabImpl(label, content, true);
}

bool TabControl::setTabLabel(int index, const std::wstring& label) {
    if (index < 0 || index >= static_cast<int>(_tabs.size())) return false;
    _tabs[static_cast<size_t>(index)].label = label;
    if (_tabStrip != nullptr) _tabStrip->setTabLabel(index, label);
    markDirty();
    return true;
}

void TabControl::addTabImpl(const std::wstring& label, Widget* content, bool owned) {
    _tabs.push_back(TabEntry{label, content, owned});
    if (_tabStrip != nullptr) {
        _tabStrip->addTab(label);
    }
    // First tab auto-selects; later tabs do not steal selection.
    if (_selectedIndex < 0 && !_tabs.empty()) {
        setSelectedIndex(0);
    }
}

void TabControl::removeTab(int index) {
    if (index < 0 || static_cast<size_t>(index) >= _tabs.size()) return;

    Widget* removedContent = _tabs[index].content;
    const bool removedOwned = _tabs[index].owned;
    // If the removed tab is the active one, detach its content from _body
    // FIRST so the body doesn't end up pointing at a destroyed widget.
    if (index == _selectedIndex && _body != nullptr && removedContent != nullptr) {
        _body->removeChild(removedContent);
    }

    _tabs.erase(_tabs.begin() + index);
    if (removedOwned) {
        destroyWidgetTree(removedContent);
        removedContent = nullptr;
    }
    if (_tabStrip != nullptr) {
        _tabStrip->removeTab(index);
    }

    // Selection repair: clamp or move.
    if (_tabs.empty()) {
        _selectedIndex = -1;
        return;
    }
    if (index < _selectedIndex) {
        // Removed a tab before the active one — slide the index down.
        --_selectedIndex;
        if (_tabStrip != nullptr) _tabStrip->setSelectedIndex(_selectedIndex);
    } else if (index == _selectedIndex) {
        // Removed the active tab. Pick a neighbor (prefer the one that
        // took its slot, otherwise the previous one).
        int newIdx = (index < static_cast<int>(_tabs.size())) ? index : index - 1;
        // Re-mount directly — do NOT recurse into setSelectedIndex (which
        // would also try to push to _tabStrip).
        _selectedIndex = newIdx;
        if (_tabStrip != nullptr) _tabStrip->setSelectedIndex(newIdx);
        remountBodyContent(newIdx);
        if (_onSelectionChanged) _onSelectionChanged(newIdx);
    }
    // Removed a tab after the active one — no selection change.
}

void TabControl::clearTabs() {
    // Detach host-owned contents and release serializer-owned contents.
    for (auto& tab : _tabs) {
        if (tab.content == nullptr) continue;
        tab.content->detachFromParent();
        if (tab.owned) destroyWidgetTree(tab.content);
    }
    _tabs.clear();
    _selectedIndex = -1;
    if (_tabStrip != nullptr) _tabStrip->clearTabs();
}

const std::wstring& TabControl::getTabLabel(int index) const {
    static const std::wstring kEmpty;
    if (index < 0 || static_cast<size_t>(index) >= _tabs.size()) return kEmpty;
    return _tabs[index].label;
}

Widget* TabControl::getTabContent(int index) const {
    if (index < 0 || static_cast<size_t>(index) >= _tabs.size()) return nullptr;
    return _tabs[index].content;
}

// ----------------------------------------------------------------------------
// Selection
// ----------------------------------------------------------------------------

void TabControl::setSelectedIndex(int index) {
    if (_tabs.empty()) {
        _selectedIndex = -1;
        return;
    }
    int clamped = std::clamp(index, 0, static_cast<int>(_tabs.size()) - 1);
    if (clamped == _selectedIndex) return;   // idempotent

    _selectedIndex = clamped;
    if (_tabStrip != nullptr) _tabStrip->setSelectedIndex(clamped);
    remountBodyContent(clamped);
    if (_onSelectionChanged) _onSelectionChanged(clamped);
}

const std::wstring& TabControl::getSelectedLabel() const {
    return getTabLabel(_selectedIndex);
}

// ----------------------------------------------------------------------------
// Internal helpers
// ----------------------------------------------------------------------------

void TabControl::handleStripSelectionChanged(int stripIndex) {
    // Mirror selection change into TabControl without re-entering the strip.
    if (stripIndex == _selectedIndex) return;
    _selectedIndex = stripIndex;
    remountBodyContent(stripIndex);
    if (_onSelectionChanged) _onSelectionChanged(stripIndex);
}

void TabControl::remountBodyContent(int newIndex) {
    if (_body == nullptr) return;
    // Detach whatever is currently inside _body. Copy the pointer list
    // first because getChildren() returns a const ref and we mutate via
    // removeChild while iterating.
    auto kidsSnapshot = _body->getChildren();
    while (!kidsSnapshot.empty()) {
        _body->removeChild(kidsSnapshot.back());
        kidsSnapshot = _body->getChildren();
    }
    if (newIndex < 0 || static_cast<size_t>(newIndex) >= _tabs.size()) return;
    Widget* content = _tabs[newIndex].content;
    if (content != nullptr) {
        if (_tabs[newIndex].owned) _body->addChild(content);
        else _body->addChildExternal(content);
    }
}

// ----------------------------------------------------------------------------
// Layout
// ----------------------------------------------------------------------------

void TabControl::performLayout() {
    CompoundFocusableWidget::performLayout();
    layoutChildren();
}

void TabControl::layoutChildren() {
    ensureStripAndBodyCreated();
    const float w = getWidth();
    const float hh = _headerHeight;
    if (_tabStrip != nullptr) {
        _tabStrip->setSize(math::FVector2(w, hh));
        _tabStrip->setPosition(math::FVector2(0.0f, 0.0f));
    }
    if (_body != nullptr) {
        const math::FVector2 bodySize(
            w, std::max(0.0f, getHeight() - hh));
        _body->setSize(bodySize);
        _body->setPosition(math::FVector2(0.0f, hh));
        // A tab page is the logical content slot, not a freely-positioned
        // sibling. Keep the active page fitted to the body so newly-created
        // or deserialized pages with no explicit size are immediately
        // visible and editable. Hosts can opt out for a custom viewport by
        // clearing the page's layout-size-managed flag.
        Widget* active = getTabContent(_selectedIndex);
        if (active != nullptr) {
            active->setPosition(math::FVector2(0.0f, 0.0f));
            if (active->isLayoutSizeManaged()) {
                active->setSize(bodySize);
            }
        }
    }
}

// ----------------------------------------------------------------------------
// Factory
// ----------------------------------------------------------------------------

Widget* createTabControlWidget() {
    return new TabControl();
}

// =============================================================================
// Phase B (B4) — keyboard navigation + header-only focus grab
// =============================================================================

bool TabControl::onMouseButtonDown(const UIMouseEvent& e) {
    // Phase B (B4): only grab focus when the click is INSIDE the header
    // rect. Clicking the body — even if it doesn't hit a focusable child
    // — must NOT steal focus from whatever's there. Returning false in
    // either case lets the click event continue to flow (a tab Button
    // click → TabStrip selection change; body click → child hit-test).
    if (e.mouseButton != 0) return false;
    const math::FRectangle b = getWorldBounds();
    // Header occupies [b.minY, b.minY + _headerHeight). Body sits below.
    if (e.mousePos.y < b.minY + _headerHeight && e.mousePos.y >= b.minY
        && e.mousePos.x >= b.minX && e.mousePos.x <= b.maxX) {
        if (UIManager* ui = UIManager::tryGet()) {
            ui->setFocus(this);
        }
    }
    return false;
}

bool TabControl::onKeyDown(int keyCode) {
    if (_tabs.empty()) return false;
    const int n = static_cast<int>(_tabs.size());
    int next = _selectedIndex;
    switch (keyCode) {
    case UIKey_Right:
        next = (_selectedIndex < 0) ? 0 : (_selectedIndex + 1) % n;
        break;
    case UIKey_Left:
        next = (_selectedIndex < 0) ? n - 1
                                    : (_selectedIndex <= 0 ? n - 1
                                                          : _selectedIndex - 1);
        break;
    default:
        return false;
    }
    setSelectedIndex(next);   // fires _onSelectionChanged + remounts body
    return true;
}

} // namespace ayt::ui
