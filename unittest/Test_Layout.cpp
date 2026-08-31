#include "AYTest.h"
#include "AYMath/MathUtils.h"
#include "AYUI/Box.h"
#include "AYUI/SplitterHandle.h"
#include "AYUI/Image.h"
#include "AYUI/Button.h"
#include "AYUI/MockRenderer.h"
#include "AYUI/Window.h"
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

TEST_CASE(test_hbox_hidden_panel_reclaims_space_for_fill) {
    HBox hbox;
    hbox.setSize(FVector2(800.0f, 400.0f));
    hbox.setSpacing(0.0f);
    hbox.setPadding(0.0f, 0.0f, 0.0f, 0.0f);

    Window left;
    SplitterHandle split;
    Image fill;
    hbox.addWidget(&left, 200.0f);
    hbox.addWidget(&split, SplitterHandle::kDefaultWidth);
    hbox.addWidget(&fill, 0.0f);
    hbox.performLayout();

    const float fillWidthVisible = fill.getWidth();
    CHECK(fillWidthVisible > 500.0f);

    left.setVisible(false);
    split.setVisible(false);
    hbox.performLayout();

    CHECK(fill.getWidth() > fillWidthVisible);
    CHECK_FLOAT_EQ(fill.getWorldBounds().minX, 0.0f, 1e-5f);
}

TEST_CASE(test_vbox_hidden_child_reclaims_fill_height) {
    VBox vbox;
    vbox.setSize(FVector2(400.0f, 300.0f));
    vbox.setSpacing(0.0f);
    vbox.setPadding(0.0f, 0.0f, 0.0f, 0.0f);

    Widget top;
    Widget fill;
    top.setSize(FVector2(100.0f, 80.0f));
    vbox.addWidget(&top, 80.0f);
    vbox.addWidget(&fill, 0.0f);
    vbox.performLayout();

    const float fillHeightVisible = fill.getHeight();
    CHECK(fillHeightVisible > 200.0f);

    top.setVisible(false);
    vbox.performLayout();

    CHECK(fill.getHeight() > fillHeightVisible);
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

TEST_CASE(test_hbox_splitter_drag_respects_min_panel_size) {
    HBox hbox;
    hbox.setSize(FVector2(800.0f, 400.0f));
    hbox.setSpacing(0.0f);
    hbox.setPadding(0.0f, 0.0f, 0.0f, 0.0f);

    Window left;
    Window center;
    Window right;
    SplitterHandle splitLeft;
    SplitterHandle splitRight;
    hbox.addWidget(&left, 220.0f);
    hbox.addWidget(&splitLeft, SplitterHandle::kDefaultWidth);
    hbox.addWidget(&center, 0.0f);   // fill
    hbox.addWidget(&splitRight, SplitterHandle::kDefaultWidth);
    hbox.addWidget(&right, 200.0f);
    hbox.rebindSplitters();
    hbox.performLayout();

    // Crush Left toward zero via its right-hand splitter.
    const float splitLX =
        (splitLeft.getWorldBounds().minX + splitLeft.getWorldBounds().maxX) * 0.5f;
    CHECK(splitLeft.onMouseButtonDown(UIMouseEvent(FVector2(splitLX, 200.0f), 0)));
    CHECK(splitLeft.onMouseMove(UIMouseEvent(FVector2(splitLX - 500.0f, 200.0f), 0)));
    CHECK(splitLeft.onMouseButtonUp(UIMouseEvent(FVector2(splitLX - 500.0f, 200.0f), 0)));
    CHECK(left.getWidth() >= BoxBase::kMinPanelSize - 0.5f);

    // Grow Right until Center (fill) would collapse — Center must keep min.
    hbox.setSlotSize(0, 220.0f);
    hbox.setSlotSize(4, 200.0f);
    hbox.performLayout();
    const float splitRX =
        (splitRight.getWorldBounds().minX + splitRight.getWorldBounds().maxX) * 0.5f;
    CHECK(splitRight.onMouseButtonDown(UIMouseEvent(FVector2(splitRX, 200.0f), 0)));
    CHECK(splitRight.onMouseMove(UIMouseEvent(FVector2(splitRX - 700.0f, 200.0f), 0)));
    CHECK(splitRight.onMouseButtonUp(UIMouseEvent(FVector2(splitRX - 700.0f, 200.0f), 0)));
    CHECK(center.getWidth() >= BoxBase::kMinPanelSize - 0.5f);
    CHECK(left.getWidth() >= BoxBase::kMinPanelSize - 0.5f);
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
    int nonRectCallCount = 0;
    for (const auto& dc : renderer.getDrawCalls()) {
        if (dc.type != MockRenderer::DrawCall::Rect) {
            ++nonRectCallCount;
        }
    }
    CHECK(nonRectCallCount == 0);

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

TEST_CASE(splitter_tick_preserves_base_widget_animation) {
    SplitterHandle split;
    split.animateOpacity(0.0f, 200.0f, AnimationCurve::EaseOut);
    CHECK(split.isOpacityAnimating());

    // No hover is active, so this also covers the old early-return path.
    split.tick(0.10f);
    CHECK(split.getOpacity() > 0.0f);
    CHECK(split.getOpacity() < 1.0f);

    split.tick(0.10f);
    CHECK_FALSE(split.isOpacityAnimating());
    CHECK_FLOAT_EQ(split.getOpacity(), 0.0f, 1e-5f);
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
    int unexpectedRevealCount = 0;
    for (int i = 0; i < 5; ++i) {
        split->tick(0.05f);
        if (split->isRevealed()) {
            ++unexpectedRevealCount;
        }
    }
    CHECK(unexpectedRevealCount == 0);

    MockRenderer renderer;
    split->render(renderer);
    CHECK(renderer.getDrawCalls().empty());

    delete split;
}

// PR-B3 hotfix — VBox::getPreferredContentSize() sums the natural
// heights of visible children (not the VBox's own viewport-bound
// size). This is what ScrollView's auto-derive uses to compute the
// scrollable extent. Without this, a content-fills-viewport VBox
// would always report contentSize = viewport, leaving the scroll
// offset permanently at 0.
//
// Note: getPreferredContentSize() must be called BEFORE performLayout()
// because VBox::layoutChildren stretches children to fillHeight, after
// which the children's getSize().y is the stretched value, not the
// natural one. In production, ScrollView calls performLayout AFTER
// capturing the preferred content size, in the right order so the
// pre-layout natural sizes are visible to getPreferredContentSize().
TEST_CASE(vbox_preferred_content_size_sums_visible_children) {
    VBox outer;
    outer.setSize(FVector2(320.0f, 110.0f));   // small viewport
    outer.setPadding(0, 0, 0, 0);
    outer.setSpacing(0);

    // Two children with fixed heights. VBox will lay them out at
    // 110 / 2 = 55 each (fillHeight), but their natural stacking
    // should be 30 + 50 = 80 + spacing.
    auto* a = new ayt::ui::Widget();
    a->setSize(FVector2(320.0f, 30.0f));
    auto* b = new ayt::ui::Widget();
    b->setSize(FVector2(320.0f, 50.0f));
    outer.addWidget(a);
    outer.addWidget(b);

    // Capture preferred size BEFORE layout — children are still at
    // their natural heights (30 + 50 = 80).
    const FVector2 pref = outer.getPreferredContentSize();
    CHECK_FLOAT_EQ(pref.y, 80.0f, 1e-3f);
    CHECK_FLOAT_EQ(pref.x, 320.0f, 1e-3f);

    outer.performLayout();
    // After layout the VBox fills its viewport; children are stretched.
    CHECK(outer.getSize().y == 110.0f);
    CHECK(a->getSize().y == 55.0f);
    CHECK(b->getSize().y == 55.0f);

    delete a;
    delete b;
}

// PR-B3 hotfix — VBox with hidden children. Hidden children should
// not contribute to preferred content size (a page-switch scenario:
// switching to a tall page hides the other pages, and the visible
// page's natural height should be the only thing in the stack).
TEST_CASE(vbox_preferred_content_size_ignores_hidden) {
    VBox outer;
    outer.setSize(FVector2(320.0f, 600.0f));
    outer.setPadding(0, 0, 0, 0);
    outer.setSpacing(10.0f);

    auto* visible = new ayt::ui::Widget();
    visible->setSize(FVector2(320.0f, 200.0f));
    auto* hidden = new ayt::ui::Widget();
    hidden->setSize(FVector2(320.0f, 999.0f));
    outer.addWidget(visible);
    outer.addWidget(hidden);
    hidden->setVisible(false);

    // Capture before layout. Visible=200, hidden skipped → 200.
    const FVector2 pref = outer.getPreferredContentSize();
    CHECK_FLOAT_EQ(pref.y, 200.0f, 1e-3f);

    delete visible;
    delete hidden;
}

// PR-B3 hotfix (Bug #4 follow-up) — preferred content size MUST
// report the natural stacked height (sum of children's natural
// heights) even AFTER the VBox has stretched its children to fill
// the viewport. Previously, getPreferredContentSize returned the
// stretched sum, which made a ScrollView wrapping a content-fills-
// viewport VBox conclude "no overflow, scrollbar disabled" — even
// when the visible page's true content (text + widgets) was
// genuinely taller than the viewport.
//
// The fix caches the natural height in VBox::layoutChildren BEFORE
// applying fillHeight stretches, and getPreferredContentSize
// returns the cached value.
TEST_CASE(vbox_preferred_content_size_after_layout_uses_natural_not_stretched) {
    VBox outer;
    outer.setSize(FVector2(320.0f, 100.0f));   // small viewport
    outer.setPadding(0, 0, 0, 0);
    outer.setSpacing(0);

    // Two children with fixed natural heights 30 + 50 = 80.
    auto* a = new ayt::ui::Widget();
    a->setSize(FVector2(320.0f, 30.0f));
    auto* b = new ayt::ui::Widget();
    b->setSize(FVector2(320.0f, 50.0f));
    outer.addWidget(a);
    outer.addWidget(b);

    outer.performLayout();   // stretches both to 50 each (fillHeight)
    CHECK(a->getSize().y == 50.0f);
    CHECK(b->getSize().y == 50.0f);

    // After layout, preferred size should still report the natural
    // 80 — not the stretched 100 (= viewport).
    const FVector2 pref = outer.getPreferredContentSize();
    CHECK_FLOAT_EQ(pref.y, 80.0f, 1e-3f);

    delete a;
    delete b;
}

// PR-B3 hotfix (Bug #4 follow-up) — nested VBox: outer walks into
// inner via walkNaturalHeight which prefers the inner's cached
// natural height. Without the cache, walkNaturalHeight recurses
// into the inner's stretched children and reports the inner's
// stretched (viewport-bound) height — the very bug this commit fixes.
TEST_CASE(vbox_preferred_content_size_walks_into_nested_vbox_using_natural) {
    VBox outer;
    outer.setSize(FVector2(320.0f, 200.0f));   // outer viewport
    outer.setPadding(0, 0, 0, 0);
    outer.setSpacing(0);

    auto* inner = new VBox();
    inner->setSize(FVector2(320.0f, 50.0f));   // inner viewport
    inner->setPadding(0, 0, 0, 0);
    inner->setSpacing(0);

    // Inner's natural children: 30 + 70 = 100. After layout in
    // viewport 50, both stretched to 25 each (stretched sum = 50).
    auto* x = new ayt::ui::Widget();
    x->setSize(FVector2(320.0f, 30.0f));
    auto* y = new ayt::ui::Widget();
    y->setSize(FVector2(320.0f, 70.0f));
    inner->addWidget(x);
    inner->addWidget(y);
    inner->performLayout();
    CHECK(x->getSize().y == 25.0f);
    CHECK(y->getSize().y == 25.0f);
    // Inner's cached natural height should be 100.
    CHECK_FLOAT_EQ(inner->getCachedNaturalHeight(), 100.0f, 1e-3f);

    outer.addWidget(inner);
    outer.performLayout();

    // Outer's preferred size should be 100 (inner's natural), not
    // 200 (inner's stretched-to-outer-viewport height).
    const FVector2 pref = outer.getPreferredContentSize();
    CHECK_FLOAT_EQ(pref.y, 100.0f, 1e-3f);

    delete x;
    delete y;
    delete inner;
}

// Gallery Backend first-entry: wireBackendPage addWidget(demo, 900) on a
// hidden page after it was already laid out. Stale _naturalHeight made
// ScrollView think there was no overflow until a later relayout.
TEST_CASE(vbox_add_widget_invalidates_natural_cache) {
    VBox page;
    page.setPadding(0, 0, 0, 0);
    page.setSpacing(0);
    page.setSize(FVector2(200.0f, 80.0f));

    auto* hdr = new ayt::ui::Widget();
    hdr->setSize(FVector2(200.0f, 24.0f));
    page.addWidget(hdr, 24.0f);
    page.performLayout();
    CHECK_FLOAT_EQ(page.getCachedNaturalHeight(), 24.0f, 1e-3f);

    auto* demo = new ayt::ui::Widget();
    demo->setSize(FVector2(200.0f, 900.0f));
    page.addWidget(demo, 900.0f);

    CHECK(page.getCachedNaturalHeight() < 0.0f);
    CHECK_FLOAT_EQ(page.getPreferredContentSize().y, 924.0f, 1e-3f);

    delete hdr;
    delete demo;
}

TEST_CASE(vbox_spacing_and_padding_invalidate_natural_cache) {
    VBox page;
    page.setSize(FVector2(200.0f, 80.0f));
    page.setPadding(0, 0, 0, 0);
    page.setSpacing(0);

    auto* a = new Widget();
    auto* b = new Widget();
    a->setSize(FVector2(200.0f, 20.0f));
    b->setSize(FVector2(200.0f, 30.0f));
    page.addWidget(a, 20.0f);
    page.addWidget(b, 30.0f);
    page.performLayout();
    CHECK_FLOAT_EQ(page.getCachedNaturalHeight(), 50.0f, 1e-3f);

    page.setSpacing(7.0f);
    CHECK(page.getCachedNaturalHeight() < 0.0f);
    CHECK_FLOAT_EQ(page.getPreferredContentSize().y, 57.0f, 1e-3f);

    page.performLayout();
    page.setPadding(1.0f, 2.0f, 3.0f, 4.0f);
    CHECK(page.getCachedNaturalHeight() < 0.0f);
    CHECK_FLOAT_EQ(page.getPreferredContentSize().y, 63.0f, 1e-3f);

    delete a;
    delete b;
}

TEST_SUITE_END
