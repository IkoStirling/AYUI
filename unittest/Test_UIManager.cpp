#include "AYTest.h"
#include "AYUIManager.h"
#include "AYMockRenderer.h"
#include "AYButton.h"
#include "AYWindow.h"

#include <cstdio>
#include <fstream>
#include <thread>
#include <chrono>
#include <filesystem>

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_UIManager)

TEST_CASE(test_uimanager_load_and_render) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    ui.bindEvent("btn_ok", "onClick", []() {});

    const char* json = R"({
        "type": "VBox",
        "id": "root",
        "size": { "w": 320, "h": 240 },
        "children": [
            { "type": "Button", "id": "btn_ok", "text": "OK", "size": { "w": 80, "h": 32 }, "onClick": "ok" }
        ]
    })";

    CHECK(ui.loadFromString(json));
    ui.setClientSize(320.0f, 240.0f);
    ui.layout();
    ui.render();
    CHECK(backend.getDrawCallCount() > 0);
    ui.shutdown();
}

TEST_CASE(test_uimanager_mouse_click) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);

    bool clicked = false;
    ui.bindEvent("btn_ok", "onClick", [&clicked]() { clicked = true; });

    const char* json = R"({
        "type": "Button",
        "id": "btn_ok",
        "text": "OK",
        "position": { "x": 10, "y": 10 },
        "size": { "w": 100, "h": 32 },
        "onClick": "ok"
    })";

    CHECK(ui.loadFromString(json));
    ui.setClientSize(200.0f, 100.0f);
    ui.layout();

    CHECK(ui.onMouseButtonDown(50.0f, 20.0f, 0));
    CHECK(ui.onMouseButtonUp(50.0f, 20.0f, 0));
    CHECK(clicked);
    ui.shutdown();
}

TEST_CASE(test_uimanager_window_drag_with_capture) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);

    const char* json = R"({
        "type": "VBox",
        "id": "root",
        "size": { "w": 640, "h": 480 },
        "children": [
            {
                "type": "Window",
                "id": "panel",
                "text": "Panel",
                "position": { "x": 40, "y": 40 },
                "size": { "w": 120, "h": 100 },
                "minSize": { "w": 80, "h": 60 }
            }
        ]
    })";

    CHECK(ui.loadFromString(json));
    ui.setClientSize(640.0f, 480.0f);
    ui.layout();

    Window* panel = dynamic_cast<Window*>(ui.findById("panel"));
    CHECK(panel != nullptr);
    CHECK(ui.onMouseButtonDown(80.0f, 50.0f, 0));
    CHECK(panel->isDragging());
    ui.onMouseLeave();
    CHECK(ui.onMouseMove(180.0f, 120.0f));
    CHECK(panel->getPosition().x > 40.0f);
    CHECK(ui.onMouseButtonUp(180.0f, 120.0f, 0));
    CHECK(!panel->isDragging());
    ui.shutdown();
}

TEST_CASE(test_uimanager_cancel_capture) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);

    const char* json = R"({
        "type": "Window",
        "id": "panel",
        "text": "Panel",
        "position": { "x": 40, "y": 40 },
        "size": { "w": 220, "h": 160 },
        "minSize": { "w": 120, "h": 80 }
    })";

    CHECK(ui.loadFromString(json));
    // Root is the panel itself, so setClientSize would resize it to the
    // viewport. Skip that step to preserve the JSON-declared 220x160 size.

    Window* panel = dynamic_cast<Window*>(ui.findById("panel"));
    CHECK(panel != nullptr);

    CHECK(ui.onMouseButtonDown(80.0f, 50.0f, 0));
    CHECK(ui.isCapturing());
    CHECK(panel->isDragging());

    ui.cancelCapture();
    CHECK(!ui.isCapturing());
    CHECK(!panel->isDragging());
    ui.shutdown();
}

// R-7: a hot reload triggered while the UIManager has a captured widget
// (mid-drag on a Window) must release the capture BEFORE destroying the
// tree. The captured widget pointer would otherwise dangle after the
// tree swap, and a subsequent onMouseButtonUp dereferences freed memory.
//
// Setup: write a layout JSON with a draggable Window to a temp file,
// load it, capture the window via onMouseButtonDown, edit the file,
// then call update() — which triggers the watcher's tryReload path.
// After update, isCapturing() must be false and a subsequent
// onMouseButtonUp must not crash.
TEST_CASE(test_uimanager_reload_during_capture) {
    namespace fs = std::filesystem;

    fs::path tmpDir = fs::temp_directory_path() / "ayui_r7_reload_capture";
    std::error_code ec;
    fs::remove_all(tmpDir, ec);
    fs::create_directories(tmpDir);
    fs::path jsonPath = tmpDir / "panel.json";

    {
        std::ofstream out(jsonPath, std::ios::binary | std::ios::trunc);
        out << R"({
            "type": "Window",
            "id": "panel",
            "text": "v1",
            "position": { "x": 40, "y": 40 },
            "size": { "w": 220, "h": 160 },
            "minSize": { "w": 120, "h": 80 }
        })";
    }

    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    CHECK(ui.loadLayout(jsonPath.string()));

    Window* panel = dynamic_cast<Window*>(ui.findById("panel"));
    CHECK(panel != nullptr);

    // Begin a drag — this sets _capturedWidget = panel inside UIManager.
    CHECK(ui.onMouseButtonDown(80.0f, 50.0f, 0));
    CHECK(ui.isCapturing());
    CHECK(panel->isDragging());

    // Edit the file to trigger a reload on the next update().
    {
        std::ofstream out(jsonPath, std::ios::binary | std::ios::trunc);
        out << R"({
            "type": "Window",
            "id": "panel",
            "text": "v2",
            "position": { "x": 40, "y": 40 },
            "size": { "w": 220, "h": 160 },
            "minSize": { "w": 120, "h": 80 }
        })";
    }

    // Allow the watcher thread to enqueue the change event.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // R-7 fix path: update() detects a pending reload, calls
    // cancelCapture() (synthesizing a mouse-up so the old Window's
    // _isDragging clears), clears _hoverWidget, then destroys the tree.
    ui.update(0.0f);

    // After reload the capture must be gone. The previous _capturedWidget
    // pointer is now dangling; reading it would be UB.
    CHECK(!ui.isCapturing());

    // Subsequent input handling must not crash on the dangling pointer.
    // If cancelCapture wasn't called, this onMouseButtonUp would
    // dereference the freed Window — R-7 prevents that.
    ui.onMouseButtonUp(80.0f, 50.0f, 0);

    ui.shutdown();
    fs::remove_all(tmpDir, ec);
}

TEST_SUITE_END