#include "AYTest.h"
#include "AYUI/WidgetSerializer.h"
#include "AYUI/TextLabel.h"
#include "AYUI/Button.h"
#include "AYUI/Window.h"
#include "AYUI/Box.h"
#include "AYUI/ScrollView.h"
#include <iostream>

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_Serializer)

TEST_CASE(test_serialize_basic_widget) {
    Widget* widget = new Widget();
    widget->setId("test_widget");
    widget->setPosition(FVector2(100.0f, 200.0f));
    widget->setSize(FVector2(300.0f, 150.0f));

    std::string json = WidgetSerializer::serialize(widget);
    CHECK(!json.empty());
    CHECK(json.find("\"id\"") != std::string::npos);
    CHECK(json.find("test_widget") != std::string::npos);

    delete widget;
}

// R-8 (B11 regression): a raw Widget with no specific subclass must export
// `type == "Widget"` even when it has a non-empty styleId. The previous
// implementation pre-wrote `j["type"] = widget->getStyleId()` as a
// placeholder, which would leak the styleId as the type field for any
// subclass branch that didn't fire — the round-trip would then fail to
// find a registered factory and fall back to `new Widget()`, losing type
// information silently.
TEST_CASE(test_serialize_basic_widget_type_default) {
    Widget* widget = new Widget();
    widget->setId("typed_widget");
    widget->setStyleId("custom_visual");  // non-empty to expose the B11 bug

    std::string json = WidgetSerializer::serialize(widget);
    // nlohmann::json::dump() emits `"key": "value"` (with a space after
    // the colon). Match that exactly.
    CHECK(json.find("\"type\": \"Widget\"") != std::string::npos);
    CHECK(json.find("\"type\": \"custom_visual\"") == std::string::npos);

    delete widget;
}

TEST_CASE(test_serialize_text_label) {
    TextLabel* label = new TextLabel();
    label->setId("my_label");
    label->setText(L"Hello World");
    label->setFontSize(24);
    label->setPosition(FVector2(10.0f, 10.0f));
    label->setSize(FVector2(200.0f, 50.0f));

    std::string json = WidgetSerializer::serialize(label);
    CHECK(json.find("TextLabel") != std::string::npos);
    CHECK(json.find("Hello World") != std::string::npos);
    CHECK(json.find("fontSize") != std::string::npos);

    delete label;
}

TEST_CASE(test_serialize_button) {
    Button* button = new Button();
    button->setId("my_button");
    button->setText(L"Click Me");
    button->setPosition(FVector2(50.0f, 50.0f));
    button->setSize(FVector2(100.0f, 32.0f));

    std::string json = WidgetSerializer::serialize(button);
    CHECK(json.find("Button") != std::string::npos);
    CHECK(json.find("Click Me") != std::string::npos);

    delete button;
}

TEST_CASE(test_serialize_window) {
    Window* window = new Window();
    window->setId("my_window");
    window->setTitle(L"My Window Title");
    window->setTitleBarHeight(32.0f);
    window->setPosition(FVector2(100.0f, 100.0f));
    window->setSize(FVector2(400.0f, 300.0f));

    std::string json = WidgetSerializer::serialize(window);
    CHECK(json.find("Window") != std::string::npos);
    CHECK(json.find("My Window Title") != std::string::npos);
    CHECK(json.find("titleBarHeight") != std::string::npos);

    delete window;
}

TEST_CASE(test_serialize_vbox_with_children) {
    VBox* vbox = new VBox();
    vbox->setId("container");
    vbox->setSpacing(8.0f);
    vbox->setSize(FVector2(200.0f, 300.0f));

    TextLabel* label = new TextLabel();
    label->setId("label1");
    label->setText(L"First");
    vbox->addChild(label);

    Button* button = new Button();
    button->setId("btn1");
    button->setText(L"Second");
    vbox->addChild(button);

    std::string json = WidgetSerializer::serialize(vbox);
    CHECK(json.find("VBox") != std::string::npos);
    CHECK(json.find("spacing") != std::string::npos);
    CHECK(json.find("label1") != std::string::npos);
    CHECK(json.find("btn1") != std::string::npos);

    destroyWidgetTree(vbox);
}

TEST_CASE(test_deserialize_basic_widget) {
    const char* json = R"({
        "type": "Widget",
        "id": "test",
        "position": { "x": 10, "y": 20 },
        "size": { "w": 100, "h": 50 }
    })";

    Widget* widget = WidgetSerializer::deserialize(json);
    CHECK(widget != nullptr);
    CHECK(widget->getId() == "test");

    FVector2 pos = widget->getPosition();
    CHECK_FLOAT_EQ(pos.x, 10.0f, 1e-5f);
    CHECK_FLOAT_EQ(pos.y, 20.0f, 1e-5f);

    FVector2 size = widget->getSize();
    CHECK_FLOAT_EQ(size.x, 100.0f, 1e-5f);
    CHECK_FLOAT_EQ(size.y, 50.0f, 1e-5f);

    delete widget;
}

TEST_CASE(test_deserialize_text_label) {
    const char* json = R"({
        "type": "TextLabel",
        "id": "label1",
        "text": "Hello",
        "fontSize": 18
    })";

    Widget* widget = WidgetSerializer::deserialize(json);
    CHECK(widget != nullptr);

    TextLabel* label = dynamic_cast<TextLabel*>(widget);
    CHECK(label != nullptr);
    CHECK(label->getText() == L"Hello");
    CHECK(label->getFontSize() == 18);

    delete widget;
}

TEST_CASE(test_deserialize_with_children) {
    const char* json = R"({
        "type": "VBox",
        "id": "root",
        "spacing": 10,
        "children": [
            { "type": "Button", "id": "btn1", "text": "OK" },
            { "type": "Button", "id": "btn2", "text": "Cancel" }
        ]
    })";

    Widget* root = WidgetSerializer::deserialize(json);
    CHECK(root != nullptr);
    CHECK(root->getId() == "root");
    CHECK(root->getChildren().size() == 2);

    Widget* btn1 = root->getChildren()[0];
    CHECK(btn1->getId() == "btn1");

    destroyWidgetTree(root);
}

TEST_CASE(test_roundtrip) {
    Window* window = new Window();
    window->setId("test_window");
    window->setTitle(L"Original Title");
    window->setPosition(FVector2(100.0f, 100.0f));
    window->setSize(FVector2(400.0f, 300.0f));

    std::string json = WidgetSerializer::serialize(window);

    Widget* restored = WidgetSerializer::deserialize(json);
    CHECK(restored != nullptr);
    CHECK(restored->getId() == "test_window");

    Window* restoredWindow = dynamic_cast<Window*>(restored);
    CHECK(restoredWindow != nullptr);
    CHECK(restoredWindow->getTitle() == L"Original Title");

    destroyWidgetTree(window);
    destroyWidgetTree(restored);
}

TEST_CASE(test_scrollview_visibility_policy_roundtrip) {
    ScrollView scroll;
    scroll.setVerticalScrollBarVisibility(
        ScrollView::ScrollBarVisibility::Always);
    scroll.setHorizontalScrollBarVisibility(
        ScrollView::ScrollBarVisibility::Auto);

    const std::string json = WidgetSerializer::serialize(&scroll);
    CHECK(json.find("verticalScrollBarVisibility") != std::string::npos);
    CHECK(json.find("horizontalScrollBarVisibility") != std::string::npos);

    Widget* restored = WidgetSerializer::deserialize(json);
    ScrollView* restoredScroll = dynamic_cast<ScrollView*>(restored);
    CHECK_NOT_NULL(restoredScroll);
    if (restoredScroll != nullptr) {
        CHECK(restoredScroll->getVerticalScrollBarVisibility()
              == ScrollView::ScrollBarVisibility::Always);
        CHECK(restoredScroll->getHorizontalScrollBarVisibility()
              == ScrollView::ScrollBarVisibility::Auto);
    }
    destroyWidgetTree(restored);
}

TEST_CASE(test_unicode_text_roundtrip_uses_utf8) {
    Button button;
    button.setText(L"\u7EE7\u7EED");

    const std::string json = WidgetSerializer::serialize(&button);
    Widget* restored = WidgetSerializer::deserialize(json);
    Button* restoredButton = dynamic_cast<Button*>(restored);

    CHECK(restoredButton != nullptr);
    if (restoredButton != nullptr) {
        CHECK(restoredButton->getText() == L"\u7EE7\u7EED");
    }
    destroyWidgetTree(restored);
}

TEST_SUITE_END
