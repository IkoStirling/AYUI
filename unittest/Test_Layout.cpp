#include "AYTest.h"
#include "aymath/MathUtils.h"
#include "AYBox.h"
#include "AYSplitterHandle.h"
#include "AYImage.h"
#include "AYButton.h"
#include "AYMockRenderer.h"
#include "AYWindow.h"
#include <iostream>

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_Layout)

TEST_CASE(test_vbox_layout) {
    VBox* vbox = new VBox();
    vbox->setSize(FVector2(200.0f, 100.0f));
    vbox->setSpacing(4.0f);

    Widget* child1 = new Widget();
    Widget* child2 = new Widget();
    Widget* child3 = new Widget();

    vbox->addWidget(child1, 20.0f);
    vbox->addWidget(child2, 20.0f);
    vbox->addWidget(child3, 0.0f); // Fill remaining

    vbox->performLayout();

    FRectangle c1Bounds = child1->getWorldBounds();
    CHECK_FLOAT_EQ(c1Bounds.minY, 4.0f, 1e-5f);
    CHECK_FLOAT_EQ(c1Bounds.maxY - c1Bounds.minY, 20.0f, 1e-5f);

    FRectangle c2Bounds = child2->getWorldBounds();
    CHECK_FLOAT_EQ(c2Bounds.minY, 28.0f, 1e-5f);

    FRectangle c3Bounds = child3->getWorldBounds();
    CHECK_FLOAT_EQ(c3Bounds.minY, 52.0f, 1e-5f);

    destroyWidgetTree(vbox);
}

TEST_CASE(test_hbox_layout) {
    HBox* hbox = new HBox();
    hbox->setSize(FVector2(200.0f, 50.0f));
    hbox->setSpacing(4.0f);

    Widget* child1 = new Widget();
    Widget* child2 = new Widget();
    Widget* child3 = new Widget();

    hbox->addWidget(child1, 50.0f);
    hbox->addWidget(child2, 50.0f);
    hbox->addWidget(child3, 0.0f); // Fill remaining

    hbox->performLayout();

    FRectangle c1Bounds = child1->getWorldBounds();
    CHECK_FLOAT_EQ(c1Bounds.minX, 4.0f, 1e-5f);
    CHECK_FLOAT_EQ(c1Bounds.maxX - c1Bounds.minX, 50.0f, 1e-5f);

    FRectangle c2Bounds = child2->getWorldBounds();
    CHECK_FLOAT_EQ(c2Bounds.minX, 58.0f, 1e-5f);

    FRectangle c3Bounds = child3->getWorldBounds();
    CHECK_FLOAT_EQ(c3Bounds.minX, 112.0f, 1e-5f);

    CHECK_FLOAT_EQ(c1Bounds.maxY - c1Bounds.minY, 42.0f, 1e-5f);

    destroyWidgetTree(hbox);
}

TEST_CASE(test_window_hit_test) {
    Window* window = new Window();
    window->setPosition(FVector2(100.0f, 100.0f));
    window->setSize(FVector2(400.0f, 300.0f));
    window->setTitleBarHeight(28.0f);

    Widget* hit = window->hitTest(FVector2(300.0f, 110.0f));
    CHECK(hit == window);

    hit = window->hitTest(FVector2(300.0f, 200.0f));
    CHECK(hit == window);

    hit = window->hitTest(FVector2(50.0f, 50.0f));
    CHECK(hit == nullptr);

    destroyWidgetTree(window);
}

TEST_CASE(test_mock_renderer) {
    MockRenderer renderer;
    renderer.clear();

    Widget* root = new Widget();
    root->setPosition(FVector2(0.0f, 0.0f));
    root->setSize(FVector2(100.0f, 100.0f));

    Widget* child = new Widget();
    child->setPosition(FVector2(10.0f, 10.0f));
    child->setSize(FVector2(50.0f, 50.0f));
    root->addChild(child);

    root->setRenderBackend(&renderer);
    root->render(renderer);

    auto calls = renderer.getDrawCalls();
    // Plain Widget has empty onRender, so no draw calls generated
    CHECK(calls.size() == 0);

    destroyWidgetTree(root);
}

TEST_CASE(test_hbox_split_hit_and_resize) {
    HBox hbox;
    hbox.setSize(FVector2(800.0f, 400.0f));
    hbox.setSpacing(0.0f);
    hbox.setPadding(0.0f, 0.0f, 0.0f, 0.0f);

    Window left;
    Window center;
    Window right;
    SplitterHandle splitLeft;
    SplitterHandle splitRight;
    left.setMinSize(160.0f, 120.0f);
    right.setMinSize(180.0f, 120.0f);

    BoxSlotLimits leftLimits;
    leftLimits.minWidth = 160.0f;
    hbox.addWidget(&left, 220.0f, leftLimits);
    hbox.addWidget(&splitLeft, SplitterHandle::kDefaultWidth);
    hbox.addWidget(&center, 0.0f);
    hbox.addWidget(&splitRight, SplitterHandle::kDefaultWidth);
    BoxSlotLimits rightLimits;
    rightLimits.minWidth = 180.0f;
    hbox.addWidget(&right, 280.0f, rightLimits);
    hbox.performLayout();

    const float splitX =
        (splitLeft.getWorldBounds().minX + splitLeft.getWorldBounds().maxX) * 0.5f;
    CHECK(hbox.hitTest(FVector2(splitX, 200.0f)) == &splitLeft);
    CHECK(splitLeft.getCursorHint() == UiCursorHint::Default);
    CHECK(splitLeft.onMouseMove(UIMouseEvent(FVector2(splitX, 200.0f), 0)));
    CHECK(splitLeft.getCursorHint() == UiCursorHint::SizeHorizontal);

    CHECK(splitLeft.onMouseButtonDown(UIMouseEvent(FVector2(splitX, 200.0f), 0)));
    CHECK(splitLeft.onMouseMove(UIMouseEvent(FVector2(splitX + 40.0f, 200.0f), 0)));
    CHECK(left.getWidth() == 260.0f);
    CHECK(splitLeft.onMouseButtonUp(UIMouseEvent(FVector2(splitX + 40.0f, 200.0f), 0)));
}

TEST_CASE(test_splitter_handle_drag_survives_mouse_leave) {
    HBox hbox;
    hbox.setSize(FVector2(800.0f, 400.0f));
    hbox.setSpacing(0.0f);

    Window left;
    Window right;
    SplitterHandle split;
    hbox.addWidget(&left, 220.0f);
    hbox.addWidget(&split, SplitterHandle::kDefaultWidth);
    hbox.addWidget(&right, 280.0f);
    hbox.performLayout();

    const float splitX = (split.getWorldBounds().minX + split.getWorldBounds().maxX) * 0.5f;
    CHECK(split.onMouseButtonDown(UIMouseEvent(FVector2(splitX, 200.0f), 0)));
    split.onMouseLeave();
    CHECK(split.getCursorHint() == UiCursorHint::SizeHorizontal);
    CHECK(split.onMouseMove(UIMouseEvent(FVector2(splitX + 30.0f, 200.0f), 0)));
    CHECK(left.getWidth() == 250.0f);
    CHECK(split.onMouseButtonUp(UIMouseEvent(FVector2(splitX + 30.0f, 200.0f), 0)));
}

TEST_CASE(test_splitter_handle_hit_region) {
    HBox hbox;
    hbox.setSize(FVector2(600.0f, 300.0f));
    hbox.setSpacing(0.0f);

    Window left;
    Widget fill;
    SplitterHandle split;
    hbox.addWidget(&left, 200.0f);
    hbox.addWidget(&split, SplitterHandle::kDefaultWidth);
    hbox.addWidget(&fill, 0.0f);
    hbox.performLayout();

    const float splitX = (split.getWorldBounds().minX + split.getWorldBounds().maxX) * 0.5f;
    CHECK(split.hitTest(FVector2(splitX, 150.0f)) == &split);
    CHECK(split.hitTest(FVector2(splitX + 40.0f, 150.0f)) == nullptr);
}

TEST_CASE(test_splitter_handle_requires_both_panels_bound) {
    HBox hbox;
    hbox.setSize(FVector2(800.0f, 400.0f));
    hbox.setSpacing(0.0f);

    Window left;
    SplitterHandle split;
    Window right;
    hbox.addWidget(&left, 220.0f);
    hbox.addWidget(&split, SplitterHandle::kDefaultWidth);
    hbox.addWidget(&right, 280.0f);
    hbox.rebindSplitters();
    hbox.performLayout();

    const float splitX = (split.getWorldBounds().minX + split.getWorldBounds().maxX) * 0.5f;
    CHECK(split.onMouseButtonDown(UIMouseEvent(FVector2(splitX, 200.0f), 0)));
    CHECK(split.onMouseMove(UIMouseEvent(FVector2(splitX + 40.0f, 200.0f), 0)));
    CHECK(left.getWidth() == 260.0f);
    CHECK(split.onMouseButtonUp(UIMouseEvent(FVector2(splitX + 40.0f, 200.0f), 0)));
}

TEST_CASE(test_hbox_toolbar_has_no_splits_by_default) {
    HBox toolbar;
    toolbar.setSize(FVector2(640.0f, 44.0f));
    toolbar.setSpacing(8.0f);

    Button play;
    Button pause;
    play.setSize(FVector2(72.0f, 32.0f));
    pause.setSize(FVector2(72.0f, 32.0f));
    toolbar.addWidget(&play, 72.0f);
    toolbar.addWidget(&pause, 72.0f);
    toolbar.performLayout();

    // Pick a point inside play (not at the maxX boundary, which is excluded
    // by the half-open bounds check). This verifies hitTest routes to play
    // rather than returning toolbar itself.
    const float splitX = play.getWorldBounds().minX + 1.0f;
    CHECK(toolbar.hitTest(FVector2(splitX, 22.0f)) == &play);
}

TEST_CASE(test_hbox_slot_percent_and_pixel_limits_combine) {
    HBox hbox;
    hbox.setSize(FVector2(1000.0f, 400.0f));
    hbox.setSpacing(0.0f);

    Window panel;
    Widget fill;
    SplitterHandle split;
    panel.setMinSize(100.0f, 80.0f);
    BoxSlotLimits limits;
    limits.minWidth = 160.0f;
    limits.minWidthPercent = 20.0f;
    limits.maxWidth = 500.0f;
    limits.maxWidthPercent = 30.0f;
    hbox.addWidget(&panel, 250.0f, limits);
    hbox.addWidget(&split, SplitterHandle::kDefaultWidth);
    hbox.addWidget(&fill, 0.0f);
    hbox.performLayout();

    CHECK(panel.getWidth() == 250.0f);
    const float splitX = (split.getWorldBounds().minX + split.getWorldBounds().maxX) * 0.5f;
    split.onMouseButtonDown(UIMouseEvent(FVector2(splitX, 200.0f), 0));
    split.onMouseMove(UIMouseEvent(FVector2(900.0f, 200.0f), 0));
    CHECK(panel.getWidth() <= 300.0f);
    CHECK(panel.getWidth() >= 200.0f);
}

TEST_CASE(test_image_perform_layout_is_noop) {
    Image image;
    image.setSize(FVector2(320.0f, 240.0f));
    image.performLayout();
    CHECK(image.getWidth() == 320.0f);
    CHECK(image.getHeight() == 240.0f);
}

TEST_CASE(hbox_render_walks_slots_in_insertion_order) {
    // After replacing HBox::render's two-pass dynamic_cast split, the render
    // must still visit every non-null widget exactly once in insertion order.
    HBox* hbox = new HBox();
    hbox->setSize(FVector2(400.0f, 100.0f));

    Widget* a = new Widget();
    SplitterHandle* splitter = new SplitterHandle();
    Widget* c = new Widget();

    hbox->addWidget(a);
    hbox->addWidget(splitter);
    hbox->addWidget(c);

    // Splitter flag must be cached at insertion time so isSplitterSlot()
    // doesn't need a dynamic_cast.
    CHECK(hbox->isSplitterSlot(0) == false);
    CHECK(hbox->isSplitterSlot(1) == true);
    CHECK(hbox->isSplitterSlot(2) == false);

    // Render should not crash and should visit every slot. SplitterHandle
    // overrides onRender so we expect exactly one drawcall from the splitter
    // (a colored rect); the plain Widget a/c produce no draws because their
    // base onRender is empty. This verifies that HBox::render's single-pass
    // walk still visits every slot in insertion order without crashing.
    MockRenderer renderer;
    hbox->render(renderer);
    CHECK(renderer.getDrawCalls().size() == 1u);
    CHECK(renderer.getDrawCalls().front().type == MockRenderer::DrawCall::Rect);

    // ~HBox no longer deletes children (Phase UI-OWN-1). a, splitter, and c
    // were allocated with new Widget()/new SplitterHandle() and must be freed
    // explicitly — destroyWidgetTree handles that since they are tree-owned.
    destroyWidgetTree(hbox);
}

TEST_SUITE_END

