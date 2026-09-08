#include "AYTest.h"
#include "AYMath/MathUtils.h"
#include "AYUI/LayoutLoader.h"
#include "AYUI/I18n.h"
#include "AYUI/Button.h"
#include "AYUI/TextLabel.h"
#include "AYUI/TextInput.h"
#include "AYUI/TextArea.h"
#include "AYUI/Tooltip.h"
#include "AYUI/Menu.h"
#include "AYUI/MenuBar.h"
#include "AYUI/Image.h"
#include "AYUI/ImageTexture.h"
#include "AYUI/GridPanel.h"
#include "AYUI/ScrollView.h"
#include "AYUI/RadioButton.h"
#include "AYUI/RichText.h"
#include "AYUI/Separator.h"
#include "AYUI/TabControl.h"
#include "AYUI/TabStrip.h"
#include "AYUI/Modal.h"
#include "AYUI/ModalDialog.h"
#include "AYUI/Dimmer.h"
#include "AYUI/TreeView.h"
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

TEST_CASE(test_layout_loader_controller_event_metadata_and_resolution) {
    UILayoutLoader loader;
    int controllerHits = 0;
    loader.bindControllerEvent("MainMenuController", "startGame",
        [&controllerHits]() { ++controllerHits; });

    Widget* raw = loader.loadFromString(R"({
        "type": "Button",
        "id": "btn_start",
        "controller": "MainMenuController",
        "events": { "onClick": "startGame" },
        "size": { "w": 120, "h": 32 }
    })");
    Button* button = dynamic_cast<Button*>(raw);
    CHECK(button != nullptr);
    if (button != nullptr) {
        CHECK(button->getControllerId() == "MainMenuController");
        CHECK(button->getEventBinding("onClick") == "startGame");
        CHECK(button->onMouseButtonDown(UIMouseEvent(FVector2(10, 10), 0)));
        CHECK(button->onMouseButtonUp(UIMouseEvent(FVector2(10, 10), 0)));
        CHECK(controllerHits == 1);

        std::string saved;
        CHECK(loader.saveLayoutToString(button, saved, false));
        CHECK(saved.find("MainMenuController") != std::string::npos);
        CHECK(saved.find("startGame") != std::string::npos);
        CHECK(saved.find("events") != std::string::npos);
    }
    destroyWidgetTree(raw);
}

TEST_CASE(test_layout_loader_named_handler_and_legacy_id_binding_precedence) {
    UILayoutLoader loader;
    int namedHits = 0;
    loader.bindHandler("openInventory", [&namedHits]() { ++namedHits; });
    Widget* namedRaw = loader.loadFromString(R"({
        "type": "Button", "id": "inventory",
        "onClick": "openInventory", "size": { "w": 100, "h": 30 }
    })");
    Button* named = dynamic_cast<Button*>(namedRaw);
    CHECK(named != nullptr);
    if (named != nullptr) {
        CHECK(named->getEventBinding("onClick") == "openInventory");
        named->onMouseButtonDown(UIMouseEvent(FVector2(5, 5), 0));
        named->onMouseButtonUp(UIMouseEvent(FVector2(5, 5), 0));
    }
    CHECK(namedHits == 1);
    destroyWidgetTree(namedRaw);

    int idHits = 0;
    int fallbackHits = 0;
    loader.clearWidgetRegistry();
    loader.bindEvent("priority", "onClick", [&idHits]() { ++idHits; });
    loader.bindHandler("fallback", [&fallbackHits]() { ++fallbackHits; });
    Widget* priorityRaw = loader.loadFromString(R"({
        "type": "Button", "id": "priority",
        "events": { "onClick": "fallback" },
        "size": { "w": 100, "h": 30 }
    })");
    Button* priority = dynamic_cast<Button*>(priorityRaw);
    if (priority != nullptr) {
        priority->onMouseButtonDown(UIMouseEvent(FVector2(5, 5), 0));
        priority->onMouseButtonUp(UIMouseEvent(FVector2(5, 5), 0));
    }
    CHECK(idHits == 1);
    CHECK(fallbackHits == 0);
    destroyWidgetTree(priorityRaw);
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
        "title": "My Title",
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

TEST_CASE(test_layout_loader_scrollview_builds_and_registers_content) {
    UILayoutLoader loader;

    const char* json = R"({
        "type": "ScrollView",
        "id": "scroll",
        "size": { "w": 200, "h": 100 },
        "verticalScrollBar": true,
        "horizontalScrollBar": false,
        "verticalScrollBarVisibility": "always",
        "horizontalScrollBarVisibility": "auto",
        "contentSize": { "w": 180, "h": 240 },
        "content": {
            "type": "VBox",
            "id": "scroll_body",
            "children": [
                { "type": "Slider", "id": "deep_slider",
                  "size": { "w": 160, "h": 24 } }
            ]
        }
    })";

    Widget* root = loader.loadFromString(json);
    auto* scroll = dynamic_cast<ScrollView*>(root);
    CHECK(scroll != nullptr);
    CHECK(scroll->getContent() != nullptr);
    CHECK(scroll->getContent()->getId() == "scroll_body");
    CHECK(loader.findWidgetById("scroll_body") == scroll->getContent());
    CHECK(loader.findWidgetById("deep_slider") != nullptr);
    CHECK(scroll->isVerticalScrollBarEnabled());
    CHECK(scroll->isHorizontalScrollBarEnabled());
    CHECK(scroll->getVerticalScrollBarVisibility()
          == ScrollView::ScrollBarVisibility::Always);
    CHECK(scroll->getHorizontalScrollBarVisibility()
          == ScrollView::ScrollBarVisibility::Auto);
    CHECK_FLOAT_EQ(scroll->getContentSize().y, 240.0f, 1e-5f);

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

TEST_CASE(layout_loader_builds_structured_menubar_items_and_shortcuts) {
    UILayoutLoader loader;
    Widget* root = loader.loadFromString(R"json({
        "type": "MenuBar",
        "id": "menu",
        "anchorWidth": 52,
        "menus": [
            { "title": "File", "items": [
                { "text": "Save", "shortcut": "Ctrl+S" }
            ] },
            { "title": "Edit", "items": [
                { "text": "Undo", "shortcut": "Ctrl+Z" },
                { "text": "Delete", "shortcut": "Delete" }
            ] }
        ]
    })json");

    auto* bar = dynamic_cast<MenuBar*>(root);
    CHECK(bar != nullptr);
    CHECK(bar != nullptr && bar->getMenuCount() == 2u);
    CHECK(bar != nullptr && bar->getMenuTitle(0) == L"File");
    CHECK(bar != nullptr && bar->getMenuTitle(1) == L"Edit");
    Menu* edit = bar != nullptr ? bar->getMenu(1) : nullptr;
    CHECK(edit != nullptr);
    CHECK(edit != nullptr && edit->getItemCount() == 2u);
    CHECK(edit == nullptr || edit->getItem(0) == nullptr
          || edit->getItem(0)->getShortcut() == L"Ctrl+Z");
    destroyWidgetTree(root);
}

TEST_CASE(layout_loader_builds_structured_tabs_and_modals) {
    UILayoutLoader loader;
    Widget* root = loader.loadFromString(R"json({
        "type": "Panel",
        "id": "root",
        "children": [
            {
                "type": "TabControl",
                "id": "tabs",
                "headerHeight": 36,
                "selectedIndex": 1,
                "tabs": [
                    {
                        "label": "General",
                        "content": {
                            "type": "Panel",
                            "id": "page_general",
                            "children": [
                                { "type": "Button", "id": "tab_button",
                                  "text": "Apply" }
                            ]
                        }
                    },
                    {
                        "label": "高级",
                        "content": {
                            "type": "Panel",
                            "id": "page_advanced"
                        }
                    }
                ]
            },
            {
                "type": "TabStrip",
                "id": "strip",
                "tabs": ["One", "Two", "Three"],
                "selectedIndex": 2,
                "tabHeight": 32,
                "spacing": 5,
                "indicatorTweenMs": 175,
                "overflowMode": 1,
                "minTabWidth": 72
            },
            {
                "type": "Modal",
                "id": "modal",
                "dismissOnDimmerClick": false,
                "dimmer": {
                    "scrimColor": { "r": 0.1, "g": 0.2, "b": 0.3, "a": 0.7 }
                },
                "content": {
                    "type": "Panel",
                    "id": "modal_content",
                    "children": [
                        { "type": "TextLabel", "id": "modal_label",
                          "text": "Modal body" }
                    ]
                }
            },
            {
                "type": "ModalDialog",
                "id": "dialog",
                "acceptText": "Confirm",
                "rejectText": "Back",
                "bodyContent": {
                    "type": "Panel",
                    "id": "dialog_body",
                    "children": [
                        { "type": "TextInput", "id": "dialog_input",
                          "text": "value" }
                    ]
                }
            }
        ]
    })json");

    CHECK(root != nullptr);

    auto* tabs = dynamic_cast<TabControl*>(loader.findWidgetById("tabs"));
    CHECK(tabs != nullptr);
    CHECK(tabs != nullptr && tabs->getTabCount() == 2u);
    CHECK(tabs != nullptr && tabs->getTabLabel(1) == L"高级");
    CHECK(tabs != nullptr && tabs->getSelectedIndex() == 1);
    CHECK(tabs != nullptr && tabs->getHeaderHeight() == 36.0f);
    CHECK(tabs != nullptr && tabs->getTabContent(0) ==
        loader.findWidgetById("page_general"));
    CHECK(tabs != nullptr && tabs->getTabContent(1) ==
        loader.findWidgetById("page_advanced"));
    CHECK(loader.findWidgetById("tab_button") != nullptr);
    CHECK(tabs == nullptr || tabs->getTabContent(1) == nullptr ||
        tabs->getTabContent(1)->getParent() == tabs->getBodyPanel());

    auto* strip = dynamic_cast<TabStrip*>(loader.findWidgetById("strip"));
    CHECK(strip != nullptr);
    CHECK(strip != nullptr && strip->getTabCount() == 3);
    CHECK(strip != nullptr && strip->getSelectedIndex() == 2);
    CHECK(strip != nullptr && strip->getTabLabel(2) == L"Three");
    CHECK(strip != nullptr && strip->getOverflowMode() ==
        TabStrip::OverflowMode::Compress);
    CHECK(strip != nullptr && strip->getMinTabWidth() == 72.0f);

    auto* modal = dynamic_cast<Modal*>(loader.findWidgetById("modal"));
    CHECK(modal != nullptr);
    CHECK(modal != nullptr && !modal->isDismissOnDimmerClick());
    CHECK(modal != nullptr && modal->getContent() ==
        loader.findWidgetById("modal_content"));
    CHECK(loader.findWidgetById("modal_label") != nullptr);
    CHECK(modal != nullptr && modal->getDimmer() != nullptr);
    CHECK(modal == nullptr || modal->getDimmer() == nullptr ||
        modal->getDimmer()->getScrimColor().w == 0.7f);

    auto* dialog = dynamic_cast<ModalDialog*>(loader.findWidgetById("dialog"));
    CHECK(dialog != nullptr);
    CHECK(dialog != nullptr && dialog->getAcceptText() == L"Confirm");
    CHECK(dialog != nullptr && dialog->getRejectText() == L"Back");
    CHECK(dialog != nullptr && dialog->getBodyContent() ==
        loader.findWidgetById("dialog_body"));
    CHECK(loader.findWidgetById("dialog_input") != nullptr);

    destroyWidgetTree(root);
}

TEST_CASE(layout_loader_preserves_designer_leaf_and_collection_payloads) {
    UILayoutLoader loader;
    int toggleEvents = 0;
    int textEvents = 0;
    loader.bindControllerEvent("FormController", "toggleOption",
        [&toggleEvents]() { ++toggleEvents; });
    loader.bindControllerEvent("FormController", "editNotes",
        [&textEvents]() { ++textEvents; });
    Widget* root = loader.loadFromString(R"json({
        "type": "Widget",
        "id": "root",
        "children": [
            {
                "type": "RadioButton", "id": "radio", "text": "Option",
                "checked": true, "groupId": 7,
                "controller": "FormController",
                "events": { "onToggled": "toggleOption" }
            },
            {
                "type": "TreeView", "id": "tree", "selectedIndex": 1,
                "itemHeight": 30,
                "tree": [
                    { "label": "Root", "hasChildren": true,
                      "expanded": true, "parentIndex": -1 },
                    { "label": "子项", "hasChildren": false,
                      "expanded": false, "parentIndex": 0 }
                ]
            },
            {
                "type": "RichText", "id": "rich", "defaultFontSize": 16,
                "wrapMode": 1, "alignment": 3, "lineSpacing": 2,
                "runs": [
                    { "text": "Hello ", "fontSize": 16, "bold": true },
                    { "text": "世界", "fontSize": 18, "italic": true }
                ]
            },
            {
                "type": "TextInput", "id": "input", "text": "secret",
                "password": true, "readOnly": true, "maxLength": 12,
                "hAlign": 2
            },
            {
                "type": "TextArea", "id": "area", "text": "line",
                "readOnly": true, "maxLength": 64, "lineHeight": 20,
                "controller": "FormController",
                "events": { "onTextChanged": "editNotes" }
            },
            {
                "type": "Separator", "id": "separator",
                "orientation": "vertical", "thickness": 3, "inset": 4,
                "color": { "r": 0.2, "g": 0.4, "b": 0.6, "a": 1.0 }
            }
        ]
    })json");

    CHECK(root != nullptr);

    auto* radio = dynamic_cast<RadioButton*>(loader.findWidgetById("radio"));
    CHECK(radio != nullptr);
    CHECK(radio != nullptr && radio->getText() == L"Option");
    CHECK(radio != nullptr && radio->isChecked());
    CHECK(radio != nullptr && radio->getGroupId() == 7);
    if (radio != nullptr) radio->setChecked(false);
    CHECK(toggleEvents == 1);

    auto* tree = dynamic_cast<TreeView*>(loader.findWidgetById("tree"));
    CHECK(tree != nullptr);
    CHECK(tree != nullptr && tree->getNodeCount() == 2u);
    CHECK(tree != nullptr && tree->getNodeData(1).label == L"子项");
    CHECK(tree != nullptr && tree->getSelectedIndex() == 1);
    CHECK(tree != nullptr && tree->getItemHeight() == 30.0f);

    auto* rich = dynamic_cast<RichText*>(loader.findWidgetById("rich"));
    CHECK(rich != nullptr);
    CHECK(rich != nullptr && rich->getRunCount() == 2u);
    CHECK(rich != nullptr && rich->getPlainText() == L"Hello 世界");
    CHECK(rich != nullptr && rich->getRun(0).bold);
    CHECK(rich != nullptr && rich->getRun(1).italic);
    CHECK(rich != nullptr && rich->getAlignment() ==
        RichTextAlignment::Justify);

    auto* input = dynamic_cast<TextInput*>(loader.findWidgetById("input"));
    CHECK(input != nullptr);
    CHECK(input != nullptr && input->isPasswordMode());
    CHECK(input != nullptr && input->isReadOnly());
    CHECK(input != nullptr && input->getMaxLength() == 12u);
    CHECK(input != nullptr && input->getHAlign() == TextInput::HAlign::Right);

    auto* area = dynamic_cast<TextArea*>(loader.findWidgetById("area"));
    CHECK(area != nullptr);
    CHECK(area != nullptr && area->isReadOnly());
    CHECK(area != nullptr && area->getMaxLength() == 64u);
    CHECK(area != nullptr && area->getLineHeight() == 20.0f);
    if (area != nullptr) area->setText(L"updated");
    CHECK(textEvents == 1);

    auto* separator = dynamic_cast<Separator*>(
        loader.findWidgetById("separator"));
    CHECK(separator != nullptr);
    CHECK(separator != nullptr && separator->getOrientation() ==
        Separator::Orientation::Vertical);
    CHECK(separator != nullptr && separator->getThickness() == 3.0f);
    CHECK(separator != nullptr && separator->getInset() == 4.0f);

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
