// =============================================================================
// dock-tree Phase 3 — nested split / join-as-tab / prune / preview tests.
//
// Exercises the Phase-3 tree drop pipeline end to end through the
// UIManager input chain (title-bar LMB → beginDrag → move → mouse-up
// drop), the same path Gallery uses:
//   * hitTestTree — deepest-leaf resolution incl. snap fallback
//   * resolveTreeDropZone — 25% edge argmin (ties West > East > North
//     > South), empty leaves always Join
//   * center drop → join as a tab (VS Code style)
//   * edge drop → nested split (new g_N leaf, 25% share, splitter
//     reuse / creation)
//   * same-leaf drop → no-op (center AND edge — K-INV-D3-1)
//   * close/float emptying a g_N leaf → prune (with adjacent
//     splitter), pinned leaves never pruned
//   * structure edits flip the pristine guard — the weight template
//     stops clobbering user sizing
//   * external (foreign) card drops still take the legacy
//     hitTestSlot + adoptCard path
//   * paintDropGuide paints the tree join/split previews
// =============================================================================

#include "AYTest.h"
#include "AYUIManager.h"
#include "AYDockArea.h"
#include "AYDockCard.h"
#include "AYDockOverlay.h"
#include "AYDockTabGroup.h"
#include "AYDragDrop.h"
#include "AYMockRenderer.h"
#include "AYBox.h"
#include "AYSplitterHandle.h"
#include "UIKeyCode.h"

#include <cmath>
#include <memory>

using namespace ayt::ui;
using namespace ayt::math;

namespace {

// Same fixture idiom as Test_DockFloat.cpp: mock renderer + UIManager +
// 800x600 client; DockArea is heap-allocated and wired into the
// UIManager's hit-test tree via the overlay root.
struct TreeFixture {
    MockRenderer backend;
    UIManager    ui;

    TreeFixture() {
        ui.initialize(&backend);
        ui.setClientSize(800.0f, 600.0f);
    }
    ~TreeFixture() {
        ui.shutdown();
    }
};

std::unique_ptr<DockCard> treeMakeCard(const std::string& id,
                                   const std::wstring& title) {
    auto c = std::make_unique<DockCard>();
    c->setId(id);
    c->setTitle(title);
    return c;
}

// The most-vexing-parse trap: returning DockCard* and rewrapping at the
// call site would parse as a declaration. Return unique_ptr instead.
std::unique_ptr<DockArea> makeDock(TreeFixture& f) {
    auto dock = std::make_unique<DockArea>();
    dock->setId("tree_dock");
    dock->setSize(FVector2(800.0f, 600.0f));
    dock->setSlotWeight(DockArea::Slot::Top, 1e-6f);
    dock->setSlotWeight(DockArea::Slot::Bottom, 1e-6f);
    f.ui.getOverlayRoot()->addChildExternal(dock.get());
    return dock;
}

// Helper: simulate "user clicked LMB on the card's title bar" by
// feeding the right UIMouseEvent to DockCard::onMouseButtonDown.
bool treeTitleBarClick(DockCard* card, FVector2 worldPos) {
    UIMouseEvent e(worldPos, 0);   // button 0 = LMB
    return card->onMouseButtonDown(e);
}

// Title-bar point in world space after performLayout.
FVector2 treeTitleBarPoint(DockCard* card) {
    const FRectangle b = card->getWorldBounds();
    return FVector2((b.minX + b.maxX) * 0.5f, b.minY + 4.0f);
}

// Drag `card` and release it at worldPt through the full UIManager
// chain (same as Gallery): title-bar LMB → beginDrag → move → mouse-up
// drop.
bool dragCardTo(TreeFixture& f, DockCard* card, const FVector2& pt) {
    if (!treeTitleBarClick(card, treeTitleBarPoint(card))) {
        return false;
    }
    f.ui.onMouseMove(pt.x, pt.y);
    f.ui.onMouseButtonUp(pt.x, pt.y, 0);
    return !f.ui.isDragging();
}

// The leaf under `pt`, or nullptr (convenience wrapper).
DockTabGroup* leafAt(DockArea* dock, const FVector2& pt) {
    return dock->hitTestTree(pt);
}

const char* leafId(DockArea* dock, const FVector2& pt) {
    DockTabGroup* leaf = leafAt(dock, pt);
    return leaf != nullptr ? leaf->getLeafId().c_str() : "-";
}

// Center of a leaf's world bounds.
FVector2 centerOf(DockTabGroup* leaf) {
    const FRectangle b = leaf->getWorldBounds();
    return FVector2((b.minX + b.maxX) * 0.5f, (b.minY + b.maxY) * 0.5f);
}

// The root VBox of the dock tree (dock->getChildren()[0]).
VBox* rootBox(DockArea* dock) {
    const std::vector<Widget*>& kids = dock->getChildren();
    for (Widget* c : kids) {
        if (auto* v = dynamic_cast<VBox*>(c)) {
            return v;
        }
    }
    return nullptr;
}

// The mid HBox inside the root VBox.
HBox* midBox(DockArea* dock) {
    VBox* root = rootBox(dock);
    if (root == nullptr) {
        return nullptr;
    }
    for (Widget* c : root->getChildren()) {
        if (auto* h = dynamic_cast<HBox*>(c)) {
            return h;
        }
    }
    return nullptr;
}

} // namespace

TEST_SUITE(AYUI_DockTree)

// -------------------------------------------------------------------------
// 1. hitTestTree resolves the deepest leaf; split-created g_N leaves
//    are found alongside pinned leaves.
// -------------------------------------------------------------------------
TEST_CASE(test_hit_test_tree_finds_deepest_leaf) {
    TreeFixture f;
    auto dock = makeDock(f);

    dock->addCard(DockArea::Slot::Center, treeMakeCard("viewport", L"Viewport"));
    dock->performLayout();

    DockCard* viewport = dock->findCard("viewport");
    CHECK_NOT_NULL(viewport);
    if (viewport == nullptr) return;

    const FRectangle vb = viewport->getWorldBounds();
    DockTabGroup* center = leafAt(dock.get(),
        FVector2((vb.minX + vb.maxX) * 0.5f, (vb.minY + vb.maxY) * 0.5f));
    CHECK_NOT_NULL(center);
    if (center == nullptr) return;
    CHECK(center->getLeafId() == "Center");
    CHECK(center->containsCard(viewport));

    // Outside the tree → nullptr (no snap to anything).
    CHECK(leafAt(dock.get(), FVector2(850.0f, 300.0f)) == nullptr);
}

// -------------------------------------------------------------------------
// 2. Center-of-leaf drop merges as a tab (VS Code style): both cards in
//    one leaf, the just-dropped card becomes active.
// -------------------------------------------------------------------------
TEST_CASE(test_drop_center_joins_tabs) {
    TreeFixture f;
    auto dock = makeDock(f);

    dock->addCard(DockArea::Slot::Left, treeMakeCard("console", L"Console"));
    dock->addCard(DockArea::Slot::Center, treeMakeCard("viewport", L"Viewport"));
    dock->performLayout();

    DockCard* console = dock->findCard("console");
    CHECK_NOT_NULL(console);
    if (console == nullptr) return;

    DockTabGroup* center = leafAt(dock.get(), FVector2(400.0f, 300.0f));
    CHECK_NOT_NULL(center);
    if (center == nullptr) return;
    CHECK(center->getLeafId() == "Center");
    const FVector2 dropPt = centerOf(center);

    CHECK(dragCardTo(f, console, dropPt));

    // Both cards now live in the Center leaf; console is active.
    CHECK(dock->getCardCount(DockArea::Slot::Left) == 0);
    CHECK(dock->getCardCount(DockArea::Slot::Center) == 2);
    DockTabGroup* leaf = leafAt(dock.get(), dropPt);
    CHECK_NOT_NULL(leaf);
    if (leaf == nullptr) return;
    CHECK(leaf->getLeafId() == "Center");
    CHECK(leaf->containsCard(console));
    CHECK(leaf->containsCard(dock->findCard("viewport")));
    CHECK(leaf->getActiveTabId() == "console");
}

// -------------------------------------------------------------------------
// 3. East-edge drop on Center creates a nested split: new g_0 leaf to
//    the right holding the dragged card; Center keeps its occupant.
// -------------------------------------------------------------------------
TEST_CASE(test_drop_edge_splits_leaf) {
    TreeFixture f;
    auto dock = makeDock(f);

    dock->addCard(DockArea::Slot::Left, treeMakeCard("console", L"Console"));
    dock->addCard(DockArea::Slot::Center, treeMakeCard("viewport", L"Viewport"));
    dock->performLayout();

    DockCard* console = dock->findCard("console");
    CHECK_NOT_NULL(console);
    if (console == nullptr) return;

    DockTabGroup* center = leafAt(dock.get(), FVector2(400.0f, 300.0f));
    CHECK_NOT_NULL(center);
    if (center == nullptr) return;
    const FRectangle cb = center->getWorldBounds();
    const FVector2 eastPt(cb.maxX - 5.0f, (cb.minY + cb.maxY) * 0.5f);
    CHECK(dock->resolveTreeDropZone(center, eastPt) ==
          DockArea::TreeDropZone::East);

    CHECK(dragCardTo(f, console, eastPt));
    // The split changed the tree structure — lay out so the new leaf
    // gets its real bounds before hit-testing it.
    dock->performLayout();

    // console moved into a new g_0 leaf east of Center; Center still
    // owns viewport.
    DockTabGroup* g0 = leafAt(dock.get(), eastPt);
    CHECK_NOT_NULL(g0);
    if (g0 == nullptr) return;
    CHECK(g0->getLeafId() == "g_0");
    CHECK(g0->containsCard(console));
    CHECK(dock->getCardCount(DockArea::Slot::Center) == 1);
    CHECK(leafAt(dock.get(), centerOf(center))->getLeafId() == "Center");

    // The new leaf has a real share (not a hairline) and sits on
    // Center's east side (its center lies east of Center's center).
    const FRectangle gb = g0->getWorldBounds();
    CHECK(gb.maxX - gb.minX >= 40.0f);
    CHECK((gb.minX + gb.maxX) * 0.5f > (cb.minX + cb.maxX) * 0.5f);
}

// -------------------------------------------------------------------------
// 4. Splitting the new g_0 leaf again yields g_1 — arbitrary nesting
//    depth along the same split axis.
// -------------------------------------------------------------------------
TEST_CASE(test_nested_split_depth_two) {
    TreeFixture f;
    auto dock = makeDock(f);

    dock->addCard(DockArea::Slot::Left, treeMakeCard("console", L"Console"));
    dock->addCard(DockArea::Slot::Center, treeMakeCard("viewport", L"Viewport"));
    dock->performLayout();

    DockCard* console = dock->findCard("console");
    DockCard* viewport = dock->findCard("viewport");
    CHECK_NOT_NULL(console);
    CHECK_NOT_NULL(viewport);
    if (console == nullptr || viewport == nullptr) return;

    // console → Center east → g_0.
    DockTabGroup* center = leafAt(dock.get(), FVector2(400.0f, 300.0f));
    CHECK_NOT_NULL(center);
    if (center == nullptr) return;
    const FRectangle cb = center->getWorldBounds();
    CHECK(dragCardTo(f, console, FVector2(cb.maxX - 5.0f,
                                         (cb.minY + cb.maxY) * 0.5f)));

    dock->performLayout();
    const FVector2 eastPt(cb.maxX - 5.0f, (cb.minY + cb.maxY) * 0.5f);
    DockTabGroup* g0 = leafAt(dock.get(), eastPt);
    CHECK_NOT_NULL(g0);
    if (g0 == nullptr) return;
    CHECK(g0->getLeafId() == "g_0");

    // viewport → g_0 east → g_1 (depth 2).
    const FRectangle g0b = g0->getWorldBounds();
    const FVector2 g0East(g0b.maxX - 5.0f, (g0b.minY + g0b.maxY) * 0.5f);
    CHECK(dragCardTo(f, viewport, g0East));
    dock->performLayout();

    DockTabGroup* g1 = leafAt(dock.get(), g0East);
    CHECK_NOT_NULL(g1);
    if (g1 == nullptr) return;
    CHECK(g1->getLeafId() == "g_1");
    CHECK(g1->containsCard(viewport));
    CHECK(g0->containsCard(console));
    // g_0 and g_1 both live in the mid HBox, side by side.
    CHECK(g1->getWorldBounds().minX >= g0->getWorldBounds().minX);
}

// -------------------------------------------------------------------------
// 5. resolveTreeDropZone: 25% edge argmin; ties West > East > North >
//    South; empty leaves are always Join.
// -------------------------------------------------------------------------
TEST_CASE(test_resolve_tree_drop_zone_corners) {
    TreeFixture f;
    auto dock = makeDock(f);

    dock->addCard(DockArea::Slot::Center, treeMakeCard("viewport", L"Viewport"));
    dock->performLayout();

    DockTabGroup* center = leafAt(dock.get(), FVector2(400.0f, 300.0f));
    CHECK_NOT_NULL(center);
    if (center == nullptr) return;
    const FRectangle r = center->getWorldBounds();
    const float midX = (r.minX + r.maxX) * 0.5f;
    const float midY = (r.minY + r.maxY) * 0.5f;

    CHECK(dock->resolveTreeDropZone(center, FVector2(r.minX + 1.0f, midY))
          == DockArea::TreeDropZone::West);
    CHECK(dock->resolveTreeDropZone(center, FVector2(r.maxX - 1.0f, midY))
          == DockArea::TreeDropZone::East);
    CHECK(dock->resolveTreeDropZone(center, FVector2(midX, r.minY + 1.0f))
          == DockArea::TreeDropZone::North);
    CHECK(dock->resolveTreeDropZone(center, FVector2(midX, r.maxY - 1.0f))
          == DockArea::TreeDropZone::South);
    CHECK(dock->resolveTreeDropZone(center, FVector2(midX, midY))
          == DockArea::TreeDropZone::Join);

    // Corner point inside both edge bands: west distance and north
    // distance tie (both 24% of the extent) → West wins (array order).
    const float wx = r.minX + (r.maxX - r.minX) * 0.24f;
    const float wy = r.minY + (r.maxY - r.minY) * 0.24f;
    CHECK(dock->resolveTreeDropZone(center, FVector2(wx, wy))
          == DockArea::TreeDropZone::West);

    // Empty leaf → Join everywhere (its whole rect merges).
    CHECK(center->removeTab(dock->findCard("viewport")));
    CHECK(dock->resolveTreeDropZone(center, FVector2(r.minX + 1.0f, midY))
          == DockArea::TreeDropZone::Join);
}

// -------------------------------------------------------------------------
// 6. Same-leaf drop is a no-op in BOTH the center and edge zones
//    (K-INV-D3-1, incl. a 20px release near the edge).
// -------------------------------------------------------------------------
TEST_CASE(test_same_leaf_drop_noop_edge_and_center) {
    TreeFixture f;
    auto dock = makeDock(f);

    dock->addCard(DockArea::Slot::Left, treeMakeCard("console", L"Console"));
    dock->performLayout();

    DockCard* console = dock->findCard("console");
    CHECK_NOT_NULL(console);
    if (console == nullptr) return;

    // Center-of-leaf release.
    const FVector2 centerPt = centerOf(
        dock->hitTestTree(FVector2(80.0f, 300.0f)));
    CHECK(dragCardTo(f, console, centerPt));
    CHECK(dock->getCardCount(DockArea::Slot::Left) == 1);
    CHECK(dock->getOverlay()->getFloatingCardCount() == 0);

    // Edge-zone release (west band, ~1% from the left edge).
    const FRectangle b = console->getWorldBounds();
    const FVector2 westPt(b.minX + 2.0f, (b.minY + b.maxY) * 0.5f);
    CHECK(dock->resolveTreeDropZone(
              dock->hitTestTree(westPt), westPt)
          == DockArea::TreeDropZone::West);
    CHECK(dragCardTo(f, console, westPt));
    CHECK(dock->getCardCount(DockArea::Slot::Left) == 1);
    CHECK(dock->getOverlay()->getFloatingCardCount() == 0);
}

// -------------------------------------------------------------------------
// 7. Closing the card of a split-created leaf prunes it together with
//    its adjacent splitter; the freed region returns to Center.
// -------------------------------------------------------------------------
TEST_CASE(test_close_split_leaf_prunes) {
    TreeFixture f;
    auto dock = makeDock(f);

    dock->addCard(DockArea::Slot::Left, treeMakeCard("console", L"Console"));
    dock->addCard(DockArea::Slot::Center, treeMakeCard("viewport", L"Viewport"));
    dock->performLayout();

    DockCard* console = dock->findCard("console");
    CHECK_NOT_NULL(console);
    if (console == nullptr) return;

    DockTabGroup* center = leafAt(dock.get(), FVector2(400.0f, 300.0f));
    CHECK_NOT_NULL(center);
    if (center == nullptr) return;
    const FRectangle cb = center->getWorldBounds();
    const FVector2 eastPt(cb.maxX - 5.0f, (cb.minY + cb.maxY) * 0.5f);
    CHECK(dragCardTo(f, console, eastPt));
    dock->performLayout();

    DockTabGroup* g0 = leafAt(dock.get(), eastPt);
    CHECK_NOT_NULL(g0);
    if (g0 == nullptr) return;
    CHECK(g0->getLeafId() == "g_0");
    const FVector2 g0Center = centerOf(g0);

    // Close the split-created leaf's only card → prune.
    CHECK(dock->closeCard("console"));
    CHECK(dock->findCard("console") == nullptr);
    dock->performLayout();
    DockTabGroup* after = leafAt(dock.get(), g0Center);
    CHECK_NOT_NULL(after);
    if (after == nullptr) return;
    CHECK(after->getLeafId() != "g_0");
    CHECK(after->getLeafId() == "Center");

    // Pinned leaves survive being emptied (no prune of Left).
    CHECK(dock->getCardCount(DockArea::Slot::Left) == 0);
    CHECK(dock->getCardCount(DockArea::Slot::Center) == 1);
    DockTabGroup* left = leafAt(dock.get(), FVector2(80.0f, 300.0f));
    CHECK_NOT_NULL(left);
    if (left == nullptr) return;
    CHECK(left->getLeafId() == "Left");
}

// -------------------------------------------------------------------------
// 8. Structure edits flip the pristine guard: after a split, the
//    weight template must not clobber user sizing on the next
//    performLayout.
// -------------------------------------------------------------------------
TEST_CASE(test_splitter_sizing_survives_relayout_after_split) {
    TreeFixture f;
    auto dock = makeDock(f);

    dock->addCard(DockArea::Slot::Left, treeMakeCard("console", L"Console"));
    dock->addCard(DockArea::Slot::Center, treeMakeCard("viewport", L"Viewport"));
    dock->performLayout();

    DockCard* console = dock->findCard("console");
    CHECK_NOT_NULL(console);
    if (console == nullptr) return;

    DockTabGroup* center = leafAt(dock.get(), FVector2(400.0f, 300.0f));
    CHECK_NOT_NULL(center);
    if (center == nullptr) return;
    const FRectangle cb = center->getWorldBounds();
    CHECK(dragCardTo(f, console, FVector2(cb.maxX - 5.0f,
                                         (cb.minY + cb.maxY) * 0.5f)));

    // Find the Left leaf's slot index in the mid HBox.
    HBox* mid = midBox(dock.get());
    CHECK_NOT_NULL(mid);
    if (mid == nullptr) return;
    int leftSlot = -1;
    {
        const std::vector<Widget*>& kids = mid->getChildren();
        for (int i = 0; i < static_cast<int>(kids.size()); ++i) {
            auto* leaf = dynamic_cast<DockTabGroup*>(kids[i]);
            if (leaf != nullptr && leaf->getLeafId() == "Left") {
                leftSlot = i;
                break;
            }
        }
    }
    CHECK(leftSlot >= 0);
    if (leftSlot < 0) return;

    // Simulate a splitter drag by resizing the slot; the split already
    // bumped the epoch, so performLayout must keep this size.
    mid->setSlotSize(leftSlot, 180.0f);
    dock->performLayout();
    CHECK(std::fabs(mid->slotSize(leftSlot) - 180.0f) < 0.01f);
}

// -------------------------------------------------------------------------
// 9. A foreign (external) card drop still routes through the legacy
//    hitTestSlot + adoptCard path.
// -------------------------------------------------------------------------
TEST_CASE(test_external_card_drop_uses_legacy_adopt) {
    TreeFixture f;
    auto dock = makeDock(f);

    dock->addCard(DockArea::Slot::Center, treeMakeCard("viewport", L"Viewport"));
    dock->performLayout();

    // A card that does NOT belong to this dock (never added).
    auto external = treeMakeCard("external", L"External");
    f.ui.getOverlayRoot()->addChildExternal(external.get());
    external->setPosition(FVector2(700.0f, 550.0f));
    external->setSize(FVector2(60.0f, 40.0f));
    dock->performLayout();

    CHECK(dragCardTo(f, external.get(), FVector2(80.0f, 300.0f)));

    // adoptCard took ownership; the card now lives in the Left leaf.
    CHECK(dock->getCardCount(DockArea::Slot::Left) == 1);
    DockTabGroup* left = leafAt(dock.get(), FVector2(80.0f, 300.0f));
    CHECK_NOT_NULL(left);
    if (left == nullptr) return;
    CHECK(left->getLeafId() == "Left");
    CHECK(left->containsCard(dock->findCard("external")));
}

// -------------------------------------------------------------------------
// 10. A floating card dropped onto a split leaf's center joins it as a
//     tab (overlay → tree path).
// -------------------------------------------------------------------------
TEST_CASE(test_float_card_redock_into_split_leaf_joins) {
    TreeFixture f;
    auto dock = makeDock(f);

    dock->addCard(DockArea::Slot::Left, treeMakeCard("console", L"Console"));
    dock->addCard(DockArea::Slot::Center, treeMakeCard("viewport", L"Viewport"));
    dock->performLayout();

    DockCard* console = dock->findCard("console");
    DockCard* viewport = dock->findCard("viewport");
    CHECK_NOT_NULL(console);
    CHECK_NOT_NULL(viewport);
    if (console == nullptr || viewport == nullptr) return;

    // console → Center east → g_0.
    DockTabGroup* center = leafAt(dock.get(), FVector2(400.0f, 300.0f));
    CHECK_NOT_NULL(center);
    if (center == nullptr) return;
    const FRectangle cb = center->getWorldBounds();
    CHECK(dragCardTo(f, console, FVector2(cb.maxX - 5.0f,
                                         (cb.minY + cb.maxY) * 0.5f)));
    dock->performLayout();

    // Float viewport → overlay. Park it far from g_0's center: the
    // floating card's own rect must not cover the drop point, or the
    // G12 drag system resolves the drop target to the card itself and
    // the drop never reaches onDrop.
    CHECK(dock->floatCard("viewport", FVector2(660.0f, 480.0f)));
    DockCard* floating = dock->getOverlay()->getFloatingCard(0);
    CHECK_NOT_NULL(floating);
    if (floating == nullptr) return;

    // Drag the floating card onto g_0's center → join.
    const FVector2 eastPt(cb.maxX - 5.0f, (cb.minY + cb.maxY) * 0.5f);
    DockTabGroup* g0 = leafAt(dock.get(), eastPt);
    CHECK_NOT_NULL(g0);
    if (g0 == nullptr) return;
    CHECK(dragCardTo(f, floating, centerOf(g0)));

    CHECK(dock->getOverlay()->getFloatingCardCount() == 0);
    CHECK(g0->containsCard(console));
    CHECK(g0->containsCard(dock->findCard("viewport")));
    CHECK(g0->getTabCount() == 2);
    CHECK(g0->getActiveTabId() == "viewport");
}

// -------------------------------------------------------------------------
// 11. paintDropGuide paints the tree previews during a dock-owned drag:
//     join fill over the leaf, split band at the edge.
// -------------------------------------------------------------------------
TEST_CASE(test_drag_paints_tree_previews) {
    TreeFixture f;
    auto dock = makeDock(f);

    dock->addCard(DockArea::Slot::Left, treeMakeCard("console", L"Console"));
    dock->addCard(DockArea::Slot::Center, treeMakeCard("viewport", L"Viewport"));
    dock->performLayout();

    DockCard* console = dock->findCard("console");
    CHECK_NOT_NULL(console);
    if (console == nullptr) return;

    CHECK(treeTitleBarClick(console, treeTitleBarPoint(console)));
    CHECK(f.ui.isDragging());

    DockTabGroup* center = leafAt(dock.get(), FVector2(400.0f, 300.0f));
    CHECK_NOT_NULL(center);
    if (center == nullptr) return;
    const FRectangle cb = center->getWorldBounds();

    // Join preview: translucent blue fill matching the leaf bounds.
    f.ui.onMouseMove(centerOf(center).x, centerOf(center).y);
    f.backend.clear();
    dock->paintDropGuide(f.backend);
    bool foundJoin = false;
    for (const auto& dc : f.backend.getDrawCalls()) {
        if (dc.type != MockRenderer::DrawCall::Rect) {
            continue;
        }
        if (std::fabs(dc.color.w - 0.20f) < 0.02f
            && std::fabs(dc.bounds.minX - cb.minX) < 0.5f
            && std::fabs(dc.bounds.maxX - cb.maxX) < 0.5f
            && std::fabs(dc.bounds.minY - cb.minY) < 0.5f
            && std::fabs(dc.bounds.maxY - cb.maxY) < 0.5f) {
            foundJoin = true;
            break;
        }
    }
    CHECK(foundJoin);

    // Split preview: 25% edge band at Center's east side.
    const FVector2 eastPt(cb.maxX - 5.0f, (cb.minY + cb.maxY) * 0.5f);
    f.ui.onMouseMove(eastPt.x, eastPt.y);
    f.backend.clear();
    dock->paintDropGuide(f.backend);
    bool foundBand = false;
    for (const auto& dc : f.backend.getDrawCalls()) {
        if (dc.type != MockRenderer::DrawCall::Rect) {
            continue;
        }
        if (std::fabs(dc.color.w - 0.30f) < 0.02f
            && std::fabs(dc.bounds.maxX - cb.maxX) < 0.5f
            && dc.bounds.minX > cb.maxX - (cb.maxX - cb.minX) * 0.26f) {
            foundBand = true;
            break;
        }
    }
    CHECK(foundBand);

    f.ui.onKeyDown(UIKey_Escape);
    CHECK_FALSE(f.ui.isDragging());
}

// -------------------------------------------------------------------------
// 12. serialize → rebuild cards → apply round-trips the tree structure,
//     tab order, and active tab (Gallery Save → Reload JSON → Load flow).
// -------------------------------------------------------------------------
TEST_CASE(test_serialize_apply_round_trip) {
    TreeFixture f;
    auto dock = makeDock(f);

    dock->addCard(DockArea::Slot::Left, treeMakeCard("left_card", L"Left"));
    dock->addCard(DockArea::Slot::Center, treeMakeCard("viewport", L"Viewport"));
    dock->addCard(DockArea::Slot::Center, treeMakeCard("console", L"Console"));
    dock->performLayout();

    const std::string s = dock->serializeDockTree();
    CHECK(s.find("\"leaf\":\"Center\"") != std::string::npos);
    CHECK(s.find("\"leaf\":\"Left\"") != std::string::npos);

    // Simulate the Gallery loadAndWire flow: a fresh dock + recreated
    // cards (content comes from the JSON layout, not the dock tree).
    auto dock2 = makeDock(f);
    dock2->addCard(DockArea::Slot::Left, treeMakeCard("left_card", L"Left"));
    dock2->addCard(DockArea::Slot::Center, treeMakeCard("viewport", L"Viewport"));
    dock2->addCard(DockArea::Slot::Center, treeMakeCard("console", L"Console"));
    dock2->performLayout();

    CHECK(dock2->applyDockTree(s));
    dock2->performLayout();

    DockTabGroup* center = leafAt(dock2.get(), FVector2(400.0f, 300.0f));
    CHECK_NOT_NULL(center);
    if (center == nullptr) return;
    CHECK(center->getLeafId() == "Center");
    CHECK(center->getTabCount() == 2);
    CHECK(center->getTab(0)->getId() == "viewport");
    CHECK(center->getTab(1)->getId() == "console");
    CHECK(center->getActiveTabId() == "console");

    DockTabGroup* left = leafAt(dock2.get(), FVector2(80.0f, 300.0f));
    CHECK_NOT_NULL(left);
    if (left == nullptr) return;
    CHECK(left->getLeafId() == "Left");
    CHECK(left->getTabCount() == 1);
    CHECK(left->getTab(0)->getId() == "left_card");
}

// -------------------------------------------------------------------------
// 13. A card the JSON references but the host never recreated is
//     skipped without error; the rest of the tree still applies.
// -------------------------------------------------------------------------
TEST_CASE(test_apply_missing_card_ids_skipped) {
    TreeFixture f;
    auto dock = makeDock(f);

    dock->addCard(DockArea::Slot::Left, treeMakeCard("left_card", L"Left"));
    dock->addCard(DockArea::Slot::Center, treeMakeCard("viewport", L"Viewport"));
    dock->addCard(DockArea::Slot::Center, treeMakeCard("console", L"Console"));
    dock->performLayout();

    const std::string s = dock->serializeDockTree();

    // Host only recreates viewport + left_card (console is gone).
    auto dock2 = makeDock(f);
    dock2->addCard(DockArea::Slot::Left, treeMakeCard("left_card", L"Left"));
    dock2->addCard(DockArea::Slot::Center, treeMakeCard("viewport", L"Viewport"));
    dock2->performLayout();

    CHECK(dock2->applyDockTree(s));
    dock2->performLayout();

    DockTabGroup* center = leafAt(dock2.get(), FVector2(400.0f, 300.0f));
    CHECK_NOT_NULL(center);
    if (center == nullptr) return;
    CHECK(center->getTabCount() == 1);
    CHECK(center->getTab(0)->getId() == "viewport");
    // Active "console" no longer exists — falls back to the first tab.
    CHECK(center->getActiveTabId() == "viewport");
}

// -------------------------------------------------------------------------
// 14. Floating cards round-trip with their rects.
// -------------------------------------------------------------------------
TEST_CASE(test_floating_round_trip) {
    TreeFixture f;
    auto dock = makeDock(f);

    dock->addCard(DockArea::Slot::Center, treeMakeCard("viewport", L"Viewport"));
    dock->performLayout();
    CHECK(dock->floatCard("viewport", FVector2(340.0f, 120.0f)));
    DockCard* floating = dock->getOverlay()->getFloatingCard(0);
    CHECK_NOT_NULL(floating);
    if (floating == nullptr) return;
    floating->setSize(FVector2(300.0f, 180.0f));

    const std::string s = dock->serializeDockTree();
    CHECK(s.find("\"floating\"") != std::string::npos);

    auto dock2 = makeDock(f);
    dock2->addCard(DockArea::Slot::Center, treeMakeCard("viewport", L"Viewport"));
    dock2->performLayout();
    CHECK(dock2->applyDockTree(s));
    dock2->performLayout();

    CHECK(dock2->getOverlay()->getFloatingCardCount() == 1);
    DockCard* restored = dock2->getOverlay()->getFloatingCard(0);
    CHECK_NOT_NULL(restored);
    if (restored == nullptr) return;
    CHECK(restored->getId() == "viewport");
    CHECK(std::fabs(restored->getPosition().x - 340.0f) < 0.5f);
    CHECK(std::fabs(restored->getPosition().y - 120.0f) < 0.5f);
    CHECK(std::fabs(restored->getSize().x - 300.0f) < 0.5f);
    CHECK(std::fabs(restored->getSize().y - 180.0f) < 0.5f);
}

// -------------------------------------------------------------------------
// 15. Cards the JSON never references float back to the overlay instead
//     of being dropped.
// -------------------------------------------------------------------------
TEST_CASE(test_apply_orphan_cards_float_back) {
    TreeFixture f;
    auto dock = makeDock(f);

    dock->addCard(DockArea::Slot::Left, treeMakeCard("left_card", L"Left"));
    dock->addCard(DockArea::Slot::Center, treeMakeCard("viewport", L"Viewport"));
    dock->performLayout();

    // Serialize a tree that only knows about viewport (left_card is
    // orphaned by hand-editing the JSON away).
    const std::string s = dock->serializeDockTree();
    const std::string orphan =
        "{\"version\":1,\"dockTree\":{\"orientation\":\"H\",\"weights\":"
        "[0.0],\"children\":[{\"leaf\":\"Center\",\"tabs\":[\"viewport\"],"
        "\"active\":\"viewport\"}]},\"floating\":[]}";

    CHECK(dock->applyDockTree(orphan));
    dock->performLayout();

    CHECK(dock->getOverlay()->getFloatingCardCount() == 1);
    DockCard* orphaned = dock->getOverlay()->getFloatingCard(0);
    CHECK_NOT_NULL(orphaned);
    if (orphaned == nullptr) return;
    CHECK(orphaned->getId() == "left_card");
    // viewport stayed docked.
    DockTabGroup* center = leafAt(dock.get(), FVector2(400.0f, 300.0f));
    CHECK_NOT_NULL(center);
    if (center == nullptr) return;
    CHECK(center->getLeafId() == "Center");
    CHECK(center->getTabCount() == 1);
    CHECK(center->getTab(0)->getId() == "viewport");
    (void)s;
}

// -------------------------------------------------------------------------
// 16. Malformed JSON returns false and leaves the tree untouched.
// -------------------------------------------------------------------------
TEST_CASE(test_apply_malformed_json_false) {
    TreeFixture f;
    auto dock = makeDock(f);

    dock->addCard(DockArea::Slot::Center, treeMakeCard("viewport", L"Viewport"));
    dock->performLayout();
    const size_t before = dock->getCardCount(DockArea::Slot::Center);

    CHECK_FALSE(dock->applyDockTree("{ not json"));
    CHECK(dock->getCardCount(DockArea::Slot::Center) == before);
    DockTabGroup* center = leafAt(dock.get(), FVector2(400.0f, 300.0f));
    CHECK_NOT_NULL(center);
    if (center == nullptr) return;
    CHECK(center->getLeafId() == "Center");
    CHECK(center->containsCard(dock->findCard("viewport")));
}

// -------------------------------------------------------------------------
// 17. Split-created g_N leaves survive the round-trip (nested structure
//     with splitters), then prune normally when emptied.
// -------------------------------------------------------------------------
TEST_CASE(test_serialize_split_leaf_round_trip) {
    TreeFixture f;
    auto dock = makeDock(f);

    dock->addCard(DockArea::Slot::Left, treeMakeCard("console", L"Console"));
    dock->addCard(DockArea::Slot::Center, treeMakeCard("viewport", L"Viewport"));
    dock->performLayout();

    DockCard* console = dock->findCard("console");
    CHECK_NOT_NULL(console);
    if (console == nullptr) return;
    DockTabGroup* center = leafAt(dock.get(), FVector2(400.0f, 300.0f));
    CHECK_NOT_NULL(center);
    if (center == nullptr) return;
    const FRectangle cb = center->getWorldBounds();
    CHECK(dragCardTo(f, console, FVector2(cb.maxX - 5.0f,
                                         (cb.minY + cb.maxY) * 0.5f)));
    dock->performLayout();

    DockTabGroup* g0 = leafAt(dock.get(),
        FVector2(cb.maxX - 5.0f, (cb.minY + cb.maxY) * 0.5f));
    CHECK_NOT_NULL(g0);
    if (g0 == nullptr) return;
    CHECK(g0->getLeafId() == "g_0");

    const std::string s = dock->serializeDockTree();
    CHECK(s.find("\"leaf\":\"g_0\"") != std::string::npos);

    auto dock2 = makeDock(f);
    dock2->addCard(DockArea::Slot::Left, treeMakeCard("console", L"Console"));
    dock2->addCard(DockArea::Slot::Center, treeMakeCard("viewport", L"Viewport"));
    dock2->performLayout();
    CHECK(dock2->applyDockTree(s));
    dock2->performLayout();

    // g_0 survived as a non-pinned leaf holding console.
    const FVector2 g0Pt(cb.maxX - 5.0f, (cb.minY + cb.maxY) * 0.5f);
    DockTabGroup* g0b = leafAt(dock2.get(), g0Pt);
    CHECK_NOT_NULL(g0b);
    if (g0b == nullptr) return;
    CHECK(g0b->getLeafId() == "g_0");
    CHECK(g0b->containsCard(dock2->findCard("console")));
    CHECK_FALSE(g0b->isPinned());

    // And it prunes when emptied, like a split leaf born at runtime.
    CHECK(dock2->closeCard("console"));
    dock2->performLayout();
    DockTabGroup* after = leafAt(dock2.get(), g0Pt);
    CHECK_NOT_NULL(after);
    if (after == nullptr) return;
    CHECK(after->getLeafId() == "Center");
}

TEST_SUITE_END
