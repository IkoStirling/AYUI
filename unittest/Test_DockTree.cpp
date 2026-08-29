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
//   * same-leaf Join → no-op; solo edge → no-op; multi-tab edge → split
//   * close/float emptying a g_N leaf → prune (with adjacent
//     splitter), pinned leaves never pruned
//   * structure edits flip the pristine guard — the weight template
//     stops clobbering user sizing
//   * external (foreign) card drops still take the legacy
//     hitTestSlot + adoptCard path
//   * paintDropGuide paints the tree join/split previews
// =============================================================================

#include "AYTest.h"
#include "AYUI/UIManager.h"
#include "AYUI/DockArea.h"
#include "AYUI/DockCard.h"
#include "AYUI/DockOverlay.h"
#include "AYUI/DockTabGroup.h"
#include "AYUI/DragDrop.h"
#include "AYUI/MockRenderer.h"
#include "AYUI/Box.h"
#include "AYUI/SplitterHandle.h"
#include "AYUI/UIKeyCode.h"

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

DockTabGroup* findLeafById(Widget* node, const std::string& leafId) {
    if (node == nullptr) {
        return nullptr;
    }
    if (auto* leaf = dynamic_cast<DockTabGroup*>(node)) {
        return leaf->getLeafId() == leafId ? leaf : nullptr;
    }
    for (Widget* child : node->getChildren()) {
        if (DockTabGroup* found = findLeafById(child, leafId)) {
            return found;
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
// 3b. South-edge drop on Center (orthogonal to the mid HBox) wraps Center
//     in a nested VBox: Center fills the remainder, g_0 takes ~25% height
//     below. Without the wrap, 25% of height was mis-applied as an HBox
//     width and Center looked "short".
// -------------------------------------------------------------------------
TEST_CASE(test_drop_south_wraps_orthogonal_nest) {
    TreeFixture f;
    auto dock = makeDock(f);

    dock->addCard(DockArea::Slot::Left, treeMakeCard("console", L"Console"));
    dock->addCard(DockArea::Slot::Center, treeMakeCard("viewport", L"Viewport"));
    dock->addCard(DockArea::Slot::Right, treeMakeCard("inspector", L"Inspector"));
    dock->performLayout();

    DockCard* inspector = dock->findCard("inspector");
    CHECK_NOT_NULL(inspector);
    if (inspector == nullptr) return;

    DockTabGroup* center = leafAt(dock.get(), FVector2(400.0f, 300.0f));
    CHECK_NOT_NULL(center);
    if (center == nullptr) return;
    const FRectangle cb = center->getWorldBounds();
    const FVector2 southPt((cb.minX + cb.maxX) * 0.5f, cb.maxY - 5.0f);
    CHECK(dock->resolveTreeDropZone(center, southPt) ==
          DockArea::TreeDropZone::South);

    CHECK(dragCardTo(f, inspector, southPt));
    dock->performLayout();

    DockTabGroup* g0 = leafAt(dock.get(), southPt);
    CHECK_NOT_NULL(g0);
    if (g0 == nullptr) return;
    CHECK(g0->getLeafId() == "g_0");
    CHECK(g0->containsCard(inspector));

    DockTabGroup* centerAfter = leafAt(dock.get(),
        FVector2((cb.minX + cb.maxX) * 0.5f, cb.minY + 20.0f));
    CHECK_NOT_NULL(centerAfter);
    if (centerAfter == nullptr) return;
    CHECK(centerAfter->getLeafId() == "Center");

    const FRectangle gb = g0->getWorldBounds();
    const FRectangle cab = centerAfter->getWorldBounds();
    // Nested below: g_0 sits under Center, shares the same column x-range,
    // and Center still fills most of the column height (not a hairline).
    CHECK(gb.minY >= cab.maxY - 2.0f);
    CHECK(std::fabs((gb.minX + gb.maxX) * 0.5f
                    - (cab.minX + cab.maxX) * 0.5f) < 40.0f);
    CHECK(cab.maxY - cab.minY >= (cb.maxY - cb.minY) * 0.50f);
    CHECK(gb.maxY - gb.minY >= 40.0f);
    // Nest parent is a VBox under the mid HBox.
    auto* nest = dynamic_cast<VBox*>(centerAfter->getParent());
    CHECK_NOT_NULL(nest);
    if (nest == nullptr) return;

    // Nest must be flush with mid (no default BoxBase 4px padding) so the
    // Left column and Center|g_0 stack share the same top/bottom and the
    // only horizontal gap is the splitter.
    DockTabGroup* leftLeaf = nullptr;
    if (HBox* mid = midBox(dock.get())) {
        for (Widget* c : mid->getChildren()) {
            auto* leaf = dynamic_cast<DockTabGroup*>(c);
            if (leaf != nullptr && leaf->getLeafId() == "Left") {
                leftLeaf = leaf;
                break;
            }
        }
    }
    CHECK_NOT_NULL(leftLeaf);
    if (leftLeaf == nullptr) return;
    const FRectangle lb = leftLeaf->getWorldBounds();
    const FRectangle nb = nest->getWorldBounds();
    CHECK(std::fabs(nb.minY - lb.minY) < 1.0f);
    CHECK(std::fabs(nb.maxY - lb.maxY) < 1.0f);
    CHECK(nb.minX >= lb.maxX - 1.0f);
    CHECK(nb.minX <= lb.maxX + SplitterHandle::kDefaultWidth + 2.0f);
}

// -------------------------------------------------------------------------
// 3c. Join after orthogonal split must not UAF: Left → Center-South →
//     drag that card onto Center's Join zone. prune deletes g_0; tracing
//     srcLeaf after prune used to crash. Nest unwrap restores Center as
//     a direct mid-HBox child.
// -------------------------------------------------------------------------
TEST_CASE(test_join_after_south_split_no_crash) {
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
    const FVector2 southPt((cb.minX + cb.maxX) * 0.5f, cb.maxY - 5.0f);
    CHECK(dragCardTo(f, console, southPt));
    dock->performLayout();

    DockTabGroup* g0 = leafAt(dock.get(), southPt);
    CHECK_NOT_NULL(g0);
    if (g0 == nullptr) return;
    CHECK(g0->getLeafId() == "g_0");

    // Join back onto Center (center of Center's remaining band).
    DockTabGroup* centerBand = leafAt(dock.get(),
        FVector2((cb.minX + cb.maxX) * 0.5f, cb.minY + 20.0f));
    CHECK_NOT_NULL(centerBand);
    if (centerBand == nullptr) return;
    const FVector2 joinPt = centerOf(centerBand);
    CHECK(dock->resolveTreeDropZone(centerBand, joinPt) ==
          DockArea::TreeDropZone::Join);
    CHECK(dragCardTo(f, console, joinPt));
    dock->performLayout();

    // Console is a Center tab; g_0 is gone; Center is no longer nested.
    CHECK(dock->getCardCount(DockArea::Slot::Center) == 2);
    CHECK(dock->findCard("console") != nullptr);
    DockTabGroup* after = leafAt(dock.get(), joinPt);
    CHECK_NOT_NULL(after);
    if (after == nullptr) return;
    CHECK(after->getLeafId() == "Center");
    CHECK(after->containsCard(console));
    CHECK(dynamic_cast<HBox*>(after->getParent()) != nullptr);
}

// -------------------------------------------------------------------------
// 3d. Real editor crash path: tear the active tab out of a two-tab pinned
//     Left leaf, then drag the remaining solo card onto the new g_0 edge.
//     Emptying Left extracts it from the nest while splitLeaf still owns an
//     empty, not-yet-populated g_1. Recursive prune at that point used to
//     delete g_1 and splitLeaf subsequently dereferenced the freed leaf.
// -------------------------------------------------------------------------
TEST_CASE(test_split_pinned_tab_then_split_remaining_card_no_uaf) {
    TreeFixture f;
    auto dock = makeDock(f);

    dock->addCard(DockArea::Slot::Left,
                  treeMakeCard("outliner", L"Outliner"));
    dock->addCard(DockArea::Slot::Left,
                  treeMakeCard("render", L"Render"));
    dock->addCard(DockArea::Slot::Center,
                  treeMakeCard("viewport", L"Viewport"));
    dock->addCard(DockArea::Slot::Right,
                  treeMakeCard("inspector", L"Inspector"));
    dock->performLayout();

    DockTabGroup* left = findLeafById(rootBox(dock.get()), "Left");
    CHECK_NOT_NULL(left);
    if (left == nullptr) return;
    CHECK(left->getActiveTabId() == "render");

    // Start through UIManager (not the direct DockCard helper) so this is
    // the same active-tab-strip input path used by AYEditorShellDemo.
    const FRectangle lb = left->getWorldBounds();
    const FVector2 activeTabPt(lb.minX + (lb.maxX - lb.minX) * 0.75f,
                               lb.minY + 10.0f);
    CHECK(f.ui.onMouseButtonDown(activeTabPt.x, activeTabPt.y, 0));
    CHECK(f.ui.isDragging());
    const FVector2 firstDrop((lb.minX + lb.maxX) * 0.5f, lb.minY + 5.0f);
    f.ui.onMouseMove(firstDrop.x, firstDrop.y);
    f.ui.onMouseButtonUp(firstDrop.x, firstDrop.y, 0);
    CHECK_FALSE(f.ui.isDragging());
    dock->performLayout();

    DockTabGroup* g0 = findLeafById(rootBox(dock.get()), "g_0");
    left = findLeafById(rootBox(dock.get()), "Left");
    DockCard* outliner = dock->findCard("outliner");
    CHECK_NOT_NULL(g0);
    CHECK_NOT_NULL(left);
    CHECK_NOT_NULL(outliner);
    if (g0 == nullptr || left == nullptr || outliner == nullptr) return;
    CHECK(g0->containsCard(dock->findCard("render")));
    CHECK(left->containsCard(outliner));

    // Split at g_0's north edge while the only remaining Left card is the
    // drag source. Left becomes empty and is re-homed into the template.
    const FVector2 sourcePt = treeTitleBarPoint(outliner);
    CHECK(f.ui.onMouseButtonDown(sourcePt.x, sourcePt.y, 0));
    CHECK(f.ui.isDragging());
    const FRectangle gb = g0->getWorldBounds();
    const FVector2 secondDrop((gb.minX + gb.maxX) * 0.5f, gb.minY + 5.0f);
    f.ui.onMouseMove(secondDrop.x, secondDrop.y);
    f.ui.onMouseButtonUp(secondDrop.x, secondDrop.y, 0);
    CHECK_FALSE(f.ui.isDragging());
    dock->performLayout();

    DockTabGroup* g1 = findLeafById(rootBox(dock.get()), "g_1");
    left = findLeafById(rootBox(dock.get()), "Left");
    CHECK_NOT_NULL(g1);
    CHECK_NOT_NULL(left);
    if (g1 == nullptr || left == nullptr) return;
    CHECK(g1->containsCard(outliner));
    CHECK(left->getTabCount() == 0);

    // Exercise the next input/tick after the structural edit; stale hover
    // or capture references often surface one frame after the drop.
    f.ui.onMouseMove(400.0f, 300.0f);
    f.ui.update(1.0f / 60.0f);
}

// -------------------------------------------------------------------------
// 3e. Empty side pinned leaves collapse (hidden + bordering splitters)
//     so the fill column expands. Re-docking into that slot expands them.
// -------------------------------------------------------------------------
TEST_CASE(test_empty_side_pinned_collapses_and_expands) {
    TreeFixture f;
    auto dock = makeDock(f);

    dock->addCard(DockArea::Slot::Left, treeMakeCard("console", L"Console"));
    dock->addCard(DockArea::Slot::Center, treeMakeCard("viewport", L"Viewport"));
    dock->addCard(DockArea::Slot::Right, treeMakeCard("inspector", L"Inspector"));
    dock->performLayout();

    DockTabGroup* center = leafAt(dock.get(), FVector2(400.0f, 300.0f));
    CHECK_NOT_NULL(center);
    if (center == nullptr) return;
    const float centerW0 = center->getWorldBounds().maxX
                         - center->getWorldBounds().minX;

    DockCard* inspector = dock->findCard("inspector");
    CHECK_NOT_NULL(inspector);
    if (inspector == nullptr) return;
    const FRectangle cb = center->getWorldBounds();
    const FVector2 southPt((cb.minX + cb.maxX) * 0.5f, cb.maxY - 5.0f);
    CHECK(dragCardTo(f, inspector, southPt));
    dock->performLayout();

    // Right pinned leaf collapsed; Center column grew.
    DockTabGroup* right = nullptr;
    if (HBox* mid = midBox(dock.get())) {
        for (Widget* c : mid->getChildren()) {
            auto* leaf = dynamic_cast<DockTabGroup*>(c);
            if (leaf != nullptr && leaf->getLeafId() == "Right") {
                right = leaf;
                break;
            }
        }
    }
    CHECK_NOT_NULL(right);
    if (right == nullptr) return;
    CHECK_FALSE(right->isVisible());

    DockTabGroup* centerAfter = leafAt(dock.get(),
        FVector2((cb.minX + cb.maxX) * 0.5f, cb.minY + 20.0f));
    CHECK_NOT_NULL(centerAfter);
    if (centerAfter == nullptr) return;
    // Center may now be inside a nest VBox — measure the nest's width
    // (parent) or the leaf itself if still direct.
    Widget* col = centerAfter->getParent();
    if (dynamic_cast<VBox*>(col) == nullptr) {
        col = centerAfter;
    }
    const float centerW1 = col->getWorldBounds().maxX
                         - col->getWorldBounds().minX;
    CHECK(centerW1 > centerW0 + 20.0f);

    // moveInSlot back into Right expands the side slot.
    CHECK(dock->moveInSlot("inspector", DockArea::Slot::Right));
    dock->performLayout();
    CHECK(right->isVisible());
    CHECK(dock->getCardCount(DockArea::Slot::Right) == 1);
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
// 6. Same-leaf Join is a no-op; same-leaf Edge with a solo card is also
//    a no-op. Multi-tab edge tear-off is covered separately.
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

    // Solo-card edge-zone release stays NO-OP (cannot split yourself).
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
// 6b. Multi-tab same-leaf edge drop tears the card into a new split
//     (Gallery: drag one Center tab to the leaf edge → separate).
// -------------------------------------------------------------------------
TEST_CASE(test_same_leaf_edge_splits_when_multi_tab) {
    TreeFixture f;
    auto dock = makeDock(f);

    dock->addCard(DockArea::Slot::Center, treeMakeCard("a", L"A"));
    dock->addCard(DockArea::Slot::Center, treeMakeCard("b", L"B"));
    dock->performLayout();

    DockCard* b = dock->findCard("b");
    CHECK_NOT_NULL(b);
    if (b == nullptr) return;

    DockTabGroup* center = leafAt(dock.get(), FVector2(400.0f, 300.0f));
    CHECK_NOT_NULL(center);
    if (center == nullptr) return;
    CHECK(center->getTabCount() == 2);

    const FRectangle cb = center->getWorldBounds();
    const FVector2 eastPt(cb.maxX - 5.0f, (cb.minY + cb.maxY) * 0.5f);
    CHECK(dock->resolveTreeDropZone(center, eastPt) ==
          DockArea::TreeDropZone::East);
    CHECK(dragCardTo(f, b, eastPt));
    dock->performLayout();

    CHECK(dock->getCardCount(DockArea::Slot::Center) == 1);
    DockTabGroup* g0 = leafAt(dock.get(), eastPt);
    CHECK_NOT_NULL(g0);
    if (g0 == nullptr) return;
    CHECK(g0->getLeafId() != "Center");
    CHECK(g0->containsCard(b));
}

// -------------------------------------------------------------------------
// 6c. East-split after Right collapsed must revive the reused Center|Right
//     splitter (was hidden with the empty Right). Gallery D1: boundary
//     between Center and the new east leaf must stay draggable.
// -------------------------------------------------------------------------
TEST_CASE(test_east_split_revives_hidden_right_splitter) {
    TreeFixture f;
    auto dock = makeDock(f);

    dock->addCard(DockArea::Slot::Left, treeMakeCard("console", L"Console"));
    dock->addCard(DockArea::Slot::Center, treeMakeCard("viewport", L"Viewport"));
    dock->addCard(DockArea::Slot::Right, treeMakeCard("inspector", L"Inspector"));
    dock->performLayout();

    DockCard* inspector = dock->findCard("inspector");
    DockCard* console = dock->findCard("console");
    CHECK_NOT_NULL(inspector);
    CHECK_NOT_NULL(console);
    if (inspector == nullptr || console == nullptr) return;

    // Collapse Right first so its adjacent splitter is hidden (the bug setup).
    CHECK(dock->floatCard("inspector", FVector2(50.0f, 50.0f)));
    dock->performLayout();

    HBox* mid = midBox(dock.get());
    CHECK_NOT_NULL(mid);
    if (mid == nullptr) return;
    SplitterHandle* hiddenSplit = nullptr;
    for (int i = 0; ; ++i) {
        Widget* w = mid->slotAt(i);
        if (w == nullptr) {
            break;
        }
        if (mid->isSplitterSlot(i) && !w->isVisible()) {
            hiddenSplit = dynamic_cast<SplitterHandle*>(w);
        }
    }
    CHECK_NOT_NULL(hiddenSplit);
    if (hiddenSplit == nullptr) return;
    CHECK_FALSE(hiddenSplit->isVisible());

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
    CHECK(g0->containsCard(console));
    CHECK(g0->getLeafId() != "Center");

    // The previously-hidden Center|Right handle is now Center|g_N and must
    // be visible + draggable again.
    CHECK(hiddenSplit->isVisible());
    const int gi = mid->slotIndexOf(g0);
    CHECK(gi > 0);
    CHECK(mid->slotAt(gi - 1) == hiddenSplit);

    const FRectangle sb = hiddenSplit->getWorldBounds();
    const float splitX = (sb.minX + sb.maxX) * 0.5f;
    const float splitY = (sb.minY + sb.maxY) * 0.5f;
    CHECK(dock->hitTest(FVector2(splitX, splitY)) == hiddenSplit);

    const float w0 = center->getWidth();
    CHECK(hiddenSplit->onMouseButtonDown(UIMouseEvent(FVector2(splitX, splitY), 0)));
    CHECK(hiddenSplit->onMouseMove(UIMouseEvent(FVector2(splitX - 40.0f, splitY), 0)));
    CHECK(hiddenSplit->onMouseButtonUp(UIMouseEvent(FVector2(splitX - 40.0f, splitY), 0)));
    dock->performLayout();
    CHECK(std::fabs(center->getWidth() - w0) > 1.0f);
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

    // Pinned side leaves collapse when emptied (no prune — they stay in
    // the tree but hidden so fill expands). Empty Center also hides.
    CHECK(dock->getCardCount(DockArea::Slot::Left) == 0);
    CHECK(dock->getCardCount(DockArea::Slot::Center) == 1);
    DockTabGroup* left = nullptr;
    if (HBox* mid = midBox(dock.get())) {
        for (Widget* c : mid->getChildren()) {
            auto* leaf = dynamic_cast<DockTabGroup*>(c);
            if (leaf != nullptr && leaf->getLeafId() == "Left") {
                left = leaf;
                break;
            }
        }
    }
    CHECK_NOT_NULL(left);
    if (left == nullptr) return;
    CHECK_FALSE(left->isVisible());
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

    // Find the Left leaf's slot index in the mid HBox (may be collapsed
    // after console moved away — still present, just hidden).
    HBox* mid = midBox(dock.get());
    CHECK_NOT_NULL(mid);
    if (mid == nullptr) return;
    int leftSlot = -1;
    DockTabGroup* leftLeaf = nullptr;
    for (Widget* c : mid->getChildren()) {
        auto* leaf = dynamic_cast<DockTabGroup*>(c);
        if (leaf != nullptr && leaf->getLeafId() == "Left") {
            leftLeaf = leaf;
            leftSlot = mid->slotIndexOf(leaf);
            break;
        }
    }
    CHECK_NOT_NULL(leftLeaf);
    CHECK(leftSlot >= 0);
    if (leftLeaf == nullptr || leftSlot < 0) return;

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
    // Layout must run so the revived Left leaf gets real bounds before
    // hit-testing (collapsed sides start at size 0 until performLayout).
    dock->performLayout();
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

    // Drop guides are immediate-mode overlay commands and must be emitted
    // every frame even if the mouse has not moved. A cursor-position gate
    // made the guide alternate between present and absent frames.
    f.backend.clear();
    dock->paintDropGuide(f.backend);
    bool foundStationaryJoin = false;
    for (const auto& dc : f.backend.getDrawCalls()) {
        if (dc.type == MockRenderer::DrawCall::Rect
            && std::fabs(dc.color.w - 0.20f) < 0.02f
            && std::fabs(dc.bounds.minX - cb.minX) < 0.5f
            && std::fabs(dc.bounds.maxX - cb.maxX) < 0.5f
            && std::fabs(dc.bounds.minY - cb.minY) < 0.5f
            && std::fabs(dc.bounds.maxY - cb.maxY) < 0.5f) {
            foundStationaryJoin = true;
            break;
        }
    }
    CHECK(foundStationaryJoin);

    // Split preview: 25% edge band at Center's east side (keep ≥10px
    // from the dock outer edge so a collapsed Right revive strip does
    // not steal the drop).
    const FVector2 eastPt(cb.maxX - 15.0f, (cb.minY + cb.maxY) * 0.5f);
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

// -------------------------------------------------------------------------
// 18. Emptying a side pinned leaf that sits inside an orthogonal wrap
//     nest must NOT leave a fill hole (Gallery: Right-North → join away
//     → dead band under g_N). The pinned leaf is re-homed to mid and
//     the nest unwraps / last panel fills.
// -------------------------------------------------------------------------
TEST_CASE(test_side_north_wrap_then_join_away_no_hole) {
    TreeFixture f;
    auto dock = makeDock(f);

    dock->addCard(DockArea::Slot::Left, treeMakeCard("left", L"Left"));
    dock->addCard(DockArea::Slot::Center, treeMakeCard("center", L"Center"));
    dock->addCard(DockArea::Slot::Right, treeMakeCard("right", L"Right"));
    dock->performLayout();

    DockCard* left = dock->findCard("left");
    DockCard* right = dock->findCard("right");
    CHECK_NOT_NULL(left);
    CHECK_NOT_NULL(right);
    if (left == nullptr || right == nullptr) return;

    DockTabGroup* rightLeaf = leafAt(dock.get(), FVector2(700.0f, 300.0f));
    CHECK_NOT_NULL(rightLeaf);
    if (rightLeaf == nullptr) return;
    CHECK(rightLeaf->getLeafId() == "Right");
    const FRectangle rb = rightLeaf->getWorldBounds();
    const FVector2 northPt((rb.minX + rb.maxX) * 0.5f, rb.minY + 5.0f);
    CHECK(dock->resolveTreeDropZone(rightLeaf, northPt) ==
          DockArea::TreeDropZone::North);

    // Left → Right-North: wraps Right into a VBox nest [g_0, Right].
    CHECK(dragCardTo(f, left, northPt));
    dock->performLayout();

    DockTabGroup* g0 = leafAt(dock.get(), northPt);
    CHECK_NOT_NULL(g0);
    if (g0 == nullptr) return;
    CHECK(g0->getLeafId() == "g_0");
    CHECK(g0->containsCard(left));
    CHECK(dynamic_cast<VBox*>(g0->getParent()) != nullptr);

    // Join Right's remaining card into Center — empties Right inside nest.
    DockTabGroup* center = leafAt(dock.get(), FVector2(400.0f, 300.0f));
    CHECK_NOT_NULL(center);
    if (center == nullptr) return;
    CHECK(dragCardTo(f, right, centerOf(center)));
    dock->performLayout();

    CHECK(dock->getCardCount(DockArea::Slot::Center) >= 1);
    CHECK(dock->getCardCount(DockArea::Slot::Right) == 0);

    // g_0 must fill the former Right column — no dead band under it.
    DockTabGroup* g0After = nullptr;
    HBox* mid = midBox(dock.get());
    CHECK_NOT_NULL(mid);
    if (mid == nullptr) return;
    for (Widget* c : mid->getChildren()) {
        auto* leaf = dynamic_cast<DockTabGroup*>(c);
        if (leaf != nullptr && leaf->getLeafId() == "g_0") {
            g0After = leaf;
            break;
        }
        // Still nested? measure the nest VBox instead.
        if (auto* nest = dynamic_cast<VBox*>(c)) {
            for (Widget* nc : nest->getChildren()) {
                auto* nl = dynamic_cast<DockTabGroup*>(nc);
                if (nl != nullptr && nl->getLeafId() == "g_0") {
                    g0After = nl;
                    break;
                }
            }
        }
    }
    CHECK_NOT_NULL(g0After);
    if (g0After == nullptr) return;

    const FRectangle gb = g0After->getWorldBounds();
    const float midH = mid->getWorldBounds().maxY - mid->getWorldBounds().minY;
    // Visible panel must consume most of the mid row height (not ~25%).
    CHECK(gb.maxY - gb.minY >= midH * 0.70f);

    // Right pinned leaf is collapsed in the template mid, not hidden
    // inside a nest.
    DockTabGroup* rightAfter = nullptr;
    for (Widget* c : mid->getChildren()) {
        auto* leaf = dynamic_cast<DockTabGroup*>(c);
        if (leaf != nullptr && leaf->getLeafId() == "Right") {
            rightAfter = leaf;
            break;
        }
    }
    CHECK_NOT_NULL(rightAfter);
    if (rightAfter == nullptr) return;
    CHECK_FALSE(rightAfter->isVisible());
    CHECK(dynamic_cast<HBox*>(rightAfter->getParent()) != nullptr);
}

// -------------------------------------------------------------------------
// 19. Dragging the last Center card into a nest sibling must nest-hide
//     empty Center (no dead fill band). Redocking into Center joins as
//     a tab without displacing the prior occupant.
// -------------------------------------------------------------------------
TEST_CASE(test_empty_center_in_nest_hides_and_adopt_joins) {
    TreeFixture f;
    auto dock = makeDock(f);

    dock->addCard(DockArea::Slot::Left, treeMakeCard("left", L"Left"));
    dock->addCard(DockArea::Slot::Center, treeMakeCard("center", L"Center"));
    dock->performLayout();

    DockCard* left = dock->findCard("left");
    DockCard* centerCard = dock->findCard("center");
    CHECK_NOT_NULL(left);
    CHECK_NOT_NULL(centerCard);
    if (left == nullptr || centerCard == nullptr) return;

    DockTabGroup* center = leafAt(dock.get(), FVector2(400.0f, 300.0f));
    CHECK_NOT_NULL(center);
    if (center == nullptr) return;
    const FRectangle cb = center->getWorldBounds();
    const FVector2 northPt((cb.minX + cb.maxX) * 0.5f, cb.minY + 5.0f);

    // Left → Center-North: nest [g_0, Center].
    CHECK(dragCardTo(f, left, northPt));
    dock->performLayout();

    DockTabGroup* g0 = leafAt(dock.get(), northPt);
    CHECK_NOT_NULL(g0);
    if (g0 == nullptr) return;
    CHECK(g0->getLeafId() == "g_0");

    // Join Center's last card into g_0 → Center empties inside nest.
    CHECK(dragCardTo(f, centerCard, centerOf(g0)));
    dock->performLayout();

    CHECK(dock->getCardCount(DockArea::Slot::Center) == 0);
    CHECK(g0->getTabCount() == 2);

    // Walk mid for the Center leaf (may be nest-hidden).
    DockTabGroup* centerLeaf = nullptr;
    if (HBox* mid = midBox(dock.get())) {
        std::vector<Widget*> stack;
        stack.push_back(mid);
        while (!stack.empty() && centerLeaf == nullptr) {
            Widget* n = stack.back();
            stack.pop_back();
            if (auto* leaf = dynamic_cast<DockTabGroup*>(n)) {
                if (leaf->getLeafId() == "Center") {
                    centerLeaf = leaf;
                    break;
                }
            }
            if (auto* box = dynamic_cast<BoxBase*>(n)) {
                for (Widget* c : box->getChildren()) {
                    stack.push_back(c);
                }
            }
        }
    }
    CHECK_NOT_NULL(centerLeaf);
    if (centerLeaf == nullptr) return;
    CHECK_FALSE(centerLeaf->isVisible());

    // g_0 (or its unwrapped self) must fill most of the mid height.
    DockTabGroup* filled = leafAt(dock.get(), FVector2(400.0f, 300.0f));
    CHECK_NOT_NULL(filled);
    if (filled == nullptr) return;
    HBox* mid = midBox(dock.get());
    CHECK_NOT_NULL(mid);
    if (mid == nullptr) return;
    const float midH = mid->getWorldBounds().maxY - mid->getWorldBounds().minY;
    const FRectangle fb = filled->getWorldBounds();
    CHECK(fb.maxY - fb.minY >= midH * 0.70f);

    // adoptCard into Center joins — does not float existing tabs.
    DockCard* inbound = treeMakeCard("inbound", L"Inbound").release();
    CHECK(dock->adoptCard(DockArea::Slot::Center, inbound));
    CHECK(dock->getCardCount(DockArea::Slot::Center) >= 1);
    CHECK(dock->getOverlay()->getFloatingCardCount() == 0);
    CHECK(centerLeaf->isVisible());
}

// -------------------------------------------------------------------------
// 20. Floating the last Center card hides empty Center so a populated
//     side expands into the middle (Gallery promote left a dead band).
// -------------------------------------------------------------------------
TEST_CASE(test_float_last_center_hides_and_side_expands) {
    TreeFixture f;
    auto dock = makeDock(f);

    dock->addCard(DockArea::Slot::Left, treeMakeCard("left", L"Left"));
    dock->addCard(DockArea::Slot::Center, treeMakeCard("center", L"Center"));
    dock->performLayout();

    DockTabGroup* left0 = leafAt(dock.get(), FVector2(80.0f, 300.0f));
    CHECK_NOT_NULL(left0);
    if (left0 == nullptr) return;
    const float leftW0 = left0->getWorldBounds().maxX
                       - left0->getWorldBounds().minX;

    CHECK(dock->floatCard("center", FVector2(500.0f, 100.0f)));
    dock->performLayout();

    CHECK(dock->getCardCount(DockArea::Slot::Center) == 0);
    DockTabGroup* centerLeaf = nullptr;
    if (HBox* mid = midBox(dock.get())) {
        for (Widget* c : mid->getChildren()) {
            auto* leaf = dynamic_cast<DockTabGroup*>(c);
            if (leaf != nullptr && leaf->getLeafId() == "Center") {
                centerLeaf = leaf;
                break;
            }
        }
    }
    CHECK_NOT_NULL(centerLeaf);
    if (centerLeaf == nullptr) return;
    CHECK_FALSE(centerLeaf->isVisible());

    DockTabGroup* left1 = leafAt(dock.get(), FVector2(400.0f, 300.0f));
    CHECK_NOT_NULL(left1);
    if (left1 == nullptr) return;
    CHECK(left1->getLeafId() == "Left");
    const float leftW1 = left1->getWorldBounds().maxX
                       - left1->getWorldBounds().minX;
    CHECK(leftW1 > leftW0 + 40.0f);
}

// -------------------------------------------------------------------------
// 21. resolveDropTarget: collapsed side wins over tree Center (preview
//     ≡ commit for Gallery redock revive).
// -------------------------------------------------------------------------
TEST_CASE(test_resolve_drop_prefers_collapsed_side) {
    TreeFixture f;
    auto dock = makeDock(f);

    dock->addCard(DockArea::Slot::Left, treeMakeCard("left", L"Left"));
    dock->addCard(DockArea::Slot::Center, treeMakeCard("center", L"Center"));
    dock->performLayout();

    CHECK(dock->floatCard("left", FVector2(50.0f, 50.0f)));
    dock->performLayout();

    // Left collapsed; cursor in the outer revive strip (not deep into
    // the expanded Center west edge-split zone).
    const DockArea::DropTarget t =
        dock->resolveDropTarget(FVector2(5.0f, 300.0f));
    CHECK(t.kind == DockArea::DropKind::Slot);
    CHECK(t.slot == DockArea::Slot::Left);

    // East of Center but outside the 10px Right revive strip → Edge split.
    DockTabGroup* center = leafAt(dock.get(), FVector2(400.0f, 300.0f));
    CHECK_NOT_NULL(center);
    if (center == nullptr) return;
    const FRectangle cb = center->getWorldBounds();
    const FVector2 eastPt(cb.maxX - 15.0f, (cb.minY + cb.maxY) * 0.5f);
    const DockArea::DropTarget te = dock->resolveDropTarget(eastPt);
    CHECK(te.kind == DockArea::DropKind::Tree);
    CHECK(te.zone == DockArea::TreeDropZone::East);
}

// -------------------------------------------------------------------------
// 22. Moving a card out of g_N via a later splitLeaf must prune the
//     emptied leaf — otherwise it remains a transparent fixed-width
//     band (Gallery: black gap between Center and Right).
// -------------------------------------------------------------------------
TEST_CASE(test_split_away_from_g_n_prunes_empty_source) {
    TreeFixture f;
    auto dock = makeDock(f);

    dock->addCard(DockArea::Slot::Left, treeMakeCard("left", L"Left"));
    dock->addCard(DockArea::Slot::Center, treeMakeCard("center", L"Center"));
    dock->performLayout();

    DockCard* left = dock->findCard("left");
    CHECK_NOT_NULL(left);
    if (left == nullptr) return;

    DockTabGroup* center = leafAt(dock.get(), FVector2(400.0f, 300.0f));
    CHECK_NOT_NULL(center);
    if (center == nullptr) return;
    const FRectangle cb = center->getWorldBounds();
    // Left → Center east → g_0 (Right stays collapsed / out of the way).
    const FVector2 eastPt(cb.maxX - 15.0f, (cb.minY + cb.maxY) * 0.5f);
    CHECK(dock->resolveTreeDropZone(center, eastPt) ==
          DockArea::TreeDropZone::East);
    CHECK(dragCardTo(f, left, eastPt));
    dock->performLayout();

    DockTabGroup* g0 = leafAt(dock.get(), eastPt);
    CHECK_NOT_NULL(g0);
    if (g0 == nullptr) return;
    CHECK(g0->getLeafId() == "g_0");
    CHECK(g0->containsCard(left));
    const FVector2 g0Pt = centerOf(g0);

    // Tear left from g_0 onto Center west → new g_1; emptied g_0 must prune.
    DockTabGroup* center2 = leafAt(dock.get(), FVector2(300.0f, 300.0f));
    CHECK_NOT_NULL(center2);
    if (center2 == nullptr) return;
    CHECK(center2->getLeafId() == "Center");
    const FRectangle cb2 = center2->getWorldBounds();
    const FVector2 westPt(cb2.minX + 15.0f, (cb2.minY + cb2.maxY) * 0.5f);
    CHECK(dock->resolveTreeDropZone(center2, westPt) ==
          DockArea::TreeDropZone::West);
    CHECK(dragCardTo(f, left, westPt));
    dock->performLayout();

    // Former g_0 location must not be an empty leaf band.
    DockTabGroup* atOldG0 = leafAt(dock.get(), g0Pt);
    CHECK_NOT_NULL(atOldG0);
    if (atOldG0 == nullptr) return;
    CHECK(atOldG0->getTabCount() >= 1);

    int emptyUnpinnedLeafCount = 0;
    if (HBox* mid = midBox(dock.get())) {
        for (Widget* c : mid->getChildren()) {
            auto* leaf = dynamic_cast<DockTabGroup*>(c);
            if (leaf != nullptr && !leaf->isPinned()
                && leaf->getTabCount() == 0) {
                ++emptyUnpinnedLeafCount;
            }
        }
    }
    CHECK(emptyUnpinnedLeafCount == 0);
}

// -------------------------------------------------------------------------
// 23. Hole eradication: a mid HBox of only fixed-size visible panels
//     must be healed on performLayout so leftover cannot remain a gap.
// -------------------------------------------------------------------------
TEST_CASE(test_perform_layout_heals_fixed_only_mid_hole) {
    TreeFixture f;
    auto dock = makeDock(f);

    dock->addCard(DockArea::Slot::Left, treeMakeCard("left", L"Left"));
    dock->addCard(DockArea::Slot::Center, treeMakeCard("center", L"Center"));
    dock->addCard(DockArea::Slot::Right, treeMakeCard("right", L"Right"));
    dock->performLayout();

    HBox* mid = midBox(dock.get());
    CHECK_NOT_NULL(mid);
    if (mid == nullptr) return;

    // Force every visible panel to a fixed size (no fill) — the
    // pre-heal condition that painted a black band in Gallery.
    for (int i = 0; ; ++i) {
        Widget* w = mid->slotAt(i);
        if (w == nullptr) {
            break;
        }
        if (w->isSplitterHandle() || !w->isVisible()) {
            continue;
        }
        mid->setSlotSize(i, 120.0f);
    }
    dock->performLayout();

    // After heal, visible panel widths must cover the mid (no leftover
    // gap larger than splitter/spacing slack).
    const FRectangle mb = mid->getWorldBounds();
    float covered = 0.0f;
    int visiblePanels = 0;
    for (Widget* c : mid->getChildren()) {
        if (c == nullptr || !c->isVisible()) {
            continue;
        }
        const FRectangle cb = c->getWorldBounds();
        covered += (cb.maxX - cb.minX);
        if (!c->isSplitterHandle()) {
            ++visiblePanels;
        }
    }
    CHECK(visiblePanels >= 2);
    const float midW = mb.maxX - mb.minX;
    CHECK(covered >= midW - 8.0f);
}

// -------------------------------------------------------------------------
// 23. Multi-step nest that leaves only a hidden Center must dissolve —
//     otherwise mid keeps a vacant box and paints a gap between Left and
//     the remaining panel group (Gallery D8).
// -------------------------------------------------------------------------
TEST_CASE(test_vacant_nest_with_hidden_center_dissolves) {
    TreeFixture f;
    auto dock = makeDock(f);

    dock->addCard(DockArea::Slot::Left, treeMakeCard("left", L"Left"));
    dock->addCard(DockArea::Slot::Center, treeMakeCard("center", L"Center"));
    dock->addCard(DockArea::Slot::Right, treeMakeCard("right", L"Right"));
    dock->performLayout();

    DockCard* left = dock->findCard("left");
    DockCard* centerCard = dock->findCard("center");
    DockCard* right = dock->findCard("right");
    CHECK_NOT_NULL(left);
    CHECK_NOT_NULL(centerCard);
    CHECK_NOT_NULL(right);
    if (left == nullptr || centerCard == nullptr || right == nullptr) return;

    DockTabGroup* center = leafAt(dock.get(), FVector2(400.0f, 300.0f));
    CHECK_NOT_NULL(center);
    if (center == nullptr) return;
    const FRectangle cb = center->getWorldBounds();
    const FVector2 northPt((cb.minX + cb.maxX) * 0.5f, cb.minY + 5.0f);

    // Left → Center-North: nest [g_0, Center]; Left collapses.
    CHECK(dragCardTo(f, left, northPt));
    dock->performLayout();

    DockTabGroup* g0 = leafAt(dock.get(), northPt);
    CHECK_NOT_NULL(g0);
    if (g0 == nullptr) return;
    CHECK(g0->getLeafId() == "g_0");

    // Join Center into g_0 → Center hidden inside nest (blocks unwrap).
    CHECK(dragCardTo(f, centerCard, centerOf(g0)));
    dock->performLayout();
    CHECK(g0->getTabCount() == 2);

    // Move both cards into Right — g_0 prunes; nest would be vacant.
    CHECK(dragCardTo(f, left, centerOf(leafAt(dock.get(), FVector2(700.0f, 300.0f)))));
    dock->performLayout();
    DockTabGroup* rightLeaf = leafAt(dock.get(), FVector2(700.0f, 300.0f));
    CHECK_NOT_NULL(rightLeaf);
    if (rightLeaf == nullptr) return;
    CHECK(dragCardTo(f, centerCard, centerOf(rightLeaf)));
    dock->performLayout();

    HBox* mid = midBox(dock.get());
    CHECK_NOT_NULL(mid);
    if (mid == nullptr) return;

    // No mid child box may be vacant (0 visible non-splitter panels).
    int vacantNestedBoxCount = 0;
    for (Widget* c : mid->getChildren()) {
        auto* nest = dynamic_cast<BoxBase*>(c);
        if (nest == nullptr) {
            continue;
        }
        int visible = 0;
        for (int i = 0; ; ++i) {
            Widget* w = nest->slotAt(i);
            if (w == nullptr) {
                break;
            }
            if (!w->isSplitterHandle() && w->isVisible()) {
                ++visible;
            }
        }
        if (visible == 0) {
            ++vacantNestedBoxCount;
        }
    }
    CHECK(vacantNestedBoxCount == 0);

    // Visible panels + splitters must cover the mid (no black gap band).
    const FRectangle mb = mid->getWorldBounds();
    float covered = 0.0f;
    for (Widget* c : mid->getChildren()) {
        if (c == nullptr || !c->isVisible()) {
            continue;
        }
        const FRectangle wb = c->getWorldBounds();
        covered += (wb.maxX - wb.minX);
    }
    CHECK(covered >= (mb.maxX - mb.minX) - 8.0f);

    // Hidden Center must live in the template mid, not a dissolved nest.
    DockTabGroup* centerLeaf = nullptr;
    for (Widget* c : mid->getChildren()) {
        auto* leaf = dynamic_cast<DockTabGroup*>(c);
        if (leaf != nullptr && leaf->getLeafId() == "Center") {
            centerLeaf = leaf;
            break;
        }
    }
    CHECK_NOT_NULL(centerLeaf);
    if (centerLeaf == nullptr) return;
    CHECK_FALSE(centerLeaf->isVisible());
}

// -------------------------------------------------------------------------
// 24. Title-bar close-X destroys on mouse-up (not down) so UIManager never
//     captures a freed DockCard — Gallery crash after close Left.
// -------------------------------------------------------------------------
TEST_CASE(test_dock_card_close_x_via_uimanager_defers_destroy) {
    TreeFixture f;
    auto dock = makeDock(f);

    auto leftOwned = treeMakeCard("left", L"Left");
    leftOwned->setClosable(true);
    leftOwned->setFloatable(true);
    leftOwned->setOnCloseRequested([dock = dock.get()](DockCard* c) {
        if (c != nullptr) {
            dock->closeCard(c->getId());
        }
    });
    dock->addCard(DockArea::Slot::Left, std::move(leftOwned));
    dock->addCard(DockArea::Slot::Center, treeMakeCard("center", L"Center"));
    dock->addCard(DockArea::Slot::Right, treeMakeCard("right", L"Right"));
    dock->performLayout();

    DockCard* card = dock->findCard("left");
    CHECK_NOT_NULL(card);
    if (card == nullptr) return;

    const FRectangle b = card->getWorldBounds();
    // Close button is the rightmost ~22px of the title strip.
    const float closeX = b.maxX - 8.0f;
    const float closeY = b.minY + 8.0f;

    CHECK(f.ui.onMouseButtonDown(closeX, closeY, 0));
    // Must still be alive after down — destroy waits for up.
    CHECK(dock->findCard("left") != nullptr);

    CHECK(f.ui.onMouseButtonUp(closeX, closeY, 0));
    CHECK(dock->findCard("left") == nullptr);

    // Capture must be clear (no dangling pointer after destroy).
    CHECK_FALSE(f.ui.onMouseButtonUp(closeX, closeY, 0));

    dock->performLayout();
    HBox* mid = midBox(dock.get());
    CHECK_NOT_NULL(mid);
    if (mid == nullptr) return;
    const FRectangle mb = mid->getWorldBounds();
    float covered = 0.0f;
    for (Widget* c : mid->getChildren()) {
        if (c == nullptr || !c->isVisible()) {
            continue;
        }
        const FRectangle wb = c->getWorldBounds();
        covered += (wb.maxX - wb.minX);
    }
    CHECK(covered >= (mb.maxX - mb.minX) - 8.0f);
}

// -------------------------------------------------------------------------
// 25. Gallery recipe: N/W/S/E wrap → join → same-leaf West → close Left
//     must not leave a mid gap / hole (20260811 GapClose follow-up).
// -------------------------------------------------------------------------
TEST_CASE(test_gallery_nwes_join_west_close_left_no_hole) {
    TreeFixture f;
    auto dock = makeDock(f);

    dock->addCard(DockArea::Slot::Left, treeMakeCard("left", L"Left"));
    dock->addCard(DockArea::Slot::Center, treeMakeCard("center", L"Center"));
    dock->addCard(DockArea::Slot::Right, treeMakeCard("right", L"Right"));
    dock->performLayout();

    DockCard* right = dock->findCard("right");
    DockCard* left = dock->findCard("left");
    CHECK_NOT_NULL(right);
    CHECK_NOT_NULL(left);
    if (right == nullptr || left == nullptr) return;

    auto centerPt = [](DockArea* d) {
        DockTabGroup* c = leafAt(d, FVector2(400.0f, 300.0f));
        return c != nullptr ? centerOf(c) : FVector2(400.0f, 300.0f);
    };
    auto edgePt = [](DockArea* d, DockArea::TreeDropZone z) {
        DockTabGroup* c = leafAt(d, FVector2(400.0f, 300.0f));
        if (c == nullptr) {
            return FVector2(400.0f, 300.0f);
        }
        const FRectangle b = c->getWorldBounds();
        switch (z) {
        case DockArea::TreeDropZone::North:
            return FVector2((b.minX + b.maxX) * 0.5f, b.minY + 5.0f);
        case DockArea::TreeDropZone::West:
            return FVector2(b.minX + 5.0f, (b.minY + b.maxY) * 0.5f);
        case DockArea::TreeDropZone::South:
            return FVector2((b.minX + b.maxX) * 0.5f, b.maxY - 5.0f);
        case DockArea::TreeDropZone::East:
            return FVector2(b.maxX - 5.0f, (b.minY + b.maxY) * 0.5f);
        default:
            return centerOf(c);
        }
    };

    CHECK(dragCardTo(f, right, edgePt(dock.get(), DockArea::TreeDropZone::North)));
    dock->performLayout();
    CHECK(dragCardTo(f, right, edgePt(dock.get(), DockArea::TreeDropZone::West)));
    dock->performLayout();
    CHECK(dragCardTo(f, right, edgePt(dock.get(), DockArea::TreeDropZone::South)));
    dock->performLayout();
    CHECK(dragCardTo(f, right, edgePt(dock.get(), DockArea::TreeDropZone::East)));
    dock->performLayout();
    CHECK(dragCardTo(f, right, centerPt(dock.get())));
    dock->performLayout();
    CHECK(dragCardTo(f, right, edgePt(dock.get(), DockArea::TreeDropZone::West)));
    dock->performLayout();

    auto midCovered = [&]() -> bool {
        HBox* mid = midBox(dock.get());
        if (mid == nullptr) {
            return false;
        }
        const FRectangle mb = mid->getWorldBounds();
        float covered = 0.0f;
        for (Widget* c : mid->getChildren()) {
            if (c == nullptr || !c->isVisible()) {
                continue;
            }
            const FRectangle wb = c->getWorldBounds();
            covered += (wb.maxX - wb.minX);
        }
        return covered >= (mb.maxX - mb.minX) - 8.0f;
    };
    CHECK(midCovered());

    CHECK(dock->closeCard("left"));
    dock->performLayout();
    CHECK(dock->findCard("left") == nullptr);
    CHECK(midCovered());
}

TEST_SUITE_END
