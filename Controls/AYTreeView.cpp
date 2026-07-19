#include "AYTreeView.h"
#include "IAYRenderBackend.h"
#include <algorithm>
#include <cassert>

namespace ayt::ui {

TreeView::TreeView() {
    setSize(math::FVector2(240.0f, 240.0f));
    setLayoutPositionManaged(false);
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

    // Depth-first walk over root nodes (-1 = root). Within each node,
    // recurse into children — we identify children by parentIndex match.
    // _maxDepth guard keeps stack bounded for pathological inputs.
    constexpr int kMaxDepth = 64;

    std::function<void(int, int)> visit = [&](int srcIdx, int depth) {
        assert(depth <= kMaxDepth);   // pathological tree — bail loudly
        if (srcIdx < 0 || srcIdx >= static_cast<int>(_source.size())) return;
        _flatData.push_back(_source[srcIdx]);
        _flatToSrc.push_back(srcIdx);
        _pendingDepths.push_back(depth);
        if (_source[srcIdx].expanded) {
            for (int i = 0; i < static_cast<int>(_source.size()); ++i) {
                if (_source[i].parentIndex == srcIdx) {
                    visit(i, depth + 1);
                }
            }
        }
    };

    for (int i = 0; i < static_cast<int>(_source.size()); ++i) {
        if (_source[i].parentIndex == -1) {
            visit(i, 0);
        }
    }
}

void TreeView::rebuildNodes() {
    // Tear down old node widgets.
    for (TreeNode* n : _nodes) {
        if (n == nullptr) continue;
        removeChild(n);
        delete n;
    }
    _nodes.clear();

    const float barW = (_vbar != nullptr) ? ScrollBar::kDefaultBarWidth : 0.0f;
    const float rowW = std::max(0.0f, getWidth() - barW);
    _nodes.reserve(_flatData.size());
    for (size_t i = 0; i < _flatData.size(); ++i) {
        TreeNode* node = new TreeNode();
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
        addChild(node);
        _nodes.push_back(node);
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
    const math::FVector2 vp(getWidth(), getHeight());
    const float maxX = (_contentSize.x > vp.x) ? (_contentSize.x - vp.x) : 0.0f;
    const float maxY = (_contentSize.y > vp.y) ? (_contentSize.y - vp.y) : 0.0f;
    math::FVector2 clamped(offset);
    if (clamped.x < 0.0f) clamped.x = 0.0f;
    if (clamped.x > maxX) clamped.x = maxX;
    if (clamped.y < 0.0f) clamped.y = 0.0f;
    if (clamped.y > maxY) clamped.y = maxY;
    _scrollState.setScrollOffset(clamped);
    syncBarToOffset();
}

void TreeView::ensureBarCreated() {
    if (_vbar != nullptr) return;
    _vbar = new ScrollBar();
    _vbar->setOrientation(ScrollBar::Orientation::Vertical);
    _vbar->setOnValueChanged([this](float v) {
        const float maxOff = (_contentSize.y - getHeight());
        if (maxOff <= 0.0f) return;
        _scrollState.setScrollOffset(math::FVector2(
            _scrollState.getScrollOffset().x, v));
    });
    addChild(_vbar);
}

void TreeView::syncBarToOffset() {
    if (_vbar == nullptr) return;
    _vbar->setRange(0.0f, _contentSize.y);
    _vbar->setViewportSize(getHeight());
    _vbar->setValue(_scrollState.getScrollOffset().y);
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

void TreeView::onRender(IRenderBackend& renderer) {
    math::FRectangle bounds = getWorldBounds();
    if (bounds.maxX <= bounds.minX || bounds.maxY <= bounds.minY) return;

    // Background.
    renderer.drawRect(bounds, math::FVector4(0.12f, 0.12f, 0.13f, 1.0f));
    renderer.drawBorderRect(bounds,
        math::FVector4(0.45f, 0.45f, 0.5f, 1.0f), 1.0f);

    // Nodes (scroll-clipped implicitly by draw order + parent world bounds).
    for (TreeNode* n : _nodes) {
        if (n != nullptr && n->isVisible()) {
            n->render(renderer);
        }
    }
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

Widget* createTreeViewWidget() { return new TreeView(); }

} // namespace ayt::ui