#include "AYUI/TreeView.h"
#include "AYUI/IRenderBackend.h"
#include "AYUI/ScrollBarSync.h"
#include "AYUI/UIManager.h"
#include "AYUI/VirtualList.h"
#include <algorithm>
#include <cassert>
#include <cmath>

namespace ayt::ui {

TreeView::TreeView() {
    setSize(math::FVector2(240.0f, 240.0f));
    // Keep layoutPositionManaged=true so VBox/HBox can place the tree.
    ensureBarCreated();
    _vbar->setVisible(false);
}

TreeView::~TreeView() {
    // R3 landmine: nodes were added via addChild; CompoundWidget dtor
    // frees them. BEFORE that, drop any UIManager transient pointers
    // that may be tracking this TreeView (or its scrollbar) so the
    // next updateHoverWidget / setFocus call doesn't dispatch into
    // a destroyed widget. Pattern mirrors TileView::~TileView, the
    // other container widget that owns child rows — TreeView was the
    // only sibling missing these calls (audit H-1).
    if (UIManager* ui = UIManager::tryGet()) {
        ui->clearHoverNoDispatch(this);
        ui->clearCaptureNoDispatch(this);
        ui->clearFocusNoDispatch(this);
        if (_vbar != nullptr) {
            ui->clearHoverNoDispatch(_vbar);
            ui->clearCaptureNoDispatch(_vbar);
            ui->clearFocusNoDispatch(_vbar);
        }
        for (TreeNode* node : _nodes) {
            if (node == nullptr) continue;
            ui->clearHoverNoDispatch(node);
            ui->clearCaptureNoDispatch(node);
            ui->clearFocusNoDispatch(node);
        }
    }
    _nodes.clear();
    _vbar = nullptr;
}

void TreeView::setTree(const std::vector<TreeNodeData>& nodes) {
    _source = nodes;
    flatten();
    if (_selectedIndex >= static_cast<int>(_flatData.size())) {
        _selectedIndex = -1;
    }
    rebuildNodes();
}

void TreeView::clearTree() {
    _source.clear();
    _flatData.clear();
    _flatToSrc.clear();
    _flatDepths.clear();
    _selectedIndex = -1;
    rebuildNodes();
}

void TreeView::flatten() {
    _flatData.clear();
    _flatToSrc.clear();
    _flatDepths.clear();
    _flatData.reserve(_source.size());
    _flatToSrc.reserve(_source.size());
    _flatDepths.reserve(_source.size());

    // AYUI-Perf-2026-08-26: precompute the parent → [child indices]
    // map once per setTree() so the DFS below does O(1) child lookup
    // instead of the previous O(N) parentIndex scan per node. The
    // pre-fix code was O(N^2) overall; this map drops the inner loop
    // to O(children-of-current), which is O(N) summed across the
    // whole tree (each node has exactly one parent).
    _childrenByParent.clear();
    _childrenByParent.reserve(_source.size());
    for (int i = 0; i < static_cast<int>(_source.size()); ++i) {
        const int p = _source[i].parentIndex;
        // Roots (parentIndex == -1) share a synthetic bucket — using
        // the literal -1 key keeps the DFS loop generic.
        _childrenByParent[p].push_back(i);
    }

    // Depth-first walk over root nodes (-1 = root). Within each node,
    // recurse into children via the precomputed map.
    // _maxDepth guard keeps stack bounded for pathological inputs.
    constexpr int kMaxDepth = 64;

    std::function<void(int, int)> visit = [&](int srcIdx, int depth) {
        assert(depth <= kMaxDepth);   // pathological tree — bail loudly
        if (srcIdx < 0 || srcIdx >= static_cast<int>(_source.size())) return;
        _flatData.push_back(_source[srcIdx]);
        _flatToSrc.push_back(srcIdx);
        _flatDepths.push_back(depth);
        if (_source[srcIdx].expanded) {
            // AYUI-Perf-2026-08-26: O(1) child lookup via the
            // precomputed parent→children map. The pre-fix code did an
            // O(N) parentIndex scan here, making the whole flatten()
            // call O(N^2) on a balanced tree.
            const auto it = _childrenByParent.find(srcIdx);
            if (it != _childrenByParent.end()) {
                for (int childIdx : it->second) {
                    visit(childIdx, depth + 1);
                }
            }
        }
    };

    // AYUI-Perf-2026-08-26: roots bucket key is -1 (TreeNodeData::parentIndex
    // sentinel). Iterate via the same map so the loop is symmetric with
    // the inner recursion.
    const auto rootsIt = _childrenByParent.find(-1);
    if (rootsIt != _childrenByParent.end()) {
        for (int rootIdx : rootsIt->second) {
            visit(rootIdx, 0);
        }
    }
}

void TreeView::rebuildNodes() {
    const float contentH = static_cast<float>(_flatData.size()) * _itemHeight;
    const bool showVbar = contentH > getHeight() + 0.5f;
    if (_vbar != nullptr) _vbar->setVisible(showVbar);
    const float barW = showVbar ? ScrollBar::kDefaultBarWidth : 0.0f;
    const float rowW = std::max(0.0f, getWidth() - barW);
    _contentSize = math::FVector2(rowW, contentH);
    _scrollState.setContentSize(_contentSize);

    const math::FVector2 clamped = ScrollableWidget::clampScrollOffset(
        _scrollState.getScrollOffset(),
        math::FVector2(getWidth(), getHeight()), _contentSize);
    _scrollState.setScrollOffset(clamped);

    const size_t needed = static_cast<size_t>(computePoolSize());
    while (_nodes.size() > needed) {
        TreeNode* n = _nodes.back();
        _nodes.pop_back();
        if (n == nullptr) continue;
        if (UIManager* ui = UIManager::tryGet()) {
            ui->clearHoverNoDispatch(n);
            ui->clearCaptureNoDispatch(n);
            ui->clearFocusNoDispatch(n);
        }
        removeChild(n);
        delete n;
    }
    while (_nodes.size() < needed) {
        TreeNode* node = new TreeNode();
        addChild(node);
        _nodes.push_back(node);
    }
    syncBarToOffset();
    rebindNodes();
}

void TreeView::setItemHeight(float h) {
    const float next = std::max(1.0f, h);
    if (std::fabs(next - _itemHeight) <= 1e-5f) return;
    _itemHeight = next;
    rebuildNodes();
    markBoundsDirty();
}

int TreeView::getNodePoolLogicalIndex(size_t slot) const {
    if (slot >= _nodes.size() || _nodes[slot] == nullptr) return -1;
    return _nodes[slot]->_index;
}

void TreeView::setSelectedIndex(int idx) {
    int clamped = idx;
    if (clamped < -1) clamped = -1;
    if (clamped >= static_cast<int>(_flatData.size())) clamped = -1;
    if (clamped == _selectedIndex) return;
    // Clear old node's selection.
    if (TreeNode* old = nodeForLogical(_selectedIndex)) {
        old->setSelected(false);
    }
    _selectedIndex = clamped;
    if (TreeNode* selected = nodeForLogical(_selectedIndex)) {
        selected->setSelected(true);
    }
    if (_onSelectionChanged) _onSelectionChanged(_selectedIndex);
}

void TreeView::toggleExpand(int flatIndex) {
    if (flatIndex < 0 || flatIndex >= static_cast<int>(_flatData.size())) return;
    if (flatIndex >= static_cast<int>(_flatToSrc.size())) return;
    const int srcIdx = _flatToSrc[flatIndex];
    if (srcIdx < 0 || srcIdx >= static_cast<int>(_source.size())) return;
    _source[srcIdx].expanded = !_source[srcIdx].expanded;
    flatten();
    if (_selectedIndex >= static_cast<int>(_flatData.size())) {
        _selectedIndex = -1;
    }
    rebuildNodes();
    if (_onExpandToggled) {
        _onExpandToggled(flatIndex, _source[srcIdx].expanded);
    }
}

void TreeView::setScrollOffset(const math::FVector2& offset) {
    // PR-Container-Shared-Contract: clamp through the shared pure
    // function. TreeView tracks _contentSize in its own field (set
    // during rebuildNodes), not inside _scrollState — same pattern as
    // Window. scrollBy() would clamp against _scrollState._contentSize
    // which is always (0,0) here; clampScrollOffset takes the content
    // size as a parameter, so it works regardless of which container
    // owns the field.
    const math::FVector2 vp(getWidth(), getHeight());
    const math::FVector2 clamped =
        ScrollableWidget::clampScrollOffset(offset, vp, _contentSize);
    if (std::fabs(clamped.x - _scrollState.getScrollOffset().x) > 1e-5f ||
        std::fabs(clamped.y - _scrollState.getScrollOffset().y) > 1e-5f) {
        _scrollState.setScrollOffset(clamped);
        syncBarToOffset();
        rebindNodes();
        markDirty();
    }
}

bool TreeView::scrollBy(float deltaY) {
    const math::FVector2 vp(getWidth(), getHeight());
    if (!_scrollState.scrollBy(math::FVector2(0.0f, deltaY), vp)) {
        return false;
    }
    syncBarToOffset();
    rebindNodes();
    markDirty();
    return true;
}

bool TreeView::onMouseWheel(const UIMouseWheelEvent& e) {
    const math::FVector2 vp(getWidth(), getHeight());
    const bool changed = _scrollState.applyWheel(
        math::FVector2(0.0f, e.deltaY), vp);
    if (changed) {
        syncBarToOffset();
        rebindNodes();
        markDirty();
    }
    return changed;
}

void TreeView::tick(float dt) {
    CompoundWidget::tick(dt);
    math::FVector2 delta;
    if (_scrollState.advanceMomentum(
            dt, math::FVector2(getWidth(), getHeight()), delta)) {
        scrollBy(delta.y);
    }
}

void TreeView::ensureBarCreated() {
    if (_vbar != nullptr) return;
    _vbar = new ScrollBar();
    _vbar->setOrientation(ScrollBar::Orientation::Vertical);
    _vbar->setOnValueChanged([this](float v) {
        setScrollOffset(math::FVector2(_scrollState.getScrollOffset().x, v));
    });
    addChild(_vbar);
}

void TreeView::syncBarToOffset() {
    // PR-SyncVerticalBar: helper handles null-bar guard + locked order.
    // TreeView passes getHeight() directly as viewport size (the vbar
    // sits inside TreeView bounds; no clientRect offset to subtract).
    syncVerticalBar(_vbar, _contentSize.y, getHeight(),
                    _scrollState.getScrollOffset().y);
}

int TreeView::computePoolSize() const {
    return computeVirtualListWindow(
        _flatData.size(), getHeight(), _itemHeight,
        _scrollState.getScrollOffset().y).poolSize;
}

TreeNode* TreeView::nodeForLogical(int index) const {
    if (index < _firstVisibleIndex) return nullptr;
    const int slot = index - _firstVisibleIndex;
    if (slot < 0 || slot >= static_cast<int>(_nodes.size())) return nullptr;
    TreeNode* node = _nodes[static_cast<size_t>(slot)];
    return node != nullptr && node->_index == index ? node : nullptr;
}

void TreeView::rebindNodes() {
    if (_nodes.empty() || _flatData.empty()) {
        for (TreeNode* node : _nodes) {
            if (node != nullptr) node->setVisible(false);
        }
        _firstVisibleIndex = 0;
        return;
    }

    const VirtualListWindow window = computeVirtualListWindow(
        _flatData.size(), getHeight(), _itemHeight,
        _scrollState.getScrollOffset().y);
    _firstVisibleIndex = window.firstIndex;
    const bool showVbar = _vbar != nullptr && _vbar->isVisible();
    const float rowW = std::max(0.0f, getWidth()
        - (showVbar ? ScrollBar::kDefaultBarWidth : 0.0f));

    for (size_t slot = 0; slot < _nodes.size(); ++slot) {
        TreeNode* node = _nodes[slot];
        if (node == nullptr) continue;
        const int logical = _firstVisibleIndex + static_cast<int>(slot);
        if (logical >= static_cast<int>(_flatData.size())) {
            node->setVisible(false);
            node->_index = -1;
            continue;
        }

        const TreeNodeData& data = _flatData[static_cast<size_t>(logical)];
        node->setVisible(true);
        node->setLabel(data.label);
        node->setIcon(data.icon);
        node->setHasChildren(data.hasChildren);
        node->setOnExpandToggled({});
        node->setExpanded(data.expanded);
        node->setDepth(static_cast<size_t>(logical) < _flatDepths.size()
            ? _flatDepths[static_cast<size_t>(logical)] : 0);
        node->_index = logical;
        node->setSelected(logical == _selectedIndex);
        node->setSize(math::FVector2(rowW, _itemHeight));
        node->setPosition(math::FVector2(
            0.0f, static_cast<float>(slot) * _itemHeight
                - window.leadingOffset));
        node->setOnExpandToggled([this, logical](bool) {
            toggleExpand(logical);
        });
        node->setOnClickByNode([this](int idx) { handleNodeClick(idx); });
    }
}

void TreeView::performLayout() {
    const float barW = ScrollBar::kDefaultBarWidth;
    if (_vbar != nullptr) {
        _vbar->setPosition(math::FVector2(getWidth() - barW, 0.0f));
        _vbar->setSize(math::FVector2(barW, getHeight()));
    }
    rebuildNodes();
    layoutChildren();
}

math::FRectangle TreeView::getClientRect() const {
    // PR-Container-Shared-Contract: world bounds minus vbar width. Matches
    // the listBounds used in onRender() above and in hitTestSlot on
    // DockArea. Single source of truth for the visible (clipped) area.
    const math::FRectangle b = getWorldBounds();
    const float barW = (_vbar != nullptr && _vbar->isVisible())
        ? ScrollBar::kDefaultBarWidth : 0.0f;
    return math::FRectangle(b.minX, b.minY, b.maxX - barW, b.maxY);
}

void TreeView::onRender(IRenderBackend& renderer) {
    math::FRectangle bounds = getWorldBounds();
    if (bounds.maxX <= bounds.minX || bounds.maxY <= bounds.minY) return;

    // Background.
    renderer.drawRect(bounds, math::FVector4(0.12f, 0.12f, 0.13f, 1.0f));
    renderer.drawBorderRect(bounds,
        math::FVector4(0.45f, 0.45f, 0.5f, 1.0f), 1.0f);

    // PR-Container-Shared-Contract: same pushClip/popClip pattern as
    // ListView.cpp:582-588. The previous "implicit clip via draw order
    // + parent world bounds" comment was wrong — fractional scroll
    // offset leaves partial nodes outside the viewport that painted
    // over chrome. Clip via getClientRect() so only visible nodes draw.
    const math::FRectangle listBounds = getClientRect();
    renderer.pushClip(listBounds);
    for (TreeNode* n : _nodes) {
        if (n != nullptr && n->isVisible()) {
            n->render(renderer);
        }
    }
    renderer.popClip();
    if (_vbar != nullptr) _vbar->render(renderer);
}

bool TreeView::onMouseButtonUp(const UIMouseEvent& e) {
    if (e.mouseButton != 0) return false;
    if (!getClientRect().contains(e.mousePos)) return false;
    for (auto it = _nodes.rbegin(); it != _nodes.rend(); ++it) {
        if (*it == nullptr) continue;
        if ((*it)->getWorldBounds().contains(e.mousePos)) {
            return (*it)->onMouseButtonUp(e);
        }
    }
    return false;
}

void TreeView::handleNodeClick(int flatIndex) {
    setSelectedIndex(flatIndex);
}

void TreeView::renderChildren(IRenderBackend& renderer) {
    // PR-Container-Contract-Cut2: defense-in-depth helper. onRender
    // above already paints nodes inside a pushClip; this override
    // catches any add-child-after-cut2 call (e.g. a debug overlay)
    // so it renders inside the clientRect clip rather than leaking
    // onto the vbar gutter. _vbar is excluded so it stays painted
    // after popClip in onRender.
    compoundDescendClippedRender(this, renderer, {_vbar});
}

Widget* TreeView::hitTest(const math::FVector2& worldPos) {
    if (!isVisible()) return nullptr;
    const math::FRectangle bounds = getWorldBounds();
    if (!bounds.contains(worldPos)) return nullptr;

    // Scrollbar chrome sits outside getClientRect(), so it must get first
    // crack before the clipped child descent.
    if (_vbar != nullptr && _vbar->isVisible()) {
        if (Widget* hit = _vbar->hitTest(worldPos)) {
            return hit;
        }
    }
    return compoundDescendHitTestClipped(this, worldPos);
}

Widget* createTreeViewWidget() { return new TreeView(); }

} // namespace ayt::ui
