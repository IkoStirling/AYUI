#include "AYTest.h"
#include "AYUI/TreeView.h"
#include "AYUI/TreeNode.h"
#include "AYUI/UIManager.h"
#include "AYUI/WidgetFactory.h"
#include "AYUI/WidgetSerializer.h"
#include "AYUI/MockRenderer.h"
#include <iostream>
#include <sstream>

using namespace ayt::ui;
using namespace ayt::math;

namespace {

// Build a 2-level tree:
//   root (idx 0, expanded) ──── child1 (idx 1)
//                            └── child2 (idx 2)
//   root2 (idx 3, collapsed) ──── grandchild (idx 4)
std::vector<TreeNodeData> makeSampleTree() {
    std::vector<TreeNodeData> v;
    v.push_back({L"root",   L">", true,  true,  -1});   // 0
    v.push_back({L"child1", L".", false, false,  0});   // 1
    v.push_back({L"child2", L".", false, false,  0});   // 2
    v.push_back({L"root2",  L">", true,  false, -1});   // 3
    v.push_back({L"grand",  L".", false, false,  3});   // 4
    return v;
}

} // namespace

TEST_SUITE(AYUI_TreeView)

TEST_CASE(treeview_initial_state) {
    TreeView tv;
    CHECK(tv.getNodeCount() == 0u);
    CHECK(tv.getSelectedIndex() == -1);
    CHECK(tv.getVerticalScrollBar() != nullptr);
    CHECK(tv.getNodePoolSize() == 0u);
}

TEST_CASE(treeview_set_tree_flattens_visible) {
    TreeView tv;
    tv.setTree(makeSampleTree());
    // root(0) expanded: emits 0, 1, 2. root2(3) collapsed: emits only 3.
    // grand(4) skipped (parent collapsed).
    CHECK(tv.getNodeCount() == 4u);
    CHECK(tv.getNodeData(0).label == L"root");
    CHECK(tv.getNodeData(1).label == L"child1");
    CHECK(tv.getNodeData(2).label == L"child2");
    CHECK(tv.getNodeData(3).label == L"root2");
}

TEST_CASE(treeview_collapse_skips_children) {
    std::vector<TreeNodeData> t = makeSampleTree();
    t[0].expanded = false;   // collapse root
    TreeView tv;
    tv.setTree(t);
    CHECK(tv.getNodeCount() == 2u);   // root(0), root2(3)
}

TEST_CASE(treeview_expand_includes_children) {
    std::vector<TreeNodeData> t = makeSampleTree();
    t[3].expanded = true;   // expand root2 → grand included
    TreeView tv;
    tv.setTree(t);
    CHECK(tv.getNodeCount() == 5u);
    CHECK(tv.getNodeData(4).label == L"grand");
}

TEST_CASE(treeview_model_refresh_does_not_emit_expand_or_reenter) {
    TreeView tv;
    int expandEvents = 0;
    tv.setOnExpandToggled(
        [&](int, bool) { ++expandEvents; });

    // Both pool slots initially represent expanded roots.
    tv.setTree({
        {L"Assets", L"folder", true, true, -1},
        {L"Imported", L"folder", false, true, -1},
    });

    // Inserting a collapsed child at slot 1 reuses the widget that previously
    // represented the expanded Imported root. This is model synchronisation,
    // not a user expand/collapse action, and must not dispatch callbacks.
    tv.setTree({
        {L"Assets", L"folder", true, true, -1},
        {L"Models", L"folder", false, false, 0},
        {L"Imported", L"folder", false, true, -1},
    });

    CHECK(tv.getNodeCount() == 3u);
    CHECK(tv.getNodeData(1).label == L"Models");
    CHECK(expandEvents == 0);
}

TEST_CASE(treeview_selection_via_row_click) {
    TreeView tv;
    tv.setTree(makeSampleTree());
    CHECK(tv.getSelectedIndex() == -1);
    tv.setSelectedIndex(1);
    CHECK(tv.getSelectedIndex() == 1);
    tv.setSelectedIndex(99);   // out of range
    CHECK(tv.getSelectedIndex() == -1);   // clamped back

    int firedTo = -1;
    tv.setOnSelectionChanged([&](int idx) { firedTo = idx; });
    tv.setSelectedIndex(2);
    CHECK(firedTo == 2);
}

TEST_CASE(tree_node_selected_band_preserves_hierarchy_indent) {
    TreeNode node;
    node.setPosition(FVector2(10.0f, 20.0f));
    node.setSize(FVector2(200.0f, 18.0f));
    node.setDepth(2);
    node.setLabel(L"grandchild");
    node.setSelected(true);

    MockRenderer renderer;
    node.onRender(renderer);

    const auto& calls = renderer.getDrawCalls();
    CHECK(calls.size() == 2u);
    if (calls.size() != 2u) return;

    CHECK(calls[0].type == MockRenderer::DrawCall::Rect);
    CHECK_FLOAT_EQ(calls[0].bounds.minX,
                   10.0f + 2.0f * TreeNode::kIndentPx, 1e-5f);
    CHECK_FLOAT_EQ(calls[0].bounds.maxX, 210.0f, 1e-5f);

    // Selection decoration must not disturb the existing text prefix:
    // depth indent + disclosure column + icon column.
    CHECK(calls[1].type == MockRenderer::DrawCall::Text);
    CHECK_FLOAT_EQ(calls[1].bounds.minX,
                   10.0f + 2.0f * TreeNode::kIndentPx
                       + TreeNode::kArrowColPx + TreeNode::kIconColPx,
                   1e-5f);
}

TEST_CASE(treeview_scroll_offset_updates_bar) {
    TreeView tv;
    tv.setSize(FVector2(240.0f, 32.0f));   // height = 2 rows visible
    tv.setItemHeight(16.0f);
    tv.setTree(makeSampleTree());
    // 4 nodes * 16 = 64 px content; viewport = 32 px; max scroll = 32.
    CHECK(tv.getScrollOffset().y == 0.0f);
    tv.setScrollOffset(FVector2(0.0f, 16.0f));
    CHECK(tv.getScrollOffset().y == 16.0f);
    // PR-SyncVerticalBar: setScrollOffset calls syncBarToOffset on the
    // change path — bar value tracks scroll offset.
    CHECK(tv.getVerticalScrollBar()->getValue() == tv.getScrollOffset().y);
    tv.setScrollOffset(FVector2(0.0f, 9999.0f));   // clamp
    CHECK(tv.getScrollOffset().y <= 32.0f);
}

TEST_CASE(treeview_vbar_auto_hides_and_wheel_scrolls_visible_rows) {
    TreeView tv;
    tv.setSize(FVector2(120.0f, 32.0f));
    tv.setItemHeight(16.0f);

    // Empty/fitting trees retain the managed bar object but reserve no
    // gutter, matching ScrollView/ListView Auto visibility.
    tv.performLayout();
    CHECK_NOT_NULL(tv.getVerticalScrollBar());
    CHECK_FALSE(tv.getVerticalScrollBar()->isVisible());
    CHECK_FLOAT_EQ(tv.getClientRect().maxX, 120.0f, 1e-5f);

    tv.setTree(makeSampleTree());  // 4 visible rows = 64px > 32px
    tv.performLayout();
    CHECK(tv.getVerticalScrollBar()->isVisible());
    CHECK_FLOAT_EQ(tv.getClientRect().maxX,
                   120.0f - ScrollBar::kDefaultBarWidth, 1e-5f);

    // Scrollbar chrome must be directly hit-testable in its gutter.
    const FRectangle barBounds = tv.getVerticalScrollBar()->getWorldBounds();
    Widget* gutterHit = tv.hitTest(FVector2(
        (barBounds.minX + barBounds.maxX) * 0.5f,
        (barBounds.minY + barBounds.maxY) * 0.5f));
    CHECK(gutterHit == tv.getVerticalScrollBar());

    // Wheel input moves the shared scroll state and the actual node widgets.
    TreeNode* first = nullptr;
    for (Widget* child : tv.getChildren()) {
        if (auto* node = dynamic_cast<TreeNode*>(child)) {
            first = node;
            break;
        }
    }
    CHECK_NOT_NULL(first);
    const float beforeY = first->getPosition().y;
    CHECK(tv.onMouseWheel(UIMouseWheelEvent(FVector2(20.0f, 16.0f), 16.0f)));
    CHECK_FLOAT_EQ(tv.getScrollOffset().y, 16.0f, 1e-5f);
    CHECK_FLOAT_EQ(first->getPosition().y, beforeY - 16.0f, 1e-5f);

    // Shrinking content hides the bar, restores full width and clamps offset.
    tv.clearTree();
    CHECK_FALSE(tv.getVerticalScrollBar()->isVisible());
    CHECK_FLOAT_EQ(tv.getScrollOffset().y, 0.0f, 1e-5f);
    CHECK_FLOAT_EQ(tv.getClientRect().maxX, 120.0f, 1e-5f);
}

TEST_CASE(treeview_factory_and_serializer_roundtrip) {
    WidgetFactory& factory = WidgetFactory::get();
    CHECK_TRUE(factory.isRegistered("TreeNode"));
    CHECK_TRUE(factory.isRegistered("TreeView"));

    Widget* rawTv = factory.create("TreeView");
    CHECK_NOT_NULL(rawTv);
    TreeView* tv = dynamic_cast<TreeView*>(rawTv);
    CHECK_NOT_NULL(tv);
    tv->setTree(makeSampleTree());
    tv->setSelectedIndex(1);

    WidgetSerializer ser;
    const std::string j = ser.serialize(tv, false);
    CHECK(j.find("\"TreeView\"") != std::string::npos);
    CHECK(j.find("\"root\"") != std::string::npos);

    // Round-trip — parse the JSON back and verify fields landed.
    Widget* restored = ser.deserialize(j);
    CHECK_NOT_NULL(restored);
    TreeView* tv2 = dynamic_cast<TreeView*>(restored);
    CHECK_NOT_NULL(tv2);
    CHECK(tv2->getNodeCount() == 4u);
    CHECK(tv2->getSelectedIndex() == 1);

    destroyWidgetTree(rawTv);
    destroyWidgetTree(restored);
}

// AYUI-Perf-2026-08-26: regression test for the O(N^2) → O(N) flatten
// optimization. Builds a 1000-node tree, times flatten + rebuildNodes
// repeatedly, and asserts the wall-clock is below a generous bound.
// The pre-fix code did ~1M parentIndex comparisons; the post-fix code
// does 1K map insertions + 1K map lookups.
TEST_CASE(treeview_flatten_1000_nodes_under_5ms) {
    // Build a 1000-node tree with every node expanded. Shape: 1 root,
    // 999 children of the root (parentIndex=0). All expanded so
    // flatten() walks every node — exercises the worst case for the
    // child-lookup inner loop.
    std::vector<TreeNodeData> big;
    big.reserve(1000);
    TreeNodeData root;
    root.label = L"root";
    root.hasChildren = true;
    root.expanded = true;
    root.parentIndex = -1;
    big.push_back(root);
    for (int i = 1; i < 1000; ++i) {
        TreeNodeData n;
        n.label = L"n";
        n.hasChildren = false;
        n.expanded = false;
        n.parentIndex = 0;  // all children of root
        big.push_back(n);
    }

    TreeView tv;
    tv.setTree(big);
    CHECK(tv.getNodeCount() == 1000u);

    TreeNode* firstNodeBefore = nullptr;
    for (Widget* child : tv.getChildren()) {
        if (auto* node = dynamic_cast<TreeNode*>(child)) {
            firstNodeBefore = node;
            break;
        }
    }
    CHECK(firstNodeBefore != nullptr);
    tv.setTree(big);
    TreeNode* firstNodeAfter = nullptr;
    for (Widget* child : tv.getChildren()) {
        if (auto* node = dynamic_cast<TreeNode*>(child)) {
            firstNodeAfter = node;
            break;
        }
    }
    CHECK(firstNodeAfter == firstNodeBefore);

    // Hammer flatten + rebuildNodes 50 times — pre-fix this would
    // total ~50M parentIndex comparisons. Post-fix: ~50K map ops.
    // The loose bound covers debug CI while still catching a return to
    // rebuilding 50,000 row allocations.
    double start = ayt::test::getTimeMs();
    for (int i = 0; i < 50; ++i) {
        tv.setTree(big);
    }
    double elapsed = ayt::test::getTimeMs() - start;
    const double averageMs = elapsed / 50.0;
    std::printf("         treeview_flatten_1000_nodes: %.3f ms (50 iterations)\n",
                elapsed);
    // Match the test name: enforce the per-iteration 5ms budget rather
    // than an accidental 2ms budget derived from the aggregate duration.
    CHECK(averageMs < 5.0);  // pre-fix was ~5-10x slower
}

TEST_CASE(treeview_flatten_balanced_1000_nodes) {
    // Balanced tree: 10 roots, each with 99 children. All expanded.
    // Exercises both the root iteration AND the recursive child
    // descent. Pre-fix this was the worst-case for the parentIndex
    // scan (every leaf scanned the full 1000-vector).
    std::vector<TreeNodeData> big;
    big.reserve(1000);
    for (int i = 0; i < 10; ++i) {
        TreeNodeData r;
        r.label = L"r";
        r.hasChildren = true;
        r.expanded = true;
        r.parentIndex = -1;
        big.push_back(r);
    }
    for (int i = 10; i < 1000; ++i) {
        TreeNodeData n;
        n.label = L"n";
        n.hasChildren = false;
        n.expanded = false;
        n.parentIndex = (i - 10) / 99;  // spread across 10 roots
        big.push_back(n);
    }

    TreeView tv;
    tv.setTree(big);
    CHECK(tv.getNodeCount() == 1000u);
}

TEST_CASE(treeview_pool_only_keeps_visible_rows_and_rebinds_on_scroll) {
    std::vector<TreeNodeData> nodes;
    nodes.reserve(5000);
    for (int i = 0; i < 5000; ++i) {
        nodes.push_back({L"node " + std::to_wstring(i), L"", false, false, -1});
    }

    TreeView tv;
    tv.setSize(FVector2(240.0f, 160.0f));
    tv.setItemHeight(16.0f);
    tv.setTree(nodes);

    CHECK(tv.getNodeCount() == 5000u);
    CHECK(tv.getNodePoolSize() <= 13u);
    CHECK(tv.getNodePoolSize() < tv.getNodeCount());
    CHECK(tv.getNodePoolLogicalIndex(0) == 0);

    tv.setScrollOffset(FVector2(0.0f, 25.0f * 16.0f));
    CHECK(tv.getFirstVisibleIndex() == 25);
    CHECK(tv.getNodePoolLogicalIndex(0) == 25);
    CHECK(tv.getNodePoolLogicalIndex(1) == 26);

    tv.setSelectedIndex(4000);
    CHECK(tv.getSelectedIndex() == 4000);
    tv.setScrollOffset(FVector2(0.0f, 4000.0f * 16.0f));
    CHECK(tv.getNodePoolLogicalIndex(0) == 4000);
}

TEST_SUITE_END
