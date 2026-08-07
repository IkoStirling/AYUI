#include "AYTest.h"
#include "AYTreeView.h"
#include "AYTreeNode.h"
#include "AYUIManager.h"
#include "AYWidgetFactory.h"
#include "AYWidgetSerializer.h"
#include "AYMockRenderer.h"
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

TEST_SUITE_END