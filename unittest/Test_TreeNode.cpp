#include "AYTest.h"
#include "AYUI/TreeNode.h"
#include "AYUI/UIManager.h"
#include "AYUI/MockRenderer.h"
#include <iostream>

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_TreeNode)

TEST_CASE(treenode_initial_state) {
    TreeNode node;
    CHECK(node.getDepth() == 0);
    CHECK_FALSE(node.hasChildren());
    CHECK_FALSE(node.isExpanded());
    CHECK(node.getIcon().empty());
    CHECK(node.getLabel().empty());
    CHECK_FALSE(node.isSelected());
}

TEST_CASE(treenode_set_depth_label_icon) {
    TreeNode node;
    node.setDepth(2);
    node.setIcon(L"📁");
    node.setLabel(L"src/");
    node.setHasChildren(true);
    CHECK(node.getDepth() == 2);
    CHECK(node.getIcon() == L"📁");
    CHECK(node.getLabel() == L"src/");
    CHECK(node.hasChildren());
    CHECK_FALSE(node.isExpanded());
}

TEST_CASE(treenode_expand_toggle_fires_callback) {
    TreeNode node;
    bool fired = false;
    bool firedValue = false;
    node.setOnExpandToggled([&](bool e) {
        fired = true;
        firedValue = e;
    });
    node.setHasChildren(true);
    CHECK_FALSE(node.isExpanded());

    node.setExpanded(true);
    CHECK(node.isExpanded());
    CHECK(fired);
    CHECK(firedValue);

    // Idempotent: setExpanded(true) again should NOT fire.
    fired = false;
    node.setExpanded(true);
    CHECK_FALSE(fired);

    node.setExpanded(false);
    CHECK_FALSE(node.isExpanded());
    CHECK(fired);
    CHECK_FALSE(firedValue);
}

TEST_CASE(treenode_click_label_selects_row) {
    TreeNode node;
    node.setPosition(FVector2(0.0f, 0.0f));
    node.setSize(FVector2(240.0f, 16.0f));
    node.setLabel(L"file.txt");
    node.setHasChildren(false);

    bool routed = false;
    int routedIdx = -1;
    node.setOnClickByNode([&](int idx) {
        routed = true;
        routedIdx = idx;
    });

    // Position well outside the arrow column (depth 0 arrow is [0, 16)).
    UIMouseEvent e(FVector2(100.0f, 8.0f), 0);
    CHECK(node.onMouseButtonUp(e));
    CHECK(routed);
    CHECK(routedIdx == -1);   // index not assigned (no TreeView parent in test)
}

TEST_CASE(treenode_click_arrow_toggles_expand) {
    TreeNode node;
    node.setPosition(FVector2(0.0f, 0.0f));
    node.setSize(FVector2(240.0f, 16.0f));
    node.setLabel(L"folder");
    node.setHasChildren(true);
    node.setDepth(1);    // arrow column at x ∈ [16, 32)

    bool fired = false;
    node.setOnExpandToggled([&](bool) { fired = true; });

    // Click inside arrow column at x = 20.
    UIMouseEvent e(FVector2(20.0f, 8.0f), 0);
    CHECK(node.onMouseButtonUp(e));
    CHECK(node.isExpanded());
    CHECK(fired);
}

TEST_CASE(treenode_click_arrow_consumed_no_select) {
    TreeNode node;
    node.setPosition(FVector2(0.0f, 0.0f));
    node.setSize(FVector2(240.0f, 16.0f));
    node.setHasChildren(true);
    node.setDepth(0);

    bool routed = false;
    node.setOnClickByNode([&](int) { routed = true; });

    // Click inside arrow column [0, 16).
    UIMouseEvent e(FVector2(5.0f, 8.0f), 0);
    CHECK(node.onMouseButtonUp(e));
    CHECK_FALSE(routed);   // arrow click does NOT route to parent
    CHECK(node.isExpanded());
}

TEST_CASE(treenode_render_columns) {
    TreeNode node;
    node.setPosition(FVector2(0.0f, 0.0f));
    node.setSize(FVector2(240.0f, 16.0f));
    node.setDepth(1);
    node.setHasChildren(true);
    node.setExpanded(false);
    node.setIcon(L">");
    node.setLabel(L"Assets");
    node.setSelected(true);

    MockRenderer renderer;
    node.render(renderer);
    // Expected drawText calls: 1 arrow ("▶") + 1 icon ("📁") + 1 label ("Assets")
    // = 3 drawText calls. Plus selection band rect + (no hover rect).
    int textCount = 0;
    int rectCount = 0;
    for (const auto& dc : renderer.getDrawCalls()) {
        if (dc.type == MockRenderer::DrawCall::Text) textCount++;
        if (dc.type == MockRenderer::DrawCall::Rect) rectCount++;
    }
    CHECK(textCount == 3);
    CHECK(rectCount == 1);
}

TEST_SUITE_END