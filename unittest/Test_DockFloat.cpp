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
#include "AYUIManager.h"
#include "AYDockArea.h"
#include "AYDockCard.h"
#include "AYDockOverlay.h"
#include "AYDragDrop.h"
#include "AYMockRenderer.h"
#include "UIKeyCode.h"

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
    DockCard* card = dock->findCard("console");
    CHECK(card != nullptr);
    if (card == nullptr) return;

    // Click on the title bar at the card's top-left corner. DockArea
    // is at world origin, card is the only child of Left VBox — its
    // world position is (0, 0) and its title strip spans [0, 22).
    const bool started = simulateTitleBarClick(card, FVector2(2.0f, 2.0f));
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
    DockCard* card = dock->findCard("console");
    CHECK(card != nullptr);
    if (card == nullptr) return;

    const bool started = simulateTitleBarClick(card, FVector2(2.0f, 2.0f));
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

    // setFloatable(false) should also clear setDraggable(false) so
    // beginDrag rejects at the source-side gate.
    CHECK_FALSE(card->isDraggable());

    const bool started = simulateTitleBarClick(card, FVector2(2.0f, 2.0f));
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

TEST_SUITE_END