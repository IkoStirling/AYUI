#include "AYTest.h"
#include "AYUI/InteractiveWidget.h"
#include "AYUI/Button.h"
#include "AYUI/Window.h"
#include "AYUI/UIManager.h"
#include "AYUI/MockRenderer.h"
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

// PR-B1 regression guard: a drag capture FREEZES the captured widget's
// hint for the whole drag — no geometry gate. Window resize/title drags
// are state-driven (_isResizing/_isDragging) and the pointer
// legitimately leaves the window when the size/position is clamped
// (min-size / parent edge); a contains() gate dropped the Size*/Move
// cursor mid-drag (S5c regression, reverted). The root must be a
// container (VBox) — a plain Widget root hit-tests only itself.
TEST_CASE(uimanager_cursor_hint_drag_frozen) {
    UIManager ui;
    MockRenderer renderer;
    ui.initialize(&renderer);

    const char* json = R"({
        "type": "VBox",
        "id": "root",
        "size": { "w": 800, "h": 600 },
        "children": [
            { "type": "Window", "id": "win",
              "position": { "x": 100, "y": 100 },
              "size": { "w": 300, "h": 200 },
              "movable": true, "resizable": true },
            { "type": "Button", "id": "btn",
              "position": { "x": 100, "y": 400 }, "size": { "w": 120, "h": 32 } }
        ]
    })";
    CHECK(ui.loadFromString(json));
    ui.setClientSize(800.0f, 600.0f);
    ui.layout();

    auto* win = dynamic_cast<Window*>(ui.findById("win"));
    auto* btn = dynamic_cast<Button*>(ui.findById("btn"));
    CHECK_NOT_NULL(win);
    CHECK_NOT_NULL(btn);
    if (win == nullptr || btn == nullptr) {
        ui.shutdown();
        return;
    }

    // ---- 1) Left-edge resize: hover → SizeWe, press → SizeWe, and the
    // cursor STAYS SizeWe while dragging far outside the window (the
    // window min-clamps at min-size; the pointer is past the edge).
    FRectangle wb = win->getWorldBounds();
    const FVector2 leftEdge(wb.minX + 8.0f, (wb.minY + wb.maxY) * 0.5f);
    ui.onMouseMove(leftEdge.x, leftEdge.y);
    CHECK(ui.getCursorHint() == UiCursorHint::SizeWe);
    ui.onMouseButtonDown(leftEdge.x, leftEdge.y, 0);
    CHECK(ui.getCursorHint() == UiCursorHint::SizeWe);
    ui.onMouseMove(1000.0f, leftEdge.y);   // far outside the window
    CHECK(ui.getCursorHint() == UiCursorHint::SizeWe);
    ui.onMouseButtonUp(1000.0f, leftEdge.y, 0);

    // ---- 2) Title-bar drag: hover → Move, press → Move, and Move is
    // kept while dragging past the parent edge (window clamps to the
    // VBox boundary, pointer ends up outside it).
    wb = win->getWorldBounds();
    const FVector2 titleCtr(wb.minX + (wb.maxX - wb.minX) * 0.5f, wb.minY + 14.0f);
    ui.onMouseMove(titleCtr.x, titleCtr.y);
    CHECK(ui.getCursorHint() == UiCursorHint::Move);
    ui.onMouseButtonDown(titleCtr.x, titleCtr.y, 0);
    CHECK(ui.getCursorHint() == UiCursorHint::Move);
    ui.onMouseMove(900.0f, titleCtr.y);    // outside the 800-wide root
    CHECK(ui.getCursorHint() == UiCursorHint::Move);
    ui.onMouseButtonUp(900.0f, titleCtr.y, 0);

    // ---- 3) Button press-drag: Hand is hover-dependent (NOT frozen).
    // Dragging off the button clears its hover → arrow, matching native
    // behavior; dragging back over it → Hand again. Only state-driven
    // hints (Window Size*/Move) freeze for the whole drag.
    const FRectangle bb = btn->getWorldBounds();
    const FVector2 bctr((bb.minX + bb.maxX) * 0.5f, (bb.minY + bb.maxY) * 0.5f);
    ui.onMouseMove(bctr.x, bctr.y);
    CHECK(ui.getCursorHint() == UiCursorHint::Hand);
    ui.onMouseButtonDown(bctr.x, bctr.y, 0);
    CHECK(ui.getCursorHint() == UiCursorHint::Hand);
    ui.onMouseMove(700.0f, 550.0f);        // far outside the button
    CHECK(ui.getCursorHint() == UiCursorHint::Default);
    ui.onMouseMove(bctr.x, bctr.y);        // back over → Hand again
    CHECK(ui.getCursorHint() == UiCursorHint::Hand);
    ui.onMouseButtonUp(bctr.x, bctr.y, 0);

    ui.shutdown();
}

TEST_SUITE_END