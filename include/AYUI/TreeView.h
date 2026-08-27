#pragma once

// =============================================================================
// C-12 TreeView: a scrollable tree of selectable nodes.
// =============================================================================
//
// Architecture (v1 — matches C-5 ListView pattern):
//   TreeView (CompoundWidget)
//     └─ vScrollBar: ScrollBar* (auto-managed)
//     └─ node widgets: TreeNode* x N (one per visible flat-list entry)
//
// Flatten strategy (D1 in C-12 plan):
//   The caller provides TreeNodeData entries with `parentIndex` (-1 for
//   roots) and `expanded` flags. TreeView walks the entries depth-first
//   starting at every root, emitting only visible nodes (children of
//   collapsed parents are skipped). The resulting flat list is what
//   `_nodes` widgets track 1:1.
//
//   Tradeoff: rebuild on every expand/collapse is O(n). Acceptable for
//   editor-chrome datasets (<10k nodes). Future PR adds delta-tracking.
//
// Selection model (mirrors C-5 ListView):
//   - Single mode in v1; _selectedIndex == -1 means no selection.
//   - setSelectedIndex fires _onSelectionChanged when value changes.
//   - setTree replaces the model; selection moves to -1 (we don't try
//     to track selected text — too lossy).
//
// Scroll: same ScrollState + ScrollBar split as ListView (D3 in plan:
//   copy-paste, no shared helper).

#include "AYUI/Widget.h"
#include "AYUI/ScrollBar.h"
#include "AYUI/ScrollableWidget.h"
#include "AYUI/TreeNode.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ayt::ui {

// Flat node metadata used by TreeView to populate widgets. The same struct
// also doubles as the serializer payload — JSON carries these fields.
struct TreeNodeData {
    std::wstring label;
    std::wstring icon;
    bool hasChildren = false;
    bool expanded   = false;
    int  parentIndex = -1;     // -1 for roots; otherwise index into the
                               // source vector passed to setTree().
};

class TreeView : public CompoundWidget {
public:
    TreeView();
    ~TreeView() override;

    // Replace the entire tree. Triggers flatten + rebuild of _nodes.
    // The caller is responsible for keeping TreeNodeData alive for as
    // long as the TreeView references it — we copy into _source, but the
    // caller may want to update _source.expanded and call setTree() again
    // to toggle.
    void setTree(const std::vector<TreeNodeData>& nodes);
    void clearTree();

    // Selection — single mode in v1.
    int  getSelectedIndex() const { return _selectedIndex; }
    void setSelectedIndex(int idx);
    void setOnSelectionChanged(std::function<void(int)> cb) {
        _onSelectionChanged = std::move(cb);
    }

    // Toggle expand state on a flat index. Calls setTree() internally.
    void toggleExpand(int flatIndex);
    void setOnExpandToggled(std::function<void(int, bool)> cb) {
        _onExpandToggled = std::move(cb);
    }

    // Layout knobs
    void  setItemHeight(float h) { _itemHeight = h; markBoundsDirty(); }
    float getItemHeight() const { return _itemHeight; }

    // Scroll
    const math::FVector2& getScrollOffset() const {
        return _scrollState.getScrollOffset();
    }
    void setScrollOffset(const math::FVector2& offset);

    ScrollBar* getVerticalScrollBar() const { return _vbar; }

    void performLayout() override;
    void onRender(IRenderBackend& renderer) override;
    // PR-Container-Contract-Cut2: fill the empty renderChildren stub +
    // add hitTest override. Same contract as the other 3 containers.
    void renderChildren(IRenderBackend& renderer) override;
    Widget* hitTest(const math::FVector2& worldPos) override;

    // PR-Container-Shared-Contract: client rect = world bounds minus
    // vbar width. Used by hit-test gating if/when TreeView overrides
    // hitTest; for now it documents the visible area consistently.
    math::FRectangle getClientRect() const override;

    // Forward clicks to the node under the cursor.
    bool onMouseButtonUp(const UIMouseEvent& e) override;

    size_t getNodeCount() const { return _flatData.size(); }
    const TreeNodeData& getNodeData(size_t i) const { return _flatData[i]; }

protected:
    void rebuildNodes();

private:
    void flatten();
    void ensureBarCreated();
    void syncBarToOffset();
    void handleNodeClick(int flatIndex);

    std::vector<TreeNodeData> _source;          // caller-supplied model
    std::vector<TreeNodeData> _flatData;        // visible flat list (after flatten)
    std::vector<TreeNode*>    _nodes;           // matching widgets (owned via addChild)
    std::vector<int>          _flatToSrc;       // flat index → source index (1:1)
    std::vector<int>          _pendingDepths;   // depth per flat entry (rebuilt each flatten)

    // AYUI-Perf-2026-08-26: parentIndex → [child indices] map rebuilt
    // once per setTree() so flatten()'s DFS does an O(1) lookup instead
    // of an O(N) parentIndex scan inside every node's child loop. The
    // pre-fix code was O(N^2) overall; with this map it drops to O(N).
    std::unordered_map<int, std::vector<int>> _childrenByParent;

    int  _selectedIndex = -1;
    float _itemHeight = 16.0f;

    ScrollBar*       _vbar = nullptr;
    ScrollableWidget _scrollState;
    math::FVector2   _contentSize{0.0f, 0.0f};

    std::function<void(int)> _onSelectionChanged;
    std::function<void(int, bool)> _onExpandToggled;
};

Widget* createTreeViewWidget();

} // namespace ayt::ui