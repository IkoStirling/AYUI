#include "AYTest.h"
#include "AYScrollView.h"
#include "AYScrollableWidget.h"
#include "AYUIManager.h"
#include "AYWidgetFactory.h"
#include "AYMockRenderer.h"

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

TEST_SUITE_END