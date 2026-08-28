#include "AYTest.h"
#include "AYMath/MathUtils.h"
#include "AYUI/LayoutLoader.h"
#include "AYUI/I18n.h"
#include "AYUI/Button.h"
#include "AYUI/TextLabel.h"
#include "AYUI/TextInput.h"
#include "AYUI/TextArea.h"
#include "AYUI/Tooltip.h"
#include "AYUI/Image.h"
#include "AYUI/ImageTexture.h"
#include "AYUI/GridPanel.h"
#include "AYUI/Window.h"
#include "AYUI/Box.h"
#include "AYUI/Widget.h"
#include <cstdio>
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>

#if defined(_WIN32)
#include <windows.h>
#endif

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_LayoutLoader)

TEST_CASE(test_layout_loader_basic) {
    UILayoutLoader loader;
    WidgetFactory::get();

    const char* json = R"({
        "type": "Widget",
        "id": "root",
        "position": { "x": 10, "y": 20 },
        "size": { "w": 100, "h": 200 }
    })";

    Widget* root = loader.loadFromString(json);
    CHECK(root != nullptr);
    CHECK(root->getId() == "root");

    FRectangle bounds = root->getWorldBounds();
    CHECK_FLOAT_EQ(bounds.minX, 10.0f, 1e-5f);
    CHECK_FLOAT_EQ(bounds.minY, 20.0f, 1e-5f);
    CHECK_FLOAT_EQ(bounds.maxX - bounds.minX, 100.0f, 1e-5f);
    CHECK_FLOAT_EQ(bounds.maxY - bounds.minY, 200.0f, 1e-5f);

    destroyWidgetTree(root);
}

TEST_CASE(test_layout_loader_button) {
    UILayoutLoader loader;

    const char* json = R"({
        "type": "Button",
        "id": "my_button",
        "text": "Click Me",
        "size": { "w": 100, "h": 32 }
    })";

    Widget* widget = loader.loadFromString(json);
    CHECK(widget != nullptr);

    Button* button = dynamic_cast<Button*>(widget);
    CHECK(button != nullptr);
    CHECK(button->getText() == L"Click Me");

    destroyWidgetTree(widget);
}

TEST_CASE(test_layout_loader_decodes_utf8_text_and_i18n) {
    UILayoutLoader loader;

    Widget* directRaw = loader.loadFromString(
        "{\"type\":\"Button\",\"text\":\"\xE7\xBB\xA7\xE7\xBB\xAD\"}");
    Button* direct = dynamic_cast<Button*>(directRaw);
    CHECK(direct != nullptr);
    if (direct != nullptr) {
        CHECK(direct->getText() == L"\u7EE7\u7EED");
    }
    destroyWidgetTree(directRaw);

    I18n& i18n = I18n::get();
    i18n.clear();
    const char* table =
        "{\"ui.resume\":{\"zh\":\"\xE7\xBB\xA7\xE7\xBB\xAD\"}}";
    CHECK(i18n.loadFromString(table, std::char_traits<char>::length(table)));
    i18n.setCurrentLanguage("zh");
    loader.setI18n(&i18n);

    Widget* translatedRaw = loader.loadFromString(
        R"({"type":"Button","text":"ui.resume"})");
    Button* translated = dynamic_cast<Button*>(translatedRaw);
    CHECK(translated != nullptr);
    if (translated != nullptr) {
        CHECK(translated->getText() == L"\u7EE7\u7EED");
    }
    destroyWidgetTree(translatedRaw);
    i18n.clear();
}

TEST_CASE(test_layout_loader_window) {
    UILayoutLoader loader;

    const char* json = R"({
        "type": "Window",
        "id": "my_window",
        "text": "My Title",
        "position": { "x": 50, "y": 50 },
        "size": { "w": 400, "h": 300 }
    })";

    Widget* widget = loader.loadFromString(json);
    CHECK(widget != nullptr);

    Window* window = dynamic_cast<Window*>(widget);
    CHECK(window != nullptr);
    CHECK(window->getTitle() == L"My Title");

    destroyWidgetTree(widget);
}

TEST_CASE(test_layout_loader_nested) {
    UILayoutLoader loader;

    const char* json = R"({
        "type": "VBox",
        "id": "container",
        "size": { "w": 200, "h": 100 },
        "children": [
            { "type": "Button", "id": "btn1", "text": "Button 1" },
            { "type": "Button", "id": "btn2", "text": "Button 2" }
        ]
    })";

    Widget* root = loader.loadFromString(json);
    CHECK(root != nullptr);
    CHECK(root->getChildren().size() == 2);

    Widget* btn1 = loader.findWidgetById("btn1");
    CHECK(btn1 != nullptr);
    CHECK(btn1->getParent() == root);

    Widget* btn2 = loader.findWidgetById("btn2");
    CHECK(btn2 != nullptr);

    destroyWidgetTree(root);
}

TEST_CASE(test_layout_loader_find_by_id) {
    UILayoutLoader loader;

    const char* json = R"({
        "type": "Widget",
        "id": "root",
        "children": [
            { "type": "Button", "id": "child1" },
            { "type": "TextLabel", "id": "child2" }
        ]
    })";

    Widget* root = loader.loadFromString(json);
    CHECK(root != nullptr);

    Widget* found = loader.findWidgetById("child1");
    CHECK(found != nullptr);
    CHECK(found->getId() == "child1");

    Widget* notFound = loader.findWidgetById("non_existent");
    CHECK(notFound == nullptr);

    destroyWidgetTree(root);
}

TEST_CASE(test_layout_loader_reload_api) {
    UILayoutLoader loader;

    // Test reload API without file - should return nullptr
    CHECK(loader.isReloadNeeded() == false);
    CHECK(loader.tryReload() == nullptr);

    // Test reload returns nullptr when no file path
    Widget* reloaded = loader.reload("test");
    CHECK(reloaded == nullptr);
}

// R-4: real hot-reload via ayio::FileWatcher. We write a JSON file to a
// temp path, loadFromFile() (which registers the OS watch), edit the file,
// sleep long enough for the watcher thread to deliver the event, then call
// tryReload() — which must return a fresh tree reflecting the edit.
TEST_CASE(test_layout_loader_hot_reload_via_file_watcher) {
    namespace fs = std::filesystem;

    // Per-user temp dir; avoid %TEMP% pollution across runs.
    fs::path tmpDir = fs::temp_directory_path() / "ayui_r4_hot_reload";
    fs::create_directories(tmpDir);
    fs::path jsonPath = tmpDir / "hot.json";
    std::string pathStr = jsonPath.string();

    auto writeJson = [&](const std::string& body) {
        std::ofstream out(jsonPath, std::ios::binary | std::ios::trunc);
        out << body;
        out.close();
    };

    auto removeAll = [&]() {
        std::error_code ec;
        fs::remove_all(tmpDir, ec);
    };

    // Cleanup any leftover from a previous failed run.
    removeAll();
    fs::create_directories(tmpDir);

    writeJson(R"({
        "type": "Widget",
        "id": "root",
        "size": { "w": 100, "h": 100 },
        "children": [
            { "type": "Button", "id": "btn_v1", "text": "v1" }
        ]
    })");

    {
        UILayoutLoader loader;
        Widget* root = loader.loadFromFile(pathStr);
        CHECK(root != nullptr);
        Widget* v1 = loader.findWidgetById("btn_v1");
        CHECK(v1 != nullptr);

        // No events yet → no reload needed.
        CHECK(loader.isReloadNeeded() == false);

        // Overwrite with a new structure: rename the button so reload is
        // detectable from the id registry. Sleep gives the watcher thread
        // time to enqueue the change event before we drain.
        writeJson(R"({
            "type": "Widget",
            "id": "root",
            "size": { "w": 100, "h": 100 },
            "children": [
                { "type": "Button", "id": "btn_v2", "text": "v2" }
            ]
        })");

        // Watcher thread latency: ReadDirectoryChangesW typically delivers
        // within 10-50ms on Windows; inotify similar on POSIX. 200ms gives
        // generous headroom for slow CI machines without making the test
        // perceptibly slow.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        CHECK(loader.isReloadNeeded() == true);
        Widget* reloaded = loader.tryReload();
        CHECK(reloaded != nullptr);

        // v1 should be gone (root was destroyed and rebuilt), v2 present.
        CHECK(loader.findWidgetById("btn_v1") == nullptr);
        Widget* v2 = loader.findWidgetById("btn_v2");
        CHECK(v2 != nullptr);

        // After successful reload, dirty flag must be clear until next event.
        CHECK(loader.isReloadNeeded() == false);

        destroyWidgetTree(reloaded);
    }

    removeAll();
}

TEST_CASE(test_layout_loader_partial_height_fills_parent_width) {
    UILayoutLoader loader;

    const char* json = R"({
        "type": "VBox",
        "id": "root",
        "size": { "w": 640, "h": 480 },
        "children": [
            {
                "type": "HBox",
                "id": "toolbar",
                "size": { "h": 44 },
                "spacing": 8,
                "padding": { "left": 8, "top": 6, "right": 8, "bottom": 6 },
                "children": [
                    { "type": "Button", "id": "btn_play",  "size": { "w": 72, "h": 32 } },
                    { "type": "Button", "id": "btn_pause", "size": { "w": 72, "h": 32 } }
                ]
            }
        ]
    })";

    Widget* root = loader.loadFromString(json);
    CHECK(root != nullptr);
    root->performLayout();

    auto* toolbar = dynamic_cast<HBox*>(loader.findWidgetById("toolbar"));
    CHECK(toolbar != nullptr);
    CHECK(toolbar->isLayoutSizeManaged());
    CHECK_FLOAT_EQ(toolbar->getHeight(), 44.0f, 1e-5f);
    // VBox padding 4 + toolbar stretches to client width (640 - 8).
    CHECK_FLOAT_EQ(toolbar->getWidth(), 632.0f, 1e-5f);

    Widget* btnPause = loader.findWidgetById("btn_pause");
    CHECK(btnPause != nullptr);
    CHECK(toolbar->hitTest(FVector2(130.0f, 22.0f)) == btnPause);

    destroyWidgetTree(root);
}

// L1 — Loader parity: TextInput.text round-trip. Before L1, the loader
// dropped this field; the inspector's `inp_pos_x` showed empty briefly
// before the first refresh tick rewrote it.
TEST_CASE(test_layout_loader_textinput_text) {
    UILayoutLoader loader;

    const char* json = R"({
        "type": "TextInput",
        "id": "inp_pos_x",
        "text": "0.00",
        "size": { "w": 50, "h": 22 }
    })";

    Widget* root = loader.loadFromString(json);
    CHECK(root != nullptr);
    auto* ti = dynamic_cast<TextInput*>(root);
    CHECK(ti != nullptr);
    CHECK(ti->getText() == L"0.00");
    destroyWidgetTree(root);
}

// L1 — Loader parity: TextArea.text.
TEST_CASE(test_layout_loader_textarea_text) {
    UILayoutLoader loader;

    const char* json = R"({
        "type": "TextArea",
        "id": "ta",
        "text": "hello\nworld",
        "size": { "w": 200, "h": 100 }
    })";

    Widget* root = loader.loadFromString(json);
    CHECK(root != nullptr);
    auto* ta = dynamic_cast<TextArea*>(root);
    CHECK(ta != nullptr);
    CHECK(ta->getText() == L"hello\nworld");
    destroyWidgetTree(root);
}

// L1 — Loader parity: Tooltip.text.
TEST_CASE(test_layout_loader_tooltip_text) {
    UILayoutLoader loader;

    const char* json = R"({
        "type": "Tooltip",
        "id": "tip",
        "text": "Hover hint",
        "size": { "w": 120, "h": 24 }
    })";

    Widget* root = loader.loadFromString(json);
    CHECK(root != nullptr);
    auto* tip = dynamic_cast<Tooltip*>(root);
    CHECK(tip != nullptr);
    CHECK(tip->getText() == L"Hover hint");
    destroyWidgetTree(root);
}

// L2 — Loader parity: Image.textureName. The loader now resolves the
// textured name via Image::setTexture, which is supposed to call into
// the TextureRegistry; if the named texture is not registered, the
// name is stored on the handle for later re-bind.
TEST_CASE(test_layout_loader_image_texture_name) {
    UILayoutLoader loader;

    const char* json = R"({
        "type": "Image",
        "id": "icon",
        "textureName": "ui.icon.play",
        "color": [1.0, 1.0, 1.0, 1.0],
        "size": { "w": 16, "h": 16 }
    })";

    Widget* root = loader.loadFromString(json);
    CHECK(root != nullptr);
    auto* img = dynamic_cast<Image*>(root);
    CHECK(img != nullptr);
    // The named texture is not registered in the unit-test process; the
    // handle still records the name so a host can re-bind later. Width
    // / height are 0 because no actual pixels live behind the name.
    const ImageTextureHandle& h = img->getTexture();
    CHECK(h.name == "ui.icon.play");
    destroyWidgetTree(root);
}

// L3 — Loader parity: GridPanel cells[] shape. (row, col, rowSpan,
// colSpan, hAlign, vAlign) all parsed and forwarded to setCell.
TEST_CASE(test_layout_loader_gridpanel_cells) {
    UILayoutLoader loader;

    const char* json = R"({
        "type": "GridPanel",
        "id": "form",
        "rowCount": 3,
        "columnCount": 3,
        "cells": [
            {
                "row": 0, "col": 0, "rowSpan": 1, "colSpan": 1,
                "hAlign": "Left", "vAlign": "Middle",
                "content": { "type": "TextLabel", "id": "lbl_name", "text": "Name" }
            },
            {
                "row": 0, "col": 1, "rowSpan": 1, "colSpan": 2,
                "hAlign": "Fill", "vAlign": "Fill",
                "content": { "type": "TextInput", "id": "inp_name", "text": "" }
            },
            {
                "row": 1, "col": 0, "rowSpan": 2, "colSpan": 1,
                "content": { "type": "Button", "id": "btn_apply", "text": "Apply" }
            }
        ]
    })";

    Widget* root = loader.loadFromString(json);
    CHECK(root != nullptr);
    auto* grid = dynamic_cast<GridPanel*>(root);
    CHECK(grid != nullptr);
    CHECK(grid->getRowCount() == 3);
    CHECK(grid->getColumnCount() == 3);
    // (0,0) = lbl_name
    Widget* c00 = grid->getCell(0, 0);
    CHECK(c00 != nullptr);
    CHECK(c00->getId() == "lbl_name");
    // (0,1) = inp_name (colSpan 2)
    Widget* c01 = grid->getCell(0, 1);
    CHECK(c01 != nullptr);
    CHECK(c01->getId() == "inp_name");
    // (1,0) = btn_apply (rowSpan 2)
    Widget* c10 = grid->getCell(1, 0);
    CHECK(c10 != nullptr);
    CHECK(c10->getId() == "btn_apply");
    destroyWidgetTree(root);
}

// L3 — Loader parity: GridPanel children[] auto-linear fallback for
// layouts that don't spell out cells[]. The next free (row, col) is
// chosen after every placed cell.
TEST_CASE(test_layout_loader_gridpanel_children_autolinear) {
    UILayoutLoader loader;

    const char* json = R"({
        "type": "GridPanel",
        "id": "form",
        "rowCount": 2,
        "columnCount": 3,
        "children": [
            { "type": "TextLabel", "id": "c0" },
            { "type": "TextLabel", "id": "c1" },
            { "type": "TextLabel", "id": "c2" },
            { "type": "TextLabel", "id": "c3" }
        ]
    })";

    Widget* root = loader.loadFromString(json);
    CHECK(root != nullptr);
    auto* grid = dynamic_cast<GridPanel*>(root);
    CHECK(grid != nullptr);
    CHECK(grid->getCell(0, 0) != nullptr);
    CHECK(grid->getCell(0, 0)->getId() == "c0");
    CHECK(grid->getCell(0, 1) != nullptr);
    CHECK(grid->getCell(0, 1)->getId() == "c1");
    CHECK(grid->getCell(0, 2) != nullptr);
    CHECK(grid->getCell(0, 2)->getId() == "c2");
    CHECK(grid->getCell(1, 0) != nullptr);
    CHECK(grid->getCell(1, 0)->getId() == "c3");
    // (1,1) and (1,2) stay empty.
    CHECK(grid->getCell(1, 1) == nullptr);
    CHECK(grid->getCell(1, 2) == nullptr);
    destroyWidgetTree(root);
}

// L4 — Loader parity: VBox.setGravity. BoxBase::setGravity has existed
// since v1.0; the loader only read spacing/padding before.
TEST_CASE(test_layout_loader_vbox_gravity) {
    UILayoutLoader loader;

    const char* json = R"({
        "type": "VBox",
        "id": "v",
        "size": { "w": 200, "h": 200 },
        "spacing": 4,
        "padding": { "left": 2, "top": 2, "right": 2, "bottom": 2 },
        "gravity": "Center",
        "children": [
            { "type": "Button", "id": "b", "size": { "w": 80, "h": 24 } }
        ]
    })";

    Widget* root = loader.loadFromString(json);
    CHECK(root != nullptr);
    auto* vbox = dynamic_cast<VBox*>(root);
    CHECK(vbox != nullptr);
    CHECK(vbox->getGravity() == BoxBase::Gravity::Center);
    destroyWidgetTree(root);
}

// L4 — Loader parity: HBox.setGravity.
TEST_CASE(test_layout_loader_hbox_gravity) {
    UILayoutLoader loader;

    const char* json = R"({
        "type": "HBox",
        "id": "h",
        "size": { "w": 200, "h": 200 },
        "gravity": "BottomRight",
        "children": [
            { "type": "Button", "id": "b", "size": { "w": 80, "h": 24 } }
        ]
    })";

    Widget* root = loader.loadFromString(json);
    CHECK(root != nullptr);
    auto* hbox = dynamic_cast<HBox*>(root);
    CHECK(hbox != nullptr);
    CHECK(hbox->getGravity() == BoxBase::Gravity::BottomRight);
    destroyWidgetTree(root);
}

// L4 — Loader parity: unknown gravity string falls back to default.
// Unknown / missing = TopLeft (pre-v1.0 default).
TEST_CASE(test_layout_loader_gravity_default_fallback) {
    UILayoutLoader loader;

    const char* json = R"({
        "type": "VBox",
        "id": "v",
        "gravity": "Bogus",
        "size": { "w": 100, "h": 100 }
    })";

    Widget* root = loader.loadFromString(json);
    CHECK(root != nullptr);
    auto* vbox = dynamic_cast<VBox*>(root);
    CHECK(vbox != nullptr);
    CHECK(vbox->getGravity() == BoxBase::Gravity::TopLeft);
    destroyWidgetTree(root);
}

// AYUI-Audit-2026-08-26 DoS: 64 MiB JSON cap. Pre-fix nlohmann::json::parse
// would happily attempt to materialize a multi-GiB document, exhausting
// memory and stalling the UI thread. The fix in
// Loader/AYLayoutLoader.cpp returns nullptr + logs an error when the
// payload exceeds 64 MiB.
TEST_CASE(layout_loader_rejects_huge_json) {
    UILayoutLoader loader;

    // 65 MiB of "{}" padding — strictly above the 64 MiB cap.
    // Avoid allocating two copies: build the JSON exactly once and
    // hand the same buffer to loadFromString. The cap check uses
    // jsonStr.size() so the literal character count matters, not the
    // parsed shape.
    constexpr size_t kPayloadBytes = 65ULL * 1024 * 1024;
    std::string huge(kPayloadBytes, '{');
    huge.append(1, '}');

    CHECK(huge.size() > kPayloadBytes);

    Widget* root = loader.loadFromString(huge);
    CHECK_NULL(root);

    // Loader's _widgetsById index must not have been mutated by a
    // failed huge-load attempt.
    CHECK(loader.findWidgetById("anything") == nullptr);
}

TEST_SUITE_END
