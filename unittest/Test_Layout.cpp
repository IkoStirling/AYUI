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

    // Render should not crash and should visit every slot. With the
    // splitter visual refactor (invisible at rest, hover reveals it),
    // a non-hovered SplitterHandle emits ZERO draw calls — the plain
    // Widget a/c also emit zero because their base onRender is empty.
    // This verifies that HBox::render's single-pass walk still visits
    // every slot in insertion order without crashing.
    MockRenderer renderer;
    hbox->render(renderer);
    CHECK(renderer.getDrawCalls().empty());

    // ~HBox no longer deletes children (Phase UI-OWN-1). a, splitter, and c
    // were allocated with new Widget()/new SplitterHandle() and must be freed
    // explicitly — destroyWidgetTree handles that since they are tree-owned.
    destroyWidgetTree(hbox);
}

// SplitterHandle visual refactor: at rest the splitter is invisible
// (no draw calls). The previous design always painted a dark grey rect
// that competed visually with adjacent Window panels — VSCode / UE /
// Unity hide the splitter at rest and reveal it on hover instead.
TEST_CASE(splitter_render_invisible_at_rest) {
    SplitterHandle* split = new SplitterHandle();
    split->setSize(FVector2(4.0f, 100.0f));
    split->setPosition(FVector2(0.0f, 0.0f));

    MockRenderer renderer;
    split->render(renderer);
    CHECK(renderer.getDrawCalls().empty());

    delete split;
}

// SplitterHandle visual refactor: hovering fills the splitter width with
// the accent color AND draws a 2px grab handle down the center (so the
// user sees "drag me"). Expected draw count = 2 (1 fill rect + 1 grab
// handle rect). The VSCode-style hover reveal delay (kHoverRevealDelay,
// 150ms) means a freshly-hovered splitter is invisible until enough time
// has elapsed — we tick() past the delay before rendering.
TEST_CASE(splitter_render_hover_emits_fill_and_grab_handle) {
    SplitterHandle* split = new SplitterHandle();
    split->setSize(FVector2(4.0f, 200.0f));
    split->setPosition(FVector2(0.0f, 0.0f));

    // Drive _hover=true via the public mouse API rather than poking
    // private state. UIMouseEvent at (0,0) — only matters that it lands
    // inside the splitter bounds for onMouseMove to flip _hover.
    UIMouseEvent hover(FVector2(2.0f, 100.0f));
    split->onMouseMove(hover);

    // Right after hover, before the delay elapses: still invisible.
    MockRenderer rendererBefore;
    split->render(rendererBefore);
    CHECK(rendererBefore.getDrawCalls().empty());

    // Tick past the 150ms reveal delay (use 0.20s for a safety margin).
    split->tick(0.20f);

    MockRenderer renderer;
    split->render(renderer);
    CHECK(renderer.getDrawCalls().size() == 2u);
    for (const auto& dc : renderer.getDrawCalls()) {
        CHECK(dc.type == MockRenderer::DrawCall::Rect);
    }

    // The first draw call must cover the full splitter bounds (the
    // accent fill). The second is the 2px grab handle inset by 4px
    // on each end — verify the grab handle's width is much smaller
    // than the splitter width.
    const auto& calls = renderer.getDrawCalls();
    CHECK(calls[0].bounds.minX <= 0.0f + 0.001f);
    CHECK(calls[0].bounds.maxX >= 4.0f - 0.001f);
    const float grabWidth = calls[1].bounds.maxX - calls[1].bounds.minX;
    CHECK(grabWidth < 4.0f);
    CHECK(grabWidth > 0.0f);

    delete split;
}

// SplitterHandle visual refactor: a hovered splitter that loses mouse
// (onMouseLeave) returns to invisible immediately — even mid-delay.
// The drag-state path keeps the visual active while dragging.
TEST_CASE(splitter_render_mouse_leave_returns_to_invisible) {
    SplitterHandle* split = new SplitterHandle();
    split->setSize(FVector2(4.0f, 100.0f));
    split->setPosition(FVector2(0.0f, 0.0f));

    UIMouseEvent hover(FVector2(2.0f, 50.0f));
    split->onMouseMove(hover);
    split->tick(0.20f);  // past the reveal delay

    MockRenderer renderer1;
    split->render(renderer1);
    CHECK(renderer1.getDrawCalls().size() == 2u);

    split->onMouseLeave();

    MockRenderer renderer2;
    split->render(renderer2);
    CHECK(renderer2.getDrawCalls().empty());

    delete split;
}

// SplitterHandle hover-reveal delay: hovering but not yet at the delay
// is invisible. The tick(dt) drives _hoverElapsed forward. Re-entering
// resets the delay counter (so back-to-back hovers don't accumulate).
TEST_CASE(splitter_hover_reveal_delay_threshold) {
    SplitterHandle* split = new SplitterHandle();
    split->setSize(FVector2(4.0f, 100.0f));
    split->setPosition(FVector2(0.0f, 0.0f));

    UIMouseEvent hover(FVector2(2.0f, 50.0f));
    split->onMouseMove(hover);

    // Below threshold: invisible.
    split->tick(SplitterHandle::kHoverRevealDelay - 0.01f);
    CHECK(!split->isRevealed());
    MockRenderer rendererBelow;
    split->render(rendererBelow);
    CHECK(rendererBelow.getDrawCalls().empty());

    // Cross the threshold: revealed.
    split->tick(0.02f);  // total = (delay - 0.01) + 0.02 = delay + 0.01
    CHECK(split->isRevealed());
    MockRenderer rendererAbove;
    split->render(rendererAbove);
    CHECK(rendererAbove.getDrawCalls().size() == 2u);

    // Leave resets immediately.
    split->onMouseLeave();
    CHECK(!split->isRevealed());

    // Re-enter: counter starts at 0 again — must NOT inherit the prior
    // accumulated time.
    split->onMouseMove(hover);
    split->tick(SplitterHandle::kHoverRevealDelay - 0.01f);
    CHECK(!split->isRevealed());

    delete split;
}

// SplitterHandle leave path via tick-only (no leave call): the UIManager
// drives tick() every frame, but the cursor may stop moving for a while.
// After leave, the only way for the splitter to forget _hover is via
// onMouseLeave. If UIManager's updateHoverWidget misses firing leave
// (e.g. hit returns the same widget for two consecutive moves because
// some descendant claims it), the splitter would stay highlighted until
// the next explicit leave call. This test pins the standalone leave path
// under explicit UIMouseEvent ordering that mirrors the UIManager flow.
TEST_CASE(splitter_leave_after_reveal_stays_invisible_across_ticks) {
    SplitterHandle* split = new SplitterHandle();
    split->setSize(FVector2(4.0f, 100.0f));
    split->setPosition(FVector2(0.0f, 0.0f));

    UIMouseEvent hover(FVector2(2.0f, 50.0f));
    split->onMouseMove(hover);
    split->tick(0.20f);  // past delay
    CHECK(split->isRevealed());

    // leave
    split->onMouseLeave();
    CHECK(!split->isRevealed());

    // Subsequent ticks must NOT re-reveal — _hover=false and onMouseMove
    // isn't being called again.
    for (int i = 0; i < 5; ++i) {
        split->tick(0.05f);
        CHECK(!split->isRevealed());
    }

    MockRenderer renderer;
    split->render(renderer);
    CHECK(renderer.getDrawCalls().empty());

    delete split;
}

TEST_SUITE_END

