// =============================================================================
// Dock-tree Phase 2 — DockTabGroup leaf + DockArea tree-ification.
//
// DockTabGroup is the leaf node of the dock tree: single card = full
// bleed (its own title bar is the only chrome, legacy geometry); ≥2
// cards = tab strip on top, active card slid up beneath it, inactive
// cards hidden. Cards are never freed here (detach-only).
//
// The DockArea back-compat gate lives in the existing
// Test_DockArea / Test_DockFloat / Test_DockAreaLoader suites — this
// file covers the NEW leaf widget plus the tree-shape facts the
// refactor introduced (lazy tree, overlay-last, disabled slots have no
// leaf, multi-card leaves become tabs).
// =============================================================================

#include "AYTest.h"
#include "AYUI/DockArea.h"
#include "AYUI/DockCard.h"
#include "AYUI/DockOverlay.h"
#include "AYUI/DockTabGroup.h"
#include "AYUI/Box.h"
#include "AYUI/Widget.h"
#include "AYUI/MockRenderer.h"
#include "AYUI/UIManager.h"
#include "AYUI/UIKeyCode.h"

#include <memory>

using namespace ayt::ui;
using namespace ayt::math;

namespace {

std::unique_ptr<DockCard> makeCard(const std::string& id) {
    auto card = std::make_unique<DockCard>();
    card->setId(id);
    card->setTitle(L"T:" + std::wstring(id.begin(), id.end()));
    return card;
}

// Common fixture: mock renderer + UIManager + 800x600 client area.
struct TabFixture {
    MockRenderer backend;
    UIManager    ui;

    TabFixture() {
        ui.initialize(&backend);
        ui.setClientSize(800.0f, 600.0f);
    }
    ~TabFixture() {
        ui.shutdown();
    }
};

} // namespace

TEST_SUITE(AYUI_DockTabGroup)

// -------------------------------------------------------------------------
// Leaf widget basics
// -------------------------------------------------------------------------

TEST_CASE(test_tab_group_render_clips_cards) {
    MockRenderer r;
    DockTabGroup leaf;
    leaf.setSize(FVector2(100.0f, 160.0f));
    auto card = makeCard("c");
    leaf.addTab(card.release());
    leaf.performLayout();
    leaf.render(r);
    CHECK(r.isClipStackBalanced());
    CHECK(r.getClipDepth() == 0);
}

TEST_CASE(test_tab_group_single_card_full_bleed) {
    DockTabGroup group;
    group.setSize(FVector2(400.0f, 300.0f));
    group.addTab(makeCard("a").release());
    group.performLayout();

    // Single card: full-bleed — legacy 5-slot geometry (its own title
    // bar is the only chrome, no strip).
    CHECK(group.getTabCount() == 1);
    DockCard* a = group.getTab(0);
    CHECK_NOT_NULL(a);
    if (a == nullptr) return;
    CHECK(a->isVisible());
    CHECK_FLOAT_EQ(a->getPosition().x, 0.0f, 1e-5f);
    CHECK_FLOAT_EQ(a->getPosition().y, 0.0f, 1e-5f);
    CHECK_FLOAT_EQ(a->getSize().x, 400.0f, 1e-5f);
    CHECK_FLOAT_EQ(a->getSize().y, 300.0f, 1e-5f);
}

TEST_CASE(test_tab_group_add_activates_new_card) {
    DockTabGroup group;
    group.addTab(makeCard("a").release());
    group.addTab(makeCard("b").release());

    CHECK(group.getTabCount() == 2);
    CHECK(group.getActiveTabId() == "b");
    // New add activates the just-added card; the other card hides.
    DockCard* a = group.getTab(0);
    DockCard* b = group.getTab(1);
    CHECK_NOT_NULL(a);
    CHECK_NOT_NULL(b);
    if (a == nullptr || b == nullptr) return;
    CHECK_FALSE(a->isVisible());
    CHECK(b->isVisible());
}

TEST_CASE(test_tab_group_activate_switches_visibility) {
    DockTabGroup group;
    group.addTab(makeCard("a").release());
    group.addTab(makeCard("b").release());

    group.activateTab(0);
    CHECK(group.getActiveTabId() == "a");
    DockCard* a = group.getTab(0);
    DockCard* b = group.getTab(1);
    CHECK_NOT_NULL(a);
    CHECK_NOT_NULL(b);
    if (a == nullptr || b == nullptr) return;
    CHECK(a->isVisible());
    CHECK_FALSE(b->isVisible());

    group.activateTabById("b");
    CHECK(group.getActiveTabId() == "b");
    CHECK_FALSE(a->isVisible());
    CHECK(b->isVisible());
}

TEST_CASE(test_tab_group_activate_resizes_newly_shown_card) {
    DockTabGroup group;
    group.setSize(FVector2(400.0f, 300.0f));
    group.addTab(makeCard("a").release());
    group.addTab(makeCard("b").release());
    group.performLayout();

    DockCard* a = group.getTab(0);
    DockCard* b = group.getTab(1);
    CHECK_NOT_NULL(a);
    CHECK_NOT_NULL(b);
    if (a == nullptr || b == nullptr) return;

    // Simulate a stale inactive card (pre-join geometry).
    a->setSize(FVector2(80.0f, 40.0f));
    group.activateTab(0);
    CHECK(a->isVisible());
    // activateTab must restretch the card to the leaf, not leave the hole.
    CHECK(a->getSize().x >= 390.0f);
    CHECK(a->getSize().y >= 250.0f);
}

TEST_CASE(test_tab_group_multi_card_layout_slides_card_under_strip) {
    DockTabGroup group;
    group.setSize(FVector2(400.0f, 300.0f));
    group.addTab(makeCard("a").release());
    group.addTab(makeCard("b").release());
    group.performLayout();

    // ≥2 cards: strip (26px) covers the active card's own title bar —
    // the card is slid up by (stripH - headerH) and its body starts at
    // stripH. Card header default is 22.
    DockCard* b = group.getTab(1);
    CHECK_NOT_NULL(b);
    if (b == nullptr) return;
    CHECK_FLOAT_EQ(b->getPosition().y, 4.0f, 1e-5f);          // 26 - 22
    CHECK_FLOAT_EQ(b->getSize().y, 296.0f, 1e-5f);            // 300 - 4
    CHECK_FLOAT_EQ(b->getSize().x, 400.0f, 1e-5f);
    // Card body starts exactly below the strip.
    CHECK_FLOAT_EQ(b->getPosition().y + b->getHeaderHeight(),
                   DockTabGroup::kTabStripHeight, 1e-5f);
}

TEST_CASE(test_tab_group_tabs_keep_fixed_width_until_strip_is_full) {
    DockTabGroup group;
    group.setSize(FVector2(500.0f, 300.0f));
    group.addTab(makeCard("a").release());
    group.addTab(makeCard("b").release());

    const FRectangle a = group.getTabRectWorld(0);
    const FRectangle b = group.getTabRectWorld(1);
    CHECK_FLOAT_EQ(a.maxX - a.minX,
                   DockTabGroup::kPreferredTabWidth, 1e-5f);
    CHECK_FLOAT_EQ(b.maxX - b.minX,
                   DockTabGroup::kPreferredTabWidth, 1e-5f);
    CHECK_FLOAT_EQ(b.minX, a.maxX, 1e-5f);
    CHECK(b.maxX < group.getWorldBounds().maxX);

    // Empty trailing strip space is chrome, not a stretched tab.
    CHECK_FALSE(group.onMouseButtonDown(
        UIMouseEvent(FVector2(450.0f, 13.0f), 0)));
}

TEST_CASE(test_tab_group_tabs_compress_and_titles_ellipsis_on_overflow) {
    MockRenderer renderer;
    DockTabGroup group;
    group.setSize(FVector2(150.0f, 300.0f));
    group.addTab(makeCard("first-very-long-panel-name").release());
    group.addTab(makeCard("second-very-long-panel-name").release());
    group.addTab(makeCard("third-very-long-panel-name").release());

    for (size_t i = 0; i < group.getTabCount(); ++i) {
        const FRectangle tab = group.getTabRectWorld(i);
        CHECK_FLOAT_EQ(tab.maxX - tab.minX, 50.0f, 1e-5f);
    }

    group.performLayout();
    group.render(renderer);
    int ellipsizedTitles = 0;
    for (const auto& call : renderer.getDrawCalls()) {
        if (call.type == MockRenderer::DrawCall::Text
            && call.text.find(L'\x2026') != std::wstring::npos) {
            ++ellipsizedTitles;
        }
    }
    CHECK(ellipsizedTitles == 3);
}

TEST_CASE(test_tab_group_zero_header_card_body_at_strip) {
    DockTabGroup group;
    group.setSize(FVector2(400.0f, 300.0f));
    group.addTab(makeCard("a").release());
    group.addTab(makeCard("b").release());
    group.getTab(1)->setHeaderHeight(0.0f);   // viewport-style card
    group.performLayout();

    // No title bar to slide — the card body starts exactly at stripH.
    DockCard* b = group.getTab(1);
    CHECK_NOT_NULL(b);
    if (b == nullptr) return;
    CHECK_FLOAT_EQ(b->getPosition().y, DockTabGroup::kTabStripHeight, 1e-5f);
}

TEST_CASE(test_tab_group_remove_detaches_without_freeing) {
    DockTabGroup group;
    DockCard* a = makeCard("a").release();
    group.addTab(a);
    group.addTab(makeCard("b").release());

    CHECK(group.removeTab(a));
    CHECK(group.getTabCount() == 1);
    // Detach-only: card pointer still valid, parent cleared.
    CHECK(a->getParent() == nullptr);
    CHECK(group.getActiveTabId() == "b");
    // Removed card can be re-parented elsewhere (float / move path).
    group.addTab(a);
    CHECK(group.getTabCount() == 2);
    CHECK(group.getActiveTabId() == "a");

    // No-op on unknown card.
    DockCard* ghost = makeCard("ghost").release();
    CHECK_FALSE(group.removeTab(ghost));
    delete ghost;   // never attached — caller-owned
}

TEST_CASE(test_tab_group_remove_active_index_adjustment) {
    DockTabGroup group;
    group.addTab(makeCard("a").release());
    group.addTab(makeCard("b").release());
    group.addTab(makeCard("c").release());
    // active = c (last added). Remove a card BEFORE the active one.
    DockCard* a = group.getTab(0);
    CHECK(group.removeTab(a));
    CHECK(group.getActiveTabId() == "c");   // index shifted, still c
    // Remove the active card itself.
    DockCard* c = group.getTab(1);
    CHECK(group.removeTab(c));
    CHECK(group.getActiveTabId() == "b");
    // Remove the last remaining card.
    DockCard* b = group.getTab(0);
    CHECK(group.removeTab(b));
    CHECK(group.getTabCount() == 0);
    CHECK(group.getActiveTab() == nullptr);
    CHECK(group.getActiveTabId().empty());
}

TEST_CASE(test_tab_group_contains_and_index_api) {
    DockTabGroup group;
    group.addTab(makeCard("x").release());
    CHECK(group.containsCard(group.getTab(0)));
    CHECK(group.getTab(5) == nullptr);
    CHECK(group.getTabCount() == 1);
    group.setLeafId("Center");
    CHECK(group.getLeafId() == "Center");
    group.setPinned(true);
    CHECK(group.isPinned());
    group.setPinned(false);
    CHECK_FALSE(group.isPinned());
}

// -------------------------------------------------------------------------
// Hit test / interaction
// -------------------------------------------------------------------------

TEST_CASE(test_tab_group_strip_claims_hits_above_card) {
    DockTabGroup group;
    group.setSize(FVector2(400.0f, 300.0f));
    group.addTab(makeCard("a").release());
    group.addTab(makeCard("b").release());
    group.performLayout();

    // Strip region → the group itself (tab click / close x / drag).
    const FRectangle activeTab = group.getTabRectWorld(1);
    CHECK(group.hitTest(FVector2(
              (activeTab.minX + activeTab.maxX) * 0.5f, 13.0f)) == &group);
    // Body region → the active card (title bar / content hit path).
    DockCard* b = group.getTab(1);
    CHECK_NOT_NULL(b);
    if (b == nullptr) return;
    CHECK(group.hitTest(FVector2(200.0f, 100.0f)) == b);
}

TEST_CASE(test_tab_group_single_card_no_strip_hits) {
    DockTabGroup group;
    group.setSize(FVector2(400.0f, 300.0f));
    group.addTab(makeCard("a").release());
    group.performLayout();

    // Single card: everything resolves to the card (its own title bar
    // is draggable — legacy behaviour).
    DockCard* a = group.getTab(0);
    CHECK_NOT_NULL(a);
    if (a == nullptr) return;
    CHECK(group.hitTest(FVector2(200.0f, 10.0f)) == a);
    CHECK(group.hitTest(FVector2(200.0f, 200.0f)) == a);
}

TEST_CASE(test_tab_group_close_x_fires_callback) {
    DockTabGroup group;
    group.setSize(FVector2(400.0f, 300.0f));
    group.addTab(makeCard("a").release());
    group.addTab(makeCard("b").release());

    DockCard* closed = nullptr;
    group.setOnCloseTab([&](DockCard* card) { closed = card; });

    // A non-active tab has no close affordance. Clicking the area where its
    // x would have been activates the tab instead of closing it.
    const FRectangle inactive = group.getTabRectWorld(0);
    const FVector2 inactiveRight(inactive.maxX - 2.0f, 13.0f);
    CHECK(group.onMouseButtonDown(UIMouseEvent(inactiveRight, 0)));
    CHECK(closed == nullptr);
    CHECK(group.getActiveTabId() == "a");

    // The newly-active tab now exposes its close x.
    const FRectangle active = group.getTabRectWorld(0);
    const FVector2 closePt(active.maxX - 2.0f, 13.0f);
    CHECK(group.onMouseButtonDown(UIMouseEvent(closePt, 0)));
    CHECK(closed == group.getTab(0));
}

TEST_CASE(test_tab_group_paints_close_only_for_active_tab) {
    MockRenderer renderer;
    DockTabGroup group;
    group.setSize(FVector2(500.0f, 300.0f));
    group.addTab(makeCard("a").release());
    group.addTab(makeCard("b").release());
    group.performLayout();
    group.render(renderer);

    const FRectangle inactive = group.getTabRectWorld(0);
    const FRectangle active = group.getTabRectWorld(1);
    int inactiveCloseGlyphs = 0;
    int activeCloseGlyphs = 0;
    for (const auto& call : renderer.getDrawCalls()) {
        if (call.type != MockRenderer::DrawCall::Text || call.text != L"x") continue;
        const float centerX = (call.bounds.minX + call.bounds.maxX) * 0.5f;
        if (centerX >= inactive.minX && centerX <= inactive.maxX) {
            ++inactiveCloseGlyphs;
        }
        if (centerX >= active.minX && centerX <= active.maxX) {
            ++activeCloseGlyphs;
        }
    }
    CHECK(inactiveCloseGlyphs == 0);
    CHECK(activeCloseGlyphs == 1);
}

TEST_CASE(test_tab_group_active_tab_press_begins_drag) {
    TabFixture f;
    auto group = std::make_unique<DockTabGroup>();
    group->setSize(FVector2(400.0f, 300.0f));
    group->addTab(makeCard("a").release());
    group->addTab(makeCard("b").release());
    f.ui.getOverlayRoot()->addChildExternal(group.get());

    // Press the ACTIVE tab (b, second half of the strip) → tear-off
    // drag begins with the G12 payload convention of the card.
    const FRectangle active = group->getTabRectWorld(1);
    const FVector2 activePt((active.minX + active.maxX) * 0.5f, 13.0f);
    UIMouseEvent e(activePt, 0);
    CHECK(group->onMouseButtonDown(e));
    CHECK(f.ui.isDragging());
    CHECK(f.ui.getDragSource() == group->getTab(1));
    CHECK(f.ui.getDragPayload().kind == "DockCard");
    f.ui.onKeyDown(UIKey_Escape);
    CHECK_FALSE(f.ui.isDragging());
}

TEST_CASE(test_tab_group_non_floatable_active_tab_no_drag) {
    TabFixture f;
    auto group = std::make_unique<DockTabGroup>();
    group->setSize(FVector2(400.0f, 300.0f));
    group->addTab(makeCard("a").release());
    DockCard* b = makeCard("b").release();
    b->setFloatable(false);   // K-INV-D3-2 — never starts a session
    group->addTab(b);
    f.ui.getOverlayRoot()->addChildExternal(group.get());

    const FRectangle active = group->getTabRectWorld(1);
    const FVector2 activePt((active.minX + active.maxX) * 0.5f, 13.0f);
    UIMouseEvent e(activePt, 0);
    CHECK(group->onMouseButtonDown(e));   // consumed (strip click)
    CHECK_FALSE(f.ui.isDragging());
}

TEST_CASE(test_tab_group_hover_tracks_strip) {
    DockTabGroup group;
    group.setSize(FVector2(400.0f, 300.0f));
    group.addTab(makeCard("a").release());
    group.addTab(makeCard("b").release());

    // Hover over the active tab's close x
    // → Hand cursor.
    const FRectangle active = group.getTabRectWorld(1);
    group.onMouseMove(UIMouseEvent(FVector2(active.maxX - 2.0f, 13.0f), 0));
    CHECK(group.getCursorHint() == UiCursorHint::Hand);
    // The inactive tab's right edge has no close hit target.
    const FRectangle inactive = group.getTabRectWorld(0);
    group.onMouseMove(UIMouseEvent(FVector2(inactive.maxX - 2.0f, 13.0f), 0));
    CHECK(group.getCursorHint() == UiCursorHint::Default);
    group.onMouseLeave();
    CHECK(group.getCursorHint() == UiCursorHint::Default);
}

// -------------------------------------------------------------------------
// DockArea tree facts (the refactor's observable shape)
// -------------------------------------------------------------------------

TEST_CASE(test_dock_area_tree_built_lazily) {
    DockArea dock;
    dock.setSize(FVector2(800.0f, 600.0f));

    // Before any card / layout: no tree, counts read 0.
    CHECK(dock.getChildren().size() == 1);   // overlay only
    CHECK(dock.getCardCount(DockArea::Slot::Left) == 0);
    CHECK(dock.getOverlay()->getFloatingCardCount() == 0);

    dock.addCard(DockArea::Slot::Left, makeCard("l"));
    // Tree now exists; children order = [tree, overlay] (overlay last
    // so floating cards paint on top of the tree).
    CHECK(dock.getChildren().size() == 2);
    CHECK(dock.getChildren().back() == dock.getOverlay());
    CHECK(dock.getCardCount(DockArea::Slot::Left) == 1);
}

TEST_CASE(test_dock_area_disabled_slots_have_no_leaf) {
    DockArea dock;
    dock.setSize(FVector2(800.0f, 600.0f));
    dock.setSlotWeight(DockArea::Slot::Top, 1e-6f);
    dock.setSlotWeight(DockArea::Slot::Bottom, 1e-6f);
    dock.addCard(DockArea::Slot::Center, makeCard("c"));
    dock.performLayout();

    // Top/Bottom disabled → root VBox has a single child (the mid HBox).
    Widget* root = dock.getChildren()[0];
    CHECK(dynamic_cast<VBox*>(root) != nullptr);
    CHECK(root->getChildren().size() == 1);
    // Center leaf still exists (fill column always present).
    CHECK(dock.getCardCount(DockArea::Slot::Top) == 0);
    CHECK(dock.getCardCount(DockArea::Slot::Bottom) == 0);
    CHECK(dock.getCardCount(DockArea::Slot::Center) == 1);
}

TEST_CASE(test_dock_area_tree_inserts_splitters_between_panels) {
    // Gallery-shaped tree: Top/Bottom disabled, L | split | C | split | R.
    DockArea dock;
    dock.setSize(FVector2(900.0f, 420.0f));
    dock.setSlotWeight(DockArea::Slot::Top, 1e-6f);
    dock.setSlotWeight(DockArea::Slot::Bottom, 1e-6f);
    dock.setSlotWeight(DockArea::Slot::Left, 0.28f);
    dock.setSlotWeight(DockArea::Slot::Right, 0.28f);
    dock.setSlotWeight(DockArea::Slot::Center, 0.44f);
    dock.addCard(DockArea::Slot::Left, makeCard("l"));
    dock.addCard(DockArea::Slot::Center, makeCard("c"));
    dock.addCard(DockArea::Slot::Right, makeCard("r"));
    dock.performLayout();

    Widget* root = dock.getChildren()[0];
    CHECK(dynamic_cast<VBox*>(root) != nullptr);
    CHECK(root->getChildren().size() == 1);
    HBox* mid = dynamic_cast<HBox*>(root->getChildren()[0]);
    CHECK_NOT_NULL(mid);
    if (mid == nullptr) {
        return;
    }
    const auto& kids = mid->getChildren();
    CHECK(kids.size() == 5);
    CHECK(kids[0]->isSplitterHandle() == false);
    CHECK(kids[1]->isSplitterHandle());
    CHECK(kids[2]->isSplitterHandle() == false);
    CHECK(kids[3]->isSplitterHandle());
    CHECK(kids[4]->isSplitterHandle() == false);

    // Live leaf rects drive drop targeting after layout.
    const FRectangle left = dock.getSlotRect(DockArea::Slot::Left);
    const FRectangle center = dock.getSlotRect(DockArea::Slot::Center);
    CHECK(left.maxX - left.minX > 100.0f);
    CHECK(center.minX >= left.maxX - 1.0f);
    CHECK(dock.hitTestSlot(FVector2(
              (left.minX + left.maxX) * 0.5f,
              (left.minY + left.maxY) * 0.5f))
          == DockArea::Slot::Left);
}

TEST_CASE(test_dock_area_multi_card_slot_becomes_tabs) {
    DockArea dock;
    dock.setSize(FVector2(800.0f, 600.0f));
    dock.setSlotWeight(DockArea::Slot::Top, 1e-6f);
    dock.setSlotWeight(DockArea::Slot::Bottom, 1e-6f);
    dock.addCard(DockArea::Slot::Left, makeCard("a"));
    dock.addCard(DockArea::Slot::Left, makeCard("b"));
    dock.performLayout();

    CHECK(dock.getCardCount(DockArea::Slot::Left) == 2);
    CHECK(dock.getCard(DockArea::Slot::Left, 0)->getId() == "a");
    CHECK(dock.getCard(DockArea::Slot::Left, 1)->getId() == "b");
    // Tab semantics: only the active card (last added) is visible.
    DockCard* a = dock.getCard(DockArea::Slot::Left, 0);
    DockCard* b = dock.getCard(DockArea::Slot::Left, 1);
    CHECK_NOT_NULL(a);
    CHECK_NOT_NULL(b);
    if (a == nullptr || b == nullptr) return;
    CHECK_FALSE(a->isVisible());
    CHECK(b->isVisible());
}

TEST_CASE(test_dock_area_first_real_size_layout_pushes_geometry) {
    // The tree may be built at size 0 (addCard before setSize, as in
    // several legacy tests). The FIRST real-size performLayout must
    // always push the weight-derived geometry.
    DockArea dock;
    dock.addCard(DockArea::Slot::Left, makeCard("l"));
    dock.addCard(DockArea::Slot::Center, makeCard("c"));
    dock.setSize(FVector2(1000.0f, 600.0f));
    dock.performLayout();

    const FRectangle slot = dock.getSlotRect(DockArea::Slot::Center);
    const FRectangle cb = dock.getCard(DockArea::Slot::Center, 0)->getWorldBounds();
    CHECK(std::fabs((cb.maxY - cb.minY) - (slot.maxY - slot.minY)) < 1.0f);
    CHECK(std::fabs(cb.minY - slot.minY) < 1.0f);
    CHECK(std::fabs(cb.minX - slot.minX) < 1.0f);
}

// Full-chain repro of the Gallery dock drag path: UIManager
// onMouseButtonDown → pickTopmostWidget → dock tree hitTest → DockCard
// title bar → beginDrag. The legacy suites call
// DockCard::onMouseButtonDown directly (Test_DockFloat.cpp:79), so this
// is the ONLY test covering the hitTest chain from the real input
// entry — where a hitTest regression would show up as "can't drag".
TEST_CASE(test_gallery_single_card_title_bar_drag_via_uimanager) {
    TabFixture f;
    auto dock = std::make_unique<DockArea>();
    dock->setId("mini_dock");
    dock->setSlotWeight(DockArea::Slot::Top, 1e-6f);
    dock->setSlotWeight(DockArea::Slot::Bottom, 1e-6f);
    dock->setSize(FVector2(900.0f, 420.0f));
    f.ui.root()->addChildExternal(dock.get());

    auto card = makeCard("card_center");
    DockCard* cp = card.get();
    dock->addCard(DockArea::Slot::Center, std::move(card));
    dock->performLayout();

    const FRectangle cb = cp->getWorldBounds();
    CHECK(cb.maxY - cb.minY > 100.0f);   // sanity: card actually laid out

    // Click the card's title bar (10px in from the left edge).
    f.ui.onMouseButtonDown(cb.minX + 10.0f, cb.minY + 5.0f, 0);
    CHECK(f.ui.isDragging());
    CHECK(f.ui.getDragSource() == cp);
    f.ui.onKeyDown(UIKey_Escape);
    CHECK_FALSE(f.ui.isDragging());
}

TEST_SUITE_END
