#include "AYTest.h"
#include "AYTooltip.h"
#include "AYButton.h"
#include "AYUIManager.h"
#include "AYWidgetFactory.h"
#include "AYWidgetSerializer.h"
#include <iostream>

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_Tooltip)

// Tooltip attached to a target via Tooltip::attachTo; initially hidden.
TEST_CASE(tooltip_initial_state) {
    Button btn;
    btn.setSize(FVector2(100.0f, 32.0f));
    btn.setPosition(FVector2(50.0f, 50.0f));
    btn.performLayout();

    Tooltip* tip = Tooltip::attachTo(&btn);
    CHECK_NOT_NULL(tip);
    CHECK(tip->getTarget() == &btn);
    CHECK_FALSE(tip->isShowing());
    CHECK_FALSE(tip->isHovered());
}

// Hovering the target for >= hoverDelay makes the tooltip visible.
TEST_CASE(tooltip_appears_on_hover_delay) {
    Button btn;
    btn.setSize(FVector2(100.0f, 32.0f));
    btn.setPosition(FVector2(50.0f, 50.0f));
    btn.performLayout();

    Tooltip* tip = Tooltip::attachTo(&btn);
    tip->setHoverDelay(0.5f);
    tip->setText(L"Hover text");

    const FVector2 mousePos(60.0f, 60.0f);
    const FVector2 viewport(800.0f, 600.0f);

    // Tick at 0.4s (less than delay) -> still hidden
    tip->tick(0.4f, mousePos, viewport);
    CHECK_FALSE(tip->isShowing());

    // Tick again at 0.4s (cumulative >= 0.5s) -> visible
    tip->tick(0.4f, mousePos, viewport);
    CHECK(tip->isShowing());
}

// Leaving the target hides the tooltip (after one tick).
TEST_CASE(tooltip_hides_on_mouse_leave) {
    Button btn;
    btn.setSize(FVector2(100.0f, 32.0f));
    btn.setPosition(FVector2(50.0f, 50.0f));
    btn.performLayout();

    Tooltip* tip = Tooltip::attachTo(&btn);
    tip->setHoverDelay(0.1f);
    tip->setText(L"Hover text");

    tip->tick(0.2f, FVector2(60.0f, 60.0f), FVector2(800.0f, 600.0f));
    CHECK(tip->isShowing());

    // Move outside.
    tip->tick(0.1f, FVector2(900.0f, 900.0f), FVector2(800.0f, 600.0f));
    CHECK_FALSE(tip->isShowing());
}

// Tooltip attaches as owning child of target — destroying target frees tooltip.
TEST_CASE(tooltip_owned_via_destroy_widget_tree) {
    Button* btn = new Button();
    btn->setSize(FVector2(100.0f, 32.0f));
    Tooltip* tip = Tooltip::attachTo(btn);
    CHECK_NOT_NULL(tip);

    // tooltip attached via addChild(owning)
    destroyWidgetTree(btn);
    // tip was a child, so it's gone; no leaks.
}

TEST_CASE(tooltip_factory_and_serializer_round_trip) {
    WidgetFactory& factory = WidgetFactory::get();
    CHECK_TRUE(factory.isRegistered("Tooltip"));

    Widget* widget = factory.create("Tooltip");
    CHECK_NOT_NULL(widget);
    Tooltip* original = dynamic_cast<Tooltip*>(widget);
    CHECK_NOT_NULL(original);
    original->setText(L"Press to save");
    original->setHoverDelay(0.7f);

    std::string json = WidgetSerializer::serialize(widget);
    CHECK(json.find("\"type\": \"Tooltip\"") != std::string::npos);
    CHECK(json.find("Press to save") != std::string::npos);

    Widget* restored = WidgetSerializer::deserialize(json);
    CHECK_NOT_NULL(restored);
    Tooltip* restoredTip = dynamic_cast<Tooltip*>(restored);
    CHECK_NOT_NULL(restoredTip);
    CHECK(restoredTip->getText() == L"Press to save");
    CHECK_FLOAT_EQ(restoredTip->getHoverDelay(), 0.7f, 1e-5f);

    destroyWidgetTree(widget);
    destroyWidgetTree(restored);
}

TEST_SUITE_END

