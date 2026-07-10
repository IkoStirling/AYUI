#include "AYTest.h"
#include "AYUIManager.h"
#include "AYMockRenderer.h"
#include "AYButton.h"
#include "AYWindow.h"

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

TEST_SUITE_END