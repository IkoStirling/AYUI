#include "AYTest.h"
#include "aymath/MathUtils.h"
#include "AYLayoutLoader.h"
#include "AYButton.h"
#include "AYTextLabel.h"
#include "AYWindow.h"
#include "AYBox.h"
#include "AYWidget.h"
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

TEST_SUITE_END