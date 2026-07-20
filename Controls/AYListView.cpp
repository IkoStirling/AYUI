#include "AYListView.h"
#include "IAYRenderBackend.h"
#include "AYStyle.h"
#include "AYUIManager.h"
#include "UIKeyCode.h"
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

bool ListView::Row::onMouseButtonUp(const UIMouseEvent& e) {
    if (!isEnabled() || e.mouseButton != 0) return false;
    if (!getWorldBounds().contains(e.mousePos)) return false;
    // Route click to ListView via _onClickByRow. ListView toggles the
    // selected state on the rows itself (single-selection model); Row
    // does NOT auto-toggle so the parent stays in charge.
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

// =============================================================================
// G4 — setVisibleRowCount + vbar auto-hide
// =============================================================================

bool ListView::needsVerticalScrollBar() const {
    // True when total content height (rows * itemHeight) exceeds the
    // viewport height. v1.1 keeps _visibleRowCount as an optional hint:
    // if the host set it and items <= visibleRowCount, the row content
    // trivially fits and vbar can hide even if host height is loose.
    // The hard gate is contentH vs viewportH — _visibleRowCount is
    // "additional early-out" so hosts setting visibleRowCount=5 with a
    // 200px-tall list and 3 items don't show a useless vbar.
    const float contentH =
        static_cast<float>(_items.size()) * _itemHeight;
    if (contentH > getHeight() + 1e-3f) return true;
    if (_visibleRowCount > 0 &&
        _items.size() > static_cast<size_t>(_visibleRowCount)) {
        return true;
    }
    return false;
}

void ListView::setVisibleRowCount(int rows) {
    // <= 0 → -1 (no cap). Preserves v1 default behavior.
    _visibleRowCount = (rows <= 0) ? -1 : rows;
    performLayout();   // re-derive vbar visibility + contentSize
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
    // G4 — derive vbar visibility BEFORE positioning it. When the vbar
    // is hidden, we want rows to occupy the full list width and the
    // vbar to not draw a 12px strip down the right edge.
    const bool needsVbar = needsVerticalScrollBar();
    const float barW = ScrollBar::kDefaultBarWidth;
    if (_vbar != nullptr) {
        _vbar->setVisible(needsVbar);
        _vbar->setPosition(math::FVector2(getWidth() - barW, 0.0f));
        _vbar->setSize(math::FVector2(barW, getHeight()));
    }
    // Update content size and sync bar. When vbar is hidden, row width
    // is the full list width; otherwise we reserve `barW` on the right.
    const float rowStripW = std::max(0.0f, getWidth() - (needsVbar ? barW : 0.0f));
    _contentSize = math::FVector2(
        rowStripW,
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

    // G4 — row width tracks the actual content-strip width, accounting for
    // whether the vbar is currently visible. layoutChildren runs before
    // rebuildRows in performLayout (which we call), but rebuildRows is
    // also called from setItems (where layoutChildren hasn't run yet) —
    // so we recompute the gate here directly.
    const float barW = ScrollBar::kDefaultBarWidth;
    const bool needsVbar = needsVerticalScrollBar();
    const float rowW = std::max(0.0f, getWidth() - (needsVbar ? barW : 0.0f));
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

    // G4 — background fills the row strip; vbar paints itself when visible.
    // When the vbar is hidden, listBounds = full bounds (no 12px strip on
    // the right that would otherwise show through as bg color behind the
    // missing vbar).
    const bool vbarShown =
        (_vbar != nullptr) && _vbar->isVisible();
    const float barW = vbarShown ? ScrollBar::kDefaultBarWidth : 0.0f;
    const math::FRectangle listBounds(
        bounds.minX, bounds.minY,
        bounds.maxX - barW, bounds.maxY);
    renderer.drawRect(listBounds, bg);
    renderer.drawBorderRect(bounds, border, bw, 2.0f);

    // Rows — positioned in rebuildRows(). Standard render() cascade draws
    // them; we render vbar ourselves to make the order explicit. vbar's
    // own Widget::render skips _visible=false automatically.
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

// =============================================================================
// Phase B (B1) — keyboard navigation + click focus grab
// =============================================================================

bool ListView::onMouseButtonDown(const UIMouseEvent& e) {
    // Phase B (B1): grab focus on press so the user can arrow-cycle
    // without needing to explicitly click-then-Tab. ListView's existing
    // onMouseButtonUp handles the actual row-click → selection path.
    // We return false so the click event continues to flow up to
    // onMouseButtonUp (no toggle duplication here — UIManager handles
    // press-vs-up semantics externally).
    if (e.mouseButton == 0) {
        if (UIManager* ui = UIManager::tryGet()) {
            ui->setFocus(this);
        }
    }
    return false;
}

bool ListView::onKeyDown(int keyCode) {
    if (_items.empty()) return false;
    const int n = static_cast<int>(_items.size());

    // Enter activates the current selection (R3 in plan: fires
    // _onItemActivated — same callback as row double-click). No
    // selection movement.
    if (keyCode == UIKey_Enter) {
        const int cur = _selectedIndex;
        if (cur >= 0 && cur < n && _onItemActivated) {
            _onItemActivated(cur);
            return true;
        }
        return false;
    }

    // Movement keys — resolve current selection (-1 → 0 for first Down,
    // -1 → n-1 for last Up so the user always lands somewhere).
    const int cur = (_selectedIndex < 0) ? 0 : _selectedIndex;
    int next = cur;

    // PageUp/PageDown step by ~one viewport of rows. Use a min 1 so
    // tiny lists still step.
    const math::FVector2 vp = getViewportSize();
    const int pageRows = std::max(1,
        static_cast<int>(vp.y / _itemHeight));

    switch (keyCode) {
    case UIKey_Up:
        next = (cur <= 0) ? n - 1 : cur - 1;
        break;
    case UIKey_Down:
        next = (cur + 1) % n;
        break;
    case UIKey_Home:
        next = 0;
        break;
    case UIKey_End:
        next = n - 1;
        break;
    case UIKey_PageUp:
        next = std::max(0, cur - pageRows);
        break;
    case UIKey_PageDown:
        next = std::min(n - 1, cur + pageRows);
        break;
    default:
        return false;
    }

    // If selection was -1 and we got Up/Down/Home/End above, next is
    // already 0 or n-1 (sentinel). setSelectedIndex handles -1→N
    // transitions by firing the callback once on real change.
    if (next != _selectedIndex) {
        setSelectedIndex(next);   // already auto-scrolls via scrollToIndex (cpp:118)
    }
    return true;
}

} // namespace ayt::ui
