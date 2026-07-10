#include "AYTest.h"
#include "AYMathUtils.h"
#include "AYLayoutLoader.h"
#include "AYButton.h"
#include "AYTextLabel.h"
#include "AYWindow.h"
#include "AYBox.h"
#include "AYWidget.h"
#include <iostream>

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

    delete root;
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

    delete widget;
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

    delete widget;
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

    delete root;
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

    delete root;
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