#include "AYTest.h"
#include "AYButton.h"
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

TEST_SUITE_END