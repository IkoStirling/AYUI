#include "AYTest.h"
#include "AYInteractiveWidget.h"
#include "AYButton.h"
#include <iostream>

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_InteractiveWidget)

TEST_CASE(interactivewidget_initial_state) {
    InteractiveWidget widget;
    CHECK(widget.getState() == ButtonState::Normal);
    CHECK(widget.isEnabled() == true);
    CHECK(widget.isMouseOver() == false);
    CHECK(widget.isPressed() == false);
}

TEST_CASE(interactivewidget_hover_state) {
    InteractiveWidget widget;
    widget.setSize(FVector2(100.0f, 32.0f));

    UIMouseEvent evt(FVector2(50.0f, 16.0f), 0);
    bool handled = widget.onMouseMove(evt);
    CHECK(handled);
    CHECK(widget.getState() == ButtonState::Hovered);
    CHECK(widget.isMouseOver() == true);
}

TEST_CASE(interactivewidget_press_and_release) {
    InteractiveWidget widget;
    widget.setSize(FVector2(100.0f, 32.0f));

    bool clicked = false;
    widget.setOnClicked([&]() { clicked = true; });

    widget.onMouseMove(UIMouseEvent(FVector2(50.0f, 16.0f), 0));
    CHECK(widget.getState() == ButtonState::Hovered);

    widget.onMouseButtonDown(UIMouseEvent(FVector2(50.0f, 16.0f), 0));
    CHECK(widget.getState() == ButtonState::Pressed);

    widget.onMouseButtonUp(UIMouseEvent(FVector2(50.0f, 16.0f), 0));
    CHECK(clicked);
    CHECK(widget.getState() == ButtonState::Hovered);
}

TEST_CASE(interactivewidget_disable_clears_hover) {
    // Regression test for B2: pre-R-1, Button::onMouseMove with !_enabled
    // set state to Disabled but never cleared _isMouseOver, so re-enabling
    // after a hover-leave would leave state stuck at Hovered.
    InteractiveWidget widget;
    widget.setSize(FVector2(100.0f, 32.0f));

    widget.onMouseMove(UIMouseEvent(FVector2(50.0f, 16.0f), 0));
    CHECK(widget.isMouseOver() == true);

    widget.setEnabled(false);
    CHECK(widget.getState() == ButtonState::Disabled);
    CHECK(widget.isMouseOver() == false);

    widget.setEnabled(true);
    // No hover event has fired since re-enable, so state must be Normal.
    CHECK(widget.getState() == ButtonState::Normal);
}

TEST_CASE(interactivewidget_disable_blocks_clicks) {
    InteractiveWidget widget;
    widget.setSize(FVector2(100.0f, 32.0f));

    bool clicked = false;
    widget.setOnClicked([&]() { clicked = true; });

    widget.setEnabled(false);
    widget.onMouseMove(UIMouseEvent(FVector2(50.0f, 16.0f), 0));
    CHECK(widget.getState() == ButtonState::Disabled);
    CHECK(!widget.onMouseButtonDown(UIMouseEvent(FVector2(50.0f, 16.0f), 0)));
    CHECK(!widget.onMouseButtonUp(UIMouseEvent(FVector2(50.0f, 16.0f), 0)));
    CHECK(!clicked);
}

TEST_CASE(interactivewidget_mouse_leave_resets_state) {
    InteractiveWidget widget;
    widget.setSize(FVector2(100.0f, 32.0f));

    widget.onMouseMove(UIMouseEvent(FVector2(50.0f, 16.0f), 0));
    CHECK(widget.getState() == ButtonState::Hovered);

    widget.onMouseLeave();
    CHECK(widget.getState() == ButtonState::Normal);
    CHECK(widget.isMouseOver() == false);
    CHECK(widget.isPressed() == false);
}

TEST_CASE(interactivewidget_cursor_hint) {
    InteractiveWidget widget;
    widget.setSize(FVector2(100.0f, 32.0f));

    CHECK(widget.getCursorHint() == UiCursorHint::Default);

    widget.onMouseMove(UIMouseEvent(FVector2(50.0f, 16.0f), 0));
    CHECK(widget.getCursorHint() == UiCursorHint::Hand);

    widget.setEnabled(false);
    CHECK(widget.getCursorHint() == UiCursorHint::Default);
}

TEST_SUITE_END