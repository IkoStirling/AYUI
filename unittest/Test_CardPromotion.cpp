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
        [&cap](const std::string& id,
               const std::wstring& title,
               int x, int y, int w, int h) -> bool {
            cap.invoked = true;
            cap.cardId = id;
            cap.title  = title;
            cap.x = x; cap.y = y; cap.w = w; cap.h = h;
            return cap.returnedAccepted;
        });

    const bool accepted = card->detachToOwnWindow();
    CHECK(accepted == true);
    CHECK(cap.invoked);

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
        [&callCount](const std::string& /*id*/,
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

TEST_SUITE_END