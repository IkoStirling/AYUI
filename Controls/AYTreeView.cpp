#include "AYUI/TreeView.h"
#include "AYUI/IRenderBackend.h"
#include "AYUI/ScrollBarSync.h"
#include <algorithm>
#include <cassert>
#include <cmath>

namespace ayt::ui {

TreeView::TreeView() {
    setSize(math::FVector2(240.0f, 240.0f));
    // Keep layoutPositionManaged=true so VBox/HBox can place the tree.
    ensureBarCreated();
}

TreeView::~TreeView() {
    // Nodes were added via addChild; CompoundWidget destructor will free
    // them. Null our pointers so the dtor runs cleanly.
    _nodes.clear();
    _vbar = nullptr;
}

void TreeView::setTree(const std::vector<TreeNodeData>& nodes) {
    _source = nodes;
    if (_selectedIndex >= static_cast<int>(_source.size())) {
        _selectedIndex = -1;
    }
    flatten();
    rebuildNodes();
}

void TreeView::clearTree() {
    _source.clear();
    _flatData.clear();
    _selectedIndex = -1;
    for (TreeNode* n : _nodes) {
        if (n != nullptr) {
            removeChild(n);
            delete n;
        }
    }
    _nodes.clear();
}

void TreeView::flatten() {
    _flatData.clear();
    _flatToSrc.clear();
    _pendingDepths.clear();
    _flatData.reserve(_source.size());
    _flatToSrc.reserve(_source.size());
    _pendingDepths.reserve(_source.size());

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
        _pendingDepths.push_back(depth);
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
    // Keep a stable pool of row widgets. setTree() is used by model-driven
    // views and may run every frame; deleting and reallocating every row made
    // an otherwise O(N) flatten pass dominated by allocator churn.
    while (_nodes.size() > _flatData.size()) {
        TreeNode* n = _nodes.back();
        _nodes.pop_back();
        if (n == nullptr) continue;
        removeChild(n);
        delete n;
    }
    while (_nodes.size() < _flatData.size()) {
        TreeNode* node = new TreeNode();
        addChild(node);
        _nodes.push_back(node);
    }

    const float barW = (_vbar != nullptr) ? ScrollBar::kDefaultBarWidth : 0.0f;
    const float rowW = std::max(0.0f, getWidth() - barW);
    for (size_t i = 0; i < _flatData.size(); ++i) {
        TreeNode* node = _nodes[i];
        node->setLabel(_flatData[i].label);
        node->setIcon(_flatData[i].icon);
        node->setHasChildren(_flatData[i].hasChildren);
        node->setExpanded(_flatData[i].expanded);
        // Depth from the parallel handoff vector.
        const int d = (i < _pendingDepths.size()) ? _pendingDepths[i] : 0;
        node->setDepth(d);
        node->_index = static_cast<int>(i);
        node->setSelected(static_cast<int>(i) == _selectedIndex);
        node->setSize(math::FVector2(rowW, _itemHeight));
        node->setOnExpandToggled([this, i](bool) {
            toggleExpand(static_cast<int>(i));
        });
        node->setOnClickByNode([this](int idx) { handleNodeClick(idx); });
    }
    _pendingDepths.clear();

    _contentSize = math::FVector2(rowW, _flatData.size() * _itemHeight);
    syncBarToOffset();
    const float yOff = -_scrollState.getScrollOffset().y;
    for (size_t i = 0; i < _nodes.size(); ++i) {
        if (_nodes[i] != nullptr) {
            _nodes[i]->setPosition(math::FVector2(
                0.0f, static_cast<float>(i) * _itemHeight + yOff));
        }
    }
}

void TreeView::setSelectedIndex(int idx) {
    int clamped = idx;
    if (clamped < -1) clamped = -1;
    if (clamped >= static_cast<int>(_flatData.size())) clamped = -1;
    if (clamped == _selectedIndex) return;
    // Clear old node's selection.
    if (_selectedIndex >= 0 &&
        _selectedIndex < static_cast<int>(_nodes.size()) &&
        _nodes[_selectedIndex] != nullptr) {
        _nodes[_selectedIndex]->setSelected(false);
    }
    _selectedIndex = clamped;
    if (_selectedIndex >= 0 &&
        _selectedIndex < static_cast<int>(_nodes.size()) &&
        _nodes[_selectedIndex] != nullptr) {
        _nodes[_selectedIndex]->setSelected(true);
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
    const float barW = (_vbar != nullptr) ? ScrollBar::kDefaultBarWidth : 0.0f;
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
    if (!_visible) return nullptr;
    const math::FRectangle bounds = getWorldBounds();
    if (!bounds.contains(worldPos)) return nullptr;

    // PR-Container-Contract-Cut2: defer to the shared clipped helper.
    // _vbar is a child and gets first crack via reverse-order walk;
    // the helper's clientRect gate (= getClientRect()) excludes the
    // vbar gutter, so gutter clicks fall through to self.
    return compoundDescendHitTestClipped(this, worldPos);
}

Widget* createTreeViewWidget() { return new TreeView(); }

} // namespace ayt::ui
