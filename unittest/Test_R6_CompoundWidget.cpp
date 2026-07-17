#include "AYTest.h"
#include "aymath/MathUtils.h"
#include "AYWidget.h"
#include "AYInteractiveWidget.h"
#include "AYButton.h"

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_R6_HoverOwnership)

// R-6: Widget::hitTest must check self only. Adding children to a raw Widget
// no longer makes those children hit-testable through the parent. This is
// the data-model honesty the refactor bought us — only CompoundWidget (or
// any subclass that overrides hitTest) can host a hit-tested child.
TEST_CASE(leaf_widget_hittest_self_only) {
    Widget* leaf = new Widget();
    leaf->setSize(FVector2(100.0f, 100.0f));
    leaf->setPosition(FVector2(0.0f, 0.0f));

    // Stuff a child into the leaf widget. R-6 makes this configuration
    // semantically wrong: the parent is a leaf, not a container.
    Widget* child = new Widget();
    child->setSize(FVector2(50.0f, 50.0f));
    child->setPosition(FVector2(10.0f, 10.0f));
    leaf->addChild(child);

    // Inside the child's bounds — leaf's hitTest must return the leaf
    // itself, NOT the child. (Previously the base-default hitTest descended
    // and returned the child. After R-6 the leaf is honest about its role.)
    Widget* hit = leaf->hitTest(FVector2(30.0f, 30.0f));
    CHECK(hit == leaf);

    // Outside the leaf's own bounds — null.
    hit = leaf->hitTest(FVector2(150.0f, 150.0f));
    CHECK(hit == nullptr);

    delete leaf;
    delete child;
}

// R-6: CompoundWidget::hitTest still descends into children (the previous
// behavior). This is the regression guard for "containers still work".
TEST_CASE(compound_widget_hittest_descends_into_children) {
    CompoundWidget* root = new CompoundWidget();
    root->setSize(FVector2(200.0f, 200.0f));

    Widget* child = new Widget();
    child->setSize(FVector2(50.0f, 50.0f));
    child->setPosition(FVector2(20.0f, 20.0f));
    root->addChild(child);

    Widget* hit = root->hitTest(FVector2(40.0f, 40.0f));
    CHECK(hit == child);

    // Inside the container but outside the child — root itself.
    hit = root->hitTest(FVector2(150.0f, 150.0f));
    CHECK(hit == root);

    // Outside the container — null.
    hit = root->hitTest(FVector2(250.0f, 250.0f));
    CHECK(hit == nullptr);

    delete child;
    destroyWidgetTree(root);
}

// R-6 (B4 fix): when a CompoundWidget's onMouseLeave fires, the leave
// must propagate to all descendants so InteractiveWidget children's
// transient hover/press state clears up. Before R-6, only the previously
// hovered child (tracked via _hoverWidget) got notified — descendants
// that were never "the hovered child" kept stale hover flags.
TEST_CASE(compound_onmouseleave_clears_descendant_interactive_flags) {
    CompoundWidget* root = new CompoundWidget();
    root->setSize(FVector2(400.0f, 400.0f));
    root->setPosition(FVector2(0.0f, 0.0f));

    Button* btn = new Button();
    btn->setSize(FVector2(100.0f, 32.0f));
    btn->setPosition(FVector2(10.0f, 10.0f));
    root->addChild(btn);

    // Hover the button to put it in Hovered state.
    btn->onMouseMove(UIMouseEvent(FVector2(50.0f, 20.0f), 0));
    CHECK(btn->isMouseOver());
    CHECK(btn->getState() == ButtonState::Hovered);

    // Mouse "leaves" the container. R-6: CompoundWidget::onMouseLeave
    // walks every descendant and calls onMouseLeave on each.
    root->onMouseLeave();

    CHECK(btn->isMouseOver() == false);
    CHECK(btn->getState() == ButtonState::Normal);

    delete btn;
    destroyWidgetTree(root);
}

// R-6: a Widget that hosts an InteractiveWidget child still works when
// the hit-test path goes through UIManager-shaped code: hit goes to the
// child, child receives onMouseMove, child transitions to Hovered. The
// CompoundWidget that wraps it does not need its own _hoverWidget field
// to keep InteractiveWidget's transient state in sync.
TEST_CASE(compound_hittest_into_interactive_child) {
    CompoundWidget* root = new CompoundWidget();
    root->setSize(FVector2(400.0f, 400.0f));

    Button* btn = new Button();
    btn->setSize(FVector2(100.0f, 32.0f));
    btn->setPosition(FVector2(20.0f, 20.0f));
    root->addChild(btn);

    Widget* hit = root->hitTest(FVector2(50.0f, 30.0f));
    CHECK(hit == btn);
    CHECK(hit->onMouseMove(UIMouseEvent(FVector2(50.0f, 30.0f), 0)));
    CHECK(btn->isMouseOver());
    CHECK(btn->getState() == ButtonState::Hovered);

    delete btn;
    destroyWidgetTree(root);
}

TEST_SUITE_END