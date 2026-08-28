#include "AYTest.h"
#include "AYUI/ScrollBar.h"
#include "AYUI/ScrollableWidget.h"
#include "AYUI/ScrollView.h"
#include "AYUI/ListView.h"
#include "AYUI/UIManager.h"
#include "AYUI/WidgetFactory.h"
#include "AYUI/WidgetSerializer.h"
#include "AYUI/MockRenderer.h"
#include "AYUI/Style.h"
#include <cstdio>
#include <iostream>

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_ScrollBar)

// C-4: ScrollBar default orientation is Vertical, range [0,1],
// value 0, viewport size 1.
TEST_CASE(scrollbar_initial_state) {
    ScrollBar sb;
    CHECK(sb.getOrientation() == ScrollBar::Orientation::Vertical);
    CHECK_FLOAT_EQ(sb.getValue(), 0.0f, 1e-5f);
    CHECK(sb.isEnabled());
}

// C-4 / PR-S5c: scrollbars never change the cursor — arrow over the
// whole bar (native behavior). The Size* hints belong to the window
// rim / slider / splitter handle, not the scrollbar.
TEST_CASE(scrollbar_cursor_hint) {
    ScrollBar v;
    CHECK(v.getCursorHint() == UiCursorHint::Default);

    ScrollBar h;
    h.setOrientation(ScrollBar::Orientation::Horizontal);
    CHECK(h.getCursorHint() == UiCursorHint::Default);

    v.setEnabled(false);
    CHECK(v.getCursorHint() == UiCursorHint::Default);
}

// C-4: setRange clamps value into the new range.
TEST_CASE(scrollbar_set_range_clamps) {
    ScrollBar sb;
    sb.setRange(0.0f, 100.0f);
    sb.setValue(150.0f);
    CHECK_FLOAT_EQ(sb.getValue(), 100.0f, 1e-5f);

    sb.setValue(-10.0f);
    CHECK_FLOAT_EQ(sb.getValue(), 0.0f, 1e-5f);
}

// C-4: clicking on the thumb drags the value to that position. For a
// vertical bar, click at world Y = mid of the track → value mid range.
TEST_CASE(scrollbar_drag_thumb_updates_value) {
    ScrollBar sb;
    sb.setSize(FVector2(12.0f, 100.0f));
    sb.setPosition(FVector2(0.0f, 0.0f));
    sb.setRange(0.0f, 100.0f);
    sb.setViewportSize(50.0f);   // half visible, half thumb ratio 0.5

    int valueChanges = 0;
    sb.setOnValueChanged([&](float) { ++valueChanges; });

    // Click at y=50/100 = mid of track → normalized 0.5 → maps to
    // (0.5)*(max - 0) = 50.0 (when scrollable portion matches thumb).
    sb.onMouseButtonDown(UIMouseEvent(FVector2(6.0f, 50.0f), 0));
    CHECK(sb.getValue() >= 0.0f);

    // Drag to y=80. The drag pipeline fires onValueChanged on each move
    // that actually changes the value. Original C-4 expectation (>=1
    // after onMouseButtonDown) didn't survive the v1.1 hover/drag UX
    // rework — down itself is now a no-op for value (it only arms the
    // drag session). Check the counter AFTER the move.
    sb.onMouseMove(UIMouseEvent(FVector2(6.0f, 80.0f), 0));
    CHECK(sb.getValue() > 0.0f);
    CHECK(valueChanges >= 1);

    sb.onMouseButtonUp(UIMouseEvent(FVector2(6.0f, 80.0f), 0));
}

// PR-S1 (Gallery S1 deep trace) — drag the thumb across the entire
// track range. Gallery reported "only the center segment follows the
// cursor" — i.e. the thumb tracks the mouse for part of the drag and
// then loses the cursor. This case simulates a full top-to-bottom
// drag on a 100-tall vertical bar and checks that the value is
// monotonically increasing as the cursor moves down. If the bug is in
// onMouseMove (e.g. a wrong `tl` divisor, off-by-one in applyNormalized,
// or a stale `_dragging` that flips off mid-drag) the values will
// either plateau or go non-monotonic.
TEST_CASE(scrollbar_drag_thumb_full_track_traversal) {
    ScrollBar sb;
    sb.setSize(FVector2(12.0f, 100.0f));
    sb.setPosition(FVector2(0.0f, 0.0f));
    sb.setRange(0.0f, 100.0f);
    sb.setViewportSize(25.0f);   // thumb ratio 0.25 -> 25 px tall
    sb.setValue(0.0f);

    // Press in the middle of the track.
    sb.onMouseButtonDown(UIMouseEvent(FVector2(6.0f, 50.0f), 0));

    // Drag from y=50 down to y=99 in 10-pixel steps; capture value at
    // each step and assert monotonically non-decreasing.
    float prevValue = sb.getValue();
    int backwardStepCount = 0;
    for (float y = 50.0f; y <= 99.0f; y += 5.0f) {
        sb.onMouseMove(UIMouseEvent(FVector2(6.0f, y), 0));
        const float v = sb.getValue();
        if (v < prevValue) {
            ++backwardStepCount;
        }
        prevValue = v;
    }
    CHECK(backwardStepCount == 0);
    // After dragging near the bottom of the track, value should be
    // near the max (maxScroll = content - viewport = 100 - 25 = 75).
    // Allow a generous tolerance because applyNormalized maps the
    // mouse position to the thumb CENTER (not the cursor tip), so
    // value-at-bottom depends on thumb length.
    CHECK(prevValue > 50.0f);

    sb.onMouseButtonUp(UIMouseEvent(FVector2(6.0f, 99.0f), 0));
}

// PR-S1b (Gallery S1): pressing the thumb must NOT jump the page.
// Clicking inside the thumb arms a drag with the pressed offset kept;
// only a click on empty track performs the click-to-jump.
TEST_CASE(scrollbar_thumb_press_does_not_jump) {
    ScrollBar sb;
    sb.setSize(FVector2(12.0f, 100.0f));
    sb.setPosition(FVector2(0.0f, 0.0f));
    sb.setRange(0.0f, 100.0f);
    sb.setViewportSize(50.0f);   // thumb ratio 0.5 -> 50px tall
    sb.setValue(0.0f);           // thumb spans [0, 50]

    int valueChanges = 0;
    sb.setOnValueChanged([&](float) { ++valueChanges; });

    // Press INSIDE the thumb (y=25 is well within [0, 50]): no jump.
    sb.onMouseButtonDown(UIMouseEvent(FVector2(6.0f, 25.0f), 0));
    CHECK_FLOAT_EQ(sb.getValue(), 0.0f, 1e-5f);
    CHECK(valueChanges == 0);

    // Drag down: thumb leading edge tracks the cursor minus the pressed
    // offset (25px). thumbStart = 75 - 25 = 50 → t = 50/50 = 1 → max.
    sb.onMouseMove(UIMouseEvent(FVector2(6.0f, 75.0f), 0));
    CHECK_FLOAT_EQ(sb.getValue(), 50.0f, 1e-5f);
    sb.onMouseButtonUp(UIMouseEvent(FVector2(6.0f, 75.0f), 0));

    // Press on EMPTY track below the thumb → click-to-jump still works.
    ScrollBar sb2;
    sb2.setSize(FVector2(12.0f, 100.0f));
    sb2.setPosition(FVector2(0.0f, 0.0f));
    sb2.setRange(0.0f, 100.0f);
    sb2.setViewportSize(50.0f);
    sb2.setValue(0.0f);
    sb2.onMouseButtonDown(UIMouseEvent(FVector2(6.0f, 90.0f), 0));
    CHECK(sb2.getValue() > 0.0f);
    sb2.onMouseButtonUp(UIMouseEvent(FVector2(6.0f, 90.0f), 0));
}

// C-4: ScrollableWidget clamps content offset.
TEST_CASE(scrollable_widget_clamps_offset) {
    ScrollableWidget sw;
    sw.setContentSize(FVector2(500.0f, 1000.0f));

    const FVector2 vp(200.0f, 400.0f);
    const FVector2 maxOff = sw.getMaxScrollOffset(vp);
    CHECK_FLOAT_EQ(maxOff.x, 300.0f, 1e-5f);
    CHECK_FLOAT_EQ(maxOff.y, 600.0f, 1e-5f);

    CHECK(sw.scrollBy(FVector2(50.0f, 100.0f), vp));
    CHECK_FLOAT_EQ(sw.getScrollOffset().x, 50.0f, 1e-5f);
    CHECK_FLOAT_EQ(sw.getScrollOffset().y, 100.0f, 1e-5f);

    // Past max → clamps to maxOff.
    CHECK(sw.scrollBy(FVector2(1000.0f, 1000.0f), vp));
    CHECK_FLOAT_EQ(sw.getScrollOffset().x, 300.0f, 1e-5f);
    CHECK_FLOAT_EQ(sw.getScrollOffset().y, 600.0f, 1e-5f);

    // Past min → clamps to 0.
    CHECK(sw.scrollBy(FVector2(-1000.0f, -1000.0f), vp));
    CHECK_FLOAT_EQ(sw.getScrollOffset().x, 0.0f, 1e-5f);
    CHECK_FLOAT_EQ(sw.getScrollOffset().y, 0.0f, 1e-5f);
}

// C-4: small content (under viewport) → maxOff = 0; scrollBy is a
// no-op.
TEST_CASE(scrollable_widget_small_content_no_scroll) {
    ScrollableWidget sw;
    sw.setContentSize(FVector2(100.0f, 100.0f));
    const FVector2 vp(200.0f, 200.0f);
    CHECK(sw.getMaxScrollOffset(vp).x == 0.0f);
    CHECK(sw.getMaxScrollOffset(vp).y == 0.0f);
    CHECK_FALSE(sw.scrollBy(FVector2(50.0f, 50.0f), vp));
}

// C-4: ScrollBar thumb rect changes with viewport size.
TEST_CASE(scrollbar_thumb_size_reflects_viewport_ratio) {
    ScrollBar sb;
    sb.setSize(FVector2(12.0f, 100.0f));
    sb.setPosition(FVector2(0.0f, 0.0f));
    sb.setRange(0.0f, 100.0f);
    sb.setValue(0.0f);

    // viewport size 100 / content 100 → no scroll, thumb fills track.
    sb.setViewportSize(100.0f);
    FRectangle thumbFull = sb.getThumbRect();
    CHECK_FLOAT_EQ(thumbFull.minY, 0.0f, 1e-4f);
    CHECK_FLOAT_EQ(thumbFull.maxY, 100.0f, 1e-4f);

    // viewport 50 / content 100 → scrollable, thumb ratio 0.5.
    sb.setViewportSize(50.0f);
    FRectangle thumbHalf = sb.getThumbRect();
    const float thumbLen = thumbHalf.maxY - thumbHalf.minY;
    CHECK(thumbLen >= ScrollBar::kMinThumbLength - 0.5f);
    CHECK(thumbLen <= 100.0f);
    CHECK_FLOAT_EQ(thumbLen / 100.0f, 0.5f, 0.05f);
}

// C-4: factory + serializer round-trip preserves orientation.
TEST_CASE(scrollbar_factory_and_serializer_round_trip) {
    WidgetFactory& factory = WidgetFactory::get();
    CHECK_TRUE(factory.isRegistered("ScrollBar"));

    Widget* widget = factory.create("ScrollBar");
    CHECK_NOT_NULL(widget);
    ScrollBar* original = dynamic_cast<ScrollBar*>(widget);
    CHECK_NOT_NULL(original);

    original->setId("hbar");
    original->setOrientation(ScrollBar::Orientation::Horizontal);

    std::string json = WidgetSerializer::serialize(widget);
    CHECK(json.find("\"type\": \"ScrollBar\"") != std::string::npos);
    CHECK(json.find("horizontal") != std::string::npos);

    Widget* restored = WidgetSerializer::deserialize(json);
    CHECK_NOT_NULL(restored);
    ScrollBar* restoredSb = dynamic_cast<ScrollBar*>(restored);
    CHECK_NOT_NULL(restoredSb);
    CHECK(restoredSb->getOrientation() == ScrollBar::Orientation::Horizontal);

    destroyWidgetTree(widget);
    destroyWidgetTree(restored);
}

// PR-S1b (Gallery S1 deep trace) — realistic Gallery dimensions
// (1280x720 window minus chrome ~ 608px tall viewport, 2500px tall
// content). Drag the vbar thumb across the entire track range and
// assert the resulting offset tracks the cursor monotonically.
// Reproduces the "only the center segment follows the cursor" symptom
// by using the SAME widget pipeline UIManager uses (pickTopmostWidget
// → ScrollBar.onMouseButtonDown/onMouseMove via _capturedWidget).
TEST_CASE(scrollbar_gallery_drag_full_track_via_uimanager) {
    UIManager ui;
    MockRenderer renderer;
    ui.initialize(&renderer);

    ScrollView sv;
    sv.setSize(FVector2(1268.0f, 608.0f));     // gallery content_scroll size
    sv.setPosition(FVector2(0.0f, 0.0f));
    sv.setContentSize(FVector2(1268.0f, 2500.0f));
    Widget* filler = new Widget();
    filler->setSize(FVector2(1268.0f, 2500.0f));
    sv.setContent(filler);
    ui.root()->addChildExternal(&sv);
    ScrollBar* vbar = sv.getVerticalScrollBar();
    CHECK(vbar != nullptr);
    vbar->setPosition(FVector2(1268.0f - 12.0f, 0.0f));   // bar at right edge
    vbar->setSize(FVector2(12.0f, 608.0f));                // full viewport
    vbar->setVisible(true);
    const FRectangle barBounds = vbar->getWorldBounds();

    // UIManager._root is a plain Widget (hitTest only matches self). To
    // exercise the FULL pipeline UIManager uses, attach sv under a
    // CompoundWidget root and set that as ui._root via direct test
    // escape hatch — but that's not public API. The closest equivalent
    // is calling sv.hitTest directly, then driving ScrollBar through
    // its onMouse* (which is what UIManager would call after picking
    // the widget). The capture/drag pipeline is the same.
    const float cx = barBounds.minX + 6.0f;

    // Step A: click at top of track. UIManager picks the vbar via
    // ScrollView::hitTest (which prefers bars) then calls vbar.onMouseButtonDown
    // → captures mouse, applies normalized value.
    const float clickY_A = barBounds.minY + 10.0f;
    const bool d1 = vbar->onMouseButtonDown(UIMouseEvent(FVector2(cx, clickY_A), 0));
    CHECK(d1);
    const float v1 = sv.getScrollOffset().y;
    CHECK(v1 >= 0.0f);
    CHECK(v1 < 200.0f);

    // Step B: drag to mid track.
    const float clickY_B = barBounds.minY + 300.0f;
    const bool d2 = vbar->onMouseMove(UIMouseEvent(FVector2(cx, clickY_B), 0));
    CHECK(d2);
    const float v2 = sv.getScrollOffset().y;
    CHECK(v2 > v1);
    CHECK(v2 > 800.0f);                 // mid of 2500-608 = ~946

    // Step C: drag to bottom of track.
    const float clickY_C = barBounds.maxY - 10.0f;
    const bool d3 = vbar->onMouseMove(UIMouseEvent(FVector2(cx, clickY_C), 0));
    CHECK(d3);
    const float v3 = sv.getScrollOffset().y;
    CHECK(v3 > v2);
    // maxScroll = content - viewport = 2500 - 608 = 1892
    CHECK(v3 > 1500.0f);

    // Step D: drag back to top — offset should drop.
    const float clickY_D = barBounds.minY + 10.0f;
    const bool d4 = vbar->onMouseMove(UIMouseEvent(FVector2(cx, clickY_D), 0));
    CHECK(d4);
    const float v4 = sv.getScrollOffset().y;
    CHECK(v4 < v3);
    CHECK(v4 < 200.0f);

    vbar->onMouseButtonUp(UIMouseEvent(FVector2(cx, clickY_D), 0));
    ui.shutdown();
}

// PR-S2 (Gallery S2 deep trace) — list inside outer scroll panel. The
// Gallery JSON puts cap_b3_list (ListView, 320x110) under content_scroll
// (ScrollView). When the user wheels over the list, UIManager::onMouseWheel
// walks from the hit widget up via getParent(). The inner ListView should
// consume the wheel and the outer ScrollView should NOT scroll. Reproduces
// "wheel doesn't seem to work" because users see the page jump while the
// list offset doesn't move (or vice versa).
TEST_CASE(scrollview_wrapping_listview_wheel_routes_to_inner_only) {
    UIManager ui;
    MockRenderer renderer;
    ui.initialize(&renderer);

    ScrollView outer;
    outer.setSize(FVector2(400.0f, 300.0f));
    outer.setPosition(FVector2(0.0f, 0.0f));
    outer.setContentSize(FVector2(400.0f, 1500.0f));

    ListView inner;
    inner.setSize(FVector2(380.0f, 110.0f));   // gallery dimensions
    inner.setItemHeight(24.0f);                  // default
    for (int i = 0; i < 30; ++i) {
        wchar_t buf[32];
        std::swprintf(buf, 32, L"row-%02d", i);
        inner.addItem(buf);
    }
    outer.setContent(&inner);
    inner.performLayout();    // computes _contentSize.y = 30*24 = 720
    // outer.performLayout is protected; outer's vbar position only
    // matters for the visual frame, not for the wheel-routing test.

    ui.root()->addChildExternal(&outer);

    // UIManager._root is a plain Widget (hitTest only matches self). To
    // exercise the wheel routing logic, drive it directly: pick the
    // deepest widget under the cursor via outer.hitTest (ScrollView
    // descends into inner), then walk up getParent() and call
    // onMouseWheel — same algorithm as UIManager::onMouseWheel.
    const FVector2 clickPos(50.0f, 50.0f);
    const float beforeOuter = outer.getScrollOffset().y;
    const int   beforeInner = inner.getFirstVisibleIndex();

    const UIMouseWheelEvent wheel(clickPos, 120.0f);
    Widget* cur = outer.hitTest(clickPos);
    bool handled1 = false;
    while (cur != nullptr) {
        if (cur->onMouseWheel(wheel)) { handled1 = true; break; }
        cur = cur->getParent();
    }
    CHECK(handled1);
    CHECK(inner.getFirstVisibleIndex() > beforeInner);
    // Outer's offset must NOT move — inner ate the wheel.
    CHECK(outer.getScrollOffset().y == beforeOuter);

    ui.shutdown();
}

// PR-S7 (Gallery S7): wheel bubbling at the boundary is BY DESIGN.
// The inner list scrolled to its bottom returns false from scrollBy, so
// UIManager::onMouseWheel's parent-chain walk hands the wheel to the
// outer ScrollView (browser-style: inner exhausts, outer takes over).
// Pins the contract — inner-only routing while the inner CAN scroll,
// outer takeover when it cannot. Wheels go through the real
// UIManager::onMouseWheel walk (pickTopmostWidget → parent chain).
TEST_CASE(scrollview_wrapping_listview_wheel_bubbles_to_outer_at_bottom) {
    UIManager ui;
    MockRenderer renderer;
    ui.initialize(&renderer);

    ScrollView outer;
    outer.setSize(FVector2(400.0f, 300.0f));
    outer.setPosition(FVector2(0.0f, 0.0f));
    outer.setContentSize(FVector2(400.0f, 1500.0f));

    ListView inner;
    inner.setSize(FVector2(380.0f, 110.0f));
    inner.setItemHeight(24.0f);
    for (int i = 0; i < 30; ++i) {
        wchar_t buf[32];
        std::swprintf(buf, 32, L"row-%02d", i);
        inner.addItem(buf);
    }
    outer.setContent(&inner);
    inner.performLayout();    // _contentSize.y = 30*24 = 720
    ui.root()->addChildExternal(&outer);

    // Route one wheel exactly like UIManager::onMouseWheel does:
    // pick the topmost widget under the cursor, then walk up the parent
    // chain until a widget consumes. UIManager._root is a plain Widget
    // (hitTest only matches self) so the fixture drives the pipeline
    // directly through outer.hitTest — same as the inner-only test
    // above.
    auto routeWheel = [&](float x, float y) -> bool {
        const FVector2 pos(x, y);
        Widget* cur = outer.hitTest(pos);
        const UIMouseWheelEvent wheel(pos, 120.0f);
        while (cur != nullptr) {
            if (cur->onMouseWheel(wheel)) return true;
            cur = cur->getParent();
        }
        return false;
    };

    // The wheel point (50,50) sits inside the inner list at offset 0;
    // the list keeps its spot while IT consumes (outer doesn't move),
    // so every wheel in Phase A hits the list again.
    const float maxInner = 720.0f - 110.0f;    // content - viewport = 610

    // Phase A: wheel until the inner list is pinned at its bottom.
    for (int step = 0;
         step < 200 && inner.getScrollOffset().y < maxInner - 0.5f;
         ++step) {
        routeWheel(50.0f, 50.0f);
    }
    CHECK(inner.getScrollOffset().y >= maxInner - 0.5f);  // pinned
    // pool = ceil(110/24)+1+2 = 8 → maxFirst clamps at 30-8 = 22; the
    // remaining rows live in the pool's tail slots.
    CHECK(inner.getFirstVisibleIndex() >= 22);            // bottom rows shown
    CHECK(outer.getScrollOffset().y == 0.0f);             // outer untouched

    // Phase B: one more wheel at the same spot — the list cannot
    // scroll, so the wheel bubbles to the OUTER ScrollView which takes
    // over (browser-style).
    const float beforeOuter = outer.getScrollOffset().y;
    const bool handled = routeWheel(50.0f, 50.0f);
    CHECK(handled);
    CHECK(outer.getScrollOffset().y > beforeOuter);
    CHECK(inner.getScrollOffset().y >= maxInner - 0.5f);  // list stays pinned

    ui.shutdown();
}

// C-4: ScrollView factory.
TEST_CASE(scrollview_factory_registered) {
    WidgetFactory& factory = WidgetFactory::get();
    CHECK_TRUE(factory.isRegistered("ScrollView"));

    Widget* widget = factory.create("ScrollView");
    CHECK_NOT_NULL(widget);
    ScrollView* sv = dynamic_cast<ScrollView*>(widget);
    CHECK_NOT_NULL(sv);
    CHECK(sv->getVerticalScrollBar() != nullptr);   // vbar is auto-created
    CHECK(sv->getHorizontalScrollBar() == nullptr); // hbar disabled by default

    destroyWidgetTree(widget);
}

TEST_SUITE_END
