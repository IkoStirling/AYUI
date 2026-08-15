#include "AYTest.h"
#include "AYMath/MathUtils.h"
#include "AYUI/WidgetFactory.h"
#include "AYUI/Widget.h"
#include "AYUI/Button.h"
#include <iostream>

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_WidgetFactory)

TEST_CASE(test_widget_factory_basic) {
    WidgetFactory& factory = WidgetFactory::get();

    CHECK(factory.isRegistered("Widget"));
    CHECK(factory.isRegistered("Button"));
    CHECK(factory.isRegistered("TextLabel"));
    CHECK(factory.isRegistered("Image"));
    CHECK(factory.isRegistered("Window"));
    CHECK(factory.isRegistered("VBox"));
    CHECK(factory.isRegistered("HBox"));

    Widget* widget = factory.create("Widget");
    CHECK(widget != nullptr);
    delete widget;

    Button* button = dynamic_cast<Button*>(factory.create("Button"));
    CHECK(button != nullptr);
    delete button;

    Widget* unknown = factory.create("UnknownType");
    CHECK(unknown == nullptr);
}

TEST_CASE(test_widget_factory_custom) {
    WidgetFactory& factory = WidgetFactory::get();

    factory.registerCreator("CustomWidget", []() { return new Widget(); });

    CHECK(factory.isRegistered("CustomWidget"));

    Widget* custom = factory.create("CustomWidget");
    CHECK(custom != nullptr);
    delete custom;

    factory.unregister("CustomWidget");
    CHECK(!factory.isRegistered("CustomWidget"));
}

TEST_CASE(test_widget_factory_register_macro) {
    WidgetFactory& factory = WidgetFactory::get();

    CHECK(factory.isRegistered("Button"));
    CHECK(factory.isRegistered("VBox"));
    CHECK(factory.isRegistered("HBox"));
}

TEST_SUITE_END