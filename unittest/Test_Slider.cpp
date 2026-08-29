#include "AYTest.h"
#include "AYUI/Slider.h"
#include "AYUI/WidgetFactory.h"
#include "AYUI/WidgetSerializer.h"
#include "AYUI/MockRenderer.h"
#include "AYUI/Style.h"

#include <iostream>

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_Slider)

// C-7: Slider default range is [0, 1] with value 0.5 (center).
TEST_CASE(slider_initial_state) {
    Slider s;
    CHECK_FLOAT_EQ(s.getMin(), 0.0f, 1e-5f);
    CHECK_FLOAT_EQ(s.getMax(), 1.0f, 1e-5f);
    CHECK_FLOAT_EQ(s.getValue(), 0.5f, 1e-5f);
    CHECK_FLOAT_EQ(s.getNormalized(), 0.5f, 1e-5f);
    CHECK_FALSE(s.isDragging());
}

// C-7: setValue clamps into [_min, _max].
TEST_CASE(slider_set_value_clamps) {
    Slider s;
    s.setValueRange(-10.0f, 10.0f);

    s.setValue(5.0f);
    CHECK_FLOAT_EQ(s.getValue(), 5.0f, 1e-5f);

    s.setValue(100.0f);
    CHECK_FLOAT_EQ(s.getValue(), 10.0f, 1e-5f);

    s.setValue(-100.0f);
    CHECK_FLOAT_EQ(s.getValue(), -10.0f, 1e-5f);
}

// C-7: setValue is idempotent — no callback fired when value hasn't
// actually changed.
TEST_CASE(slider_set_value_idempotent) {
    Slider s;
    int count = 0;
    s.setOnValueChanged([&](float) { ++count; });

    s.setValue(s.getValue());   // no-op
    CHECK(count == 0);

    s.setValue(0.75f);
    CHECK(count == 1);

    s.setValue(0.75f);
    CHECK(count == 1);

    s.setValue(0.50f);
    CHECK(count == 2);
}

// C-7: setValueRange preserves _value if it's still in the new range,
// otherwise resets to the new min. Also normalizes max >= min + epsilon.
TEST_CASE(slider_set_value_range_resets_when_needed) {
    Slider s;
    s.setValue(0.7f);

    s.setValueRange(0.0f, 1.0f);
    CHECK_FLOAT_EQ(s.getValue(), 0.7f, 1e-5f);

    s.setValueRange(0.0f, 0.5f);
    // _value 0.7 > new max 0.5 → reset to 0.5.
    CHECK_FLOAT_EQ(s.getValue(), 0.5f, 1e-5f);

    // Inverse: max < min → max bumps up.
    s.setValueRange(0.5f, 0.2f);
    CHECK(s.getMax() >= s.getMin() + Slider::kMinMaxEpsilon - 1e-7f);
}

// C-7: click on the slider sets _value to the click position within the
// track. The handle jumps to the click x, snapping to the normalized
// 0..1 of the click.
TEST_CASE(slider_click_sets_value_to_click_position) {
    Slider s;
    s.setSize(FVector2(200.0f, 24.0f));
    s.setPosition(FVector2(0.0f, 0.0f));

    // Center of the slider width = 200, so click at x=50 → normalized 0.25.
    s.onMouseButtonDown(UIMouseEvent(FVector2(50.0f, 12.0f), 0));

    CHECK(s.isDragging());
    CHECK_FLOAT_EQ(s.getNormalized(), 0.25f, 1e-4f);
    CHECK_FLOAT_EQ(s.getValue(), 0.25f, 1e-4f);

    // Mouse-up without subsequent move — dragging must clear.
    s.onMouseButtonUp(UIMouseEvent(FVector2(50.0f, 12.0f), 0));
    CHECK_FALSE(s.isDragging());
}

// C-7: dragging continuously updates _value. The down event sets the
// initial value; moves re-update until mouse-up.
TEST_CASE(slider_drag_updates_value) {
    Slider s;
    s.setSize(FVector2(200.0f, 24.0f));
    s.setPosition(FVector2(0.0f, 0.0f));

    int changeCount = 0;
    s.setOnValueChanged([&](float) { ++changeCount; });

    s.onMouseButtonDown(UIMouseEvent(FVector2(20.0f, 12.0f), 0));
    CHECK(changeCount == 1);   // initial jump on click
    const float valueAtClick = s.getValue();

    s.onMouseMove(UIMouseEvent(FVector2(80.0f, 12.0f), 0));
    CHECK(changeCount >= 2);
    CHECK(s.getValue() > valueAtClick);

    s.onMouseMove(UIMouseEvent(FVector2(150.0f, 12.0f), 0));
    CHECK(s.getValue() > 0.5f);

    // Drag past the trailing edge — clamp to 1.0.
    s.onMouseMove(UIMouseEvent(FVector2(500.0f, 12.0f), 0));
    CHECK_FLOAT_EQ(s.getValue(), 1.0f, 1e-5f);

    s.onMouseButtonUp(UIMouseEvent(FVector2(500.0f, 12.0f), 0));
    CHECK_FALSE(s.isDragging());
}

// C-7: cursor hint is SizeHorizontal when enabled (differs from
// InteractiveWidget's default Hand hint — Slider overrides because
// the user expects a horizontal-resize cursor on the track).
TEST_CASE(slider_cursor_hint_size_horizontal) {
    Slider s;
    CHECK(s.getCursorHint() == UiCursorHint::SizeHorizontal);

    s.setEnabled(false);
    CHECK(s.getCursorHint() == UiCursorHint::Default);
}

// C-7: disabled Slider refuses drag — click does not set dragging.
TEST_CASE(slider_disabled_blocks_drag) {
    Slider s;
    s.setSize(FVector2(200.0f, 24.0f));
    s.setPosition(FVector2(0.0f, 0.0f));
    s.setEnabled(false);

    s.onMouseButtonDown(UIMouseEvent(FVector2(50.0f, 12.0f), 0));
    CHECK_FALSE(s.isDragging());

    s.onMouseMove(UIMouseEvent(FVector2(80.0f, 12.0f), 0));
    CHECK_FLOAT_EQ(s.getValue(), 0.5f, 1e-5f);   // unchanged
}

// C-7: getTrackRect anchors at vertical center; getHandleRect stays
// within the slider's bounds.
TEST_CASE(slider_track_and_handle_layout) {
    Slider s;
    s.setSize(FVector2(200.0f, 24.0f));
    s.setPosition(FVector2(10.0f, 20.0f));

    FRectangle track = s.getTrackRect();
    CHECK_FLOAT_EQ(track.minX, 10.0f, 1e-4f);
    CHECK_FLOAT_EQ(track.maxX, 210.0f, 1e-4f);
    // Slider world y center = (20 + 44) / 2 = 32, minus kTrackHeight/2 = 3
    // → track.minY = 29, track.maxY = 35.
    CHECK_FLOAT_EQ(track.minY, 29.0f, 1e-4f);
    CHECK_FLOAT_EQ(track.maxY, 35.0f, 1e-4f);

    FRectangle handle = s.getHandleRect();
    // Default value 0.5 → handle center at slider midX = 110.
    CHECK_FLOAT_EQ((handle.minX + handle.maxX) * 0.5f, 110.0f, 1e-4f);
    CHECK(handle.minX >= track.minX);
    CHECK(handle.maxX <= track.maxX + Slider::kHandleWidth * 0.5f);
}

// C-7: render emits at least one Rect for the filled portion. We can't
// pin draw-call counts (color fan-out interacts with rectangle count),
// but we can pin the accent fill color (0.18, 0.45, 0.78) presence.
TEST_CASE(slider_render_emits_accent_fill) {
    Slider s;
    s.setSize(FVector2(200.0f, 24.0f));
    s.setPosition(FVector2(0.0f, 0.0f));
    s.setValue(0.5f);

    MockRenderer renderer;
    s.render(renderer);

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

TEST_CASE(slider_retained_display_list_tracks_click_and_drag_value) {
    Slider s;
    s.setSize(FVector2(200.0f, 24.0f));
    s.setPosition(FVector2(0.0f, 0.0f));

    auto accentFillMaxX = [](const MockRenderer& renderer) {
        for (const auto& dc : renderer.getDrawCalls()) {
            if (dc.type == MockRenderer::DrawCall::Rect
                && fabsf(dc.color.x - 0.18f) < 1e-4f
                && fabsf(dc.color.y - 0.45f) < 1e-4f
                && fabsf(dc.color.z - 0.78f) < 1e-4f) {
                return dc.bounds.maxX;
            }
        }
        return -1.0f;
    };

    MockRenderer renderer;
    s.render(renderer);
    CHECK_TRUE(s.hasCachedDisplayList());
    const float initialMaxX = accentFillMaxX(renderer);
    CHECK(initialMaxX > 0.0f);

    CHECK(s.onMouseButtonDown(
        UIMouseEvent(FVector2(50.0f, 12.0f), 0)));
    renderer.beginFrame();
    s.render(renderer);
    const float clickMaxX = accentFillMaxX(renderer);
    CHECK(clickMaxX < initialMaxX);

    CHECK(s.onMouseMove(UIMouseEvent(FVector2(150.0f, 12.0f), 0)));
    renderer.beginFrame();
    s.render(renderer);
    const float dragMaxX = accentFillMaxX(renderer);
    CHECK(dragMaxX > initialMaxX);
    CHECK(dragMaxX > clickMaxX);

    CHECK(s.onMouseButtonUp(
        UIMouseEvent(FVector2(150.0f, 12.0f), 0)));
}

// C-7: factory + serializer round-trip. factory.create("Slider") returns
// a Slider; serialize emits type Slider + min/max/value; deserialize
// restores all three.
TEST_CASE(slider_factory_and_serializer_round_trip) {
    WidgetFactory& factory = WidgetFactory::get();
    CHECK_TRUE(factory.isRegistered("Slider"));

    Widget* widget = factory.create("Slider");
    CHECK_NOT_NULL(widget);
    Slider* original = dynamic_cast<Slider*>(widget);
    CHECK_NOT_NULL(original);

    original->setId("volume");
    original->setValueRange(-100.0f, 100.0f);
    original->setValue(42.5f);

    std::string json = WidgetSerializer::serialize(widget);
    CHECK(json.find("\"type\": \"Slider\"") != std::string::npos);
    CHECK(json.find("-100") != std::string::npos);
    CHECK(json.find("100") != std::string::npos);
    CHECK(json.find("42.5") != std::string::npos);

    Widget* restored = WidgetSerializer::deserialize(json);
    CHECK_NOT_NULL(restored);
    Slider* restoredSlider = dynamic_cast<Slider*>(restored);
    CHECK_NOT_NULL(restoredSlider);
    CHECK(restoredSlider->getId() == "volume");
    CHECK_FLOAT_EQ(restoredSlider->getMin(), -100.0f, 1e-3f);
    CHECK_FLOAT_EQ(restoredSlider->getMax(),  100.0f, 1e-3f);
    CHECK_FLOAT_EQ(restoredSlider->getValue(),  42.5f, 1e-3f);

    destroyWidgetTree(widget);
    destroyWidgetTree(restored);
}

// C-7: getNormalized is 0 when value == min, 1 when value == max, mid
// in between.
TEST_CASE(slider_normalized_mapping) {
    Slider s;
    s.setValueRange(0.0f, 200.0f);

    s.setValue(0.0f);   CHECK_FLOAT_EQ(s.getNormalized(), 0.0f, 1e-5f);
    s.setValue(50.0f);  CHECK_FLOAT_EQ(s.getNormalized(), 0.25f, 1e-5f);
    s.setValue(100.0f); CHECK_FLOAT_EQ(s.getNormalized(), 0.5f,  1e-5f);
    s.setValue(200.0f); CHECK_FLOAT_EQ(s.getNormalized(), 1.0f,  1e-5f);
}

// C-7: drag-end synthesizes a final value emit only if the post-clamp
// value differs from the last-set value. The synthesized move goes
// through setValue which has its own callback guard. So once we're
// pinned at max=1.0, subsequent moves that still clamp to 1.0 must
// NOT fire _onValueChanged.
TEST_CASE(slider_drag_end_no_extra_emit_when_clamped) {
    Slider s;
    s.setSize(FVector2(200.0f, 24.0f));
    s.setPosition(FVector2(0.0f, 0.0f));

    int count = 0;
    s.setOnValueChanged([&](float) { ++count; });

    // Drag the handle past the right edge — value clamps to 1.0.
    s.onMouseButtonDown(UIMouseEvent(FVector2(150.0f, 12.0f), 0));
    CHECK(count == 1);  // value -> 0.75
    // Move further right; clamps to 1.0, fires once.
    s.onMouseMove(UIMouseEvent(FVector2(500.0f, 12.0f), 0));
    CHECK(count == 2);
    CHECK_FLOAT_EQ(s.getValue(), 1.0f, 1e-5f);

    // Subsequent moves that stay clamped to 1.0 must NOT refire.
    s.onMouseMove(UIMouseEvent(FVector2(501.0f, 12.0f), 0));
    s.onMouseMove(UIMouseEvent(FVector2(1000.0f, 12.0f), 0));
    CHECK(count == 2);

    s.onMouseButtonUp(UIMouseEvent(FVector2(1000.0f, 12.0f), 0));
    CHECK_FALSE(s.isDragging());
}

TEST_SUITE_END
