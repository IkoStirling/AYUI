#include "AYTest.h"
#include "AYUI/RadioButton.h"
#include "AYUI/WidgetFactory.h"
#include "AYUI/WidgetSerializer.h"
#include "AYUI/MockRenderer.h"
#include "AYUI/Style.h"

#include <iostream>

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_RadioButton)

// C-2b: a fresh RadioButton reports defaults: unselected, default group 0,
// base state from InteractiveWidget's Normal + Enabled.
TEST_CASE(radiobutton_initial_state) {
    RadioButton rb;
    CHECK_FALSE(rb.isChecked());
    CHECK(rb.getGroupId() == 0);
    CHECK(rb.getText() == L"");
    CHECK(rb.getState() == ButtonState::Normal);
    CHECK(rb.isEnabled());
}

// C-2b: hover puts the RadioButton into InteractiveWidget's Hovered state
// (proves that overriding only onMouseButtonUp leaves the rest of the
// base state machine intact).
TEST_CASE(radiobutton_hover_state) {
    RadioButton rb;
    rb.setSize(FVector2(140.0f, 24.0f));
    UIMouseEvent evt(FVector2(40.0f, 12.0f), 0);
    CHECK(rb.onMouseMove(evt));
    CHECK(rb.getState() == ButtonState::Hovered);
}

// C-2b: a full click cycle (move + down + up over the widget) selects the
// radio. _onSelected fires once, _onToggled(true) fires once. _onClicked
// also fires (Button-style contract preserved). Final state is Hovered.
TEST_CASE(radiobutton_click_selects) {
    RadioButton rb;
    rb.setSize(FVector2(140.0f, 24.0f));
    bool clicked = false;
    bool selectedFired = false;
    int toggledTo = -1;
    rb.setOnClicked([&]() { clicked = true; });
    rb.setOnSelected([&]() { selectedFired = true; });
    rb.setOnToggled([&](bool v) { toggledTo = v ? 1 : 0; });

    rb.onMouseMove(UIMouseEvent(FVector2(40.0f, 12.0f), 0));
    rb.onMouseButtonDown(UIMouseEvent(FVector2(40.0f, 12.0f), 0));
    CHECK(rb.getState() == ButtonState::Pressed);

    rb.onMouseButtonUp(UIMouseEvent(FVector2(40.0f, 12.0f), 0));

    CHECK_TRUE(rb.isChecked());
    CHECK(clicked);
    CHECK(selectedFired);
    CHECK(toggledTo == 1);
    CHECK(rb.getState() == ButtonState::Hovered);
}

// C-2b: a second click on an already-selected RadioButton is a NO-OP —
// _onSelected / _onToggled do NOT fire again. Only the Button-style
// _onClicked fires. This makes radio semantics stable under repeated
// clicks (typical UX: clicking your currently-selected radio button
// keeps it selected without any observable callback traffic).
TEST_CASE(radiobutton_second_click_noop) {
    RadioButton rb;
    rb.setSize(FVector2(140.0f, 24.0f));
    int selectCount = 0;
    int toggleCount = 0;
    rb.setOnSelected([&]() { ++selectCount; });
    rb.setOnToggled([&](bool) { ++toggleCount; });

    // First click — selects.
    rb.onMouseMove(UIMouseEvent(FVector2(40.0f, 12.0f), 0));
    rb.onMouseButtonDown(UIMouseEvent(FVector2(40.0f, 12.0f), 0));
    rb.onMouseButtonUp(UIMouseEvent(FVector2(40.0f, 12.0f), 0));
    CHECK_TRUE(rb.isChecked());
    CHECK(selectCount == 1);
    CHECK(toggleCount == 1);

    // Second click — no observable transition.
    rb.onMouseButtonDown(UIMouseEvent(FVector2(40.0f, 12.0f), 0));
    rb.onMouseButtonUp(UIMouseEvent(FVector2(40.0f, 12.0f), 0));
    CHECK_TRUE(rb.isChecked());
    CHECK(selectCount == 1);
    CHECK(toggleCount == 1);
}

// C-2b: disabled RadioButton refuses clicks. B2 fix path pin —
// setEnabled(false) clears hover and pressed so a stale hover cannot
// survive into re-enable.
TEST_CASE(radiobutton_disabled_blocks_click) {
    RadioButton rb;
    rb.setSize(FVector2(140.0f, 24.0f));
    int selectCount = 0;
    rb.setOnSelected([&]() { ++selectCount; });

    rb.onMouseMove(UIMouseEvent(FVector2(40.0f, 12.0f), 0));
    rb.setEnabled(false);

    CHECK_FALSE(rb.onMouseButtonDown(UIMouseEvent(FVector2(40.0f, 12.0f), 0)));
    CHECK_FALSE(rb.onMouseButtonUp(UIMouseEvent(FVector2(40.0f, 12.0f), 0)));
    CHECK_FALSE(rb.isChecked());
    CHECK(selectCount == 0);
    CHECK(rb.getState() == ButtonState::Disabled);
}

// C-2b: programmatic setChecked(true) fires _onSelected + _onToggled once,
// setChecked(true) again is a no-op (idempotent), setChecked(false)
// does NOT fire _onSelected (only transitions to selected are reported
// through _onSelected) but DOES fire _onToggled(false).
TEST_CASE(radiobutton_set_checked_callbacks) {
    RadioButton rb;
    int selectCount = 0;
    int lastToggle = -1;
    int toggleCount = 0;
    rb.setOnSelected([&]() { ++selectCount; });
    rb.setOnToggled([&](bool v) { lastToggle = v ? 1 : 0; ++toggleCount; });

    rb.setChecked(true);
    CHECK_TRUE(rb.isChecked());
    CHECK(selectCount == 1);
    CHECK(toggleCount == 1);
    CHECK(lastToggle == 1);

    rb.setChecked(true);
    CHECK(selectCount == 1);
    CHECK(toggleCount == 1);

    rb.setChecked(false);
    CHECK_FALSE(rb.isChecked());
    CHECK(selectCount == 1);          // _onSelected does NOT fire
    CHECK(toggleCount == 2);
    CHECK(lastToggle == 0);
}

// C-2b: group mutex via host-side arbitration lambda. rb_a + rb_b share
// groupId=42; rb_c is in group 7 (must NOT be touched). When rb_a gets
// selected (via clicking through the input pipeline), the host's lambda
// walks the peer list and deselects any same-group peer. This is the
// canonical pattern documented in the class header — RadioButton does
// not own group mutex, the host does.
TEST_CASE(radiobutton_group_mutex_host_lambda) {
    RadioButton* rb_a = new RadioButton();
    RadioButton* rb_b = new RadioButton();
    RadioButton* rb_c = new RadioButton();
    rb_a->setSize(FVector2(140.0f, 24.0f));
    rb_b->setSize(FVector2(140.0f, 24.0f));
    rb_c->setSize(FVector2(140.0f, 24.0f));

    rb_a->setGroupId(42);
    rb_b->setGroupId(42);
    rb_c->setGroupId(7);

    std::vector<RadioButton*> peers = { rb_a, rb_b, rb_c };

    // Wire mutex on rb_a + rb_b AFTER initial state is set so the
    // setChecked(true) below only triggers programmatic transitions,
    // not click-driven ones. (Click pipeline calls setChecked(true)
    // directly, which fires _onToggled; the host lambda deselects
    // peers WITHOUT re-firing their own lambdas because the call-chain
    // is setChecked(false) (programmatic, bool change) which DOES fire
    // their _onToggled; the guard `v` skips deselect of already-false
    // peers.)
    auto arbitrateA = [&](bool v) {
        if (!v) return;
        for (RadioButton* peer : peers) {
            if (peer != rb_a && peer->getGroupId() == rb_a->getGroupId()) {
                peer->setChecked(false);
            }
        }
    };
    auto arbitrateB = [&](bool v) {
        if (!v) return;
        for (RadioButton* peer : peers) {
            if (peer != rb_b && peer->getGroupId() == rb_b->getGroupId()) {
                peer->setChecked(false);
            }
        }
    };
    rb_a->setOnToggled(arbitrateA);
    rb_b->setOnToggled(arbitrateB);

    // Start with rb_b checked — the host needs the mutex to deselect
    // rb_a, but rb_a is already unchecked so the lambda is a no-op.
    rb_b->setChecked(true);
    CHECK_FALSE(rb_a->isChecked());
    CHECK_TRUE(rb_b->isChecked());

    // Click rb_a through the input pipeline. The base state machine's
    // onMouseButtonUp override sees rb_a unchecked → flips it to true,
    // fires arbitrateA(true), which deselects rb_b.
    rb_a->onMouseMove(UIMouseEvent(FVector2(40.0f, 12.0f), 0));
    rb_a->onMouseButtonDown(UIMouseEvent(FVector2(40.0f, 12.0f), 0));
    rb_a->onMouseButtonUp(UIMouseEvent(FVector2(40.0f, 12.0f), 0));

    CHECK_TRUE(rb_a->isChecked());
    CHECK_FALSE(rb_b->isChecked());   // mutex deselected rb_b
    CHECK_FALSE(rb_c->isChecked());   // group 7 untouched

    delete rb_a;
    delete rb_b;
    delete rb_c;
}

// C-2b: render — unchecked state must NOT emit an accent-colored fill
// rect. Only the outer circle bg + border. No Text draw call when text
// is empty.
TEST_CASE(radiobutton_render_unchecked_no_accent) {
    RadioButton rb;
    rb.setSize(FVector2(140.0f, 24.0f));
    rb.setPosition(FVector2(0.0f, 0.0f));
    rb.setText(L"");

    MockRenderer renderer;
    rb.render(renderer);

    int rectCount = 0;
    int textCount = 0;
    for (const auto& dc : renderer.getDrawCalls()) {
        if (dc.type == MockRenderer::DrawCall::Rect) ++rectCount;
        if (dc.type == MockRenderer::DrawCall::Text) ++textCount;
    }
    CHECK(rectCount >= 1);
    CHECK(textCount == 0);

    // No accent fill (0.18, 0.45, 0.78) when unchecked.
    int unexpectedAccentFillCount = 0;
    for (const auto& dc : renderer.getDrawCalls()) {
        if (dc.type != MockRenderer::DrawCall::Rect) continue;
        if (fabsf(dc.color.x - 0.18f) < 1e-4f &&
            fabsf(dc.color.y - 0.45f) < 1e-4f &&
            fabsf(dc.color.z - 0.78f) < 1e-4f) {
            ++unexpectedAccentFillCount;
        }
    }
    CHECK(unexpectedAccentFillCount == 0);
}

// C-2b: render — selected state emits an accent-colored inner fill
// (the selected dot). Pin the second consecutive Rect fill's color so
// 'forgot to draw the dot' AND 'drew with wrong color' regressions
// both fail loud.
TEST_CASE(radiobutton_render_selected_accent_dot) {
    RadioButton rb;
    rb.setSize(FVector2(140.0f, 24.0f));
    rb.setPosition(FVector2(0.0f, 0.0f));
    rb.setChecked(true);

    MockRenderer renderer;
    rb.render(renderer);

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

// C-2b: a non-empty label produces exactly one drawText with the label.
TEST_CASE(radiobutton_render_label_text) {
    RadioButton rb;
    rb.setSize(FVector2(140.0f, 24.0f));
    rb.setPosition(FVector2(0.0f, 0.0f));
    rb.setText(L"1920x1080");

    MockRenderer renderer;
    rb.render(renderer);

    int textCount = 0;
    std::wstring seen;
    for (const auto& dc : renderer.getDrawCalls()) {
        if (dc.type == MockRenderer::DrawCall::Text) {
            ++textCount;
            seen = dc.text;
        }
    }
    CHECK(textCount == 1);
    CHECK(seen == L"1920x1080");
}

// C-2b: factory + serializer round-trip. factory.create("RadioButton")
// returns a RadioButton, serialize emits type RadioButton + groupId +
// checked + text, deserialize restores all four fields.
TEST_CASE(radiobutton_factory_and_serializer_round_trip) {
    WidgetFactory& factory = WidgetFactory::get();
    CHECK_TRUE(factory.isRegistered("RadioButton"));

    Widget* widget = factory.create("RadioButton");
    CHECK_NOT_NULL(widget);
    RadioButton* original = dynamic_cast<RadioButton*>(widget);
    CHECK_NOT_NULL(original);

    original->setId("rb_1080");
    original->setText(L"1920x1080");
    original->setChecked(true);
    original->setGroupId(42);

    std::string json = WidgetSerializer::serialize(original);
    CHECK(json.find("\"type\": \"RadioButton\"") != std::string::npos);
    CHECK(json.find("\"checked\": true") != std::string::npos);
    CHECK(json.find("\"groupId\": 42") != std::string::npos);
    CHECK(json.find("1920x1080") != std::string::npos);

    Widget* restored = WidgetSerializer::deserialize(json);
    CHECK_NOT_NULL(restored);
    RadioButton* restoredRb = dynamic_cast<RadioButton*>(restored);
    CHECK_NOT_NULL(restoredRb);
    CHECK(restoredRb->getId() == "rb_1080");
    CHECK(restoredRb->getText() == L"1920x1080");
    CHECK_TRUE(restoredRb->isChecked());
    CHECK(restoredRb->getGroupId() == 42);

    destroyWidgetTree(widget);
    destroyWidgetTree(restored);
}

// C-2b: getCircleRect anchors the circle at the widget's leading edge
// with kCirclePadding, centered vertically when height exceeds
// kCircleSize. (Matches CheckBox's getBoxRect layout pattern.)
TEST_CASE(radiobutton_circle_rect_layout) {
    RadioButton rb;
    rb.setSize(FVector2(140.0f, 30.0f));
    rb.setPosition(FVector2(10.0f, 20.0f));

    FRectangle c = rb.getCircleRect();
    CHECK_FLOAT_EQ(c.minX, 14.0f, 1e-4f);   // 10 + kCirclePadding(4)
    CHECK_FLOAT_EQ(c.maxX, 30.0f, 1e-4f);   // + kCircleSize(16)
    CHECK_FLOAT_EQ(c.minY, 27.0f, 1e-4f);   // (30 - 16)/2 + 20
    CHECK_FLOAT_EQ(c.maxY, 43.0f, 1e-4f);
}

TEST_SUITE_END
