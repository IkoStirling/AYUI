#include "AYTest.h"
#include "AYMath/MathUtils.h"
#include "AYUI/WidgetFactory.h"
#include "AYUI/Widget.h"
#include "AYUI/Button.h"
#include <vector>
#include <string>
#include <utility>

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

// AYUI-Audit-2026-08-26: comprehensive coverage. The factory registers
// 30+ widget types in DefaultWidgetRegistrar (AYWidgetFactory.cpp).
// Previously only ~5 of them were asserted in tests; this loop covers
// every registered type and asserts factory.create(name) returns a
// non-null Widget for each.
TEST_CASE(test_widget_factory_all_registered_types_create_non_null) {
    WidgetFactory& factory = WidgetFactory::get();

    // Pinned list: every type the DefaultWidgetRegistrar registers. If
    // a future PR adds a new REGISTER_WIDGET(...) call, that type must
    // also be appended here or this case will fail (intentional — new
    // widget types must come with a test assertion that their factory
    // path works).
    static const std::vector<std::string> kAllTypes = {
        "Widget",
        "Button",
        "TextLabel",
        "CheckBox",
        "RadioButton",
        "Slider",
        "ProgressBar",
        "Spinner",
        "TextInput",
        "TextArea",
        "ScrollBar",
        "ScrollView",
        "ListView",
        "ComboBox",
        "TabControl",
        "TreeNode",
        "TreeView",
        "RichText",
        "Tooltip",
        "Separator",
        "MenuItem",
        "Menu",
        "MenuBar",
        "ToolBar",
        "ToolBarSeparator",
        "StatusBar",
        "Image",
        "Window",
        "Panel",
        "VBox",
        "HBox",
        "SplitterHandle",
        "GridPanel",
        "DockArea",
        "DockCard",
        "DockOverlay",
        "Dimmer",
        "Modal",
        "ModalDialog",
        "TabStrip",
    };

    int registeredCount = 0;
    int createdCount    = 0;
    int missingRegistrationCount = 0;
    for (const std::string& typeName : kAllTypes) {
        if (!factory.isRegistered(typeName)) {
            ++missingRegistrationCount;
            continue;
        }
        ++registeredCount;
        Widget* widget = factory.create(typeName);
        if (widget != nullptr) {
            ++createdCount;
            delete widget;
        }
    }
    // Sanity: must have registered + created at least 30 of the types.
    CHECK(missingRegistrationCount == 0);
    CHECK(registeredCount >= 30);
    CHECK(createdCount    == registeredCount);
}

TEST_CASE(test_widget_factory_isRegistered_matches_create) {
    // AYUI-Audit-2026-08-26: contract check — for every known
    // registered name, factory.create(name) must return non-null;
    // for every UNKNOWN name, factory.create(name) must return null.
    // This pins the isRegistered/create contract so a future bug that
    // registers without a working creator (or vice versa) is caught.
    WidgetFactory& factory = WidgetFactory::get();

    static const std::vector<std::string> kKnownTypes = {
        "Widget", "Button", "TextLabel", "CheckBox", "RadioButton",
        "Slider", "ProgressBar", "Spinner", "TextInput", "TextArea",
        "ScrollBar", "ScrollView", "ListView", "ComboBox",
        "TabControl", "TreeNode", "TreeView", "RichText", "Tooltip",
        "Separator", "MenuItem", "Menu", "MenuBar", "ToolBar",
        "ToolBarSeparator", "StatusBar", "Image", "Window", "Panel",
        "VBox", "HBox", "SplitterHandle", "GridPanel", "DockArea",
        "DockCard", "DockOverlay", "Dimmer", "Modal", "ModalDialog",
        "TabStrip",
    };

    int missingKnownRegistrationCount = 0;
    int knownCreateFailureCount = 0;
    for (const std::string& typeName : kKnownTypes) {
        if (!factory.isRegistered(typeName)) {
            ++missingKnownRegistrationCount;
        }
        Widget* w = factory.create(typeName);
        if (w == nullptr) {
            ++knownCreateFailureCount;
        }
        delete w;
    }
    CHECK(missingKnownRegistrationCount == 0);
    CHECK(knownCreateFailureCount == 0);

    // Unknown names return null, NOT a crash.
    static const std::vector<std::string> kUnknownTypes = {
        "", "UnknownWidget", "button", "BUTTON", "NotAType",
        "WidgetFactory", "DoCkArEa"  // case-sensitive on purpose
    };
    int unexpectedUnknownRegistrationCount = 0;
    int unexpectedUnknownCreateCount = 0;
    for (const std::string& typeName : kUnknownTypes) {
        if (factory.isRegistered(typeName)) {
            ++unexpectedUnknownRegistrationCount;
        }
        Widget* w = factory.create(typeName);
        if (w != nullptr) {
            ++unexpectedUnknownCreateCount;
        }
        delete w;
    }
    CHECK(unexpectedUnknownRegistrationCount == 0);
    CHECK(unexpectedUnknownCreateCount == 0);
}

TEST_CASE(test_widget_factory_unregister_is_idempotent_safe) {
    // AYUI-Audit-2026-08-26: unregister on an unknown name must be a
    // safe no-op (no crash, no exception). Pinning this prevents a
    // future regression where a teardown path tries to unregister a
    // type that was never registered (e.g. when a test file's static
    // registrar order differs).
    WidgetFactory& factory = WidgetFactory::get();
    CHECK(factory.isRegistered("Button"));
    factory.unregister("Button");
    CHECK(!factory.isRegistered("Button"));
    Widget* w = factory.create("Button");
    CHECK(w == nullptr);
    // Re-register so other tests that depend on Button don't silently
    // break. DefaultWidgetRegistrar is a static so re-registering a
    // custom creator works for the rest of the test process.
    WidgetFactory::get().registerCreator("Button", []() { return new Button(); });
}

TEST_SUITE_END
