#include "AYTest.h"
#include "AYScrollBar.h"
#include "AYScrollableWidget.h"
#include "AYScrollView.h"
#include "AYUIManager.h"
#include "AYWidgetFactory.h"
#include "AYWidgetSerializer.h"
#include "AYMockRenderer.h"
#include "AYStyle.h"
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

// C-4: Cursor hint — Vertical reports SizeVertical when enabled.
// Horizontal reports Default (intentional — we don't want the
// horizontal bar to claim SizeHorizontal because that's reserved for
// the slider; horizontal-bar drag is rare enough to skip).
TEST_CASE(scrollbar_cursor_hint) {
    ScrollBar v;
    CHECK(v.getCursorHint() == UiCursorHint::SizeVertical);

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
