// =============================================================================
// D5.5 — Card promotion tests (DockCard::detachToOwnWindow).
// =============================================================================
//
// Verifies the PromoteCallback injection hook added in D5.5:
//   * No callback set → detachToOwnWindow returns false, card stays put
//     (K-INV-D5.5-1).
//   * Callback injected → callback receives the card's id, title, and
//     frame (x, y, w, h); host returning true causes DockCard to detach
//     from its parent DockOverlay.
//   * Host returning false aborts the promotion; card stays in overlay
//     and floatCard count is unchanged.
//
// AYUI does not depend on AYEditor — the host is a plain std::function
// here. The editor shell wires EditorChildWindowManager::openChildWindow
// into the same hook in its own code path (D5.5 wiring on the AYEditor
// side is a separate, single-line commit).
// =============================================================================

#include "AYTest.h"
#include "AYDockArea.h"
#include "AYDockCard.h"
#include "AYDockOverlay.h"
#include "AYMockRenderer.h"
#include "AYUIManager.h"

#include <memory>
#include <string>

using namespace ayt::ui;
using namespace ayt::math;

namespace {

// Captured callback arguments; the test inspects these to verify the
// contract.
struct CapturedPromote {
    bool invoked = false;
    bool returnedAccepted = true;
    DockCard* card = nullptr;
    std::string cardId;
    std::wstring title;
    int x = 0, y = 0, w = 0, h = 0;
};

// Build a dock with one floating card. Returns the dock so the test can
// query findCard / getOverlay after the promotion attempt.
struct PromotionFixture {
    MockRenderer backend;
    UIManager    ui;
    std::unique_ptr<DockArea> dock;

    PromotionFixture()
        : dock(std::make_unique<DockArea>())
    {
        ui.initialize(&backend);
        ui.setClientSize(1024.0f, 768.0f);
        dock->setId("shell");
    }
    ~PromotionFixture() {
        ui.shutdown();
    }

    DockCard* addProfilerCard() {
        DockOverlay* overlay = dock->getOverlay();
        auto* card = new DockCard();
        card->setId("profiler");
        card->setTitle(L"Profiler");
        card->setPosition({800.0f, 60.0f});
        card->setSize({320.0f, 220.0f});
        overlay->addFloatingCard(card);
        return card;
    }
};

} // namespace

TEST_SUITE(AYUI_CardPromotion)

// -------------------------------------------------------------------------
// 1. detachToOwnWindow without a callback returns false and the card
//    remains in the overlay (K-INV-D5.5-1).
// -------------------------------------------------------------------------
TEST_CASE(test_detach_without_callback_is_noop) {
    PromotionFixture f;
    DockCard* card = f.addProfilerCard();

    // No setPromoteCallback — default is empty std::function.
    const bool accepted = card->detachToOwnWindow();
    CHECK(accepted == false);

    // Card still in the overlay.
    DockOverlay* overlay = f.dock->getOverlay();
    CHECK(overlay != nullptr);
    CHECK(overlay->getFloatingCardCount() == 1);
    CHECK(overlay->getFloatingCard(0) == card);
}

// -------------------------------------------------------------------------
// 2. Callback injection: detachToOwnWindow forwards id/title/frame,
//    host returning true detaches the card from the overlay.
// -------------------------------------------------------------------------
TEST_CASE(test_detach_with_callback_forwards_frame_and_detaches) {
    PromotionFixture f;
    DockCard* card = f.addProfilerCard();

    CapturedPromote cap;
    cap.returnedAccepted = true;  // simulate host accepting the promotion

    card->setPromoteCallback(
        [&cap](DockCard* promoted,
               const std::wstring& title,
               int x, int y, int w, int h) -> bool {
            cap.invoked = true;
            cap.card    = promoted;
            cap.cardId  = promoted->getId();
            cap.title   = title;
            cap.x = x; cap.y = y; cap.w = w; cap.h = h;
            return cap.returnedAccepted;
        });

    const bool accepted = card->detachToOwnWindow();
    CHECK(accepted == true);
    CHECK(cap.invoked);
    // PR-Dock-TearOff: the host receives the card itself (live-card
    // migration), not just its id.
    CHECK(cap.card == card);

    // Frame forwarded verbatim — ints (not float), per the API surface.
    CHECK(cap.cardId == "profiler");
    CHECK(cap.title  == L"Profiler");
    CHECK(cap.x == 800);
    CHECK(cap.y == 60);
    CHECK(cap.w == 320);
    CHECK(cap.h == 220);

    // Card detached from the overlay (overlay no longer holds it as a
    // floating child). dock->findCard also returns nullptr because we
    // are not in a slot either.
    DockOverlay* overlay = f.dock->getOverlay();
    CHECK(overlay->getFloatingCardCount() == 0);
    CHECK(f.dock->findCard("profiler") == nullptr);
    // ~DockArea destroys children; the card pointer is dangling after
    // this test ends, which is fine — f.dock's destructor handles it.
}

// -------------------------------------------------------------------------
// 3. Host returning false aborts: callback fires, card stays in overlay.
// -------------------------------------------------------------------------
TEST_CASE(test_detach_host_declined_keeps_card_in_overlay) {
    PromotionFixture f;
    DockCard* card = f.addProfilerCard();

    int callCount = 0;
    card->setPromoteCallback(
        [&callCount](DockCard* /*card*/,
                     const std::wstring& /*title*/,
                     int, int, int, int) -> bool {
            ++callCount;
            return false;  // host declined
        });

    const bool accepted = card->detachToOwnWindow();
    CHECK(accepted == false);
    CHECK(callCount == 1);

    DockOverlay* overlay = f.dock->getOverlay();
    CHECK(overlay->getFloatingCardCount() == 1);
    CHECK(overlay->getFloatingCard(0) == card);
}

// -------------------------------------------------------------------------
// 4. PR-Dock-TearOff live-card migration: the callback reparents the
//    card into a SECOND UIManager's tree (the child-window host). The
//    whole live subtree moves; the source overlay forgets the card.
// -------------------------------------------------------------------------
TEST_CASE(test_promote_live_migration_reparents_card) {
    // Second UIManager = the child-window host tree the card migrates to.
    MockRenderer childBackend;
    UIManager    childUi;
    childUi.initialize(&childBackend);
    childUi.setClientSize(320.0f, 220.0f);

    PromotionFixture f;
    DockCard* card = f.addProfilerCard();
    auto* content = new Widget();
    content->setId("live-content");
    card->setContent(content);

    card->setPromoteCallback(
        [&childUi](DockCard* c, const std::wstring&,
                   int, int, int w, int h) -> bool {
            c->setPosition(FVector2(0.0f, 0.0f));
            c->setSize(FVector2(static_cast<float>(w),
                                      static_cast<float>(h)));
            // addChild auto-detaches from the old parent (the overlay).
            childUi.root()->addChild(c);
            return true;
        });

    const bool accepted = card->detachToOwnWindow();
    CHECK(accepted == true);
    CHECK(card->getParent() == childUi.root());
    // Source overlay no longer holds it.
    CHECK(f.dock->getOverlay()->getFloatingCardCount() == 0);
    // The live content subtree migrated with the card.
    CHECK(card->getContent() == content);

    childUi.shutdown();
}

// -------------------------------------------------------------------------
// 5. PR-Dock-TearOff nested-dock coordinates: floatCard takes a ROOT-space
//    point; the card lands at LOCAL (pos - dock world origin), and the
//    promote callback receives the WORLD position (= root space).
// -------------------------------------------------------------------------
TEST_CASE(test_nested_dock_float_and_promote_coords) {
    PromotionFixture f;
    // Nest the dock inside an offset container — mirrors the editor shell
    // (main_dock sits below the header HBox) and gallery (mini_dock in a
    // padded page). addChildExternal = borrowed parenting, so the
    // fixture's unique_ptr stays the owner.
    f.dock->setPosition(FVector2(40.0f, 80.0f));
    f.dock->setSize(FVector2(800.0f, 600.0f));
    f.dock->setSlotWeight(DockArea::Slot::Top, 1e-6f);
    f.dock->setSlotWeight(DockArea::Slot::Bottom, 1e-6f);
    f.ui.getOverlayRoot()->addChildExternal(f.dock.get());
    f.dock->performLayout();

    auto card = std::make_unique<DockCard>();
    card->setId("nested");
    f.dock->addCard(DockArea::Slot::Left, std::move(card));
    f.dock->performLayout();
    DockCard* c = f.dock->findCard("nested");
    CHECK(c != nullptr);

    CapturedPromote cap;
    cap.returnedAccepted = true;
    c->setPromoteCallback(
        [&cap](DockCard* promoted, const std::wstring&,
               int x, int y, int, int) -> bool {
            cap.invoked = true;
            cap.card    = promoted;
            cap.x = x; cap.y = y;
            return cap.returnedAccepted;
        });

    // floatCard takes a ROOT-space point. Dock world origin = (40,80),
    // so root (140,180) → local (100,100).
    f.dock->floatCard("nested", FVector2(140.0f, 180.0f));
    DockCard* floater = f.dock->findCard("nested");
    CHECK(floater != nullptr);
    const FVector2 local = floater->getPosition();
    CHECK(std::fabs(local.x - 100.0f) < 0.01f);
    CHECK(std::fabs(local.y - 100.0f) < 0.01f);

    // detachToOwnWindow forwards the WORLD position (== root space).
    const bool accepted = floater->detachToOwnWindow();
    CHECK(accepted == true);
    CHECK(cap.invoked);
    CHECK(cap.card == floater);
    CHECK(cap.x == 140);
    CHECK(cap.y == 180);
}

TEST_SUITE_END