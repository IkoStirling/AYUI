// =============================================================================
// D3 — DockArea tear-off UX tests.
//
// Exercises the drag-source / drop-target wiring added by D3:
//   * DockCard::onMouseButtonDown → UIManager::beginDrag (title-bar LMB)
//   * DockCard::setFloatable(false) → draggable off (K-INV-D3-2)
//   * DockArea::floatCard / dockCard (the actual move)
//   * DockOverlay::hitTest pass-through when no floating card is hit
//     (K-INV-D3-4)
//   * Esc during drag → cancelDrag → card stays in place (K-INV-D3-3)
//
// Test fixture convention (per Test_DragDrop.cpp idiom):
//   MockRenderer backend; UIManager ui; ui.initialize(&backend);
//   setClientSize; mouse event sequences via the public API;
//   ui.shutdown().
//
// D3's DockArea is constructed as a stack fixture (not via the factory)
// because we never wire it into UIManager's hit-test root — the D3
// drop path uses DockArea::onDrop directly, which is invoked when
// G12's pickTopmostWidget finds DockArea under the cursor. For
// unit testing we exercise that callback directly via DockArea's
// public floatCard/dockCard API (which onDrop delegates to).
// =============================================================================

#include "AYTest.h"
#include "AYUI/UIManager.h"
#include "AYUI/DockArea.h"
#include "AYUI/DockCard.h"
#include "AYUI/DockOverlay.h"
#include "AYUI/DragDrop.h"
#include "AYUI/MockRenderer.h"
#include "AYUI/UIKeyCode.h"

#include <cmath>
#include <memory>

using namespace ayt::ui;
using namespace ayt::math;

namespace {

// Common fixture: mock renderer + UIManager + 800x600 client area.
// DockArea is built by each test as needed (some tests use stack,
// some use heap depending on whether the test wants the dock in
// the UIManager's hit-test tree).
struct DockFixture {
    MockRenderer backend;
    UIManager    ui;

    DockFixture() {
        ui.initialize(&backend);
        ui.setClientSize(800.0f, 600.0f);
    }
    ~DockFixture() {
        ui.shutdown();
    }
};

// Helper: build a docked card wrapped in std::unique_ptr so callers
// can hand it straight to DockArea::addCard without falling into the
// most-vexing parse (`std::unique_ptr<DockCard>(card)` would parse as
// a variable declaration in some argument contexts). Returning
// std::unique_ptr is intentional — returning DockCard* and rewrapping
// at the call site reproduces the most-vexing parse on every call.
std::unique_ptr<DockCard> makeFloatCard(const std::string& id,
                                   const std::wstring& title) {
    auto c = std::make_unique<DockCard>();
    c->setId(id);
    c->setTitle(title);
    return c;
}

// Helper: simulate "user clicked LMB on the card's title bar" by
// feeding the right UIMouseEvent to DockCard::onMouseButtonDown.
// The card must already be in a tree (have a parent) for beginDrag
// to accept it.
bool simulateTitleBarClick(DockCard* card, FVector2 worldPos) {
    UIMouseEvent e(worldPos, 0); // button 0 = LMB
    return card->onMouseButtonDown(e);
}

// Title-bar point in world space after performLayout. (0,0)/(2,2) is
// often Top-slot chrome once empty slots keep their configured weight.
FVector2 titleBarPoint(DockCard* card) {
    const FRectangle b = card->getWorldBounds();
    return FVector2(b.minX + std::min(60.0f, (b.maxX - b.minX) * 0.5f),
                    b.minY + 4.0f);
}

} // namespace

TEST_SUITE(AYUI_DockFloat)

// -------------------------------------------------------------------------
// 1. Float a card via drop on overlay area (K-INV-D3-1 same-slot no-op
//    already covered separately; this is the cross-region transition)
// -------------------------------------------------------------------------
TEST_CASE(test_float_card_moves_to_overlay) {
    DockArea dock;
    dock.setSize(FVector2(800.0f, 600.0f));

    dock.addCard(DockArea::Slot::Left, makeFloatCard("hierarchy", L"Hierarchy"));

    CHECK(dock.getCardCount(DockArea::Slot::Left) == 1);
    CHECK(dock.getOverlay()->getFloatingCardCount() == 0);

    const bool moved = dock.floatCard("hierarchy", FVector2(120.0f, 80.0f));
    CHECK(moved);

    CHECK(dock.getCardCount(DockArea::Slot::Left) == 0);
    CHECK(dock.getOverlay()->getFloatingCardCount() == 1);
    DockCard* card = dock.getOverlay()->getFloatingCard(0);
    CHECK(card != nullptr);
    if (card) {
        CHECK(card->getPosition().x == 120.0f);
        CHECK(card->getPosition().y == 80.0f);
        // findCard must still locate it (overlay fallback linear scan).
        CHECK(dock.findCard("hierarchy") == card);
    }
}

// -------------------------------------------------------------------------
// 2. Re-dock a floating card into a slot (the reverse path).
// -------------------------------------------------------------------------
TEST_CASE(test_dock_card_moves_from_overlay_to_slot) {
    DockArea dock;
    dock.setSize(FVector2(800.0f, 600.0f));

    auto cardU = makeFloatCard("inspector", L"Inspector");
    cardU->setPosition(FVector2(50.0f, 50.0f));
    // addFloatingCard takes a raw DockCard*; .release() avoids the
    // most-vexing parse while still handing the pointer in.
    dock.getOverlay()->addFloatingCard(cardU.release());

    CHECK(dock.getOverlay()->getFloatingCardCount() == 1);
    CHECK(dock.getCardCount(DockArea::Slot::Right) == 0);

    const bool moved = dock.dockCard("inspector", DockArea::Slot::Right);
    CHECK(moved);

    CHECK(dock.getOverlay()->getFloatingCardCount() == 0);
    CHECK(dock.getCardCount(DockArea::Slot::Right) == 1);
    DockCard* card = dock.getCard(DockArea::Slot::Right, 0);
    CHECK(card != nullptr);
    if (card) {
        CHECK(dock.findCard("inspector") == card);
    }
}

// -------------------------------------------------------------------------
// 2b. adoptCard into an occupied slot joins as a tab (Phase-3
//     DockTabGroup). Displace-to-float was the pre-tab policy and kicked
//     Center content out on Gallery child-window redock.
// -------------------------------------------------------------------------
TEST_CASE(test_hit_test_slot_outside_dock_is_count) {
    DockArea dock;
    dock.setPosition(FVector2(100.0f, 100.0f));
    dock.setSize(FVector2(400.0f, 300.0f));
    dock.performLayout();
    // Above the dock used to map to Top (local.y < midY) and redock
    // snapped floating windows into the disabled Top band.
    CHECK(dock.hitTestSlot(FVector2(200.0f, 50.0f))
          == DockArea::Slot::Count);
    CHECK(dock.hitTestSlot(FVector2(50.0f, 200.0f))
          == DockArea::Slot::Count);
    CHECK(dock.hitTestSlot(FVector2(200.0f, 200.0f))
          != DockArea::Slot::Count);
}

TEST_CASE(test_adopt_card_joins_occupied_slot) {
    DockArea dock;
    dock.setSize(FVector2(800.0f, 600.0f));
    dock.performLayout();

    dock.addCard(DockArea::Slot::Left, makeFloatCard("old", L"Old"));
    CHECK(dock.getCardCount(DockArea::Slot::Left) == 1);
    CHECK(dock.getOverlay()->getFloatingCardCount() == 0);

    DockCard* inbound = makeFloatCard("new", L"New").release();
    CHECK(dock.adoptCard(DockArea::Slot::Left, inbound));

    CHECK(dock.getCardCount(DockArea::Slot::Left) == 2);
    CHECK(dock.findCard("new") != nullptr);
    CHECK(dock.findCard("old") != nullptr);
    CHECK(dock.isCardInSlot(dock.findCard("new"), DockArea::Slot::Left));
    CHECK(dock.isCardInSlot(dock.findCard("old"), DockArea::Slot::Left));
    CHECK(dock.getOverlay()->getFloatingCardCount() == 0);
}

// -------------------------------------------------------------------------
// 3. Same-slot drop is a no-op (K-INV-D3-1) — tested via dock->addCard
//    chain that the onDrop callback walks (isCardInSlot short-circuit).
// -------------------------------------------------------------------------
TEST_CASE(test_same_slot_noop_through_isCardInSlot) {
    DockArea dock;
    dock.addCard(DockArea::Slot::Center, makeFloatCard("viewport", L"Viewport"));

    // Grab the raw pointer before addCard takes ownership. addCard
    // stores it in _slotCards[Center] and _cardIndex["viewport"], so
    // findCard returns the live pointer.
    DockCard* card = dock.findCard("viewport");
    CHECK(card != nullptr);
    if (card == nullptr) return;

    // isCardInSlot should report true for the card's current slot.
    CHECK(dock.isCardInSlot(card, DockArea::Slot::Center));

    // Float it (now card is in overlay, NOT in any slot).
    dock.floatCard("viewport", FVector2(200.0f, 200.0f));
    CHECK(dock.isCardInSlot(card, DockArea::Slot::Center) == false);

    // Re-dock into Center — succeeds (was floating).
    dock.dockCard("viewport", DockArea::Slot::Center);
    CHECK(dock.isCardInSlot(card, DockArea::Slot::Center));
    CHECK(dock.getCardCount(DockArea::Slot::Center) == 1);
}

// -------------------------------------------------------------------------
// 4. Esc cancels a drag session — card stays in place (K-INV-D3-3).
// -------------------------------------------------------------------------
TEST_CASE(test_esc_during_drag_cancels) {
    DockFixture f;

    // Heap-allocated dock (so it owns its overlay + slot containers)
    // attached to UIManager's overlay root so beginDrag's source
    // parent-check passes. UIManager won't delete it because
    // addChildExternal marks it externally-owned.
    auto dock = std::make_unique<DockArea>();
    dock->setSize(FVector2(800.0f, 600.0f));
    f.ui.getOverlayRoot()->addChildExternal(dock.get());

    dock->addCard(DockArea::Slot::Left, makeFloatCard("console", L"Console"));
    dock->performLayout();
    DockCard* card = dock->findCard("console");
    CHECK(card != nullptr);
    if (card == nullptr) return;

    const bool started = simulateTitleBarClick(card, titleBarPoint(card));
    CHECK(started);
    CHECK(f.ui.isDragging());

    // Esc — G12 drag wins over modal-Esc (AYUIManager.cpp:1531).
    f.ui.onKeyDown(UIKey_Escape);
    CHECK_FALSE(f.ui.isDragging());

    // Card must still be docked in Left.
    CHECK(dock->getCardCount(DockArea::Slot::Left) == 1);
    CHECK(dock->getOverlay()->getFloatingCardCount() == 0);
}

// -------------------------------------------------------------------------
// 4b. PR-S5e: dragging keeps the docked copy VISIBLE — the ghost is a
//     drop preview, the original content must not vanish (previously
//     onDragStart hid the card and onDragEnd restored it).
// -------------------------------------------------------------------------
TEST_CASE(test_drag_keeps_docked_copy_visible) {
    DockFixture f;

    auto dock = std::make_unique<DockArea>();
    dock->setSize(FVector2(800.0f, 600.0f));
    f.ui.getOverlayRoot()->addChildExternal(dock.get());

    dock->addCard(DockArea::Slot::Left, makeFloatCard("console", L"Console"));
    dock->performLayout();
    DockCard* card = dock->findCard("console");
    CHECK(card != nullptr);
    if (card == nullptr) return;

    const bool started = simulateTitleBarClick(card, titleBarPoint(card));
    CHECK(started);
    CHECK(f.ui.isDragging());

    // The docked copy stays in place AND visible during the drag.
    CHECK(card->isVisible());
    CHECK(dock->getCardCount(DockArea::Slot::Left) == 1);

    // Esc cancels — card untouched (still visible, still docked).
    f.ui.onKeyDown(UIKey_Escape);
    CHECK_FALSE(f.ui.isDragging());
    CHECK(card->isVisible());
    CHECK(dock->getCardCount(DockArea::Slot::Left) == 1);
}

// -------------------------------------------------------------------------
// 5. Non-floatable card ignores title-bar drag (K-INV-D3-2).
// -------------------------------------------------------------------------
TEST_CASE(test_non_floatable_card_ignores_drag) {
    DockFixture f;

    auto dock = std::make_unique<DockArea>();
    dock->setSize(FVector2(800.0f, 600.0f));
    f.ui.getOverlayRoot()->addChildExternal(dock.get());

    auto cardU = makeFloatCard("locked", L"Locked");
    cardU->setFloatable(false);
    DockCard* card = cardU.get();
    dock->addCard(DockArea::Slot::Left, std::move(cardU));
    dock->performLayout();

    // setFloatable(false) should also clear setDraggable(false) so
    // beginDrag rejects at the source-side gate.
    CHECK_FALSE(card->isDraggable());

    const bool started = simulateTitleBarClick(card, titleBarPoint(card));
    CHECK_FALSE(started);
    CHECK_FALSE(f.ui.isDragging());
    CHECK(dock->getCardCount(DockArea::Slot::Left) == 1);
}

// -------------------------------------------------------------------------
// 6. DockOverlay::hitTest pass-through (K-INV-D3-4).
// -------------------------------------------------------------------------
TEST_CASE(test_overlay_hit_test_passthrough_when_empty) {
    DockArea dock;
    dock.setSize(FVector2(800.0f, 600.0f));
    // DockArea is at origin; performLayout sizes overlay to (800, 600).

    DockOverlay* overlay = dock.getOverlay();
    CHECK(overlay != nullptr);

    // Empty overlay — any in-bounds position must pass-through (nullptr).
    CHECK(overlay->hitTest(FVector2(400.0f, 300.0f)) == nullptr);
    CHECK(overlay->hitTest(FVector2(10.0f, 10.0f))   == nullptr);
    CHECK(overlay->hitTest(FVector2(790.0f, 590.0f)) == nullptr);

    // Outside overlay bounds — nullptr (regardless of card placement).
    CHECK(overlay->hitTest(FVector2(-5.0f, -5.0f)) == nullptr);

    // Add a floating card and check the same in-bounds point.
    auto cardU = makeFloatCard("graph", L"Profiler");
    cardU->setPosition(FVector2(100.0f, 100.0f));
    cardU->setSize(FVector2(80.0f, 60.0f));
    DockCard* card = cardU.get();
    overlay->addFloatingCard(cardU.release());

    // Inside the floating card bounds → hit the card.
    Widget* hit = overlay->hitTest(FVector2(140.0f, 130.0f));
    CHECK(hit == card);

    // Inside overlay bounds but NOT on the card → pass-through nullptr.
    CHECK(overlay->hitTest(FVector2(700.0f, 500.0f)) == nullptr);
}

// -------------------------------------------------------------------------
// 7. hitTestSlot mirrors performLayout region math.
// -------------------------------------------------------------------------
TEST_CASE(test_hit_test_slot_covers_all_five_regions) {
    DockArea dock;
    dock.setSize(FVector2(800.0f, 600.0f));
    // Default weights: Top=0.15, Bottom=0.20, Left=0.20, Right=0.25, Center=0.55.

    // Middle-row: y in [topH, h - botH). topH = 600 * 0.15/1.35 ≈ 66.67.
    // botH    = 600 * 0.20/1.35 ≈ 88.89.
    // Inside middle row, Left/Right/Center split horizontally.
    // sumHoriz = 0.20+0.25+0.55 = 1.0, so leftW = 800*0.20 = 160,
    // centerW = 800*0.55 = 440, rightW = 800*0.25 = 200.

    // Top strip (y < ~66.67).
    CHECK(dock.hitTestSlot(FVector2(400.0f, 30.0f)) == DockArea::Slot::Top);
    // Bottom strip (y >= ~511.11).
    CHECK(dock.hitTestSlot(FVector2(400.0f, 550.0f)) == DockArea::Slot::Bottom);
    // Middle-row Left (x < 160).
    CHECK(dock.hitTestSlot(FVector2(50.0f, 300.0f)) == DockArea::Slot::Left);
    // Middle-row Center (x in [160, 600)).
    CHECK(dock.hitTestSlot(FVector2(400.0f, 300.0f)) == DockArea::Slot::Center);
    // Middle-row Right (x >= 600).
    CHECK(dock.hitTestSlot(FVector2(700.0f, 300.0f)) == DockArea::Slot::Right);
}

// -------------------------------------------------------------------------
// 7b. PR-Dock-SlotHighlight: getSlotRect regions contain exactly the
//     points hitTestSlot maps to that slot (shared region math).
// -------------------------------------------------------------------------
TEST_CASE(test_get_slot_rect_matches_hit_test) {
    DockArea dock;
    dock.setSize(FVector2(800.0f, 600.0f));

    struct Probe { FVector2 pos; DockArea::Slot slot; };
    const Probe probes[] = {
        { FVector2(400.0f, 30.0f),  DockArea::Slot::Top },
        { FVector2(400.0f, 550.0f), DockArea::Slot::Bottom },
        { FVector2(50.0f, 300.0f),  DockArea::Slot::Left },
        { FVector2(400.0f, 300.0f), DockArea::Slot::Center },
        { FVector2(700.0f, 300.0f), DockArea::Slot::Right },
    };
    int containmentFailureCount = 0;
    int hitTestFailureCount = 0;
    for (const Probe& p : probes) {
        const FRectangle r = dock.getSlotRect(p.slot);
        if (!r.contains(p.pos)) {
            ++containmentFailureCount;
        }
        if (dock.hitTestSlot(p.pos) != p.slot) {
            ++hitTestFailureCount;
        }
    }
    CHECK(containmentFailureCount == 0);
    CHECK(hitTestFailureCount == 0);
    // Invalid slot → empty rect.
    CHECK(dock.getSlotRect(static_cast<DockArea::Slot>(99)).maxX
          <= dock.getSlotRect(static_cast<DockArea::Slot>(99)).minX);
}

// -------------------------------------------------------------------------
// 7c. PR-Dock-SlotHighlight: dragging a DockCard paints the slot region
//     under the cursor (translucent fill + border), not just the generic
//     outline. The fill's bounds must equal getSlotRect(Left) exactly.
// -------------------------------------------------------------------------
TEST_CASE(test_dockcard_drag_paints_slot_highlight) {
    DockFixture f;

    auto dock = std::make_unique<DockArea>();
    dock->setSize(FVector2(800.0f, 600.0f));
    dock->setSlotWeight(DockArea::Slot::Top, 1e-6f);
    dock->setSlotWeight(DockArea::Slot::Bottom, 1e-6f);
    f.ui.getOverlayRoot()->addChildExternal(dock.get());
    dock->addCard(DockArea::Slot::Left, makeFloatCard("console", L"Console"));
    dock->performLayout();

    DockCard* card = dock->findCard("console");
    CHECK_NOT_NULL(card);
    if (card == nullptr) return;

    const FRectangle b = card->getWorldBounds();
    const FVector2 titlePt((b.minX + b.maxX) * 0.5f, b.minY + 4.0f);
    CHECK(simulateTitleBarClick(card, titlePt));
    CHECK(f.ui.isDragging());

    // Cursor over the LEFT leaf join zone (avoid the 25% west edge).
    DockTabGroup* leftLeaf0 = dock->hitTestTree(
        FVector2(b.minX + 20.0f, (b.minY + b.maxY) * 0.5f));
    CHECK_NOT_NULL(leftLeaf0);
    if (leftLeaf0 == nullptr) return;
    const FRectangle lb0 = leftLeaf0->getWorldBounds();
    const FVector2 leftPt((lb0.minX + lb0.maxX) * 0.5f,
                          (lb0.minY + lb0.maxY) * 0.5f);
    f.ui.onMouseMove(leftPt.x, leftPt.y);
    CHECK(f.ui.getDragPayload().kind == "DockCard");
    CHECK(dock->hitTestSlot(leftPt) == DockArea::Slot::Left);

    f.backend.clear();
    // External-drop bridge: preview uses the same resolveDropTarget as
    // redockAt. Over a visible Left leaf that is Join → tree join paint.
    dock->setExternalDropPos(leftPt);
    dock->paintDropGuide(f.backend);

    DockTabGroup* leftLeaf = dock->hitTestTree(leftPt);
    CHECK_NOT_NULL(leftLeaf);
    if (leftLeaf == nullptr) return;
    const FRectangle leftBounds = leftLeaf->getWorldBounds();
    bool foundHoverFill = false;
    for (const auto& dc : f.backend.getDrawCalls()) {
        if (dc.type != MockRenderer::DrawCall::Rect) {
            continue;
        }
        // Join preview alpha is 0.20 (paintTreeDropZone).
        const bool alphaOk = (std::fabs(dc.color.w - 0.20f) < 0.02f);
        const bool matchesLeft =
            std::fabs(dc.bounds.minX - leftBounds.minX) < 0.5f
            && std::fabs(dc.bounds.maxX - leftBounds.maxX) < 0.5f
            && std::fabs(dc.bounds.minY - leftBounds.minY) < 0.5f
            && std::fabs(dc.bounds.maxY - leftBounds.maxY) < 0.5f;
        if (alphaOk && matchesLeft) {
            foundHoverFill = true;
            break;
        }
    }
    CHECK(foundHoverFill);
    dock->clearExternalDropPos();

    // Cancel cleanly (Esc) so the session doesn't outlive the fixture;
    // idle must paint no guide.
    f.ui.onKeyDown(UIKey_Escape);
    CHECK_FALSE(f.ui.isDragging());
    f.backend.clear();
    dock->paintDropGuide(f.backend);
    CHECK(f.backend.getDrawCalls().empty());
}

// -------------------------------------------------------------------------
// 7d. PR-Dock-SlotHighlight: non-DockCard payloads get no slot highlight
//     (only the generic drop-target outline).
// -------------------------------------------------------------------------
TEST_CASE(test_non_dockcard_drag_gets_no_slot_highlight) {
    DockFixture f;

    auto dock = std::make_unique<DockArea>();
    dock->setSize(FVector2(800.0f, 600.0f));
    f.ui.getOverlayRoot()->addChildExternal(dock.get());
    dock->performLayout();

    // Plain draggable source with a non-DockCard payload.
    Widget source;
    source.setDraggable(true);
    source.setSize(FVector2(40.0f, 40.0f));
    source.setPosition(FVector2(10.0f, 10.0f));
    DragPayload p;
    p.kind = "file";
    source.setDragPayload(p);
    f.ui.getOverlayRoot()->addChildExternal(&source);

    CHECK(f.ui.beginDrag(&source));
    f.ui.updateDrag(50.0f, 300.0f);
    CHECK(f.ui.isDragging());

    f.backend.clear();
    dock->render(f.backend);

    // Foreign payload must not paint a translucent slot guide.
    const FRectangle left = dock->getSlotRect(DockArea::Slot::Left);
    bool foundGuide = false;
    for (const auto& dc : f.backend.getDrawCalls()) {
        if (dc.type != MockRenderer::DrawCall::Rect) {
            continue;
        }
        if (dc.color.w < 0.5f
            && std::fabs(dc.bounds.minX - left.minX) < 0.01f
            && std::fabs(dc.bounds.maxX - left.maxX) < 0.01f) {
            foundGuide = true;
            break;
        }
    }
    CHECK_FALSE(foundGuide);

    f.ui.endDrag(true);
    CHECK_FALSE(f.ui.isDragging());
}

// -------------------------------------------------------------------------
// 8. PR-Dock-TearOff: releasing a drag OUTSIDE the DockArea (no accepting
//    target) promotes the card to a host window — the card is floated to
//    the release point, then detachToOwnWindow hands it to the host.
// -------------------------------------------------------------------------
TEST_CASE(test_drag_outside_dock_promotes_to_host) {
    DockFixture f;

    // Smaller dock so the test can drag out of its bounds while staying
    // inside the 800x600 client.
    auto dock = std::make_unique<DockArea>();
    dock->setSize(FVector2(600.0f, 400.0f));
    f.ui.getOverlayRoot()->addChildExternal(dock.get());
    dock->performLayout();

    dock->addCard(DockArea::Slot::Left, makeFloatCard("console", L"Console"));
    dock->performLayout();
    DockCard* card = dock->findCard("console");
    CHECK_NOT_NULL(card);
    if (card == nullptr) return;

    bool promoted = false;
    std::string promotedId;
    card->setPromoteCallback(
        [&](DockCard* promotedCard, const std::wstring&,
            int, int, int, int) -> bool {
            promoted = true;
            promotedId = promotedCard->getId();
            return true;   // host accepts
        });

    CHECK(simulateTitleBarClick(card, titleBarPoint(card)));
    CHECK(f.ui.isDragging());

    // Drag to a point OUTSIDE the DockArea (client is 800x600).
    f.ui.onMouseMove(750.0f, 500.0f);
    CHECK_FALSE(dock->isCurrentDropTarget());

    // Release → void drop → promote.
    f.ui.onMouseButtonUp(750.0f, 500.0f, 0);
    CHECK_FALSE(f.ui.isDragging());
    CHECK(promoted);
    CHECK(promotedId == "console");
    // Card left the slot (floated + detached to the host window).
    CHECK(dock->getCardCount(DockArea::Slot::Left) == 0);
    CHECK(dock->getOverlay()->getFloatingCardCount() == 0);
}

// -------------------------------------------------------------------------
// 8b. PR-Dock-TearOff: a title-bar CLICK without movement must NOT
//     promote (movement threshold). The card stays docked.
// -------------------------------------------------------------------------
TEST_CASE(test_titlebar_click_without_drag_does_not_promote) {
    DockFixture f;

    auto dock = std::make_unique<DockArea>();
    dock->setSize(FVector2(600.0f, 400.0f));
    // Match Gallery: disable Top/Bottom so a title click isn't classified
    // as a Top-slot drop on mouse-up (updateDrag runs before endDrag).
    dock->setSlotWeight(DockArea::Slot::Top, 1e-6f);
    dock->setSlotWeight(DockArea::Slot::Bottom, 1e-6f);
    f.ui.getOverlayRoot()->addChildExternal(dock.get());
    dock->addCard(DockArea::Slot::Left, makeFloatCard("console", L"Console"));
    dock->performLayout();

    DockCard* card = dock->findCard("console");
    CHECK_NOT_NULL(card);
    if (card == nullptr) return;

    int promoteCount = 0;
    card->setPromoteCallback(
        [&](DockCard*, const std::wstring&,
            int, int, int, int) -> bool {
            ++promoteCount;
            return true;
        });

    const FRectangle b = card->getWorldBounds();
    const FVector2 titlePt((b.minX + b.maxX) * 0.5f, b.minY + 4.0f);
    CHECK(simulateTitleBarClick(card, titlePt));
    CHECK(f.ui.isDragging());

    // Release at the same spot — no travel → no promote.
    f.ui.onMouseButtonUp(titlePt.x, titlePt.y, 0);
    CHECK_FALSE(f.ui.isDragging());
    CHECK(promoteCount == 0);
    CHECK(dock->getCardCount(DockArea::Slot::Left) == 1);
    CHECK(dock->getOverlay()->getFloatingCardCount() == 0);
}

// -------------------------------------------------------------------------
// 9. PR-Dock-TearOff regression: the G12 payload kind must reach
//    DockArea::onDrop. beginDrag reads source->getDragPayload() BEFORE
//    firing _onDragStart, so the card's ctor must pre-stamp
//    kind="DockCard" — otherwise the kind gate silently rejects every
//    drop (the "drag does nothing / can't change layout" symptom).
// -------------------------------------------------------------------------
TEST_CASE(test_drop_on_slot_moves_card_end_to_end) {
    DockFixture f;

    auto dock = std::make_unique<DockArea>();
    dock->setSize(FVector2(800.0f, 600.0f));
    dock->setSlotWeight(DockArea::Slot::Top, 1e-6f);
    dock->setSlotWeight(DockArea::Slot::Bottom, 1e-6f);
    f.ui.getOverlayRoot()->addChildExternal(dock.get());
    dock->addCard(DockArea::Slot::Left, makeFloatCard("console", L"Console"));
    dock->performLayout();

    DockCard* card = dock->findCard("console");
    CHECK_NOT_NULL(card);
    if (card == nullptr) return;

    CHECK(simulateTitleBarClick(card, titleBarPoint(card)));
    CHECK(f.ui.isDragging());

    // Drag into the CENTER slot region and release.
    f.ui.onMouseMove(400.0f, 300.0f);
    f.ui.onMouseButtonUp(400.0f, 300.0f, 0);

    CHECK_FALSE(f.ui.isDragging());
    CHECK(dock->getCardCount(DockArea::Slot::Left) == 0);
    CHECK(dock->getCardCount(DockArea::Slot::Center) == 1);
    CHECK(dock->findCard("console") != nullptr);
}

// Same-slot drop must stay docked (K-INV-D3-1). Regression: onDrop used
// to fall through to floatCard because hitTestOverlay covers the dock.
TEST_CASE(test_same_slot_drop_keeps_card_docked) {
    DockFixture f;

    auto dock = std::make_unique<DockArea>();
    dock->setSize(FVector2(800.0f, 600.0f));
    dock->setSlotWeight(DockArea::Slot::Top, 1e-6f);
    dock->setSlotWeight(DockArea::Slot::Bottom, 1e-6f);
    f.ui.getOverlayRoot()->addChildExternal(dock.get());
    dock->addCard(DockArea::Slot::Left, makeFloatCard("console", L"Console"));
    dock->performLayout();

    DockCard* card = dock->findCard("console");
    CHECK_NOT_NULL(card);
    if (card == nullptr) return;

    int promoteCount = 0;
    card->setPromoteCallback(
        [&](DockCard*, const std::wstring&,
            int, int, int, int) -> bool {
            ++promoteCount;
            return true;
        });

    const FRectangle b = card->getWorldBounds();
    const FVector2 titlePt((b.minX + b.maxX) * 0.5f, b.minY + 4.0f);
    CHECK(simulateTitleBarClick(card, titlePt));
    const FVector2 dropPt(b.minX + 20.0f, (b.minY + b.maxY) * 0.5f);
    f.ui.onMouseMove(dropPt.x, dropPt.y);
    CHECK(dock->hitTestSlot(dropPt) == DockArea::Slot::Left);
    f.ui.onMouseButtonUp(dropPt.x, dropPt.y, 0);

    CHECK_FALSE(f.ui.isDragging());
    CHECK(dock->getCardCount(DockArea::Slot::Left) == 1);
    CHECK(dock->getOverlay()->getFloatingCardCount() == 0);
    CHECK(promoteCount == 0);
}

// Center is single-occupant: moving Left→Center swaps the prior Center
// card into Left instead of stacking full-rect siblings.
TEST_CASE(test_move_into_center_swaps_occupant) {
    DockFixture f;

    auto dock = std::make_unique<DockArea>();
    dock->setSize(FVector2(800.0f, 600.0f));
    f.ui.getOverlayRoot()->addChildExternal(dock.get());
    dock->performLayout();

    dock->addCard(DockArea::Slot::Center, makeFloatCard("viewport", L"Center"));
    dock->addCard(DockArea::Slot::Left, makeFloatCard("console", L"Console"));
    CHECK(dock->moveInSlot("console", DockArea::Slot::Center));

    CHECK(dock->getCardCount(DockArea::Slot::Center) == 1);
    CHECK(dock->getCardCount(DockArea::Slot::Left) == 1);
    CHECK(dock->isCardInSlot(dock->findCard("console"), DockArea::Slot::Center));
    CHECK(dock->isCardInSlot(dock->findCard("viewport"), DockArea::Slot::Left));
}

// Opposite side with an occupant must swap, not VBox-stack both on one side.
TEST_CASE(test_move_into_occupied_side_swaps) {
    DockFixture f;

    auto dock = std::make_unique<DockArea>();
    dock->setSize(FVector2(800.0f, 600.0f));
    f.ui.getOverlayRoot()->addChildExternal(dock.get());
    dock->performLayout();

    dock->addCard(DockArea::Slot::Left, makeFloatCard("left", L"Left"));
    dock->addCard(DockArea::Slot::Right, makeFloatCard("right", L"Right"));
    CHECK(dock->moveInSlot("right", DockArea::Slot::Left));

    CHECK(dock->getCardCount(DockArea::Slot::Left) == 1);
    CHECK(dock->getCardCount(DockArea::Slot::Right) == 1);
    CHECK(dock->isCardInSlot(dock->findCard("right"), DockArea::Slot::Left));
    CHECK(dock->isCardInSlot(dock->findCard("left"), DockArea::Slot::Right));
}

// After a swap into Center the incoming card must still receive title-bar
// hits (regression: full-bleed content stole presses → undraggable).
TEST_CASE(test_center_card_title_bar_hit_after_swap) {
    DockFixture f;

    auto dock = std::make_unique<DockArea>();
    dock->setSize(FVector2(800.0f, 600.0f));
    dock->setSlotWeight(DockArea::Slot::Top, 1e-6f);
    dock->setSlotWeight(DockArea::Slot::Bottom, 1e-6f);
    f.ui.getOverlayRoot()->addChildExternal(dock.get());
    dock->addCard(DockArea::Slot::Center, makeFloatCard("viewport", L"Center"));
    dock->addCard(DockArea::Slot::Left, makeFloatCard("console", L"Console"));
    CHECK(dock->moveInSlot("console", DockArea::Slot::Center));
    dock->performLayout();

    DockCard* console = dock->findCard("console");
    CHECK_NOT_NULL(console);
    if (console == nullptr) return;

    const FVector2 titlePt = titleBarPoint(console);
    CHECK(console->hitTest(titlePt) == console);
    CHECK(simulateTitleBarClick(console, titlePt));
    CHECK(f.ui.isDragging());
    f.ui.onKeyDown(UIKey_Escape);
}

// Gallery path: Center must be reachable through UIManager hit routing.
// Plain Widget Center containers swallowed hits — direct DockCard::
// onMouseButtonDown tests stayed green while Gallery stayed undraggable.
TEST_CASE(test_center_card_drag_via_uimanager_hit) {
    DockFixture f;

    auto dock = std::make_unique<DockArea>();
    dock->setSize(FVector2(800.0f, 600.0f));
    dock->setSlotWeight(DockArea::Slot::Top, 1e-6f);
    dock->setSlotWeight(DockArea::Slot::Bottom, 1e-6f);
    f.ui.getOverlayRoot()->addChildExternal(dock.get());
    dock->addCard(DockArea::Slot::Center, makeFloatCard("viewport", L"Center"));
    dock->addCard(DockArea::Slot::Left, makeFloatCard("console", L"Console"));
    CHECK(dock->moveInSlot("console", DockArea::Slot::Center));
    dock->performLayout();

    DockCard* console = dock->findCard("console");
    CHECK_NOT_NULL(console);
    if (console == nullptr) return;

    const FVector2 titlePt = titleBarPoint(console);
    CHECK(f.ui.onMouseButtonDown(titlePt.x, titlePt.y, 0));
    CHECK(f.ui.isDragging());
    CHECK(f.ui.getDragSource() == console);
    f.ui.onKeyDown(UIKey_Escape);
    CHECK_FALSE(f.ui.isDragging());
}

// Center leaf must fill the slot. The card starts four pixels below the leaf
// because the 26px tab strip covers its 22px legacy header.
TEST_CASE(test_center_card_fills_slot_height) {
    DockFixture f;

    auto dock = std::make_unique<DockArea>();
    dock->setSize(FVector2(800.0f, 600.0f));
    dock->setSlotWeight(DockArea::Slot::Top, 1e-6f);
    dock->setSlotWeight(DockArea::Slot::Bottom, 1e-6f);
    f.ui.getOverlayRoot()->addChildExternal(dock.get());
    dock->addCard(DockArea::Slot::Center, makeFloatCard("viewport", L"Center"));
    dock->performLayout();
    // Second layout mimics UIManager::layout re-entrancy (compoundDescend
    // then slot split) — VBox Center used to crush the card here.
    dock->performLayout();

    DockCard* card = dock->findCard("viewport");
    CHECK_NOT_NULL(card);
    if (card == nullptr) return;

    const FRectangle slot = dock->getSlotRect(DockArea::Slot::Center);
    const FRectangle cb = card->getWorldBounds();
    CHECK(std::fabs(cb.maxY - slot.maxY) < 1.0f);
    CHECK(std::fabs(cb.minY - (slot.minY + 4.0f)) < 1.0f);
}

// Nested dock (Gallery page padding) — world mouse must map through
// getWorldPosition(); local-only hit-test pinned every drop under Center.
TEST_CASE(test_hit_test_slot_uses_world_origin) {
    DockFixture f;
    auto dock = std::make_unique<DockArea>();
    dock->setPosition(FVector2(40.0f, 80.0f));
    dock->setSize(FVector2(800.0f, 600.0f));
    dock->setSlotWeight(DockArea::Slot::Top, 1e-6f);
    dock->setSlotWeight(DockArea::Slot::Bottom, 1e-6f);
    f.ui.getOverlayRoot()->addChildExternal(dock.get());
    dock->performLayout();

    // Local (50, 300) Left → world (90, 380). Top/Bottom disabled.
    CHECK(dock->hitTestSlot(FVector2(90.0f, 380.0f)) == DockArea::Slot::Left);
    CHECK(dock->hitTestSlot(FVector2(440.0f, 380.0f)) == DockArea::Slot::Center);

    const FRectangle left = dock->getSlotRect(DockArea::Slot::Left);
    CHECK(std::fabs(left.minX - 40.0f) < 0.01f);
    CHECK(std::fabs(left.minY - 80.0f) < 0.5f);
    CHECK(left.contains(FVector2(90.0f, 380.0f)));
}

TEST_SUITE_END
