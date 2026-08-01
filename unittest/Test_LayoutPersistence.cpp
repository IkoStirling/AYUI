// =============================================================================
// D4 — Layout persistence (saveLayout / saveLayoutToString) round-trip tests.
// =============================================================================
//
// These tests exercise the UILayoutLoader::saveLayout + saveLayoutToString
// pair added in D4. The wire format is unchanged from D2/D3 (D2 wire shape is
// owned by WidgetSerializer); D4 only adds a loader-resident save entry point
// so callers do not need to reach into WidgetSerializer themselves.
//
// Round-trip invariants verified:
//   * saveLayoutToString -> loadFromString preserves dock topology (cards
//     in slot, floating cards with frame).
//   * saveLayout (file) -> loadFromFile re-loads identical state.
//   * A loader instance with prior state does not leak into a save (no
//     stale _widgetsById entries — loadFromString clears at entry).
//
// K-INV-D1 applies: reloaded trees are heap-owned via the WidgetFactory,
// so each test calls destroyWidgetTree(reloaded) before returning.
// =============================================================================

#include "AYTest.h"
#include "AYLayoutLoader.h"
#include "AYWidgetFactory.h"
#include "AYWidgetSerializer.h"
#include "AYWidget.h"
#include "AYDockArea.h"
#include "AYDockCard.h"
#include "AYDockOverlay.h"
#include <ayio/File.h>

#include <cstdio>
#include <memory>
#include <string>

using namespace ayt::ui;

namespace {

// Builds a representative DockArea with cards in 3 slots plus one floating
// card. Mirrors the shape used by Test_DockAreaLoader.cpp tests so the
// serialized output is wire-compatible with what loadFromString expects.
struct BuiltDock {
    std::unique_ptr<DockArea> dock;

    BuiltDock() {
        dock = std::make_unique<DockArea>();
        dock->setId("shell");

        {
            auto h = std::make_unique<DockCard>();
            h->setId("hierarchy");
            h->setTitle(L"Hierarchy");
            dock->addCard(DockArea::Slot::Left, std::move(h));
        }
        {
            auto i = std::make_unique<DockCard>();
            i->setId("inspector");
            i->setTitle(L"Inspector");
            dock->addCard(DockArea::Slot::Right, std::move(i));
        }
        {
            auto v = std::make_unique<DockCard>();
            v->setId("viewport");
            dock->addCard(DockArea::Slot::Center, std::move(v));
        }

        DockOverlay* overlay = dock->getOverlay();
        if (overlay) {
            auto* graph = new DockCard();
            graph->setId("graph");
            graph->setTitle(L"Profiler");
            graph->setPosition({800.0f, 60.0f});
            graph->setSize({320.0f, 220.0f});
            overlay->addFloatingCard(graph);
        }

        dock->setSlotWeight(DockArea::Slot::Left, 0.30f);
        dock->setSlotWeight(DockArea::Slot::Right, 0.18f);
    }
};

} // namespace

TEST_SUITE(AYUI_LayoutPersistence)

// -------------------------------------------------------------------------
// 1. saveLayoutToString -> loadFromString round-trip on a populated DockArea.
// -------------------------------------------------------------------------
TEST_CASE(test_save_layout_to_string_round_trips_dock_area) {
    WidgetFactory::get();  // touch factory so default registrar runs
    BuiltDock built;

    UILayoutLoader saver;
    std::string json;
    const bool ok = saver.saveLayoutToString(built.dock.get(), json, false);
    CHECK(ok);
    CHECK(!json.empty());
    // Card ids + slot markers + floating frame all surface in the JSON.
    CHECK(json.find("\"hierarchy\"") != std::string::npos);
    CHECK(json.find("\"inspector\"") != std::string::npos);
    CHECK(json.find("\"viewport\"")  != std::string::npos);
    CHECK(json.find("\"cards\"")     != std::string::npos);
    CHECK(json.find("\"slot\"")      != std::string::npos);
    CHECK(json.find("\"floating\"")  != std::string::npos);
    CHECK(json.find("\"graph\"")     != std::string::npos);

    // Reload via the same loader type and verify the topology survived.
    Widget* raw = saver.loadFromString(json);
    std::unique_ptr<DockArea> reloaded(dynamic_cast<DockArea*>(raw));
    CHECK(reloaded != nullptr);
    if (!reloaded) {
        destroyWidgetTree(raw);
        return;
    }

    CHECK(reloaded->findCard("hierarchy") != nullptr);
    CHECK(reloaded->findCard("inspector") != nullptr);
    CHECK(reloaded->findCard("viewport")  != nullptr);

    DockOverlay* overlay = reloaded->getOverlay();
    CHECK(overlay != nullptr);
    if (overlay) {
        CHECK(overlay->getFloatingCardCount() == 1);
        DockCard* graph = overlay->getFloatingCard(0);
        CHECK(graph != nullptr);
        if (graph) {
            CHECK(graph->getId() == "graph");
            CHECK_FLOAT_EQ(graph->getPosition().x, 800.0f, 1e-5f);
            CHECK_FLOAT_EQ(graph->getPosition().y, 60.0f,  1e-5f);
            CHECK_FLOAT_EQ(graph->getSize().x, 320.0f, 1e-5f);
            CHECK_FLOAT_EQ(graph->getSize().y, 220.0f, 1e-5f);
        }
    }

    CHECK_FLOAT_EQ(reloaded->getSlotWeight(DockArea::Slot::Left),  0.30f, 1e-5f);
    CHECK_FLOAT_EQ(reloaded->getSlotWeight(DockArea::Slot::Right), 0.18f, 1e-5f);
}

// -------------------------------------------------------------------------
// 2. saveLayout (file) writes a file that loadFromFile re-loads cleanly.
// -------------------------------------------------------------------------
TEST_CASE(test_save_layout_to_file_round_trips_via_disk) {
    WidgetFactory::get();
    BuiltDock built;

    const std::string path = "D:/tmp/ayui_d4_persist_test.json";
    ayt::io::File::remove(path);  // start clean

    UILayoutLoader saver;
    const bool ok = saver.saveLayout(path, built.dock.get(), true /*pretty*/);
    CHECK(ok);

    // Confirm the file actually has bytes.
    const std::string readBack = ayt::io::File::readAllText(path);
    CHECK(!readBack.empty());
    CHECK(readBack.find("\"hierarchy\"") != std::string::npos);

    UILayoutLoader loader;
    Widget* raw = loader.loadFromFile(path);
    std::unique_ptr<DockArea> reloaded(dynamic_cast<DockArea*>(raw));
    CHECK(reloaded != nullptr);
    if (!reloaded) {
        destroyWidgetTree(raw);
        return;
    }
    CHECK(reloaded->findCard("hierarchy") != nullptr);
    CHECK(reloaded->findCard("inspector") != nullptr);
    CHECK(reloaded->findCard("viewport")  != nullptr);

    ayt::io::File::remove(path);
}

// -------------------------------------------------------------------------
// 3. A loader that already has stale _widgetsById entries does not leak
// them into a saveLayout -> loadFromString cycle. (loadFromString clears
// _widgetsById on entry; saveLayoutToString does not depend on it — this
// test pins that contract.)
// -------------------------------------------------------------------------
TEST_CASE(test_save_layout_clears_stale_widget_index_on_reload) {
    WidgetFactory::get();
    BuiltDock built;

    UILayoutLoader loader;

    // Seed the loader's _widgetsById with an unrelated prior load so we can
    // assert it gets cleared. The seed JSON creates a standalone Window.
    Widget* seedRaw = loader.loadFromString(R"({
        "type": "Window", "id": "stale_seed"
    })");
    CHECK(seedRaw != nullptr);
    destroyWidgetTree(seedRaw);

    // Now save + reload the dock; loadFromString must wipe _widgetsById so
    // findWidgetById("stale_seed") returns nullptr after the reload.
    std::string json;
    CHECK(loader.saveLayoutToString(built.dock.get(), json, false));
    Widget* raw = loader.loadFromString(json);
    std::unique_ptr<DockArea> reloaded(dynamic_cast<DockArea*>(raw));
    CHECK(reloaded != nullptr);
    if (!reloaded) {
        destroyWidgetTree(raw);
        return;
    }
    CHECK(loader.findWidgetById("stale_seed") == nullptr);
    CHECK(loader.findWidgetById("hierarchy")  != nullptr);
    // The reload still has the dock topology:
    CHECK(reloaded->findCard("hierarchy") != nullptr);
}

TEST_SUITE_END