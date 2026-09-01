#include "AYTest.h"
#include "AYUI/DockArea.h"
#include "AYUI/DockCard.h"
#include "AYUI/DockOverlay.h"
#include "AYUI/Box.h"
#include "AYUI/Widget.h"
#include "AYUI/TextLabel.h"
#include "AYUI/Button.h"
#include "AYUI/MockRenderer.h"
#include <memory>

using namespace ayt::ui;
using namespace ayt::math;
namespace um = ayt::math;

namespace {

// Helper: build a card with the given id/title so each test stays terse.
std::unique_ptr<DockCard> makeCard(const std::string& id, const std::wstring& title) {
    auto card = std::make_unique<DockCard>();
    card->setId(id);
    card->setTitle(title);
    card->setSize(FVector2(200.0f, 150.0f));
    return card;
}

} // namespace

TEST_SUITE(AYUI_DockArea)

TEST_CASE(test_dock_area_basic_construction) {
    DockArea dock;
    // The overlay is always present.
    CHECK(dock.getOverlay() != nullptr);
    // DockArea is a DockCard transport target, not a generic drop sink.
    // Unrelated payloads must keep walking to their own editor target.
    CHECK(dock.isAcceptDrops());
    CHECK(dock.acceptsKind("DockCard"));
    CHECK_FALSE(dock.acceptsKind("EditorAsset"));
    // Slot weights default to non-zero per the header initialiser.
    CHECK(dock.getSlotWeight(DockArea::Slot::Left) > 0.0f);
    CHECK(dock.getSlotWeight(DockArea::Slot::Center) > 0.0f);
    // Each slot starts empty.
    CHECK(dock.getCardCount(DockArea::Slot::Left) == 0);
    CHECK(dock.getCardCount(DockArea::Slot::Right) == 0);
    CHECK(dock.getCardCount(DockArea::Slot::Top) == 0);
    CHECK(dock.getCardCount(DockArea::Slot::Bottom) == 0);
    CHECK(dock.getCardCount(DockArea::Slot::Center) == 0);
    // Stack DockArea: ~DockArea frees owned children. Do NOT
    // destroyWidgetTree(&dock) — that would operator delete a stack object.
}

TEST_CASE(test_dock_area_parse_slot_strings) {
    DockArea::Slot s;
    CHECK(DockArea::parseSlot("Left", s) == true);
    CHECK(s == DockArea::Slot::Left);
    CHECK(DockArea::parseSlot("Right", s) == true);
    CHECK(s == DockArea::Slot::Right);
    CHECK(DockArea::parseSlot("Top", s) == true);
    CHECK(s == DockArea::Slot::Top);
    CHECK(DockArea::parseSlot("Bottom", s) == true);
    CHECK(s == DockArea::Slot::Bottom);
    CHECK(DockArea::parseSlot("Center", s) == true);
    CHECK(s == DockArea::Slot::Center);
    // Unknown slot strings return false.
    CHECK(DockArea::parseSlot("Bogus", s) == false);
    CHECK(DockArea::parseSlot("", s) == false);
    // s must remain untouched after a failed parse.
    CHECK(DockArea::parseSlot("Left", s) == true);
}

TEST_CASE(test_dock_area_add_card_to_left_slot) {
    DockArea dock;
    dock.addCard(DockArea::Slot::Left, makeCard("inspector", L"Inspector"));
    CHECK(dock.getCardCount(DockArea::Slot::Left) == 1);
    CHECK(dock.getCard(DockArea::Slot::Left, 0) != nullptr);
    CHECK(dock.getCard(DockArea::Slot::Left, 0)->getId() == "inspector");
    CHECK(dock.getCard(DockArea::Slot::Left, 0)->getTitle() == L"Inspector");
}

TEST_CASE(test_dock_area_find_card_across_slots) {
    DockArea dock;
    dock.addCard(DockArea::Slot::Left,   makeCard("hierarchy", L"Hierarchy"));
    dock.addCard(DockArea::Slot::Right,  makeCard("inspector", L"Inspector"));
    dock.addCard(DockArea::Slot::Center, makeCard("viewport",  L"Viewport"));

    DockCard* c1 = dock.findCard("hierarchy");
    CHECK(c1 != nullptr);
    CHECK(c1->getId() == "hierarchy");
    DockCard* c2 = dock.findCard("inspector");
    CHECK(c2 != nullptr);
    CHECK(c2->getId() == "inspector");
    DockCard* c3 = dock.findCard("viewport");
    CHECK(c3 != nullptr);
    CHECK(c3->getId() == "viewport");
    // Missing ids return nullptr.
    CHECK(dock.findCard("does_not_exist") == nullptr);
}

TEST_CASE(test_dock_area_remove_card) {
    DockArea dock;
    dock.addCard(DockArea::Slot::Left,  makeCard("a", L"A"));
    dock.addCard(DockArea::Slot::Left,  makeCard("b", L"B"));
    dock.addCard(DockArea::Slot::Right, makeCard("c", L"C"));

    CHECK(dock.getCardCount(DockArea::Slot::Left) == 2);
    CHECK(dock.removeCard("a") == true);
    CHECK(dock.getCardCount(DockArea::Slot::Left) == 1);
    CHECK(dock.findCard("a") == nullptr);
    // The remaining card on the left slot is "b".
    CHECK(dock.getCard(DockArea::Slot::Left, 0)->getId() == "b");
    // Removing a missing card returns false.
    CHECK(dock.removeCard("ghost") == false);
    // Removing from a different slot still works.
    CHECK(dock.removeCard("c") == true);
    CHECK(dock.getCardCount(DockArea::Slot::Right) == 0);
}

TEST_CASE(test_dock_area_add_card_replaces_same_id) {
    // D1 invariant: addCard with an existing id replaces the old card
    // ("last write wins" - mirrors Loader round-trip).
    DockArea dock;
    dock.addCard(DockArea::Slot::Left, makeCard("dup", L"First"));
    DockCard* first = dock.findCard("dup");
    CHECK(first != nullptr);
    CHECK(first->getTitle() == L"First");

    dock.addCard(DockArea::Slot::Left, makeCard("dup", L"Second"));
    DockCard* second = dock.findCard("dup");
    CHECK(second != nullptr);
    CHECK(second->getTitle() == L"Second");
    // Only one card remains under that id.
    CHECK(dock.getCardCount(DockArea::Slot::Left) == 1);
}

TEST_CASE(test_dock_area_layout_distributes_slots) {
    DockArea dock;
    dock.setSize(FVector2(1000.0f, 600.0f));
    dock.addCard(DockArea::Slot::Left,  makeCard("l", L"L"));
    dock.addCard(DockArea::Slot::Right, makeCard("r", L"R"));
    dock.addCard(DockArea::Slot::Center, makeCard("c", L"C"));
    dock.performLayout();

    // Cards live inside slot containers (VBox/HBox); local getPosition()
    // is relative to the padded container. Use world coords vs DockArea.
    DockCard* leftCard = dock.getCard(DockArea::Slot::Left, 0);
    DockCard* rightCard = dock.getCard(DockArea::Slot::Right, 0);
    DockCard* centerCard = dock.getCard(DockArea::Slot::Center, 0);
    CHECK(leftCard != nullptr);
    CHECK(rightCard != nullptr);
    CHECK(centerCard != nullptr);

    const FRectangle leftBounds  = leftCard->getWorldBounds();
    const FRectangle rightBounds = rightCard->getWorldBounds();
    const FRectangle centerBounds = centerCard->getWorldBounds();
    // Left column starts near x=0 (slot weight); Right near the right edge.
    CHECK(leftBounds.minX < 50.0f);
    CHECK(rightBounds.minX > 500.0f);
    CHECK(centerBounds.minX > leftBounds.minX);
    CHECK(centerBounds.minX < rightBounds.minX);
}

TEST_CASE(test_dock_card_basic) {
    auto card = std::make_unique<DockCard>();
    card->setId("c");
    card->setTitle(L"Hello");
    card->setIcon("ui.icon.panel");
    card->setClosable(true);
    card->setFloatable(true);
    card->setCollapsed(false);
    card->setHeaderHeight(24.0f);

    CHECK(card->getId() == "c");
    CHECK(card->getTitle() == L"Hello");
    CHECK(card->getIcon() == "ui.icon.panel");
    CHECK(card->isClosable() == true);
    CHECK(card->isFloatable() == true);
    CHECK(card->isCollapsed() == false);
    CHECK_FLOAT_EQ(card->getHeaderHeight(), 24.0f, 1e-5f);
    // No content yet.
    CHECK(card->getContent() == nullptr);
}

TEST_CASE(test_dock_card_maximize_only_when_host_chrome) {
    auto card = std::make_unique<DockCard>();
    card->setSize(FVector2(240.0f, 200.0f));
    card->setPosition(FVector2(0.0f, 0.0f));
    card->setHeaderHeight(22.0f);
    card->setClosable(true);
    card->setFloatable(false);   // avoid beginDrag on title presses
    card->performLayout();

    int maxClicks = 0;
    card->setMaximizeHandler(
        [](void* user, DockCard*) {
            *static_cast<int*>(user) += 1;
        },
        &maxClicks);

    // Maximize sits immediately left of the close X (~22px).
    const FRectangle b = card->getWorldBounds();
    const FVector2 maxPt(b.maxX - 33.0f, b.minY + 8.0f);

    CHECK_FALSE(card->showMaximizeButton());
    card->onMouseButtonDown(UIMouseEvent(maxPt, 0));
    card->onMouseButtonUp(UIMouseEvent(maxPt, 0));
    CHECK(maxClicks == 0);

    card->setShowMaximizeButton(true);
    CHECK(card->showMaximizeButton());
    card->onMouseButtonDown(UIMouseEvent(maxPt, 0));
    card->onMouseButtonUp(UIMouseEvent(maxPt, 0));
    CHECK(maxClicks == 1);
}

TEST_CASE(test_dock_card_host_chrome_uses_three_vector_buttons) {
    auto card = std::make_unique<DockCard>();
    card->setTitle(L"Detached");
    card->setSize(FVector2(240.0f, 160.0f));
    card->setHeaderHeight(22.0f);
    card->setClosable(true);
    card->setFloatable(false);
    card->setShowMinimizeButton(true);
    card->setShowMaximizeButton(true);

    int minimizeClicks = 0;
    int maximizeClicks = 0;
    card->setMinimizeHandler(
        [](void* user, DockCard*) {
            *static_cast<int*>(user) += 1;
        },
        &minimizeClicks);
    card->setMaximizeHandler(
        [](void* user, DockCard*) {
            *static_cast<int*>(user) += 1;
        },
        &maximizeClicks);
    card->performLayout();

    const FRectangle bounds = card->getWorldBounds();
    const FVector2 minimizePoint(
        bounds.maxX - 55.0f, bounds.minY + 10.0f);
    const FVector2 maximizePoint(
        bounds.maxX - 33.0f, bounds.minY + 10.0f);
    CHECK(card->onMouseButtonDown(UIMouseEvent(minimizePoint, 0)));
    CHECK(card->onMouseButtonUp(UIMouseEvent(minimizePoint, 0)));
    CHECK(card->onMouseButtonDown(UIMouseEvent(maximizePoint, 0)));
    CHECK(card->onMouseButtonUp(UIMouseEvent(maximizePoint, 0)));
    CHECK(minimizeClicks == 1);
    CHECK(maximizeClicks == 1);

    MockRenderer renderer;
    card->render(renderer);
    int vectorIcons = 0;
    bool usedTextGlyph = false;
    for (const auto& call : renderer.getDrawCalls()) {
        if (call.type == MockRenderer::DrawCall::Path) ++vectorIcons;
        if (call.type == MockRenderer::DrawCall::Text
            && (call.text == L"x" || call.text == L"X"
                || call.text == L"-" || call.text == L"□")) {
            usedTextGlyph = true;
        }
    }
    CHECK(vectorIcons >= 3);
    CHECK_FALSE(usedTextGlyph);
}

TEST_CASE(test_dock_card_clips_content_to_body) {
    MockRenderer r;
    auto card = std::make_unique<DockCard>();
    card->setSize(FVector2(80.0f, 120.0f));
    card->setHeaderHeight(22.0f);
    card->setPosition(FVector2(0.0f, 0.0f));

    auto* fat = new Widget();
    fat->setSize(FVector2(200.0f, 40.0f));   // wider than the card body
    card->setContent(fat);
    card->performLayout();

    // Body client rect excludes the header strip.
    const FRectangle cr = card->getClientRect();
    CHECK_FLOAT_EQ(cr.minY, 22.0f, 1e-5f);
    CHECK_FLOAT_EQ(cr.maxX - cr.minX, 80.0f, 1e-5f);

    card->render(r);
    CHECK(r.isClipStackBalanced());
    CHECK(r.getClipDepth() == 0);

    // Overflow past the card's right edge must not hit the fat child —
    // the card's world-bounds gate rejects first; a point inside the
    // card but outside getClientRect (header) resolves to the card.
    CHECK(card->hitTest(FVector2(90.0f, 50.0f)) == nullptr);
    CHECK(card->hitTest(FVector2(40.0f, 10.0f)) == card.get());
}

TEST_CASE(test_dock_card_set_content_owns_widget) {
    auto card = std::make_unique<DockCard>();
    auto* lbl = new TextLabel();
    lbl->setText(L"Content here");
    card->setContent(lbl);
    CHECK(card->getContent() == lbl);
    // Replacing content tears down the previous one.
    TextLabel* old = static_cast<TextLabel*>(card->getContent());
    auto* lbl2 = new TextLabel();
    lbl2->setText(L"Replaced");
    card->setContent(lbl2);
    CHECK(card->getContent() == lbl2);
    AYUNREFERENCED_PARAM(old);

    // The Card's children tree should contain the new label.
    const auto& kids = card->getChildren();
    bool found = false;
    for (auto* c : kids) {
        if (c == lbl2) { found = true; break; }
    }
    CHECK(found);
    // Destroy the card - the content label must be freed by the tree
    // teardown (no double-free). destroyWidgetTree is the only entry.
    destroyWidgetTree(card.release());
}

TEST_CASE(test_dock_card_collapse_collapses_content) {
    auto card = std::make_unique<DockCard>();
    card->setSize(FVector2(200.0f, 200.0f));
    card->setHeaderHeight(22.0f);

    auto* lbl = new TextLabel();
    card->setContent(lbl);

    card->performLayout();
    const float collapsedH = card->getContent()->getSize().y;
    // Body height = 200 - 22 = 178 when not collapsed.
    CHECK_FLOAT_EQ(collapsedH, 178.0f, 1e-5f);

    card->setCollapsed(true);
    card->performLayout();
    const float collapsedBodyH = card->getContent()->getSize().y;
    CHECK_FLOAT_EQ(collapsedBodyH, 0.0f, 1e-5f);
    destroyWidgetTree(card.release());
}

TEST_CASE(test_dock_overlay_add_remove_floating_card) {
    DockArea dock;
    DockOverlay* overlay = dock.getOverlay();
    CHECK(overlay != nullptr);
    CHECK(overlay->getFloatingCardCount() == 0);

    auto* c1 = new DockCard();
    c1->setId("console");
    overlay->addFloatingCard(c1);
    CHECK(overlay->getFloatingCardCount() == 1);
    CHECK(overlay->getFloatingCard(0) == c1);

    auto* c2 = new DockCard();
    c2->setId("graph");
    overlay->addFloatingCard(c2);
    CHECK(overlay->getFloatingCardCount() == 2);

    overlay->removeFloatingCard(c1);
    CHECK(overlay->getFloatingCardCount() == 1);
    CHECK(overlay->getFloatingCard(0) == c2);

    overlay->removeFloatingCard(c2);
    CHECK(overlay->getFloatingCardCount() == 0);

    // ~DockArea frees remaining overlay/slot children.
}

TEST_CASE(test_dock_area_slot_weight_override) {
    DockArea dock;
    dock.setSlotWeight(DockArea::Slot::Left, 0.50f);
    CHECK_FLOAT_EQ(dock.getSlotWeight(DockArea::Slot::Left), 0.50f, 1e-5f);
    // Other slots keep their defaults.
    CHECK(dock.getSlotWeight(DockArea::Slot::Right) > 0.0f);
    dock.setSlotMinSize(DockArea::Slot::Left, 120.0f);
    CHECK_FLOAT_EQ(dock.getSlotMinSize(DockArea::Slot::Left), 120.0f, 1e-5f);
}

TEST_SUITE_END
