#include "AYTest.h"
#include "AYSelectableWidget.h"
#include "AYUIManager.h"
#include "AYMockRenderer.h"
#include "AYWidgetFactory.h"
#include "AYWidgetSerializer.h"
#include "AYMath.h"
#include <iostream>

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_SelectableWidget)

// C-11 SelectableWidget mixin: initial state, setSelected triggers callback
// idempotently, handleClick fires _onActivated.
TEST_CASE(selectablewidget_initial_state) {
    SelectableWidget w;
    CHECK_FALSE(w.isSelected());
    CHECK(w.isEnabled());
    CHECK(w.getState() == ButtonState::Normal);
}

TEST_CASE(selectablewidget_set_selected_toggles_and_callbacks) {
    SelectableWidget w;
    int cbCount = 0;
    bool lastVal = false;
    w.setOnSelectionChanged([&](bool s) { ++cbCount; lastVal = s; });

    w.setSelected(true);
    CHECK(w.isSelected());
    CHECK(cbCount == 1);
    CHECK(lastVal);

    w.setSelected(true);   // idempotent
    CHECK(cbCount == 1);

    w.setSelected(false);
    CHECK_FALSE(w.isSelected());
    CHECK(cbCount == 2);
    CHECK_FALSE(lastVal);
}

TEST_CASE(selectablewidget_hover_changes_state_machine) {
    SelectableWidget w;
    CHECK(w.getState() == ButtonState::Normal);

    UIMouseEvent e(FVector2(50.0f, 12.0f), 0);
    w.setSize(FVector2(100.0f, 24.0f));
    w.setPosition(FVector2(0.0f, 0.0f));
    w.performLayout();

    w.onMouseMove(e);
    CHECK(w.getState() == ButtonState::Hovered);
}

TEST_SUITE_END

