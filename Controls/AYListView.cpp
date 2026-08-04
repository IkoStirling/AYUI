#include "AYListView.h"
#include "IAYRenderBackend.h"
#include "AYStyle.h"
#include "AYUIManager.h"
#include "UIKeyCode.h"
#include "aymath/MathUtils.h"

#include <algorithm>
#include <numeric>

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
    // Route click to ListView via _onClickByRow with current modifier
    // bitmask. ListView pulls modifiers via UIManager::tryGet() because
    // modifier keys (Shift/Ctrl/Alt) are intercepted at the UIManager
    // level and never reach Row as key events (see UIKeyCode.h:28-31).
    // If no UIManager is active (e.g. bare Row in a test fixture), the
    // modifiers default to 0 → plain click → v1 single-select semantics.
    uint32_t mods = 0;
    if (UIManager* ui = UIManager::tryGet()) {
        mods = ui->getModifiers();
    }
    if (_onClickByRow) {
        _onClickByRow(_index, mods);
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
    // Keep layoutPositionManaged=true (default) so VBox/HBox can place the
    // list in flow layouts. Absolute hosts can still call
    // setLayoutPositionManaged(false) after construction.
    ensureBarCreated();
}

ListView::~ListView() {
    // Pool rows AND the vbar are attached via addChildExternal
    // (host-lifetime semantics so a parent container can own us without
    // owning our internals). However, when *we* are destroyed (a top-level
    // ListView, or one whose host uses the destroyWidgetTree path) the
    // externally-owned flag means destroyWidgetTree SKIPS them — which
    // would leak the rows and the ScrollBar we allocated. Mirror the
    // rebuildRows() teardown pattern: detach + delete each row, then
    // delete the vbar (it lives in our _children list).
    for (Row* r : _rowPool) {
        if (r == nullptr) continue;
        if (r->getParent() == this) {
            removeChild(r);
        }
        delete r;
    }
    _rowPool.clear();

    if (_vbar != nullptr) {
        if (_vbar->getParent() == this) {
            removeChild(_vbar);
        }
        delete _vbar;
        _vbar = nullptr;
    }
}

void ListView::setItems(const std::vector<std::wstring>& items) {
    _items = items;
    // Clamp stale selections to -1 (G1 — multi mode: filter out-of-range
    // entries from the vector). We funnel through setSelectedIndices so
    // row visuals + callbacks fire once with the cleaned-up vector.
    std::vector<int> cleaned;
    cleaned.reserve(_selectedIndices.size());
    for (int idx : _selectedIndices) {
        if (idx >= 0 && idx < static_cast<int>(_items.size())) {
            cleaned.push_back(idx);
        }
    }
    std::sort(cleaned.begin(), cleaned.end());
    if (cleaned.size() != _selectedIndices.size()) {
        setSelectedIndices(cleaned);   // fires callbacks if changed
    } else {
        _selectedIndex = _selectedIndices.empty()
            ? -1
            : _selectedIndices.back();
    }
    // Anchor may now be out of range — reset to last selected if valid,
    // else -1.
    if (_anchorIndex >= static_cast<int>(_items.size())) {
        _anchorIndex = _selectedIndex;
    }
    // G2 — rebuild the pool. setItems is the only path that may grow
    // the pool; scroll/remap paths don't touch allocation.
    rebuildRows();
}

void ListView::addItem(const std::wstring& item) {
    _items.push_back(item);
    rebuildRows();
}

void ListView::clearItems() {
    _items.clear();
    // G1 — clear selection vector (was previously just _selectedIndex=-1).
    // Anchor + range end reset too so a subsequent Ctrl/Shift+click
    // doesn't extend from a stale index.
    if (!_selectedIndices.empty()) {
        setSelectedIndices({});
    } else {
        _selectedIndex = -1;
    }
    _anchorIndex = -1;
    _rangeEndIndex = -1;
    rebuildRows();
}

const std::wstring& ListView::getItem(size_t index) const {
    static const std::wstring kEmpty;
    if (index >= _items.size()) return kEmpty;
    return _items[index];
}

int ListView::getRowPoolLogicalIndex(size_t slot) const {
    if (slot >= _rowPool.size()) return -1;
    if (_rowPool[slot] == nullptr) return -1;
    return _rowPool[slot]->getIndex();
}

// =============================================================================
// G4 — setVisibleRowCount + vbar auto-hide
// =============================================================================

bool ListView::needsVerticalScrollBar() const {
    // True when total content height (rows * itemHeight) exceeds the
    // viewport height. v1.1 keeps _visibleRowCount as an optional hint:
    // if the host set it and items <= visibleRowCount, the row content
    // trivially fits and vbar can hide even if host height is loose.
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

// =============================================================================
// G2 — row pool: sizing + rebind
// =============================================================================

int ListView::computePoolSize() const {
    if (_items.empty()) return 0;
    // +2 buffer rows so the half-visible top/bottom rows that scroll
    // into view don't pop in/out one frame at a time. For 200px /
    // 24px items → ceil(200/24) + 2 = 9 + 2 = 11. For a 24px-tall
    // list with 1 item → 1 + 2 = 3 (cap at item count below).
    const float vpH = std::max(0.0f, getHeight());
    const int visibleRows = std::max(1,
        static_cast<int>(vpH / _itemHeight) + 1);
    const int poolSize = visibleRows + 2;
    return std::min(static_cast<int>(_items.size()), poolSize);
}

ListView::Row* ListView::rowForLogical(int index) const {
    if (index < 0 || index >= static_cast<int>(_items.size())) return nullptr;
    const int first = _firstVisibleIndex;
    const int last  = first + static_cast<int>(_rowPool.size()) - 1;
    if (index < first || index > last) return nullptr;
    const size_t slot = static_cast<size_t>(index - first);
    if (slot >= _rowPool.size()) return nullptr;
    return _rowPool[slot];
}

void ListView::rebindPoolRows() {
    // Pure mapping step. Pool allocation is handled by rebuildRows();
    // rebindPoolRows() only swaps which logical item each pool slot
    // displays and updates row text/index/selection/visibility.
    if (_rowPool.empty() || _items.empty()) {
        for (Row* r : _rowPool) {
            if (r != nullptr) r->setVisible(false);
        }
        _firstVisibleIndex = 0;
        return;
    }

    // Derive first visible index from the current scrollOffset.y.
    // Each itemHeight step moves by one logical item. Clamp so the
    // last logical item is never beyond _items.size() - poolSize().
    const float yOff = _scrollState.getScrollOffset().y;
    const int rawFirst = static_cast<int>(yOff / _itemHeight);
    const int maxFirst = std::max(0,
        static_cast<int>(_items.size())
        - static_cast<int>(_rowPool.size()));
    _firstVisibleIndex = std::max(0, std::min(rawFirst, maxFirst));

    const float barW = ScrollBar::kDefaultBarWidth;
    const bool needsVbar = needsVerticalScrollBar();
    const float rowW = std::max(0.0f, getWidth() - (needsVbar ? barW : 0.0f));

    for (size_t slot = 0; slot < _rowPool.size(); ++slot) {
        Row* row = _rowPool[slot];
        if (row == nullptr) continue;
        const int logical = _firstVisibleIndex + static_cast<int>(slot);
        if (logical >= static_cast<int>(_items.size())) {
            // Pool may be larger than remaining items (e.g. shrunk list);
            // hide surplus slots.
            row->setVisible(false);
            continue;
        }
        row->setVisible(true);
        row->setSize(math::FVector2(rowW, _itemHeight));
        // Pool slot s is at local y = s * itemHeight. World bounds walk
        // gives the painted y. No per-row scroll-offset math.
        row->setPosition(math::FVector2(0.0f,
            static_cast<float>(slot) * _itemHeight));
        row->setText(_items[logical]);
        row->setIndex(logical);
        row->setSelected(isSelected(logical));
    }
}

// =============================================================================
// G1 — selection model (SelectionMode + vector<int> + anchor)
// =============================================================================

void ListView::setSelectedIndex(int index) {
    // G1 — setSelectedIndex is now the "single-element" front for
    // setSelectedIndices. Behavior by mode:
    //   Single: replaces selection with this index (v1 semantics).
    //   Extended: clears + pushes this index. Subsequent Ctrl/Shift
    //             clicks will keep accumulating from this point.
    // Clamping mirrors v1: out-of-range and <-1 collapse to -1 (no-op
    // when current is also -1; otherwise clears selection).
    int clamped = index;
    if (clamped < -1) clamped = -1;
    if (clamped >= static_cast<int>(_items.size())) clamped = -1;
    if (clamped == -1) {
        setSelectedIndices({});
    } else {
        setSelectedIndices({clamped});
    }
}

void ListView::setSelectedIndices(const std::vector<int>& indices) {
    // G1 — the authoritative setter. Syncs pool row visual flags and
    // fires BOTH _onSelectionChanged (single-int, last selected or -1)
    // AND _onSelectionIndicesChanged (full vector, multi-mode users).
    //
    // We early-out only if the new vector is identical (same size + same
    // elements at each position). Order-independence matters because
    // callers may pass unsorted vectors; we sort defensively below.
    bool same = (indices.size() == _selectedIndices.size());
    if (same) {
        for (size_t i = 0; i < indices.size(); ++i) {
            if (indices[i] != _selectedIndices[i]) { same = false; break; }
        }
    }
    if (same) return;

    // Defensive: filter + sort + dedupe ascending. Bounds-check each
    // index; out-of-range entries are dropped (host bug — we don't crash).
    std::vector<int> filtered;
    filtered.reserve(indices.size());
    for (int idx : indices) {
        if (idx >= 0 && idx < static_cast<int>(_items.size())) {
            filtered.push_back(idx);
        }
    }
    std::sort(filtered.begin(), filtered.end());
    filtered.erase(std::unique(filtered.begin(), filtered.end()),
                   filtered.end());

    // Clear pool row visual flags for the OLD selection. G2 — only
    // touch rows currently in the pool viewport (logical indices
    // outside the pool window have no row widget yet).
    for (int oldIdx : _selectedIndices) {
        if (Row* r = rowForLogical(oldIdx)) {
            r->setSelected(false);
        }
    }
    // Apply new selection visuals + state.
    _selectedIndices = filtered;
    for (int idx : _selectedIndices) {
        if (Row* r = rowForLogical(idx)) {
            r->setSelected(true);
        }
    }
    // Mirror cache for getSelectedIndex() / backwards-compat callers.
    _selectedIndex = _selectedIndices.empty()
        ? -1
        : _selectedIndices.back();

    // Auto-scroll to keep the most-recent selection visible (matches v1
    // scrollToIndex-on-setSelectedIndex). In Extended mode, prefer the
    // LAST index (most recent click), matching standard list UX.
    if (!_selectedIndices.empty()) {
        scrollToIndex(_selectedIndices.back());
    }

    // Fire callbacks. _onSelectionChanged (single-int) gets the most
    // recently selected index (-1 if selection cleared). _onSelection
    // IndicesChanged (vector) only fires when something actually
    // changed — guaranteed by the early-out above.
    if (_onSelectionChanged) {
        _onSelectionChanged(_selectedIndex);
    }
    if (_onSelectionIndicesChanged) {
        _onSelectionIndicesChanged(_selectedIndices);
    }
}

bool ListView::isSelected(int index) const {
    if (index < 0 || index >= static_cast<int>(_items.size())) return false;
    return std::find(_selectedIndices.begin(), _selectedIndices.end(), index)
        != _selectedIndices.end();
}

void ListView::clearSelection() {
    setSelectedIndices({});
}

void ListView::setAnchorIndex(int idx) {
    if (idx < -1) idx = -1;
    if (idx >= static_cast<int>(_items.size())) idx = -1;
    _anchorIndex = idx;
    // Range end follows anchor on plain clicks so Shift+arrow knows
    // where the range starts when extending.
    if (idx >= 0) _rangeEndIndex = idx;
}

int ListView::getSelectedIndex() const {
    // G1 — returns -1 if empty, else the last (most recently selected)
    // entry. Single-mode callers (Combobox popup / TabStrip / TreeView)
    // always see a 0/1-element vector, so this is identical to v1.
    if (_selectedIndices.empty()) return -1;
    return _selectedIndices.back();
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
    // G2 — _scrollState is has-a, not base, so no virtual hook fires
    // here. ListView owns the chokepoint: every site that mutates the
    // scrollOffset must call rebindPoolRows() to refresh the pool.
    _scrollState.setScrollOffset(clamped);
    syncBarToOffset();
    rebindPoolRows();
}

void ListView::ensureBarCreated() {
    if (_vbar != nullptr) return;
    _vbar = new ScrollBar();
    _vbar->setOrientation(ScrollBar::Orientation::Vertical);
    _vbar->setOnValueChanged([this](float v) {
        const math::FVector2 vp = getViewportSize();
        const float maxOff = (_contentSize.y - vp.y);
        if (maxOff <= 0.0f) return;
        // Map v (0..contentHeight) to scrollOffset.y. G2 chokepoint:
        // the ScrollableWidget is has-a not base, so we call
        // rebindPoolRows() here too (not relying on a virtual hook).
        _scrollState.setScrollOffset(math::FVector2(
            _scrollState.getScrollOffset().x, v));
        rebindPoolRows();
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
    // Mirror into _scrollState so its getMaxScrollOffset(vp) returns a
    // non-zero maxY when items overflow the viewport. Without this,
    // setScrollOffset clamps every positive y to 0 (silent bug carried
    // over from pre-G1; v1's per-row position loop didn't use
    // getMaxScrollOffset, so the bug never bit).
    _scrollState.setContentSize(_contentSize);
    syncBarToOffset();
}

void ListView::rebuildRows() {
    // G2 — replace the previous N-row allocation with a pool of K rows.
    // Pool size is derived from the current viewport so 5k items only
    // allocate ~11 widgets instead of 5000.
    //
    // First: tear down the old pool (children; removeChild + delete so
    // the tree doesn't accumulate dead nodes between setItems calls).
    for (Row* r : _rowPool) {
        if (r == nullptr) continue;
        removeChild(r);
        delete r;
    }
    _rowPool.clear();

    const int poolSize = computePoolSize();
    if (poolSize <= 0) {
        // Empty items — keep _firstVisibleIndex at 0 so rebindPoolRows
        // is well-defined next time items arrive.
        _firstVisibleIndex = 0;
        _contentSize = math::FVector2(getWidth(), 0.0f);
        _scrollState.setContentSize(_contentSize);
        syncBarToOffset();
        return;
    }

    const float barW = ScrollBar::kDefaultBarWidth;
    const bool needsVbar = needsVerticalScrollBar();
    const float rowW = std::max(0.0f, getWidth() - (needsVbar ? barW : 0.0f));

    _rowPool.reserve(static_cast<size_t>(poolSize));
    for (int i = 0; i < poolSize; ++i) {
        Row* row = new Row();
        row->setSize(math::FVector2(rowW, _itemHeight));
        row->_onClickByRow = [this](int idx, uint32_t mods) {
            handleRowClick(idx, mods);
        };
        addChildExternal(row);
        _rowPool.push_back(row);
    }

    _contentSize = math::FVector2(rowW, _items.size() * _itemHeight);
    _scrollState.setContentSize(_contentSize);
    syncBarToOffset();
    // Map each pool slot to its starting logical item.
    rebindPoolRows();
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
    const bool vbarShown =
        (_vbar != nullptr) && _vbar->isVisible();
    const float barW = vbarShown ? ScrollBar::kDefaultBarWidth : 0.0f;
    const math::FRectangle listBounds(
        bounds.minX, bounds.minY,
        bounds.maxX - barW, bounds.maxY);
    renderer.drawRect(listBounds, bg);
    renderer.drawBorderRect(bounds, border, bw, 2.0f);

    // G2 — rows: iterate the pool, not N items. Each pool slot's
    // local y is fixed (slot * itemHeight); the row's getWorldBounds()
    // walks parents to give the painted y.
    for (Row* r : _rowPool) {
        if (r != nullptr && r->isVisible()) {
            r->render(renderer);
        }
    }
    if (_vbar != nullptr) _vbar->render(renderer);
}

void ListView::handleRowClick(int index, uint32_t mods) {
    if (index < 0 || index >= static_cast<int>(_items.size())) return;
    // G1 — modifier dispatch. Bit positions mirror UIManager::_modifiers
    // (UIKey_Shift - UIKey_Shift = 0, UIKey_Control - UIKey_Shift = 1,
    // UIKey_Alt - UIKey_Shift = 2). See UIManager.cpp:1380.
    const bool shift = (mods & (1u << (UIKey_Shift   - UIKey_Shift))) != 0;
    const bool ctrl  = (mods & (1u << (UIKey_Control - UIKey_Shift))) != 0;

    if (_selectionMode == SelectionMode::Single || (!shift && !ctrl)) {
        // Single mode OR plain click: replace selection with this index
        // (v1 behavior). Anchor moves to clicked index so a subsequent
        // Shift+click extends from here.
        setSelectedIndices({index});
        setAnchorIndex(index);
    } else if (ctrl && !shift) {
        // Ctrl+click: toggle this index in/out of selection without
        // clearing other rows. Anchor always moves to clicked index
        // (so a subsequent Shift+click extends from the new toggle point).
        std::vector<int> v = _selectedIndices;
        auto it = std::find(v.begin(), v.end(), index);
        if (it != v.end()) {
            v.erase(it);
        } else {
            v.push_back(index);
        }
        std::sort(v.begin(), v.end());
        setSelectedIndices(v);
        setAnchorIndex(index);
    } else if (shift) {
        // Shift+click: range select [anchorIndex..index] inclusive.
        // Standard list UX — range REPLACES prior selection (not additive).
        // If no anchor yet (clicked without prior plain/Ctrl+click),
        // fall back to single-select this index.
        const int anchor = (_anchorIndex >= 0 &&
                            _anchorIndex < static_cast<int>(_items.size()))
            ? _anchorIndex
            : index;
        const int lo = std::min(anchor, index);
        const int hi = std::max(anchor, index);
        std::vector<int> range(static_cast<size_t>(hi - lo + 1));
        std::iota(range.begin(), range.end(), lo);
        setSelectedIndices(range);
        _rangeEndIndex = index;
        // Anchor stays put during range select so subsequent Shift+arrows
        // extend from the original click origin.
    }

    // ItemActivated: kept fires-on-single-click for v1 backwards-compat
    // (test listview_enter_key_activates + v1 host expectations). Hosts
    // wanting activate-only-on-explicit-gesture should listen on
    // Enter key (which still calls _onItemActivated via onKeyDown).
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
    // G2 — only mutate if needed. If the row is already visible, no
    // _scrollState.setScrollOffset call → no rebindPoolRows() needed
    // (we'd just re-do the same work).
    bool changed = false;
    if (rowTop < viewTop) {
        _scrollState.setScrollOffset(math::FVector2(0.0f, rowTop));
        changed = true;
    } else if (rowBot > viewBot) {
        _scrollState.setScrollOffset(math::FVector2(
            0.0f, rowBot - vp.y));
        changed = true;
    }
    if (changed) {
        syncBarToOffset();
        rebindPoolRows();
    }
}

Widget* createListViewWidget() {
    return new ListView();
}

bool ListView::onMouseButtonUp(const UIMouseEvent& e) {
    if (e.mouseButton != 0) return false;
    // G2 — walk the pool in reverse draw order and route to the first
    // hit. Pool slots with logical index >= items.size() are hidden
    // (rebindPoolRows sets visible=false) so they're skipped naturally
    // by getWorldBounds.contains.
    for (auto it = _rowPool.rbegin(); it != _rowPool.rend(); ++it) {
        if (*it == nullptr) continue;
        if (!(*it)->isVisible()) continue;
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

    // Pull current modifier bitmask once per keypress so the G1 dispatch
    // below sees a consistent view (UIManager owns the canonical state).
    uint32_t mods = 0;
    if (UIManager* ui = UIManager::tryGet()) {
        mods = ui->getModifiers();
    }
    const bool ctrl  = (mods & (1u << (UIKey_Control - UIKey_Shift))) != 0;

    // Enter activates the current selection (fires _onItemActivated).
    if (keyCode == UIKey_Enter) {
        const int cur = getSelectedIndex();
        if (cur >= 0 && cur < n && _onItemActivated) {
            _onItemActivated(cur);
            return true;
        }
        return false;
    }

    // G1 — Ctrl+A: select all items. Extended mode → all entries; Single
    // mode → first item only (consistent with setSelectedIndex(0) since
    // single-mode can never hold more than one). Both paths use
    // setSelectedIndices so the visual + callback contract is shared.
    if (ctrl && keyCode == UIKey_A) {
        if (_selectionMode == SelectionMode::Extended) {
            std::vector<int> all(n);
            std::iota(all.begin(), all.end(), 0);
            setSelectedIndices(all);
        } else {
            setSelectedIndex(0);
        }
        return true;
    }

    // G1 — Escape: clear selection. Single mode behaves identically
    // (setSelectedIndices({}) is the v1 way to clear too).
    if (keyCode == UIKey_Escape) {
        if (!_selectedIndices.empty()) {
            clearSelection();
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

    // Shift detection: Shift+arrow extends range from anchorIndex.
    // Shift+Home/End extends to first/last. Plain keys collapse to
    // single-element selection (the v1 behavior).
    const bool shift = (mods & (1u << (UIKey_Shift - UIKey_Shift))) != 0;
    const bool wantRange = shift &&
        _selectionMode == SelectionMode::Extended;

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

    if (wantRange) {
        // Extend range from anchorIndex → next.
        const int anchor = (_anchorIndex >= 0 &&
                            _anchorIndex < n) ? _anchorIndex : cur;
        const int lo = std::min(anchor, next);
        const int hi = std::max(anchor, next);
        std::vector<int> range(static_cast<size_t>(hi - lo + 1));
        std::iota(range.begin(), range.end(), lo);
        setSelectedIndices(range);
        // Anchor + range end update for subsequent Shift+arrows.
        _rangeEndIndex = next;
        if (_anchorIndex < 0) {
            _anchorIndex = cur;   // first Shift+arrow seeds anchor at cur
        }
    } else {
        // Plain key OR Single mode: collapse to single-element.
        if (next != _selectedIndex) {
            setSelectedIndex(next);   // already auto-scrolls via scrollToIndex
            // Single-mode / plain arrow updates the anchor so a future
            // Shift+arrow extends from the new position.
            setAnchorIndex(next);
        }
    }
    return true;
}

} // namespace ayt::ui