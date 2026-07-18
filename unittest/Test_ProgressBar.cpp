#include "AYTest.h"
#include "AYProgressBar.h"
#include "AYWidgetFactory.h"
#include "AYWidgetSerializer.h"
#include "AYMockRenderer.h"
#include "AYStyle.h"

#include <iostream>

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_ProgressBar)

// C-7: ProgressBar default range is [0, 1] with value 0.5.
TEST_CASE(progressbar_initial_state) {
    ProgressBar p;
    CHECK_FLOAT_EQ(p.getMin(), 0.0f, 1e-5f);
    CHECK_FLOAT_EQ(p.getMax(), 1.0f, 1e-5f);
    CHECK_FLOAT_EQ(p.getValue(), 0.5f, 1e-5f);
    CHECK_FLOAT_EQ(p.getNormalized(), 0.5f, 1e-5f);
}

// C-7: setValue clamps into [_min, _max].
TEST_CASE(progressbar_set_value_clamps) {
    ProgressBar p;
    p.setValueRange(0.0f, 100.0f);

    p.setValue(50.0f);
    CHECK_FLOAT_EQ(p.getValue(), 50.0f, 1e-5f);

    p.setValue(500.0f);
    CHECK_FLOAT_EQ(p.getValue(), 100.0f, 1e-5f);

    p.setValue(-50.0f);
    CHECK_FLOAT_EQ(p.getValue(), 0.0f, 1e-5f);
}

// C-7: setValue is idempotent — no callback fire on no-op.
TEST_CASE(progressbar_set_value_idempotent) {
    ProgressBar p;
    int count = 0;
    p.setOnValueChanged([&](float) { ++count; });

    p.setValue(p.getValue());  // no-op
    CHECK(count == 0);

    p.setValue(0.25f);
    CHECK(count == 1);

    p.setValue(0.25f);
    CHECK(count == 1);

    p.setValue(0.75f);
    CHECK(count == 2);
}

// C-7: ProgressBar extends LeafWidget — its default hitTest does NOT
// descend into children (R-6 invariant). Its mouse handlers are no-op
// inherited from LeafWidget (which inherits Widget default). It is
// genuinely non-interactive.
TEST_CASE(progressbar_is_leaf_widget) {
    ProgressBar p;
    p.onMouseMove(UIMouseEvent(FVector2(50.0f, 8.0f), 0));
    p.onMouseButtonDown(UIMouseEvent(FVector2(50.0f, 8.0f), 0));
    p.onMouseButtonUp(UIMouseEvent(FVector2(50.0f, 8.0f), 0));
    CHECK_FLOAT_EQ(p.getValue(), 0.5f, 1e-5f);   // unchanged
    CHECK(p.isVisible());   // default visible

    // Cursor hint is the default (Default), not SizeHorizontal.
    CHECK(p.getCursorHint() == UiCursorHint::Default);
}

// C-7: getBarRect anchors at vertical center; getFilledRect grows from
// minX to track * normalized.
TEST_CASE(progressbar_bar_and_filled_layout) {
    ProgressBar p;
    p.setSize(FVector2(200.0f, 16.0f));
    p.setPosition(FVector2(10.0f, 5.0f));
    p.setValueRange(0.0f, 100.0f);
    p.setValue(50.0f);   // 50%

    FRectangle bar = p.getBarRect();
    CHECK_FLOAT_EQ(bar.minX, 10.0f, 1e-4f);
    CHECK_FLOAT_EQ(bar.maxX, 210.0f, 1e-4f);
    // ProgressBar world y center = (5 + 21) / 2 = 13, minus kBarHeight/2
    // = 3 → bar.minY = 10, bar.maxY = 16.
    CHECK_FLOAT_EQ(bar.minY, 10.0f, 1e-4f);
    CHECK_FLOAT_EQ(bar.maxY, 16.0f, 1e-4f);

    FRectangle filled = p.getFilledRect();
    CHECK_FLOAT_EQ(filled.minX, 10.0f, 1e-4f);
    CHECK_FLOAT_EQ(filled.maxX, 110.0f, 1e-4f);  // 50% of 200
}

// C-7: value-zero edge — filled rect collapses to a single x and the
// render path skips the filled draw (>= 1px required).
TEST_CASE(progressbar_zero_value_no_filled_fill) {
    ProgressBar p;
    p.setSize(FVector2(200.0f, 16.0f));
    p.setPosition(FVector2(0.0f, 0.0f));
    p.setValue(0.0f);

    FRectangle filled = p.getFilledRect();
    CHECK_FLOAT_EQ(filled.maxX, filled.minX, 1e-5f);
}

// C-7: full value — filled rect covers the entire track width.
TEST_CASE(progressbar_full_value_full_filled) {
    ProgressBar p;
    p.setSize(FVector2(200.0f, 16.0f));
    p.setPosition(FVector2(0.0f, 0.0f));
    p.setValue(1.0f);

    FRectangle filled = p.getFilledRect();
    CHECK_FLOAT_EQ(filled.minX, 0.0f, 1e-5f);
    CHECK_FLOAT_EQ(filled.maxX, 200.0f, 1e-5f);
}

// C-7: render emits the accent fill (0.18, 0.45, 0.78).
TEST_CASE(progressbar_render_emits_accent_fill) {
    ProgressBar p;
    p.setSize(FVector2(200.0f, 16.0f));
    p.setPosition(FVector2(0.0f, 0.0f));
    p.setValue(0.5f);

    MockRenderer renderer;
    p.render(renderer);

    bool foundAccent = false;
    for (const auto& dc : renderer.getDrawCalls()) {
        if (dc.type != MockRenderer::DrawCall::Rect) continue;
        if (fabsf(dc.color.x - 0.18f) < 1e-4f &&
            fabsf(dc.color.y - 0.45f) < 1e-4f &&
            fabsf(dc.color.z - 0.78f) < 1e-4f) {
            foundAccent = true;
            break;
        }
    }
    CHECK(foundAccent);
}

// C-7: factory + serializer round-trip.
TEST_CASE(progressbar_factory_and_serializer_round_trip) {
    WidgetFactory& factory = WidgetFactory::get();
    CHECK_TRUE(factory.isRegistered("ProgressBar"));

    Widget* widget = factory.create("ProgressBar");
    CHECK_NOT_NULL(widget);
    ProgressBar* original = dynamic_cast<ProgressBar*>(widget);
    CHECK_NOT_NULL(original);

    original->setId("loading");
    original->setValueRange(0.0f, 100.0f);
    original->setValue(73.5f);

    std::string json = WidgetSerializer::serialize(widget);
    CHECK(json.find("\"type\": \"ProgressBar\"") != std::string::npos);
    CHECK(json.find("73.5") != std::string::npos);

    Widget* restored = WidgetSerializer::deserialize(json);
    CHECK_NOT_NULL(restored);
    ProgressBar* restoredPb = dynamic_cast<ProgressBar*>(restored);
    CHECK_NOT_NULL(restoredPb);
    CHECK(restoredPb->getId() == "loading");
    CHECK_FLOAT_EQ(restoredPb->getMin(), 0.0f, 1e-3f);
    CHECK_FLOAT_EQ(restoredPb->getMax(), 100.0f, 1e-3f);
    CHECK_FLOAT_EQ(restoredPb->getValue(), 73.5f, 1e-3f);

    destroyWidgetTree(widget);
    destroyWidgetTree(restored);
}

TEST_SUITE_END
