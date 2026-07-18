#include "AYTabControl.h"
#include "AYUIManager.h"
#include "UIKeyCode.h"
#include <algorithm>

namespace ayt::ui {

// ----------------------------------------------------------------------------
// Construction / destruction
// ----------------------------------------------------------------------------

TabControl::TabControl() {
    setSize(math::FVector2(320.0f, 200.0f));
    ensureHeaderAndBodyCreated();
}

TabControl::~TabControl() {
    // We do NOT delete tab contents here — see DECISION 2 in the header.
    // _header and _body are CompoundWidget children and will be released by
    // ~CompoundWidget without freeing (the factory / caller owns them).
}

void TabControl::ensureHeaderAndBodyCreated() {
    if (_header == nullptr) {
        _header = new ListView();
        _header->setItemHeight(kDefaultRowHeight);
        _header->setSize(math::FVector2(getSize().x, _headerHeight));
        addChildExternal(_header);
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
    _tabs.push_back(TabEntry{label, content});
    syncHeaderItems();
    // First tab auto-selects; later tabs do not steal selection.
    if (_selectedIndex < 0 && !_tabs.empty()) {
        setSelectedIndex(0);
    } else if (_header != nullptr) {
        // Push the new row's selection target without firing callback
        // (we're not actually changing the visible tab).
        _header->setSelectedIndex(_selectedIndex);
    }
}

void TabControl::removeTab(int index) {
    if (index < 0 || static_cast<size_t>(index) >= _tabs.size()) return;

    Widget* removedContent = _tabs[index].content;
    // If the removed tab is the active one, detach its content from _body
    // FIRST so the body doesn't end up pointing at a destroyed widget.
    if (index == _selectedIndex && _body != nullptr && removedContent != nullptr) {
        _body->removeChild(removedContent);
    }

    _tabs.erase(_tabs.begin() + index);
    syncHeaderItems();

    // Selection repair: clamp or move.
    if (_tabs.empty()) {
        _selectedIndex = -1;
        return;
    }
    if (index < _selectedIndex) {
        // Removed a tab before the active one — slide the index down.
        --_selectedIndex;
        if (_header != nullptr) _header->setSelectedIndex(_selectedIndex);
    } else if (index == _selectedIndex) {
        // Removed the active tab. Pick a neighbor (prefer the one that
        // took its slot, otherwise the previous one).
        int newIdx = (index < static_cast<int>(_tabs.size())) ? index : index - 1;
        // Re-mount directly — do NOT recurse into setSelectedIndex (which
        // would also try to clamp via _header).
        _selectedIndex = newIdx;
        if (_header != nullptr) _header->setSelectedIndex(newIdx);
        remountBodyContent(newIdx);
        if (_onSelectionChanged) _onSelectionChanged(newIdx);
    }
    // Removed a tab after the active one — no selection change.
}

void TabControl::clearTabs() {
    // Detach every content from _body so no dangling parents remain.
    if (_body != nullptr) {
        for (auto& t : _tabs) {
            if (t.content != nullptr) _body->removeChild(t.content);
        }
    }
    _tabs.clear();
    _selectedIndex = -1;
    syncHeaderItems();
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
    if (_header != nullptr) _header->setSelectedIndex(clamped);
    remountBodyContent(clamped);
    if (_onSelectionChanged) _onSelectionChanged(clamped);
}

const std::wstring& TabControl::getSelectedLabel() const {
    return getTabLabel(_selectedIndex);
}

// ----------------------------------------------------------------------------
// Internal helpers
// ----------------------------------------------------------------------------

void TabControl::syncHeaderItems() {
    if (_header == nullptr) return;
    std::vector<std::wstring> labels;
    labels.reserve(_tabs.size());
    for (const auto& t : _tabs) labels.push_back(t.label);
    _header->setItems(labels);
    if (_selectedIndex >= 0) _header->setSelectedIndex(_selectedIndex);
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
        _body->addChildExternal(content);
    }
}

void TabControl::handleHeaderSelectionChanged(int listIndex) {
    // Mirror selection change into TabControl without re-entering _header.
    if (listIndex == _selectedIndex) return;
    _selectedIndex = listIndex;
    remountBodyContent(listIndex);
    if (_onSelectionChanged) _onSelectionChanged(listIndex);
}

// ----------------------------------------------------------------------------
// Layout
// ----------------------------------------------------------------------------

void TabControl::performLayout() {
    CompoundFocusableWidget::performLayout();
    layoutChildren();
}

void TabControl::layoutChildren() {
    ensureHeaderAndBodyCreated();
    const float w = getWidth();
    const float hh = _headerHeight;
    if (_header != nullptr) {
        _header->setSize(math::FVector2(w, hh));
        _header->setPosition(math::FVector2(0.0f, 0.0f));
    }
    if (_body != nullptr) {
        _body->setSize(math::FVector2(w, std::max(0.0f, getHeight() - hh)));
        _body->setPosition(math::FVector2(0.0f, hh));
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
    // either case lets the click event continue to flow (header ListView
    // row click → selection change; body click → child hit-test).
    if (e.mouseButton != 0) return false;
    const math::FRectangle b = getWorldBounds();
    // Header occupies [b.minY, b.minY + _headerHeight). Body sits below.
    if (e.mousePos.y < b.minY + _headerHeight && e.mousePos.y >= b.minY
        && e.mousePos.x >= b.minX && e.mousePos.x <= b.maxX) {
        UIManager::get().setFocus(this);
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