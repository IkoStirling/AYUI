#include "AYTest.h"
#include "AYTabControl.h"
#include "AYListView.h"
#include "AYPanel.h"
#include "AYTextLabel.h"
#include "AYUIManager.h"
#include "AYWidgetFactory.h"
#include "AYWidgetSerializer.h"
#include "AYMockRenderer.h"
#include "AYStyle.h"
#include <iostream>

// =============================================================================
// Known-not-covered scenarios for C-9 TabControl v1
// =============================================================================
// See Controls/AYTabControl.h top-of-file "v1 design decisions" block for
// design rationale + upgrade paths. Pinned here as entry points.
//
// T1. Horizontal tab strip look.
//     v1 ships TabControl with a vertical-list header — each tab is a row
//     inside a one-row-tall ListView. To get a horizontal-strip appearance
//     (icon + label side-by-side), the host must replace _header with a
//     TabStrip subclass (planned v1.1). For now, set
//     `tc.getHeaderListView()->getVerticalScrollBar()->setVisible(false)`
//     to hide the unused scrollbar, and clamp tab count to fit one row.
//
// T2. Keyboard navigation (Tab to focus, Left/Right to switch, Enter to
//     activate). ListView v1 does not override onKeyDown; TabControl
//     inherits this gap. v1.1 fix mirrors ComboBox DECISION 3 — make
//     TabControl inherit FocusableWidget and override onKeyDown.
//
// T3. Closeable / reorderable tabs.
//     No close button, no drag-to-reorder in v1. Pure visual v2+ features.
//
// T4. Content widgets shared across tabs.
//     v1 hosts one content widget per tab. If two tabs need to show the
//     same widget, hosts must either clone it or accept switching state.
//     v1.1 fix: add `addSharedTab(label, contentRef)` that re-parents
//     without taking ownership; tab removal does NOT detach.
//
// T5. Nested TabControl.
//     A TabControl inside another TabControl's body — works in v1 because
//     addChildExternal is recursive, but the inner tab's header sizing
//     does not propagate to the outer's body. v1.1: cascading layout pass.
// =============================================================================

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_TabControl)

// C-9: default state — no tabs, no selection, but header + body pre-created.
TEST_CASE(tabcontrol_initial_state) {
    TabControl tc;
    CHECK(tc.getTabCount() == 0u);
    CHECK(tc.getSelectedIndex() == -1);
    CHECK(tc.getHeaderListView() != nullptr);
    CHECK(tc.getBodyPanel() != nullptr);
    CHECK_FLOAT_EQ(tc.getHeaderHeight(), 28.0f, 1e-5f);
}

// C-9: addTab auto-selects first; later addTab does not change selection.
TEST_CASE(tabcontrol_add_tab_auto_selects_first) {
    TabControl tc;
    TextLabel* a = new TextLabel(); a->setText(L"Tab A");
    TextLabel* b = new TextLabel(); b->setText(L"Tab B");
    tc.addTab(L"A", a);
    CHECK(tc.getTabCount() == 1u);
    CHECK(tc.getSelectedIndex() == 0);
    CHECK(tc.getSelectedLabel() == L"A");

    tc.addTab(L"B", b);
    CHECK(tc.getTabCount() == 2u);
    CHECK(tc.getSelectedIndex() == 0);   // adding later tab doesn't steal selection

    // Headers' ListView mirrors the tab labels.
    CHECK(tc.getHeaderListView()->getItemCount() == 2u);
    CHECK(tc.getHeaderListView()->getItem(1) == L"B");

    // Active content (a) is mounted in _body.
    CHECK(tc.getBodyPanel()->getChildren().size() == 1u);
    CHECK(tc.getBodyPanel()->getChildren().front() == a);
}

// C-9: setSelectedIndex swaps the body content + fires callback idempotently.
TEST_CASE(tabcontrol_set_selected_index_swaps_content) {
    TabControl tc;
    TextLabel* a = new TextLabel(); a->setText(L"A content");
    TextLabel* b = new TextLabel(); b->setText(L"B content");
    TextLabel* c = new TextLabel(); c->setText(L"C content");
    tc.addTab(L"A", a);
    tc.addTab(L"B", b);
    tc.addTab(L"C", c);

    int changes = 0;
    tc.setOnSelectionChanged([&](int idx) {
        ++changes;
    });

    tc.setSelectedIndex(1);
    CHECK(tc.getSelectedIndex() == 1);
    CHECK(changes == 1);
    CHECK(tc.getBodyPanel()->getChildren().size() == 1u);
    CHECK(tc.getBodyPanel()->getChildren().front() == b);

    tc.setSelectedIndex(1);   // idempotent
    CHECK(changes == 1);

    tc.setSelectedIndex(2);
    CHECK(changes == 2);
    CHECK(tc.getBodyPanel()->getChildren().front() == c);

    tc.setSelectedIndex(99);  // clamps to last valid (already at 2 — idempotent)
    CHECK(tc.getSelectedIndex() == 2);
    CHECK(changes == 2);   // still 2: clamping to current value is a no-op

    // Clamping to a DIFFERENT valid index still fires callback.
    tc.setSelectedIndex(0);
    CHECK(tc.getSelectedIndex() == 0);
    CHECK(changes == 3);
}

// C-9: removeTab detaches content + clamps selection correctly.
TEST_CASE(tabcontrol_remove_tab_repairs_selection) {
    TabControl tc;
    TextLabel* a = new TextLabel(); a->setText(L"A");
    TextLabel* b = new TextLabel(); b->setText(L"B");
    TextLabel* c = new TextLabel(); c->setText(L"C");
    tc.addTab(L"A", a);
    tc.addTab(L"B", b);
    tc.addTab(L"C", c);
    tc.setSelectedIndex(1);

    // Remove the active tab — selection should pick a neighbor (the new
    // index-1 is C since B was removed).
    tc.removeTab(1);
    CHECK(tc.getTabCount() == 2u);
    CHECK(tc.getSelectedIndex() == 1);
    CHECK(tc.getTabLabel(1) == L"C");
    CHECK(tc.getBodyPanel()->getChildren().front() == c);

    // Remove a tab BEFORE the active one — selection slides down.
    tc.removeTab(0);
    CHECK(tc.getTabCount() == 1u);
    CHECK(tc.getSelectedIndex() == 0);
    CHECK(tc.getTabLabel(0) == L"C");

    // Remove the last tab — selection becomes -1.
    tc.removeTab(0);
    CHECK(tc.getTabCount() == 0u);
    CHECK(tc.getSelectedIndex() == -1);
    CHECK(tc.getBodyPanel()->getChildren().empty());
}

// C-9: clearTabs resets everything.
TEST_CASE(tabcontrol_clear_tabs) {
    TabControl tc;
    TextLabel* a = new TextLabel();
    TextLabel* b = new TextLabel();
    tc.addTab(L"A", a);
    tc.addTab(L"B", b);
    tc.setSelectedIndex(1);
    CHECK(tc.getTabCount() == 2u);

    tc.clearTabs();
    CHECK(tc.getTabCount() == 0u);
    CHECK(tc.getSelectedIndex() == -1);
    CHECK(tc.getBodyPanel()->getChildren().empty());

    // The contents are detached but NOT freed (DECISION 2 in header) —
    // destroyWidgetTree is the caller's job.
    destroyWidgetTree(a);
    destroyWidgetTree(b);
}

// C-9: factory + serializer round-trip preserves tabs + selectedIndex +
// headerHeight. Content widgets are nested recursively via the tabs[] array.
TEST_CASE(tabcontrol_factory_and_serializer_round_trip) {
    WidgetFactory& factory = WidgetFactory::get();
    CHECK_TRUE(factory.isRegistered("TabControl"));

    Widget* widget = factory.create("TabControl");
    CHECK_NOT_NULL(widget);
    TabControl* original = dynamic_cast<TabControl*>(widget);
    CHECK_NOT_NULL(original);

    original->setId("tc_main");
    TextLabel* a = new TextLabel(); a->setText(L"first panel");
    TextLabel* b = new TextLabel(); b->setText(L"second panel");
    a->setId("content_a");
    b->setId("content_b");
    original->addTab(L"First", a);
    original->addTab(L"Second", b);
    original->setSelectedIndex(1);
    original->setHeaderHeight(32.0f);

    std::string json = WidgetSerializer::serialize(widget);
    CHECK(json.find("\"type\": \"TabControl\"") != std::string::npos);
    CHECK(json.find("First") != std::string::npos);
    CHECK(json.find("second panel") != std::string::npos);
    CHECK(json.find("\"selectedIndex\": 1") != std::string::npos);
    CHECK(json.find("\"headerHeight\": 32") != std::string::npos);

    Widget* restored = WidgetSerializer::deserialize(json);
    CHECK_NOT_NULL(restored);
    TabControl* restoredTc = dynamic_cast<TabControl*>(restored);
    CHECK_NOT_NULL(restoredTc);
    CHECK(restoredTc->getId() == "tc_main");
    CHECK(restoredTc->getTabCount() == 2u);
    CHECK(restoredTc->getTabLabel(0) == L"First");
    CHECK(restoredTc->getTabLabel(1) == L"Second");
    CHECK(restoredTc->getSelectedIndex() == 1);
    CHECK_FLOAT_EQ(restoredTc->getHeaderHeight(), 32.0f, 1e-5f);

    // Content widgets were nested-deserialized.
    Widget* contentA = restoredTc->getTabContent(0);
    Widget* contentB = restoredTc->getTabContent(1);
    CHECK_NOT_NULL(contentA);
    CHECK_NOT_NULL(contentB);
    TextLabel* labelA = dynamic_cast<TextLabel*>(contentA);
    TextLabel* labelB = dynamic_cast<TextLabel*>(contentB);
    CHECK_NOT_NULL(labelA);
    CHECK_NOT_NULL(labelB);
    CHECK(labelA->getText() == L"first panel");
    CHECK(labelB->getText() == L"second panel");

    destroyWidgetTree(widget);
    destroyWidgetTree(restored);
}

// C-9: render emits at least the header (ListView rect) + body (Panel rect).
TEST_CASE(tabcontrol_render_emits_header_and_body) {
    TabControl tc;
    TextLabel* a = new TextLabel(); a->setText(L"A");
    tc.addTab(L"only", a);
    tc.setSize(FVector2(200.0f, 120.0f));
    tc.setPosition(FVector2(0.0f, 0.0f));
    tc.performLayout();

    MockRenderer renderer;
    tc.render(renderer);

    int rectCount = 0;
    for (const auto& dc : renderer.getDrawCalls()) {
        if (dc.type == MockRenderer::DrawCall::Rect) ++rectCount;
    }
    // ListView emits bg + at least 1 row + ScrollBar = multiple rects.
    // Panel emits bg + border. Expect a healthy count.
    CHECK(rectCount >= 2);
}

TEST_SUITE_END