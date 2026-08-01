// =============================================================================
// Leak-detection regression tests — code-review 2026-08-02 #1-5
// =============================================================================
//
// These tests verify the dtor leak fixes landed in commit 7502ca7. They
// are MEANINGFUL only when the build has AY_ENABLE_ASAN=ON — LSan
// reports unreachable memory at process exit. Without ASAN the tests
// just exercise the destroy path (which the regular suite already
// covers); with ASAN they catch any future regression to the
// addChildExternal / addChild distinction the original leaks abused.
//
// Strategy: each test builds a small widget tree on the heap, drops
// the root widget via the public destroy path, and returns. LSan walks
// the heap at exit and flags any widget-owned allocations that weren't
// freed.
//
// Keep tests SHORT and STANDALONE — no UIManager (which would set up
// the overlay tree and complicate lifetime), no factory registration
// (which would touch cross-TU state). Just `new` + `delete`.
// =============================================================================

#include "AYTest.h"
#include "aymath/MathTypes.h"
#include "AYListView.h"
#include "AYScrollView.h"
#include "AYTabStrip.h"
#include "AYDockCard.h"
#include "AYMockRenderer.h"

#include <memory>

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_Leak)

// =============================================================================
// #1 ListView no-leak — ~ListView frees _vbar + pool rows on destruction.
//     Pre-fix: both were attached via addChildExternal so destroyWidgetTree
//     (which skips externally-owned children) leaked them; ~ListView itself
//     was a no-op. Post-fix: explicit delete loop mirrors rebuildRows()
//     teardown.
// =============================================================================
TEST_CASE(test_listview_destroy_frees_vbar_and_pool_rows) {
    auto* lv = new ListView();
    lv->setSize(FVector2(400.0f, 200.0f));
    lv->setItems({L"row 0", L"row 1", L"row 2", L"row 3", L"row 4",
                  L"row 5", L"row 6", L"row 7", L"row 8", L"row 9",
                  L"row 10", L"row 11", L"row 12", L"row 13", L"row 14"});
    lv->performLayout();
    delete lv;
}

// =============================================================================
// #2 ScrollView no-leak — ~ScrollView frees _vbar + _hbar on destruction.
//     Pre-fix: same addChildExternal trap. Post-fix: explicit delete.
// =============================================================================
TEST_CASE(test_scrollview_destroy_frees_both_bars) {
    auto* sv = new ScrollView();
    sv->setSize(FVector2(400.0f, 300.0f));
    // _content is host-owned (we pass a Widget* we own); ~ScrollView
    // does NOT free it (mirrors the destroyWidgetTree externally-owned
    // contract). We delete it after.
    auto* content = new Widget();
    content->setSize(FVector2(400.0f, 800.0f));
    sv->setContent(content);
    sv->setContentSize(FVector2(400.0f, 800.0f));
    delete sv;
    delete content;
}

// =============================================================================
// #3 TabStrip no-leak — ~TabStrip frees every Button on destruction.
//     Pre-fix: only _tabButtons.clear() ran. Post-fix: ~TabStrip calls
//     destroyAllButtons() as its first action.
// =============================================================================
TEST_CASE(test_tabstrip_destroy_frees_all_buttons) {
    auto* ts = new TabStrip();
    ts->setSize(FVector2(600.0f, 30.0f));
    ts->addTab(L"Tab 1");
    ts->addTab(L"Tab 2");
    ts->addTab(L"Tab 3");
    ts->addTab(L"Tab 4");
    ts->addTab(L"Tab 5");
    delete ts;
}

// =============================================================================
// #4 DockCard drag-session cleanup — ~DockCard must call
//     clearDragStateNoDispatch(this). Without the fix, if a drag is
//     active at destruction time, _dragSession.source would dangle and
//     the next endDrag() / shutdown() would dereference freed memory.
//
//     We don't have an easy way to invoke beginDrag from this fixture
//     (it requires the source to be in a UIManager tree), so the
//     fallback assertion is just "dtor runs without crashing and
//     releases the content widget". The fixed ~DockCard still calls
//     clearDragStateNoDispatch on a no-op drag session (tryGet path)
//     so the wiring is exercised.
// =============================================================================
TEST_CASE(test_dockcard_destroy_releases_content) {
    auto* card = new DockCard();
    card->setId("leak_test");
    card->setSize(FVector2(240.0f, 200.0f));
    auto* content = new Widget();
    content->setSize(FVector2(100.0f, 100.0f));
    card->setContent(content);
    delete card;   // ~DockCard + CompoundWidget tear down content
    // content is now freed through card's child tree (CompoundWidget
    // destroys children on dtor). Explicit delete would be a
    // double-free — LSan catches it.
}

// =============================================================================
// #5 (defense) Compound dtor chain — many widgets, all heap, destroy the
//     root with destroyWidgetTree. This pattern is what AYEditor uses
//     for window teardown and exercises the standard path. ASAN
//     validates no owned-by-self child leaks through the externally-
//     owned escape hatch.
// =============================================================================
TEST_CASE(test_compound_destroy_tree_no_leak) {
    auto* a = new DockCard();
    a->setSize(FVector2(100.0f, 100.0f));
    auto* b = new TabStrip();
    b->setSize(FVector2(200.0f, 30.0f));
    b->addTab(L"A");
    b->addTab(L"B");
    auto* c = new ListView();
    c->setSize(FVector2(300.0f, 200.0f));
    c->setItems({L"x", L"y", L"z"});
    // Stack the three under a DockCard — destroyWidgetTree on the
    // outer card recursively frees the inner ones.
    a->addChildExternal(b);
    b->addChild(c);   // owned by TabStrip via addChild
    a->addChildExternal(c);  // <-- bug if real code did this: detached-from-tree
    // The last addChildExternal call moves c out of b's tree (via
    // detachFromParent inside addChild). For this test that's fine —
    // we just want to verify destroyWidgetTree(a) frees all reachable
    // children. LSan flags any leak.
    destroyWidgetTree(a);
}

TEST_SUITE_END