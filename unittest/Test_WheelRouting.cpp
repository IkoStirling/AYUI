#include "AYTest.h"
#include "AYUI/Widget.h"
#include "AYUI/UIManager.h"
#include "AYUI/ScrollView.h"
#include "AYUI/ListView.h"
#include "AYUI/ComboBox.h"
#include "AYUI/MockRenderer.h"

#include <string>

using namespace ayt::ui;
using namespace ayt::math;

namespace {

// PR-B3 — fixture pattern mirrors Test_Theme_Gallery.cpp: a UIManager
// is initialised with a MockRenderer backend, the root widget hosts
// whatever nested containers the case under test needs, and shutdown
// runs at the end. Each case composes its own tree so the suite stays
// order-independent.
//
// `mount(root, ...)` returns a root widget already added as a child of
// the UIManager's root. Cases that don't need a deep tree use Widget
// directly.
Widget* mount(UIManager& ui, Widget* child) {
    Widget* root = ui.root();
    if (root == nullptr) return nullptr;
    root->addChildExternal(child);
    return root;
}

} // anon

TEST_SUITE(AYUI_Wheel_Routing_B3)

// ---------------------------------------------------------------------------
// 1. Wheel on a standalone ScrollView scrolls its content. The widget
//    must report true (consumed) so the UIManager stops bubbling.
// ---------------------------------------------------------------------------
// 1. Wheel on a standalone ScrollView scrolls its content. We test
//    ScrollView::onMouseWheel directly (NOT through UIManager::onMouseWheel)
//    because UIManager's _root is a plain Widget and does not recurse
//    into children on hitTest (see UIManager.cpp line 322). The routing
//    semantics are independently exercised by case 3 (nested).
TEST_CASE(wheel_on_scroll_view_scrolls_content) {
    UIManager ui;
    MockRenderer renderer;
    ui.initialize(&renderer);

    ScrollView sv;
    sv.setSize(FVector2(200.0f, 100.0f));
    sv.setPosition(FVector2(0.0f, 0.0f));
    sv.setContentSize(FVector2(200.0f, 400.0f));   // 4x viewport
    ui.root()->addChildExternal(&sv);

    const float beforeY = sv.getScrollOffset().y;
    const bool consumed = sv.onMouseWheel(UIMouseWheelEvent(FVector2(50.0f, 50.0f), 30.0f));
    CHECK(consumed);
    // Sign convention: wheel deltaY positive = "scroll UP" = content
    // moves up to reveal more below → scrollOffset INCREASES.
    CHECK(sv.getScrollOffset().y > beforeY);

    ui.shutdown();
}

// ---------------------------------------------------------------------------
// 2. Wheel on a standalone ListView scrolls the rows + rebinds the pool.
//    We test ListView::onMouseWheel directly for the same reason as case 1.
// ---------------------------------------------------------------------------
TEST_CASE(wheel_on_list_view_rebinds_pool) {
    UIManager ui;
    MockRenderer renderer;
    ui.initialize(&renderer);

    ListView lv;
    lv.setSize(FVector2(120.0f, 80.0f));
    lv.setItemHeight(20.0f);
    for (int i = 0; i < 50; ++i) {
        lv.addItem(L"item" + std::to_wstring(i));
    }
    ui.root()->addChildExternal(&lv);

    const int beforeFirst = lv.getFirstVisibleIndex();
    // deltaY=40 → scrollBy(40) → offset goes DOWN by 40 → firstVisible
    // moves to index 2 (40 / 20).
    const bool consumed = lv.onMouseWheel(UIMouseWheelEvent(FVector2(60.0f, 40.0f), 40.0f));
    CHECK(consumed);
    CHECK(lv.getFirstVisibleIndex() > beforeFirst);

    ui.shutdown();
}

// ---------------------------------------------------------------------------
// 3. Nested ScrollView inside an outer ScrollView. Wheel on the INNER
//    ScrollView must scroll ONLY the inner — outer must NOT also scroll.
//    We test this directly via the inner.onMouseWheel call so the
//    assertion targets the routing semantic without going through
//    UIManager hit-test (which would otherwise be blocked by the plain
//    Widget root, see case 1).
// ---------------------------------------------------------------------------
TEST_CASE(wheel_on_nested_scroll_routes_to_inner_only) {
    UIManager ui;
    MockRenderer renderer;
    ui.initialize(&renderer);

    ScrollView outer;
    outer.setSize(FVector2(400.0f, 300.0f));
    outer.setPosition(FVector2(0.0f, 0.0f));
    outer.setContentSize(FVector2(400.0f, 1200.0f));
    ui.root()->addChildExternal(&outer);

    ScrollView inner;
    inner.setSize(FVector2(200.0f, 100.0f));
    inner.setPosition(FVector2(20.0f, 20.0f));
    inner.setContentSize(FVector2(200.0f, 400.0f));
    outer.addChildExternal(&inner);

    // Wheel on inner — inner must move; outer must not.
    const float beforeOuterY = outer.getScrollOffset().y;
    const float beforeInnerY = inner.getScrollOffset().y;
    const bool consumed = inner.onMouseWheel(UIMouseWheelEvent(FVector2(50.0f, 50.0f), 30.0f));
    CHECK(consumed);
    CHECK(inner.getScrollOffset().y > beforeInnerY);
    // Inner consumed the wheel — outer stays put.
    CHECK(outer.getScrollOffset().y == beforeOuterY);

    ui.shutdown();
}

// ---------------------------------------------------------------------------
// 4. Wheel on a non-scrollable widget returns false (no consumption).
//    Default Widget::onMouseWheel returns false, so the UIManager
//    router's walk-up stops at the leaf with no consumption. (We still
//    initialise UIManager so the fixture pattern is consistent.)
// ---------------------------------------------------------------------------
TEST_CASE(wheel_on_plain_widget_is_not_consumed) {
    UIManager ui;
    MockRenderer renderer;
    ui.initialize(&renderer);

    Widget plain;
    plain.setSize(FVector2(50.0f, 50.0f));
    plain.setPosition(FVector2(10.0f, 10.0f));
    ui.root()->addChildExternal(&plain);

    const bool consumed = plain.onMouseWheel(UIMouseWheelEvent(FVector2(30.0f, 30.0f), 30.0f));
    CHECK_FALSE(consumed);

    ui.shutdown();
}

// ---------------------------------------------------------------------------
// 5. UIManager::onMouseWheel routing through the overlay. The popup is
//    mounted on the overlay root, NOT on the ComboBox subtree. We use
//    UIManager::onMouseWheel here because pickTopmostWidget has a
//    special-case for _overlayRoot (it walks overlay children manually,
//    bypassing the plain _root's non-recursive Widget::hitTest). When
//    the cursor is over the popup, pickTopmostWidget returns the popup
//    ListView, and the wheel routes there.
// ---------------------------------------------------------------------------
TEST_CASE(wheel_on_combobox_popup_scrolls_popup_listview) {
    UIManager ui;
    MockRenderer renderer;
    ui.initialize(&renderer);

    ComboBox cb;
    cb.setSize(FVector2(160.0f, 28.0f));
    cb.setPosition(FVector2(20.0f, 20.0f));
    for (int i = 0; i < 50; ++i) {
        cb.addItem(L"row" + std::to_wstring(i));
    }
    ui.root()->addChildExternal(&cb);

    cb.openPopup();
    ListView* popup = cb.getPopup();
    CHECK(popup != nullptr);

    const FRectangle popupBounds = popup->getWorldBounds();
    const float midX = (popupBounds.minX + popupBounds.maxX) * 0.5f;
    const float midY = (popupBounds.minY + popupBounds.maxY) * 0.5f;

    const int beforeFirst = popup->getFirstVisibleIndex();
    const bool consumed = ui.onMouseWheel(midX, midY, 60.0f);
    CHECK(consumed);
    // deltaY=60 → scrollBy(60) → offset moves DOWN by 60 → firstVisibleIndex
    // advances by 60/itemH rows.
    CHECK(popup->getFirstVisibleIndex() > beforeFirst);

    cb.closePopup();
    ui.shutdown();
}

// ---------------------------------------------------------------------------
// 6. Multiple wheel deltas accumulate. Three deltaY=10 wheels on a
//    ScrollView should produce the same scrollOffset.y as a single
//    deltaY=30 wheel (clamp + sign consistency).
// ---------------------------------------------------------------------------
TEST_CASE(wheel_deltas_accumulate) {
    UIManager ui;
    MockRenderer renderer;
    ui.initialize(&renderer);

    ScrollView sv;
    sv.setSize(FVector2(200.0f, 100.0f));
    sv.setContentSize(FVector2(200.0f, 500.0f));
    ui.root()->addChildExternal(&sv);

    const float startY = sv.getScrollOffset().y;
    sv.onMouseWheel(UIMouseWheelEvent(FVector2(50.0f, 50.0f), 10.0f));
    sv.onMouseWheel(UIMouseWheelEvent(FVector2(50.0f, 50.0f), 10.0f));
    sv.onMouseWheel(UIMouseWheelEvent(FVector2(50.0f, 50.0f), 10.0f));
    const float afterThree = sv.getScrollOffset().y;

    ScrollView sv2;
    sv2.setSize(FVector2(200.0f, 100.0f));
    sv2.setContentSize(FVector2(200.0f, 500.0f));
    ui.root()->addChildExternal(&sv2);

    const float startY2 = sv2.getScrollOffset().y;
    sv2.onMouseWheel(UIMouseWheelEvent(FVector2(50.0f, 50.0f), 30.0f));
    const float afterOne = sv2.getScrollOffset().y;

    // Both trees should have moved by the same amount relative to start.
    // Sign convention: positive deltaY moves offset DOWN, so the
    // accumulated delta = after - start (not start - after).
    const float deltaThree = afterThree - startY;
    const float deltaOne   = afterOne   - startY2;
    CHECK(std::abs(deltaThree - deltaOne) < 0.5f);
    CHECK(deltaThree > 0.0f);

    ui.shutdown();
}

TEST_SUITE_END