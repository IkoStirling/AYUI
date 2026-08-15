// =============================================================================
// D2 — DockArea JSON bridge round-trip tests.
// =============================================================================
//
// These tests exercise the JSON wire format documented in `ay-ui.md` §D2:
//   * slotWeights + slotMinSizes (sparse maps)
//   * cards[]  — each entry has a `slot` field plus DockCard meta and an
//                optional `content` subtree built via WidgetSerializer recursion
//   * floating[] — overlays host extra cards with explicit x/y/w/h frames
//
// K-INV-D1 applies to the DockArea tree the loader builds: it is
// heap-allocated (via the WidgetFactory), so the test MUST call
// destroyWidgetTree(reloaded) on the way out. We return the DockArea*
// so the test can also call it back through destroyWidgetTree for
// correct cleanup; failing to do so leaks the dock subtree.
// =============================================================================

#include "AYTest.h"
#include "AYUI/LayoutLoader.h"
#include "AYUI/WidgetSerializer.h"
#include "AYUI/WidgetFactory.h"
#include "AYUI/DockArea.h"
#include "AYUI/DockCard.h"
#include "AYUI/DockOverlay.h"
#include "AYUI/TextLabel.h"
#include "AYUI/Box.h"
#include "AYUI/Widget.h"
#include "AYMath/MathTypes.h"
#include <cstdio>
#include <memory>
#include <string>

using namespace ayt::ui;
using namespace ayt::math;

namespace {

// Loads a DockArea from the given JSON snippet. Returns nullptr on parse
// failure or wrong type. The caller must destroyWidgetTree(returned) so
// the dock subtree is freed (K-INV-D1).
DockArea* loadDock(const char* json) {
    static UILayoutLoader loader;
    WidgetFactory::get();  // touch factory so default registrar runs
    Widget* w = loader.loadFromString(json);
    return dynamic_cast<DockArea*>(w);
}

} // namespace

TEST_SUITE(AYUI_DockAreaLoader)

// -------------------------------------------------------------------------
// 1. Minimal DockArea: empty, no cards, defaults intact
// -------------------------------------------------------------------------
TEST_CASE(test_dock_loader_minimal) {
    std::unique_ptr<DockArea> dock(loadDock(R"({
        "type": "DockArea",
        "id": "shell"
    })"));
    CHECK(dock != nullptr);
    if (!dock) return;
    CHECK(dock->getId() == "shell");
    CHECK(dock->getOverlay() != nullptr);
    for (int i = 0; i < (int)DockArea::Slot::Count; ++i) {
        CHECK(dock->getCardCount((DockArea::Slot)i) == 0);
    }
    CHECK(dock->getOverlay()->getFloatingCardCount() == 0);
    CHECK_FLOAT_EQ(dock->getSlotWeight(DockArea::Slot::Left), 0.20f, 1e-5f);
    CHECK_FLOAT_EQ(dock->getSlotWeight(DockArea::Slot::Center), 0.55f, 1e-5f);
}

// -------------------------------------------------------------------------
// 2. Per-slot weight + min-size overrides
// -------------------------------------------------------------------------
TEST_CASE(test_dock_loader_slot_weights_and_min_sizes) {
    std::unique_ptr<DockArea> dock(loadDock(R"({
        "type": "DockArea",
        "id": "shell",
        "slotWeights": {
            "Left":  0.30,
            "Right": 0.18
        },
        "slotMinSizes": {
            "Left":   120.0,
            "Bottom": 64.0
        }
    })"));
    CHECK(dock != nullptr);
    if (!dock) return;
    CHECK_FLOAT_EQ(dock->getSlotWeight(DockArea::Slot::Left),  0.30f, 1e-5f);
    CHECK_FLOAT_EQ(dock->getSlotWeight(DockArea::Slot::Right), 0.18f, 1e-5f);
    CHECK_FLOAT_EQ(dock->getSlotWeight(DockArea::Slot::Top), 0.15f, 1e-5f);
    CHECK_FLOAT_EQ(dock->getSlotWeight(DockArea::Slot::Bottom), 0.20f, 1e-5f);
    CHECK_FLOAT_EQ(dock->getSlotMinSize(DockArea::Slot::Left), 120.0f, 1e-5f);
    CHECK_FLOAT_EQ(dock->getSlotMinSize(DockArea::Slot::Bottom), 64.0f, 1e-5f);
}

// -------------------------------------------------------------------------
// 3. Cards in 3 different slots, no content, no floating
// -------------------------------------------------------------------------
TEST_CASE(test_dock_loader_cards_three_slots) {
    std::unique_ptr<DockArea> dock(loadDock(R"({
        "type": "DockArea",
        "id": "shell",
        "cards": [
            { "slot": "Left",   "id": "hierarchy", "title": "Hierarchy" },
            { "slot": "Right",  "id": "inspector", "title": "Inspector" },
            { "slot": "Center", "id": "viewport" }
        ]
    })"));
    CHECK(dock != nullptr);
    if (!dock) return;

    CHECK(dock->getCardCount(DockArea::Slot::Left)   == 1);
    CHECK(dock->getCardCount(DockArea::Slot::Right)  == 1);
    CHECK(dock->getCardCount(DockArea::Slot::Center) == 1);
    CHECK(dock->getCardCount(DockArea::Slot::Top)    == 0);
    CHECK(dock->getCardCount(DockArea::Slot::Bottom) == 0);

    DockCard* h = dock->findCard("hierarchy");
    CHECK(h != nullptr);
    if (h) CHECK(h->getTitle() == L"Hierarchy");

    DockCard* v = dock->findCard("viewport");
    CHECK(v != nullptr);
    if (v) CHECK(v->getTitle().empty());
}

// -------------------------------------------------------------------------
// 4. Card content subtree builds via WidgetSerializer recursion
// -------------------------------------------------------------------------
TEST_CASE(test_dock_loader_card_with_content) {
    std::unique_ptr<DockArea> dock(loadDock(R"({
        "type": "DockArea",
        "id": "shell",
        "cards": [
            { "slot": "Left", "id": "console", "title": "Console",
              "content": {
                  "type": "TextLabel",
                  "text": "Hello console",
                  "fontSize": 13
              }
            }
        ]
    })"));
    CHECK(dock != nullptr);
    if (!dock) return;

    DockCard* c = dock->findCard("console");
    CHECK(c != nullptr);
    if (!c) return;
    Widget* content = c->getContent();
    CHECK(content != nullptr);
    if (!content) return;
    TextLabel* lbl = dynamic_cast<TextLabel*>(content);
    CHECK(lbl != nullptr);
    if (lbl) CHECK(lbl->getText() == L"Hello console");
}

// -------------------------------------------------------------------------
// 5. Floating cards host on the overlay with explicit frame coords
// -------------------------------------------------------------------------
TEST_CASE(test_dock_loader_floating_card_with_frame) {
    std::unique_ptr<DockArea> dock(loadDock(R"({
        "type": "DockArea",
        "id": "shell",
        "floating": [
            { "id": "graph", "title": "Profiler",
              "x": 800.0, "y": 60.0, "w": 320.0, "h": 220.0 }
        ]
    })"));
    CHECK(dock != nullptr);
    if (!dock) return;

    DockOverlay* overlay = dock->getOverlay();
    CHECK(overlay != nullptr);
    if (!overlay) return;
    CHECK(overlay->getFloatingCardCount() == 1);

    DockCard* graph = overlay->getFloatingCard(0);
    CHECK(graph != nullptr);
    if (!graph) return;
    CHECK(graph->getId()   == "graph");
    CHECK(graph->getTitle() == L"Profiler");
    CHECK_FLOAT_EQ(graph->getSize().x, 320.0f, 1e-5f);
    CHECK_FLOAT_EQ(graph->getSize().y, 220.0f, 1e-5f);
    CHECK_FLOAT_EQ(graph->getPosition().x, 800.0f, 1e-5f);
    CHECK_FLOAT_EQ(graph->getPosition().y, 60.0f,  1e-5f);
}

// -------------------------------------------------------------------------
// 6. Last-write-wins on duplicate card ids (mirrors DockArea::addCard rule)
// -------------------------------------------------------------------------
TEST_CASE(test_dock_loader_duplicate_card_id_last_wins) {
    std::unique_ptr<DockArea> dock(loadDock(R"({
        "type": "DockArea",
        "id": "shell",
        "cards": [
            { "slot": "Left", "id": "dup", "title": "First" },
            { "slot": "Left", "id": "dup", "title": "Second" }
        ]
    })"));
    CHECK(dock != nullptr);
    if (!dock) return;

    CHECK(dock->getCardCount(DockArea::Slot::Left) == 1);
    DockCard* c = dock->findCard("dup");
    CHECK(c != nullptr);
    if (c) CHECK(c->getTitle() == L"Second");
}

// -------------------------------------------------------------------------
// 7. Unknown slot string is silently skipped (no crash)
// -------------------------------------------------------------------------
TEST_CASE(test_dock_loader_unknown_slot_is_skipped) {
    std::unique_ptr<DockArea> dock(loadDock(R"({
        "type": "DockArea",
        "id": "shell",
        "cards": [
            { "slot": "Left",   "id": "ok" },
            { "slot": "Sidebar","id": "rejected" },
            { "slot": "Right",  "id": "also_ok" }
        ]
    })"));
    CHECK(dock != nullptr);
    if (!dock) return;

    CHECK(dock->findCard("ok")      != nullptr);
    CHECK(dock->findCard("also_ok") != nullptr);
    CHECK(dock->findCard("rejected") == nullptr);
}

// -------------------------------------------------------------------------
// 8. Serialize a built DockArea back to JSON — round-trip
// -------------------------------------------------------------------------
TEST_CASE(test_dock_serializer_emits_wire_compatible_json) {
    DockArea dock;
    dock.setId("shell");
    {
        auto h = std::make_unique<DockCard>();
        h->setId("hierarchy");
        h->setTitle(L"Hierarchy");
        dock.addCard(DockArea::Slot::Left, std::move(h));
    }
    {
        auto i = std::make_unique<DockCard>();
        i->setId("inspector");
        i->setTitle(L"Inspector");
        dock.addCard(DockArea::Slot::Right, std::move(i));
    }
    {
        auto v = std::make_unique<DockCard>();
        v->setId("viewport");
        dock.addCard(DockArea::Slot::Center, std::move(v));
    }

    const std::string s = WidgetSerializer::serialize(&dock, false);
    CHECK(!s.empty());
    CHECK(s.find("\"hierarchy\"") != std::string::npos);
    CHECK(s.find("\"inspector\"") != std::string::npos);
    CHECK(s.find("\"viewport\"")  != std::string::npos);
    CHECK(s.find("\"cards\"")     != std::string::npos);
    CHECK(s.find("\"slot\"")      != std::string::npos);

    // Round-trip back through the loader and verify identity.
    static UILayoutLoader loader;
    Widget* reloadedRaw = loader.loadFromString(s);
    std::unique_ptr<DockArea> reloaded(dynamic_cast<DockArea*>(reloadedRaw));
    CHECK(reloaded != nullptr);
    if (!reloaded) {
        destroyWidgetTree(reloadedRaw);
        return;
    }
    CHECK(reloaded->findCard("hierarchy") != nullptr);
    CHECK(reloaded->findCard("inspector") != nullptr);
    CHECK(reloaded->findCard("viewport")  != nullptr);
}

// -------------------------------------------------------------------------
// 9. Card-level serialize emits meta + content subtree
// -------------------------------------------------------------------------
TEST_CASE(test_dock_serializer_emits_content_for_card_with_label) {
    DockCard card;
    card.setId("console");
    card.setTitle(L"Console");
    auto* lbl = new TextLabel();
    lbl->setText(L"Hello console");
    card.setContent(lbl);

    const std::string s = WidgetSerializer::serializeWidget(&card);
    CHECK(s.find("DockCard") != std::string::npos);
    CHECK(s.find("\"id\": \"console\"") != std::string::npos);
    CHECK(s.find("\"content\"")   != std::string::npos);
    CHECK(s.find("\"TextLabel\"") != std::string::npos);
    CHECK(s.find("Hello console") != std::string::npos);
    // DockCard's children[] must be suppressed when "content" is present;
    // otherwise the loader would double-add the label subtree.
    CHECK(s.find("\"children\"") == std::string::npos);
}

TEST_SUITE_END
