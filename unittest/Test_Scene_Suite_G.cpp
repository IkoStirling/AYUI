#include "AYTest.h"
#include "AYWidget.h"
#include "AYUIManager.h"
#include "AYScrollView.h"
#include "AYListView.h"
#include "AYMenu.h"
#include "AYMenuItem.h"
#include "AYMockRenderer.h"

#include <cstdlib>
#include <string>

using namespace ayt::ui;
using namespace ayt::math;

// ============================================================================
// PR-Scene-Suite-G (Gallery-regression layer): 3 scenario-level UTs that
// cover UT blind spots the Cut2 ship exposed — the kind of multi-control
// "layout then interact" cases that crash visually in Gallery but are
// impossible to express via single-widget pure-function tests.
//
//   1. scrollview_then_click_outside_viewport_misses_child
//      → post-scroll clip + hitTest stay in sync (S1/S2 Gallery row).
//   2. scrollview_wrapping_listview_inner_consumes_wheel
//      → nested wheel routes to inner ListView only (S7 Gallery row).
//   3. menu_typeahead_letter_then_enter_activates_match
//      → end-to-end keyboard sequence fires the right callback (S3 row).
//
// Naming convention: existing scene suites use plain Test_*.cpp files
// without a "Scene_" prefix; this one follows the same shape so the
// future incremental additions drop into the same file. Register this
// file in unittest/main.cpp.
// ============================================================================

TEST_SUITE(AYUI_Scene_Suite_G)

// ---------------------------------------------------------------------------
// 1. Layout → scroll → click far below the visible viewport must miss.
//    The bug class is "visual moved but click stayed in old coordinate".
//    A pure unit test on ListView alone wouldn't catch it because it
//    only fires when ScrollView's clip gate stays in sync with the
//    scroll offset. ScrollView → ListView content, scroll the
//    ScrollView's vbar, then hitTest at a y well past the viewport
//    bottom (but inside the world bounds) — should return nullptr.
// ---------------------------------------------------------------------------
TEST_CASE(scrollview_then_click_outside_viewport_misses_child) {
    UIManager ui;
    MockRenderer renderer;
    ui.initialize(&renderer);

    ScrollView sv;
    sv.setSize(FVector2(200.0f, 100.0f));
    sv.setPosition(FVector2(0.0f, 0.0f));
    sv.setContentSize(FVector2(200.0f, 800.0f));   // 8x viewport
    ui.root()->addChildExternal(&sv);

    ListView inner;
    inner.setSize(FVector2(200.0f, 800.0f));        // matches content
    inner.setItemHeight(20.0f);
    for (int i = 0; i < 40; ++i) {
        inner.addItem(L"item" + std::to_wstring(i));
    }
    sv.setContent(&inner);

    // Scroll the ScrollView by 400px (half the content). The ListView's
    // own _firstVisibleIndex stays 0 — only the ScrollView's offset
    // changes, the helper clips hitTest to the visible viewport.
    sv.scrollBy(FVector2(0.0f, 400.0f));

    // Click at y=950 — inside world bounds (0..800 from the ListView's
    // local perspective — which the ScrollView's clip gate REJECTS
    // because viewport ends at 100 + scroll 400 = world y 500).
    Widget* hit = sv.hitTest(FVector2(50.0f, 950.0f));
    CHECK(hit == nullptr);

    ui.shutdown();
}

// ---------------------------------------------------------------------------
// 2. S7 — wheel on an inner ListView inside a ScrollView scrolls the
//    inner only. The classic Gallery bug: scrolling a list inside a
//    scroll panel scrolls BOTH (outer jumps when inner hits its edge).
//    Direct call to inner.onMouseWheel — mirrors the routing semantics
//    exercised in Test_WheelRouting.cpp case 3 (nested ScrollViews).
// ---------------------------------------------------------------------------
TEST_CASE(scrollview_wrapping_listview_inner_consumes_wheel) {
    UIManager ui;
    MockRenderer renderer;
    ui.initialize(&renderer);

    ScrollView outer;
    outer.setSize(FVector2(400.0f, 300.0f));
    outer.setPosition(FVector2(0.0f, 0.0f));
    outer.setContentSize(FVector2(400.0f, 1200.0f));
    ui.root()->addChildExternal(&outer);

    ListView inner;
    inner.setSize(FVector2(380.0f, 200.0f));        // viewport=200 < content=1200
    inner.setPosition(FVector2(0.0f, 0.0f));
    inner.setItemHeight(20.0f);
    for (int i = 0; i < 60; ++i) {
        inner.addItem(L"row" + std::to_wstring(i));
    }
    outer.setContent(&inner);
    // PR-Landmine: ListView::_contentSize is computed inside
    // layoutChildren() (which performLayout calls). Without this call
    // scrollBy clamps every positive dy to 0 and firstVisibleIndex
    // never moves. UIManager triggers performLayout on every frame, but
    // direct widget-level tests don't get that for free.
    inner.performLayout();

    const float beforeOuterY = outer.getScrollOffset().y;
    const int   beforeInner  = inner.getFirstVisibleIndex();

    // Wheel on the inner ListView at a position inside both world bounds.
    // ListView::onMouseWheel scrolls its own offset; outer's offset must
    // not move.
    const bool ok = inner.onMouseWheel(UIMouseWheelEvent(
        FVector2(50.0f, 50.0f), 60.0f));
    CHECK(ok);
    CHECK(inner.getFirstVisibleIndex() > beforeInner);
    CHECK(outer.getScrollOffset().y == beforeOuterY);

    ui.shutdown();
}

// ---------------------------------------------------------------------------
// 3. S3 — Menu typeahead letter, then Enter, fires _onItemActivated
//    with the matched item's index. End-to-end keyboard sequence:
//    no mouse interaction, no scroll/click — pure typeahead + activate.
//    This pins the path that Gallery shows visually ("B" highlights
//    Blueberry, Enter activates). Without the end-to-end coverage, the
//    _onItemActivated wiring could regress to firing the wrong index
//    (e.g. _hoveredIndex's pre-typeahead value).
// ---------------------------------------------------------------------------
TEST_CASE(menu_typeahead_letter_then_enter_activates_match) {
    UIManager ui;
    MockRenderer renderer;
    ui.initialize(&renderer);

    Menu menu;
    menu.addItem(L"Banana");
    menu.addItem(L"Blueberry");
    menu.addItem(L"Cherry");
    menu.addItem(L"Date");
    ui.root()->addChildExternal(&menu);

    int activatedIndex = -1;
    menu.setOnItemActivated([&activatedIndex](int idx) {
        activatedIndex = idx;
    });

    // PR-S3: with _hoveredIndex defaulting to -1 (AYMenu.h:175), the
    // first-letter typeahead from a fresh menu includes idx 0. 'B'
    // matches Banana (idx 0) first; subsequent 'L' (buffer "bl")
    // advances to Blueberry (idx 1).
    menu.onKeyDown(UIKey_B);
    CHECK(menu.getHoveredIndex() == 0);          // Banana
    menu.onKeyDown(UIKey_L);
    CHECK(menu.getHoveredIndex() == 1);          // Blueberry (buffer "bl")

    // Enter activates the hovered row → _onItemActivated fires with 1.
    menu.onKeyDown(UIKey_Enter);
    CHECK(activatedIndex == 1);
    // Menu closes itself on activate (PR-C3 contract).
    CHECK_FALSE(menu.isOpen());

    ui.shutdown();
}

TEST_SUITE_END