#include "AYTest.h"
#include "AYUI/ScrollView.h"
#include "AYUI/ScrollableWidget.h"
#include "AYUI/UIManager.h"
#include "AYUI/WidgetFactory.h"
#include "AYUI/MockRenderer.h"

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_ScrollView)

// PR-Container-Shared-Contract: ScrollView's vertical scrollbar callback
// must clamp through the canonical ScrollableWidget::scrollBy path
// (sub-cut 1.2). Drives both the scrollBy() entry AND the bar's
// onValueChanged callback to confirm they share the same clamp.
//
// The ScrollView ctor calls ensureBarsCreated() which wires the bar
// callback. _scrollState._contentSize starts (0,0); we set it via the
// public setContentSize() so the clamp math has a non-zero maxScroll.
TEST_CASE(scrollview_scrollby_and_bar_drive_share_clamp) {
    auto* sv = new ScrollView();
    sv->setSize(FVector2(200.0f, 100.0f));   // viewport
    sv->setContentSize(FVector2(200.0f, 500.0f));  // maxOffset = 400 px

    // 1) scrollBy() clamps delta to maxOffset (400 px).
    sv->scrollBy(FVector2(0.0f, 9999.0f));
    CHECK_FLOAT_EQ(sv->getScrollOffset().y, 400.0f, 1e-5f);

    // 2) Negative scrollBy clamps offset back to 0.
    sv->scrollBy(FVector2(0.0f, -500.0f));
    CHECK_FLOAT_EQ(sv->getScrollOffset().y, 0.0f, 1e-5f);

    // 3) Bar callback path: the vbar's onValueChanged lambda was
    // installed in ScrollView ctor → ensureBarsCreated(). It must
    // clamp the same way (sub-cut 1.2 unification).
    ScrollBar* vbar = sv->getVerticalScrollBar();
    CHECK_NOT_NULL(vbar);
    vbar->setValue(9999.0f);
    CHECK_FLOAT_EQ(sv->getScrollOffset().y, 400.0f, 1e-5f);

    // PR-SyncVerticalBar: scrollBy() triggers syncBarsToOffset() internally,
    // so the bar's value tracks the scroll state's offset. Reset to 0
    // then scroll a non-clamped delta to force the sync path to run.
    sv->scrollBy(FVector2(0.0f, -9999.0f));    // state → 0
    sv->scrollBy(FVector2(0.0f, 250.0f));      // state → 250, bar re-synced
    CHECK_FLOAT_EQ(sv->getVerticalScrollBar()->getValue(),
                   sv->getScrollOffset().y, 1e-5f);

    delete sv;
}

// =============================================================================
// UI-anim cut 2 — wheel momentum. applyWheel keeps the immediate scroll
// AND seeds a velocity; tick() glides it down while decaying.
// =============================================================================

TEST_CASE(scrollview_wheel_sets_velocity_and_glides) {
    auto* sv = new ScrollView();
    sv->setSize(FVector2(200.0f, 100.0f));
    sv->setContentSize(FVector2(200.0f, 1000.0f));   // maxOffset = 900

    // Wheel still moves immediately (pre-momentum semantics preserved).
    UIMouseWheelEvent e(FVector2(100.0f, 50.0f), 120.0f);
    CHECK(sv->onMouseWheel(e));
    CHECK_FLOAT_EQ(sv->getScrollOffset().y, 120.0f, 1e-5f);

    // Glide: velocity decays with a ~0.23s half-life; 10 frames at 16ms
    // still moves the offset past the wheel position.
    for (int i = 0; i < 10; ++i) sv->tick(0.016f);
    CHECK(sv->getScrollOffset().y > 120.0f);

    // Eventually parks; never over the boundary, never backwards.
    float last = sv->getScrollOffset().y;
    int backwardStepCount = 0;
    int overflowStepCount = 0;
    for (int i = 0; i < 400; ++i) {
        sv->tick(0.016f);
        const float y = sv->getScrollOffset().y;
        if (y < last) {
            ++backwardStepCount;
        }
        if (y > 900.0f) {
            ++overflowStepCount;
        }
        last = y;
    }
    CHECK(backwardStepCount == 0);
    CHECK(overflowStepCount == 0);
    const float parked = sv->getScrollOffset().y;
    sv->tick(0.016f);
    CHECK_FLOAT_EQ(sv->getScrollOffset().y, parked, 1e-5f);

    delete sv;
}

TEST_CASE(scrollview_momentum_stops_at_max_offset) {
    auto* sv = new ScrollView();
    sv->setSize(FVector2(200.0f, 100.0f));
    sv->setContentSize(FVector2(200.0f, 500.0f));    // maxOffset = 400

    // Oversized wheel: immediate clamp to 400, velocity still seeded —
    // but the next glide frame parks at the boundary and zeroes speed.
    UIMouseWheelEvent e(FVector2(100.0f, 50.0f), 9999.0f);
    CHECK(sv->onMouseWheel(e));
    CHECK_FLOAT_EQ(sv->getScrollOffset().y, 400.0f, 1e-5f);

    sv->tick(0.016f);
    CHECK_FLOAT_EQ(sv->getScrollOffset().y, 400.0f, 1e-5f);
    sv->tick(0.016f);
    CHECK_FLOAT_EQ(sv->getScrollOffset().y, 400.0f, 1e-5f);
    CHECK_FALSE(sv->getScrollOffset().y > 400.0f);

    delete sv;
}

TEST_CASE(scrollview_momentum_cleared_by_bar_drag) {
    auto* sv = new ScrollView();
    sv->setSize(FVector2(200.0f, 100.0f));
    sv->setContentSize(FVector2(200.0f, 500.0f));

    UIMouseWheelEvent e(FVector2(100.0f, 50.0f), 120.0f);
    sv->onMouseWheel(e);
    sv->tick(0.016f);                        // glide in flight
    CHECK(sv->getScrollOffset().y > 120.0f);

    // Bar drag takes over → momentum cleared.
    sv->getVerticalScrollBar()->setValue(300.0f);
    CHECK_FLOAT_EQ(sv->getScrollOffset().y, 300.0f, 1e-5f);
    for (int i = 0; i < 100; ++i) sv->tick(0.016f);
    CHECK_FLOAT_EQ(sv->getScrollOffset().y, 300.0f, 1e-5f);

    delete sv;
}

TEST_CASE(scrollview_runtime_bar_enable_flags_control_visibility) {
    ScrollView sv;
    sv.setSize(FVector2(200.0f, 100.0f));
    sv.setHorizontalScrollBarEnabled(true);
    sv.setContentSize(FVector2(500.0f, 500.0f));
    sv.performLayout();
    CHECK(sv.getVerticalScrollBar()->isVisible());
    CHECK(sv.getHorizontalScrollBar()->isVisible());

    sv.setVerticalScrollBarEnabled(false);
    sv.setHorizontalScrollBarEnabled(false);
    CHECK_FALSE(sv.getVerticalScrollBar()->isVisible());
    CHECK_FALSE(sv.getHorizontalScrollBar()->isVisible());

    sv.setVerticalScrollBarEnabled(true);
    sv.setHorizontalScrollBarEnabled(true);
    CHECK(sv.getVerticalScrollBar()->isVisible());
    CHECK(sv.getHorizontalScrollBar()->isVisible());
}

TEST_CASE(scrollview_auto_hides_when_content_fits_and_reappears_on_overflow) {
    ScrollView sv;
    sv.setSize(FVector2(200.0f, 100.0f));

    // Vertical Auto is the default. Equal-size content does not reserve a
    // gutter or paint an inactive track.
    sv.setContentSize(FVector2(200.0f, 100.0f));
    sv.performLayout();
    CHECK_NOT_NULL(sv.getVerticalScrollBar());
    CHECK_FALSE(sv.getVerticalScrollBar()->isVisible());
    CHECK_FLOAT_EQ(sv.getClientRect().maxX, 199.0f, 1e-5f);

    sv.setContentSize(FVector2(200.0f, 240.0f));
    sv.performLayout();
    CHECK(sv.getVerticalScrollBar()->isVisible());
    CHECK_FLOAT_EQ(sv.getClientRect().maxX, 187.0f, 1e-5f);

    // Shrinking while scrolled hides the bar and clamps the stale offset.
    sv.scrollBy(FVector2(0.0f, 120.0f));
    CHECK(sv.getScrollOffset().y > 0.0f);
    sv.setContentSize(FVector2(200.0f, 80.0f));
    sv.performLayout();
    CHECK_FALSE(sv.getVerticalScrollBar()->isVisible());
    CHECK_FLOAT_EQ(sv.getScrollOffset().y, 0.0f, 1e-5f);
}

TEST_CASE(scrollview_visibility_policy_supports_always_auto_and_hidden) {
    ScrollView sv;
    sv.setSize(FVector2(200.0f, 100.0f));
    sv.setContentSize(FVector2(100.0f, 50.0f));

    sv.setVerticalScrollBarVisibility(ScrollView::ScrollBarVisibility::Always);
    sv.performLayout();
    CHECK(sv.getVerticalScrollBar()->isVisible());

    sv.setVerticalScrollBarVisibility(ScrollView::ScrollBarVisibility::Hidden);
    sv.setContentSize(FVector2(100.0f, 500.0f));
    sv.performLayout();
    CHECK_FALSE(sv.getVerticalScrollBar()->isVisible());

    sv.setVerticalScrollBarVisibility(ScrollView::ScrollBarVisibility::Auto);
    sv.performLayout();
    CHECK(sv.getVerticalScrollBar()->isVisible());
}

TEST_CASE(scrollview_auto_visibility_resolves_cross_axis_overflow) {
    ScrollView sv;
    sv.setSize(FVector2(100.0f, 100.0f));
    sv.setHorizontalScrollBarVisibility(ScrollView::ScrollBarVisibility::Auto);

    // Height overflow first enables V. Its 12px gutter reduces width from
    // 100 to 88, which makes the 95px content overflow horizontally too.
    sv.setContentSize(FVector2(95.0f, 101.0f));
    sv.performLayout();
    CHECK(sv.getVerticalScrollBar()->isVisible());
    CHECK(sv.getHorizontalScrollBar()->isVisible());
    CHECK_FLOAT_EQ(sv.getVerticalScrollBar()->getHeight(), 88.0f, 1e-5f);
    CHECK_FLOAT_EQ(sv.getHorizontalScrollBar()->getWidth(), 88.0f, 1e-5f);

    // Recompute from the no-gutter baseline when content shrinks; neither
    // previously-visible bar may keep the other one alive.
    sv.setContentSize(FVector2(80.0f, 80.0f));
    sv.performLayout();
    CHECK_FALSE(sv.getVerticalScrollBar()->isVisible());
    CHECK_FALSE(sv.getHorizontalScrollBar()->isVisible());
}

TEST_SUITE_END
