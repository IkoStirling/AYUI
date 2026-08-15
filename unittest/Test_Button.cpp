#include "AYTest.h"
#include "AYButton.h"
#include "AYStyle.h"
#include "AYMockRenderer.h"
#include <iostream>

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_Button)

TEST_CASE(button_initial_state) {
    Button button;
    CHECK(button.getState() == ButtonState::Normal);
    CHECK(button.isEnabled() == true);
    CHECK(button.getText() == L"");
}

TEST_CASE(button_set_text) {
    Button button;
    button.setText(L"Click Me");
    CHECK(button.getText() == L"Click Me");
}

TEST_CASE(button_hover_state) {
    Button button;
    button.setSize(FVector2(100.0f, 32.0f));

    UIMouseEvent evt(FVector2(50.0f, 16.0f), 0);
    bool handled = button.onMouseMove(evt);
    CHECK(handled);
    CHECK(button.getState() == ButtonState::Hovered);
}

TEST_CASE(button_press_and_release) {
    Button button;
    button.setSize(FVector2(100.0f, 32.0f));

    bool clicked = false;
    button.setOnClicked([&]() { clicked = true; });

    // Move to button
    UIMouseEvent moveEvt(FVector2(50.0f, 16.0f), 0);
    button.onMouseMove(moveEvt);
    CHECK(button.getState() == ButtonState::Hovered);

    // Press
    UIMouseEvent downEvt(FVector2(50.0f, 16.0f), 0);
    button.onMouseButtonDown(downEvt);
    CHECK(button.getState() == ButtonState::Pressed);

    // Release (still over button)
    UIMouseEvent upEvt(FVector2(50.0f, 16.0f), 0);
    button.onMouseButtonUp(upEvt);
    CHECK(clicked);
    CHECK(button.getState() == ButtonState::Hovered);
}

TEST_CASE(button_disabled) {
    Button button;
    button.setSize(FVector2(100.0f, 32.0f));
    button.setEnabled(false);

    UIMouseEvent evt(FVector2(50.0f, 16.0f), 0);
    bool handled = button.onMouseMove(evt);
    CHECK(!handled);
    CHECK(button.getState() == ButtonState::Disabled);
}

TEST_CASE(button_leave_area) {
    Button button;
    button.setSize(FVector2(100.0f, 32.0f));

    // Hover
    UIMouseEvent hoverEvt(FVector2(50.0f, 16.0f), 0);
    button.onMouseMove(hoverEvt);
    CHECK(button.getState() == ButtonState::Hovered);

    // Leave
    UIMouseEvent leaveEvt(FVector2(200.0f, 200.0f), 0);
    button.onMouseMove(leaveEvt);
    CHECK(button.getState() == ButtonState::Normal);
}

TEST_CASE(button_hover_enter_leave_enter) {
    Button button;
    button.setSize(FVector2(100.0f, 32.0f));

    // Enter hover
    UIMouseEvent enter(FVector2(50.0f, 16.0f), 0);
    CHECK(button.onMouseMove(enter));
    CHECK(button.getState() == ButtonState::Hovered);

    // Leave hover
    UIMouseEvent leave(FVector2(200.0f, 200.0f), 0);
    button.onMouseMove(leave);
    CHECK(button.getState() == ButtonState::Normal);

    // Re-enter hover
    UIMouseEvent reenter(FVector2(50.0f, 16.0f), 0);
    CHECK(button.onMouseMove(reenter));
    CHECK(button.getState() == ButtonState::Hovered);
}

TEST_CASE(button_render_preserves_hover_fill) {
    MockRenderer renderer;
    Button button;
    button.setSize(FVector2(100.0f, 32.0f));
    button.setText(L"OK");

    UIMouseEvent hover(FVector2(50.0f, 16.0f), 0);
    button.onMouseMove(hover);
    // UI animation lane: hover fill transitions over 90ms — advance the
    // tween to completion so the target color is asserted.
    button.tick(0.09f);
    button.render(renderer);

    CHECK(renderer.getDrawCalls().size() >= 5u);

    const MockRenderer::DrawCall& fill = renderer.getDrawCalls()[0];
    CHECK(fill.type == MockRenderer::DrawCall::Rect);
    CHECK(fill.color.x == 0.36f);
    CHECK(fill.color.y == 0.38f);
    CHECK(fill.color.z == 0.42f);
    // B1: fallback fill is a 2px rounded rect (matches the 2px rounded
    // border); MockRenderer rides the radius in floatParam1.
    CHECK_FLOAT_EQ(fill.floatParam1, 2.0f, 1e-5f);
}

// B1: with a wired StyleSheet the rounded fill must follow the style's
// corner radius (not the fallback 2px), so fill and border stay aligned.
TEST_CASE(button_style_rounded_fill_follows_corner_radius) {
    StyleManager::get().setStyleSheet(nullptr);

    StyleSheet sheet;
    WidgetStyle st = StyleBuilder::makeButton();
    st.border.cornerRadius = 6.0f;
    sheet.setStyle("test_btn_rounded", st);
    StyleManager::get().setStyleSheet(&sheet);

    Button button;
    button.setSize(FVector2(100.0f, 32.0f));
    button.setText(L"OK");
    button.setStyleId("test_btn_rounded");

    MockRenderer renderer;
    button.render(renderer);

    CHECK(renderer.getDrawCalls().size() >= 2u);
    const auto& fill = renderer.getDrawCalls()[0];
    CHECK(fill.type == MockRenderer::DrawCall::Rect);  // rounded → Rect (compat)
    CHECK_FLOAT_EQ(fill.floatParam1, 6.0f, 1e-5f);     // style radius rode through

    StyleManager::get().setStyleSheet(nullptr);
}

TEST_SUITE_END