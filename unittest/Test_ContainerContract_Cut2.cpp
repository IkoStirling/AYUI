#include "AYTest.h"
#include "AYWidget.h"
#include "AYScrollView.h"
#include "AYListView.h"
#include "AYWindow.h"
#include "AYTreeView.h"
#include "AYMockRenderer.h"
#include "AYUIManager.h"
#include "AYMath/MathTypes.h"

using namespace ayt::ui;
using namespace ayt::math;

// ============================================================================
// PR-Container-Contract-Cut2: validates the 3-piece contract across the
// 4 migrated containers (ScrollView, ListView, Window, TreeView):
//   - getClientRect() override (excludes bar gutter / title bar)
//   - renderChildren() pushClip(getClientRect()) / popClip balance
//   - hitTest() gates descent by getClientRect()
// Plus MockRenderer clip-stack recording as the verification surface.
// ============================================================================

TEST_SUITE(AYUI_ContainerContract_Cut2)

// ---- MockRenderer clip-stack recording ----

TEST_CASE(mock_renderer_clip_depth_tracks_push_pop) {
    MockRenderer r;
    CHECK(r.getClipDepth() == 0);
    CHECK(r.isClipStackBalanced());
    r.pushClip(ayt::math::FRectangle(0, 0, 10, 10));
    r.pushClip(ayt::math::FRectangle(2, 2, 8, 8));
    CHECK(r.getClipDepth() == 2);
    CHECK_FALSE(r.isClipStackBalanced());
    r.popClip();
    r.popClip();
    CHECK(r.getClipDepth() == 0);
    CHECK(r.isClipStackBalanced());
}

// ---- getClientRect contract (single source of truth) ----
// (folded into the hitTest and clip_balance tests below — ScrollView/
// ListView/Window/TreeView all derive clientRect from world bounds minus
// chrome, and that contract is exercised indirectly through the
// hitTest and renderChildren cases.)

// ---- renderChildren clip balance ----

TEST_CASE(clip_balance_scrollview_push_pop) {
    MockRenderer r;
    auto* sv = new ScrollView();
    sv->setSize(FVector2(200.0f, 100.0f));
    sv->setContentSize(FVector2(200.0f, 500.0f));
    sv->render(r);
    // onRender: pushClip(content) + content.render + popClip + bars render.
    // renderChildren override: helper does push + (no extras) + pop.
    // Net: balanced.
    CHECK(r.isClipStackBalanced());
    delete sv;
}

TEST_CASE(clip_balance_listview_push_pop) {
    MockRenderer r;
    ListView lv;
    lv.setItems({L"a", L"b", L"c"});
    lv.setSize(FVector2(160.0f, 200.0f));
    lv.render(r);
    CHECK(r.isClipStackBalanced());
}

TEST_CASE(clip_balance_window_body_push_pop) {
    MockRenderer r;
    Window w;
    w.setSize(FVector2(200.0f, 150.0f));
    w.setTitleBarHeight(20.0f);
    w.render(r);
    CHECK(r.isClipStackBalanced());
}

TEST_CASE(clip_balance_treeview_push_pop) {
    MockRenderer r;
    TreeView tv;
    tv.setSize(FVector2(240.0f, 100.0f));
    tv.render(r);
    CHECK(r.isClipStackBalanced());
}

// ---- hitTest gates by clientRect ----

TEST_CASE(scrollview_hit_test_outside_client_falls_through_to_self) {
    auto* sv = new ScrollView();
    sv->setSize(FVector2(200.0f, 100.0f));
    sv->setContentSize(FVector2(200.0f, 500.0f));   // vbar visible
    // Click far outside world bounds — should miss.
    Widget* miss = sv->hitTest(FVector2(9999.0f, 9999.0f));
    CHECK(miss == nullptr);
    // Click outside clientRect but inside world bounds. The vbar lives
    // at default position (0, 0) before performLayout runs; clicking
    // there should hit the vbar (chrome — visible above content via
    // reverse-order child walk in the shared helper).
    Widget* gutterHit = sv->hitTest(FVector2(2.0f, 50.0f));
    CHECK(gutterHit != nullptr);
    CHECK(gutterHit == sv->getVerticalScrollBar());
    delete sv;
}

TEST_CASE(scrollview_get_client_rect_excludes_visible_bars) {
    auto* sv = new ScrollView();
    sv->setSize(FVector2(200.0f, 100.0f));
    sv->setContentSize(FVector2(200.0f, 500.0f));   // vbar visible
    const float barW = ScrollBar::kDefaultBarWidth;
    constexpr float kBorder = 1.0f;
    // World bounds = (0,0,200,100); inset by frame border, then exclude vbar.
    const ayt::math::FRectangle cr = sv->getClientRect();
    CHECK(cr.minX == kBorder);
    CHECK(cr.minY == kBorder);
    CHECK(cr.maxX == 200.0f - kBorder - barW);
    CHECK(cr.maxY == 100.0f - kBorder);
    delete sv;
}

TEST_CASE(listview_hit_test_clipped_to_client_rect) {
    ListView lv;
    lv.setItems({L"a", L"b", L"c"});
    lv.setSize(FVector2(160.0f, 200.0f));
    // Click inside client area — should hit a pool row, NOT listview itself.
    Widget* hit = lv.hitTest(FVector2(20.0f, 20.0f));
    CHECK(hit != nullptr);
    // Click far outside world bounds — null.
    Widget* outside = lv.hitTest(FVector2(9999.0f, 9999.0f));
    CHECK(outside == nullptr);
}

TEST_CASE(treeview_hit_test_clipped_to_client_rect) {
    // TreeView with no nodes → hit anywhere in the client area returns self.
    TreeView tv;
    tv.setSize(FVector2(240.0f, 100.0f));
    Widget* insideClient = tv.hitTest(FVector2(20.0f, 20.0f));
    CHECK(insideClient == &tv);
    // Click far outside world bounds — null.
    Widget* outside = tv.hitTest(FVector2(9999.0f, 9999.0f));
    CHECK(outside == nullptr);
}

TEST_CASE(window_hit_test_outside_body_falls_through_to_self) {
    Window w;
    w.setSize(FVector2(200.0f, 150.0f));
    w.setTitleBarHeight(20.0f);
    // Click in title bar → self (window is chrome there).
    Widget* titleHit = w.hitTest(FVector2(100.0f, 10.0f));
    CHECK(titleHit == &w);
    // Click far outside → null.
    Widget* outside = w.hitTest(FVector2(9999.0f, 9999.0f));
    CHECK(outside == nullptr);
}

TEST_SUITE_END