#include "AYTest.h"
#include "AYMath/MathUtils.h"
#include "AYWidget.h"
#include "AYButton.h"
#include <iostream>

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_Widget)

TEST_CASE(test_widget_tree) {
    // R-6: a Widget that hosts children MUST be a CompoundWidget. After
    // R-6, Widget::hitTest only checks self; CompoundWidget::hitTest
    // descends into children. Using a raw Widget here would silently
    // route child hits to the root itself, which is the bug the refactor
    // exists to make obvious.
    Widget* root = new CompoundWidget();
    root->setSize(FVector2(100.0f, 100.0f));

    Widget* child1 = new Widget();
    child1->setPosition(FVector2(10.0f, 10.0f));
    child1->setSize(FVector2(50.0f, 50.0f));

    Widget* child2 = new Widget();
    child2->setPosition(FVector2(70.0f, 70.0f));
    child2->setSize(FVector2(30.0f, 30.0f));

    root->addChild(child1);
    root->addChild(child2);

    CHECK(root->getChildren().size() == 2);
    CHECK(child1->getParent() == root);
    CHECK(child2->getParent() == root);

    FRectangle rootBounds = root->getWorldBounds();
    CHECK_FLOAT_EQ(rootBounds.minX, 0.0f, 1e-5f);
    CHECK_FLOAT_EQ(rootBounds.minY, 0.0f, 1e-5f);
    CHECK_FLOAT_EQ(rootBounds.maxX, 100.0f, 1e-5f);
    CHECK_FLOAT_EQ(rootBounds.maxY, 100.0f, 1e-5f);

    FRectangle child1Bounds = child1->getWorldBounds();
    CHECK_FLOAT_EQ(child1Bounds.minX, 10.0f, 1e-5f);
    CHECK_FLOAT_EQ(child1Bounds.minY, 10.0f, 1e-5f);
    CHECK_FLOAT_EQ(child1Bounds.maxX, 60.0f, 1e-5f);
    CHECK_FLOAT_EQ(child1Bounds.maxY, 60.0f, 1e-5f);

    Widget* hit = root->hitTest(FVector2(30.0f, 30.0f));
    CHECK(hit == child1);

    hit = root->hitTest(FVector2(80.0f, 80.0f));
    CHECK(hit == child2);

    hit = root->hitTest(FVector2(5.0f, 5.0f));
    CHECK(hit == root);

    hit = root->hitTest(FVector2(120.0f, 120.0f));
    CHECK(hit == nullptr);

    child1->detachFromParent();
    CHECK(root->getChildren().size() == 1);
    CHECK(child1->getParent() == nullptr);

    // Phase UI-OWN-1: ~Widget() does not delete children. child1 was detached
    // and must be freed explicitly; child2 is still attached and goes with root.
    delete child1;
    destroyWidgetTree(root);
}

TEST_CASE(test_button_events) {
    Button* button = new Button();
    button->setSize(FVector2(100.0f, 32.0f));
    button->setText(L"Click Me");

    bool clicked = false;
    button->setOnClicked([&]() { clicked = true; });

    UIMouseEvent moveEvt(FVector2(50.0f, 16.0f), 0);
    bool handled = button->onMouseMove(moveEvt);
    CHECK(handled);
    CHECK(button->getState() == ButtonState::Hovered);

    UIMouseEvent downEvt(FVector2(50.0f, 16.0f), 0);
    handled = button->onMouseButtonDown(downEvt);
    CHECK(handled);
    CHECK(button->getState() == ButtonState::Pressed);

    UIMouseEvent upEvt(FVector2(50.0f, 16.0f), 0);
    handled = button->onMouseButtonUp(upEvt);
    CHECK(handled);
    CHECK(clicked);

    UIMouseEvent leaveEvt(FVector2(200.0f, 200.0f), 0);
    handled = button->onMouseMove(leaveEvt);
    CHECK(!handled);
    CHECK(button->getState() == ButtonState::Normal);

    delete button;
}

TEST_SUITE_END
