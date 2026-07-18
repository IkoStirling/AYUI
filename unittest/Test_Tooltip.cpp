#include "AYTest.h"
#include "AYTooltip.h"
#include "AYButton.h"
#include "AYUIManager.h"
#include "AYWidgetFactory.h"
#include "AYWidgetSerializer.h"
#include "AYMockRenderer.h"
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

// Phase A (A2): the tooltip lives on UIManager's overlay root, NOT as a
// child of the target. Tooltip lifetime is owned by the overlay. After
// `Tooltip::attachTo`, calling `UIManager::shutdown()` (or closePopup on
// the tooltip) frees the tooltip — the target can be torn down
// independently without UAF.
TEST_CASE(tooltip_owned_via_destroy_widget_tree) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);

    Button* btn = new Button();
    btn->setSize(FVector2(100.0f, 32.0f));

    Tooltip* tip = Tooltip::attachTo(btn);
    CHECK_NOT_NULL(tip);

    // The tooltip should be a child of the overlay, not of `btn`.
    bool foundOnOverlay = false;
    for (Widget* w : ui.getOverlayRoot()->getChildren()) {
        if (w == tip) { foundOnOverlay = true; break; }
    }
    CHECK(foundOnOverlay);
    CHECK(btn->getChildren().empty());

    // Tear down `btn` — overlay still owns the tip, no UAF on the target.
    destroyWidgetTree(btn);

    // UIManager shutdown destroys overlay children (the tip).
    ui.shutdown();
    // If we get here without a crash, the tooltip was freed cleanly.
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

// Phase A (A2): Tooltip::attachTo mounts the tip on the overlay, not as a
// child of the target.
TEST_CASE(tooltip_attach_to_mounts_on_overlay) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);

    Button btn;
    btn.setSize(FVector2(100.0f, 32.0f));
    btn.setPosition(FVector2(50.0f, 50.0f));
    btn.performLayout();

    Tooltip* tip = Tooltip::attachTo(&btn);
    CHECK_NOT_NULL(tip);

    bool foundOnOverlay = false;
    for (Widget* w : ui.getOverlayRoot()->getChildren()) {
        if (w == tip) { foundOnOverlay = true; break; }
    }
    CHECK(foundOnOverlay);
    CHECK(btn.getChildren().empty());

    ui.shutdown();
}

// Phase A (A2): Tooltip::tick viewport fallback. If the caller passes
// (0,0) the tip pulls live metrics from UIManager::getClientSize().
// This is the production case — hosts that wire Tooltip::tick to a
// per-frame delta shouldn't need to know the viewport size.
TEST_CASE(tooltip_tick_viewport_fallback_to_uimanager) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);
    ui.setClientSize(800.0f, 100.0f);   // tiny height to force flip

    Button btn;
    btn.setSize(FVector2(100.0f, 32.0f));
    btn.setPosition(FVector2(50.0f, 60.0f));   // near bottom
    btn.performLayout();

    Tooltip* tip = Tooltip::attachTo(&btn);
    tip->setHoverDelay(0.1f);
    tip->setText(L"Fallback test");

    // Pass viewport=(0,0) → tick should pull from UIManager (800x100).
    // Anchor is at y ∈ [60, 92); default-below would put tip at y=98,
    // which overflows the 100-tall viewport — flip kicks in.
    tip->tick(0.2f, FVector2(80.0f, 70.0f), FVector2(0.0f, 0.0f));
    CHECK(tip->isShowing());

    const FRectangle tb = tip->getWorldBounds();
    // After flip, tip should be ABOVE the anchor (anchor minY=60).
    CHECK(tb.maxY <= 60.0f);

    ui.shutdown();
}

TEST_SUITE_END

