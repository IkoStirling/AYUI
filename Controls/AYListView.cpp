#include "AYListView.h"
#include "AYIRenderBackend.h"
#include "AYStyle.h"
#include "aymath/MathUtils.h"

#include <algorithm>

namespace ayt::ui {

// =============================================================================
// Row
// =============================================================================
ListView::Row::Row() {
    setSize(math::FVector2(100.0f, 24.0f));
    setLayoutPositionManaged(false);   // ListView positions rows itself
    setLayoutSizeManaged(false);       // ... and sizes them via itemHeight
}

ListView::Row::~Row() = default;

void ListView::Row::setSelected(bool s) {
    if (_selected == s) return;
    _selected = s;
    markBoundsDirty();
}

bool ListView::Row::onMouseButtonUp(const UIMouseEvent& e) {
    if (!_enabled || e.mouseButton != 0) return false;
    if (!getWorldBounds().contains(e.mousePos)) return false;
    // Defer to InteractiveWidget::onMouseButtonUp — fires _onClicked if set
    // (which the list leaves null on rows). The list routes its own click
    // via _onClickByRow which we invoke AFTER base to keep state
    // transitions consistent with CheckBox / Button.
    InteractiveWidget::onMouseButtonUp(e);
    if (_onClickByRow) {
        _onClickByRow(_index);
    }
    return true;
}

void ListView::Row::onRender(IRenderBackend& renderer) {
    math::FRectangle bounds = getWorldBounds();
    if (bounds.maxX <= bounds.minX || bounds.maxY <= bounds.minY) return;

    // Selection band — full row, accent color when selected.
    if (_selected) {
        renderer.drawRect(bounds, math::FVector4(0.18f, 0.45f, 0.78f, 0.55f));
    } else if (isMouseOver() && isEnabled()) {
        renderer.drawRect(bounds, math::FVector4(0.30f, 0.30f, 0.32f, 0.5f));
    }

    // Text label, padded 8 px on the leading edge.
    if (!_text.empty()) {
        const float padX = 8.0f;
        math::FRectangle textBounds(
            bounds.minX + padX,
            bounds.minY,
            bounds.maxX - padX,
            bounds.maxY);
        const math::FVector4 textColor = isEnabled()
            ? math::FVector4(1.0f, 1.0f, 1.0f, 1.0f)
            : math::FVector4(0.55f, 0.55f, 0.55f, 1.0f);
        renderer.drawText(textBounds, _text, 14, textColor);
    }
}

// =============================================================================
// ListView
// =============================================================================
ListView::ListView() {
    setSize(math::FVector2(160.0f, 200.0f));
    setLayoutPositionManaged(false);   // host positions the list
    ensureBarCreated();
}

ListView::~ListView() {
    // Rows were added via addChildExternal — destroyWidgetTree (called
    // from the factory-owned tree's root delete) will free them. Just
    // NULL our pointer to avoid double-free in case dtor runs first.
    _rows.clear();
    _vbar = nullptr;
}

void ListView::setItems(const std::vector<std::wstring>& items) {
    _items = items;
    if (_selectedIndex >= static_cast<int>(_items.size())) {
        _selectedIndex = -1;
    }
    rebuildRows();
}

void ListView::addItem(const std::wstring& item) {
    _items.push_back(item);
    rebuildRows();
}

void ListView::clearItems() {
    _items.clear();
    _selectedIndex = -1;
    rebuildRows();
}

const std::wstring& ListView::getItem(size_t index) const {
    static const std::wstring kEmpty;
    if (index >= _items.size()) return kEmpty;
    return _items[index];
}

void ListView::setSelectedIndex(int index) {
    int clamped = index;
    if (clamped < -1) clamped = -1;
    if (clamped >= static_cast<int>(_items.size())) clamped = -1;
    if (clamped == _selectedIndex) return;
    // Clear old row's selection flag.
    if (_selectedIndex >= 0 &&
        _selectedIndex < static_cast<int>(_rows.size()) &&
        _rows[_selectedIndex] != nullptr) {
        _rows[_selectedIndex]->setSelected(false);
    }
    _selectedIndex = clamped;
    if (_selectedIndex >= 0 &&
        _selectedIndex < static_cast<int>(_rows.size()) &&
        _rows[_selectedIndex] != nullptr) {
        _rows[_selectedIndex]->setSelected(true);
    }
    scrollToIndex(_selectedIndex);
    if (_onSelectionChanged) {
        _onSelectionChanged(_selectedIndex);
    }
}

const std::wstring& ListView::getSelectedItem() const {
    static const std::wstring kEmpty;
    if (_selectedIndex < 0 ||
        _selectedIndex >= static_cast<int>(_items.size())) {
        return kEmpty;
    }
    return _items[_selectedIndex];
}

void ListView::setScrollOffset(const math::FVector2& offset) {
    const math::FVector2 vp = getViewportSize();
    const math::FVector2 maxOff = _scrollState.getMaxScrollOffset(vp);
    math::FVector2 clamped(offset);
    if (clamped.x < 0.0f) clamped.x = 0.0f;
    if (clamped.x > maxOff.x) clamped.x = maxOff.x;
    if (clamped.y < 0.0f) clamped.y = 0.0f;
    if (clamped.y > maxOff.y) clamped.y = maxOff.y;
    _scrollState.setScrollOffset(clamped);
    syncBarToOffset();
}

void ListView::ensureBarCreated() {
    if (_vbar != nullptr) return;
    _vbar = new ScrollBar();
    _vbar->setOrientation(ScrollBar::Orientation::Vertical);
    _vbar->setOnValueChanged([this](float v) {
        const math::FVector2 vp = getViewportSize();
        const float maxOff = (_contentSize.y - vp.y);
        if (maxOff <= 0.0f) return;
        // Map v (0..contentHeight) to scrollOffset.y.
        _scrollState.setScrollOffset(math::FVector2(
            _scrollState.getScrollOffset().x, v));
        (void)vp;
    });
    addChildExternal(_vbar);
}

void ListView::syncBarToOffset() {
    if (_vbar == nullptr) return;
    const math::FVector2 vp = getViewportSize();
    _vbar->setRange(0.0f, _contentSize.y);
    _vbar->setViewportSize(vp.y);
    _vbar->setValue(_scrollState.getScrollOffset().y);
}

math::FVector2 ListView::getViewportSize() const {
    return math::FVector2(getWidth(), getHeight());
}

void ListView::performLayout() {
    const float barW = ScrollBar::kDefaultBarWidth;
    if (_vbar != nullptr) {
        _vbar->setPosition(math::FVector2(getWidth() - barW, 0.0f));
        _vbar->setSize(math::FVector2(barW, getHeight()));
    }
    rebuildRows();
    layoutChildren();
}

void ListView::layoutChildren() {
    if (_vbar != nullptr) {
        // CompoundWidget::layoutChildren iterates _children; rows already
        // positioned by rebuildRows. Re-pin the vbar so layout-driven
        // callers (Resize etc.) don't leave it stale.
        const float barW = ScrollBar::kDefaultBarWidth;
        _vbar->setPosition(math::FVector2(getWidth() - barW, 0.0f));
        _vbar->setSize(math::FVector2(barW, getHeight()));
    }
    // Update content size and sync bar.
    _contentSize = math::FVector2(
        getWidth() - (_vbar ? ScrollBar::kDefaultBarWidth : 0.0f),
        _items.size() * _itemHeight);
    syncBarToOffset();
}

void ListView::rebuildRows() {
    // Tear down old rows (they are children; removeChild + delete so the
    // tree doesn't accumulate dead nodes between setItems calls).
    for (Row* r : _rows) {
        if (r == nullptr) continue;
        removeChild(r);
        delete r;
    }
    _rows.clear();

    const float barW = ScrollBar::kDefaultBarWidth;
    const float rowW = std::max(0.0f, getWidth() - barW);
    _rows.reserve(_items.size());
    for (size_t i = 0; i < _items.size(); ++i) {
        Row* row = new Row();
        row->setText(_items[i]);
        row->setIndex(static_cast<int>(i));
        row->setSelected(static_cast<int>(i) == _selectedIndex);
        row->setSize(math::FVector2(rowW, _itemHeight));
        row->_onClickByRow = [this](int idx) { handleRowClick(idx); };
        addChildExternal(row);
        _rows.push_back(row);
    }

    _contentSize = math::FVector2(rowW, _items.size() * _itemHeight);
    syncBarToOffset();
    // Position rows on every rebuild — children may have just been added.
    const float yOff = -_scrollState.getScrollOffset().y;
    for (size_t i = 0; i < _rows.size(); ++i) {
        if (_rows[i] != nullptr) {
            _rows[i]->setPosition(math::FVector2(
                0.0f, static_cast<float>(i) * _itemHeight + yOff));
        }
    }
}

void ListView::onRender(IRenderBackend& renderer) {
    math::FRectangle bounds = getWorldBounds();
    if (bounds.maxX <= bounds.minX || bounds.maxY <= bounds.minY) return;

    const ResolvedStyle style = resolveStyle(getStyleId());
    math::FVector4 bg = style.hasStyle
        ? style.backgroundColor
        : math::FVector4(0.12f, 0.12f, 0.13f, 1.0f);
    math::FVector4 border = style.hasStyle
        ? style.borderColor
        : math::FVector4(0.45f, 0.45f, 0.5f, 1.0f);
    float bw = style.hasStyle ? style.borderWidth : 1.0f;

    // Background — only behind rows; ScrollBar paints itself.
    const float barW = (_vbar != nullptr) ? ScrollBar::kDefaultBarWidth : 0.0f;
    const math::FRectangle listBounds(
        bounds.minX, bounds.minY,
        bounds.maxX - barW, bounds.maxY);
    renderer.drawRect(listBounds, bg);
    renderer.drawBorderRect(bounds, border, bw, 2.0f);

    // Rows — positioned in rebuildRows(). Standard render() cascade draws
    // them; we render vbar ourselves to make the order explicit.
    for (Row* r : _rows) {
        if (r != nullptr && r->isVisible()) {
            r->render(renderer);
        }
    }
    if (_vbar != nullptr) _vbar->render(renderer);
}

void ListView::handleRowClick(int index) {
    if (index < 0 || index >= static_cast<int>(_items.size())) return;
    // Single-click selects in v1. Double-click → activation lives behind a
    // future Tick-based timer (no monotonic time plumbing in UIManager
    // today); the public _onItemActivated is wired up but only fires when
    // a future engine-level double-click arrives (out of v1 scope).
    setSelectedIndex(index);
    if (_onItemActivated) {
        _onItemActivated(index);
    }
}

void ListView::scrollToIndex(int index) {
    if (index < 0 || index >= static_cast<int>(_items.size())) return;
    const math::FVector2 vp = getViewportSize();
    const float rowTop = static_cast<float>(index) * _itemHeight;
    const float rowBot = rowTop + _itemHeight;
    const float viewTop = _scrollState.getScrollOffset().y;
    const float viewBot = viewTop + vp.y;
    if (rowTop < viewTop) {
        _scrollState.setScrollOffset(math::FVector2(0.0f, rowTop));
    } else if (rowBot > viewBot) {
        _scrollState.setScrollOffset(math::FVector2(
            0.0f, rowBot - vp.y));
    }
    syncBarToOffset();
    // Reposition rows after offset change.
    const float yOff = -_scrollState.getScrollOffset().y;
    for (size_t i = 0; i < _rows.size(); ++i) {
        if (_rows[i] != nullptr) {
            _rows[i]->setPosition(math::FVector2(
                0.0f, static_cast<float>(i) * _itemHeight + yOff));
        }
    }
}

Widget* createListViewWidget() {
    return new ListView();
}

bool ListView::onMouseButtonUp(const UIMouseEvent& e) {
    if (e.mouseButton != 0) return false;
    // Walk rows in reverse draw order and route to the first hit.
    for (auto it = _rows.rbegin(); it != _rows.rend(); ++it) {
        if (*it == nullptr) continue;
        if ((*it)->getWorldBounds().contains(e.mousePos)) {
            return (*it)->onMouseButtonUp(e);
        }
    }
    return false;
}

} // namespace ayt::ui
