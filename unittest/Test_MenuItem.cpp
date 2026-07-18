#include "AYTest.h"
#include "AYMenuItem.h"
#include "AYMockRenderer.h"
#include "AYWidgetFactory.h"
#include "AYWidgetSerializer.h"
#include <iostream>

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_MenuItem)

TEST_CASE(menuitem_initial_state) {
    MenuItem item;
    CHECK(item.getText() == L"");
    CHECK(item.getShortcut() == L"");
    CHECK_FALSE(item.hasSubmenu());
}

TEST_CASE(menuitem_set_text_and_shortcut) {
    MenuItem item;
    item.setText(L"Save");
    item.setShortcut(L"Ctrl+S");
    CHECK(item.getText() == L"Save");
    CHECK(item.getShortcut() == L"Ctrl+S");
}

TEST_CASE(menuitem_activate_does_not_toggle_selection) {
    // DECISION: MenuItem fires activate, doesn't toggle _selected.
    MenuItem item;
    item.setText(L"Save");
    int actCount = 0;
    item.setOnActivate([&]() { ++actCount; });

    item.handleClick();
    CHECK(actCount == 1);
    CHECK_FALSE(item.isSelected());
    item.handleClick();
    CHECK(actCount == 2);
    CHECK_FALSE(item.isSelected());
}

TEST_CASE(menuitem_render_emits_text) {
    MenuItem item;
    item.setText(L"Open File");
    item.setSize(FVector2(200.0f, 24.0f));
    item.setPosition(FVector2(0.0f, 0.0f));
    item.performLayout();

    MockRenderer renderer;
    item.render(renderer);

    // Item is not hovered, so no Rect highlight; text is emitted as
    // drawText call.
    bool sawText = false;
    for (const auto& dc : renderer.getDrawCalls()) {
        if (dc.type == MockRenderer::DrawCall::Text) { sawText = true; break; }
    }
    CHECK(sawText);
}

TEST_CASE(menuitem_factory_and_serializer_round_trip) {
    WidgetFactory& factory = WidgetFactory::get();
    CHECK_TRUE(factory.isRegistered("MenuItem"));

    Widget* widget = factory.create("MenuItem");
    CHECK_NOT_NULL(widget);
    MenuItem* original = dynamic_cast<MenuItem*>(widget);
    CHECK_NOT_NULL(original);
    original->setId("mi_save");
    original->setText(L"Save");
    original->setShortcut(L"Ctrl+S");

    std::string json = WidgetSerializer::serialize(widget);
    CHECK(json.find("\"type\": \"MenuItem\"") != std::string::npos);
    CHECK(json.find("Save") != std::string::npos);
    CHECK(json.find("Ctrl+S") != std::string::npos);

    Widget* restored = WidgetSerializer::deserialize(json);
    CHECK_NOT_NULL(restored);
    MenuItem* restoredMi = dynamic_cast<MenuItem*>(restored);
    CHECK_NOT_NULL(restoredMi);
    CHECK(restoredMi->getId() == "mi_save");
    CHECK(restoredMi->getText() == L"Save");
    CHECK(restoredMi->getShortcut() == L"Ctrl+S");
    CHECK_FALSE(restoredMi->hasSubmenu());

    destroyWidgetTree(widget);
    destroyWidgetTree(restored);
}

TEST_SUITE_END

