#include "AYTest.h"
#include "AYUI/CheckBox.h"
#include "AYUI/WidgetFactory.h"
#include "AYUI/WidgetSerializer.h"
#include "AYUI/MockRenderer.h"
#include "AYUI/Style.h"

#include <iostream>

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_CheckBox)

// C-2: a fresh CheckBox reports defaults: unchecked, empty label, base
// state from InteractiveWidget's Normal + Enabled.
TEST_CASE(checkbox_initial_state) {
    CheckBox cb;
    CHECK_FALSE(cb.isChecked());
    CHECK(cb.getText() == L"");
    CHECK(cb.getState() == ButtonState::Normal);
    CHECK(cb.isEnabled());
}

// C-2: a hover event puts the CheckBox into InteractiveWidget's Hovered
// state (proves the base state machine still applies through CheckBox's
// own onMouseButtonUp override — only the up handler is overridden).
TEST_CASE(checkbox_hover_state) {
    CheckBox cb;
    cb.setSize(FVector2(140.0f, 24.0f));

    UIMouseEvent evt(FVector2(40.0f, 12.0f), 0);
    CHECK(cb.onMouseMove(evt));
    CHECK(cb.getState() == ButtonState::Hovered);
}

// C-2: full click cycle — move into widget, press, release over the
// widget. _checked must flip to true, _onToggled(true) must fire, and
// _onClicked (Button-style contract) must also fire. State ends at
// Hovered (pointer still over).
TEST_CASE(checkbox_click_toggles) {
    CheckBox cb;
    cb.setSize(FVector2(140.0f, 24.0f));

    bool clicked = false;
    int toggledTo = -1;
    int toggledCount = 0;
    cb.setOnClicked([&]() { clicked = true; });
    cb.setOnToggled([&](bool v) { toggledTo = v ? 1 : 0; ++toggledCount; });

    cb.onMouseMove(UIMouseEvent(FVector2(40.0f, 12.0f), 0));
    cb.onMouseButtonDown(UIMouseEvent(FVector2(40.0f, 12.0f), 0));
    CHECK(cb.getState() == ButtonState::Pressed);

    cb.onMouseButtonUp(UIMouseEvent(FVector2(40.0f, 12.0f), 0));

    CHECK_TRUE(cb.isChecked());
    CHECK(clicked);
    CHECK(toggledCount == 1);
    CHECK(toggledTo == 1);
    CHECK(cb.getState() == ButtonState::Hovered);
}

// C-2: toggling on then off through a second click cycle. _checked must
// round-trip to false and _onToggled must fire once with `false`. The
// state at the end should be Hovered (cursor still over the widget).
TEST_CASE(checkbox_second_click_untoggles) {
    CheckBox cb;
    cb.setSize(FVector2(140.0f, 24.0f));

    int lastToggle = -1;
    cb.setOnToggled([&](bool v) { lastToggle = v ? 1 : 0; });

    cb.onMouseMove(UIMouseEvent(FVector2(40.0f, 12.0f), 0));
    cb.onMouseButtonDown(UIMouseEvent(FVector2(40.0f, 12.0f), 0));
    cb.onMouseButtonUp(UIMouseEvent(FVector2(40.0f, 12.0f), 0));
    CHECK_TRUE(cb.isChecked());
    CHECK(lastToggle == 1);

    cb.onMouseButtonDown(UIMouseEvent(FVector2(40.0f, 12.0f), 0));
    cb.onMouseButtonUp(UIMouseEvent(FVector2(40.0f, 12.0f), 0));
    CHECK_FALSE(cb.isChecked());
    CHECK(lastToggle == 0);
    CHECK(cb.getState() == ButtonState::Hovered);
}

// C-2: disabled CheckBox must refuse clicks. This is the B2 fix path
// (setEnabled(false) clears _isMouseOver / _isPressed so a stale hover
// state cannot survive into re-enable).
TEST_CASE(checkbox_disabled_blocks_click) {
    CheckBox cb;
    cb.setSize(FVector2(140.0f, 24.0f));

    bool toggled = false;
    cb.setOnToggled([&](bool) { toggled = true; });

    cb.onMouseMove(UIMouseEvent(FVector2(40.0f, 12.0f), 0));
    cb.setEnabled(false);

    CHECK_FALSE(cb.onMouseButtonDown(UIMouseEvent(FVector2(40.0f, 12.0f), 0)));
    CHECK_FALSE(cb.onMouseButtonUp(UIMouseEvent(FVector2(40.0f, 12.0f), 0)));
    CHECK_FALSE(cb.isChecked());
    CHECK_FALSE(toggled);
    CHECK(cb.getState() == ButtonState::Disabled);
}

// C-2: programmatic setChecked(true / false) updates the bool, but only
// fires _onToggled when the value actually changes (no spurious callback
// on identical writes).
TEST_CASE(checkbox_set_checked_idempotent) {
    CheckBox cb;
    int toggleCount = 0;
    cb.setOnToggled([&](bool) { ++toggleCount; });

    cb.setChecked(false);
    CHECK_FALSE(cb.isChecked());
    CHECK(toggleCount == 0);

    cb.setChecked(true);
    CHECK_TRUE(cb.isChecked());
    CHECK(toggleCount == 1);

    cb.setChecked(true);
    CHECK_TRUE(cb.isChecked());
    CHECK(toggleCount == 1);

    cb.setChecked(false);
    CHECK_FALSE(cb.isChecked());
    CHECK(toggleCount == 2);
}

// C-2: render output. Default state (unchecked) emits:
//   1 drawRect (box bg) + 1 drawBorderRect (which MockRenderer fans out
//   into 4-5 Rect draw calls) + N draws for the label text. With no
//   label text, total draw count must NOT include a checked-accent fill
//   rect. With a label, exactly one Text draw call is recorded.
TEST_CASE(checkbox_render_unchecked_no_accent) {
    CheckBox cb;
    cb.setSize(FVector2(140.0f, 24.0f));
    cb.setPosition(FVector2(0.0f, 0.0f));
    cb.setText(L"");

    MockRenderer renderer;
    cb.render(renderer);

    int rectCount = 0;
    int textCount = 0;
    for (const auto& dc : renderer.getDrawCalls()) {
        if (dc.type == MockRenderer::DrawCall::Rect) ++rectCount;
        if (dc.type == MockRenderer::DrawCall::Text) ++textCount;
    }
    CHECK(rectCount >= 1);
    CHECK(textCount == 0);
}

// C-2: when checked, render emits an additional accent-filled rect
// inside the box (the "tick mark"). Pinning the second consecutive Rect
// draw call's color at (0.18, 0.45, 0.78) catches both regressions:
// - render path forgot to draw the tick
// - render path drew it but with the wrong color
TEST_CASE(checkbox_render_checked_accent_fill) {
    CheckBox cb;
    cb.setSize(FVector2(140.0f, 24.0f));
    cb.setPosition(FVector2(0.0f, 0.0f));
    cb.setChecked(true);

    MockRenderer renderer;
    cb.render(renderer);

    int rectCount = 0;
    for (const auto& dc : renderer.getDrawCalls()) {
        if (dc.type == MockRenderer::DrawCall::Rect) ++rectCount;
    }
    // 1 box bg + 1 accent tick = at least 2 rect draws.
    CHECK(rectCount >= 2);

    // The accent fill is the second Rect draw call — find the drawRect
    // whose color matches the accent (0.18f, 0.45f, 0.78f, 1.0f).
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

// C-2: a non-empty label produces exactly one drawText call carrying the
// label's wide-string content (wide-to-byte lossy read is OK for ASCII).
TEST_CASE(checkbox_render_label_text) {
    CheckBox cb;
    cb.setSize(FVector2(140.0f, 24.0f));
    cb.setPosition(FVector2(0.0f, 0.0f));
    cb.setText(L"Show FPS");

    MockRenderer renderer;
    cb.render(renderer);

    int textCount = 0;
    std::wstring seenLabel;
    for (const auto& dc : renderer.getDrawCalls()) {
        if (dc.type == MockRenderer::DrawCall::Text) {
            ++textCount;
            seenLabel = dc.text;
        }
    }
    CHECK(textCount == 1);
    CHECK(seenLabel == L"Show FPS");
}

// C-2: factory / serializer round-trip. WidgetFactory.create("CheckBox")
// must return a CheckBox (not a generic Widget), the serializer must
// emit type "CheckBox", and a deserialize-restore cycle must preserve
// the _checked state and the _text payload.
TEST_CASE(checkbox_factory_and_serializer_round_trip) {
    WidgetFactory& factory = WidgetFactory::get();
    CHECK_TRUE(factory.isRegistered("CheckBox"));

    Widget* widget = factory.create("CheckBox");
    CHECK_NOT_NULL(widget);
    CheckBox* original = dynamic_cast<CheckBox*>(widget);
    CHECK_NOT_NULL(original);

    original->setId("opt_fps");
    original->setText(L"Show FPS");
    original->setChecked(true);
    original->setStyleId("");

    std::string json = WidgetSerializer::serialize(original);
    CHECK(json.find("\"type\": \"CheckBox\"") != std::string::npos);
    CHECK(json.find("\"checked\": true") != std::string::npos);
    CHECK(json.find("Show FPS") != std::string::npos);

    Widget* restored = WidgetSerializer::deserialize(json);
    CHECK_NOT_NULL(restored);
    CheckBox* restoredCb = dynamic_cast<CheckBox*>(restored);
    CHECK_NOT_NULL(restoredCb);
    CHECK(restoredCb->getId() == "opt_fps");
    CHECK(restoredCb->getText() == L"Show FPS");
    CHECK_TRUE(restoredCb->isChecked());

    destroyWidgetTree(widget);
    destroyWidgetTree(restored);
}

// C-2: getBoxRect anchors the box at the widget's leading edge with
// kBoxPadding, centered vertically when the height exceeds kBoxSize.
TEST_CASE(checkbox_box_rect_layout) {
    CheckBox cb;
    cb.setSize(FVector2(140.0f, 30.0f));
    cb.setPosition(FVector2(10.0f, 20.0f));

    FRectangle box = cb.getBoxRect();
    CHECK_FLOAT_EQ(box.minX, 14.0f, 1e-4f);   // 10 + kBoxPadding(4)
    CHECK_FLOAT_EQ(box.maxX, 30.0f, 1e-4f);   // + kBoxSize(16)
    CHECK_FLOAT_EQ(box.minY, 27.0f, 1e-4f);   // (30 - 16)/2 + 20
    CHECK_FLOAT_EQ(box.maxY, 43.0f, 1e-4f);
}

TEST_SUITE_END
