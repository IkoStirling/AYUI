// =============================================================================
// D5 — per-window UIManager scoping tests.
//
// ActiveScope (RAII singleton-swap guard) lets the editor tick multiple
// UIManager instances (one per top-level HWND) without losing the
// "current active manager" semantics that ComboBox/Menu/Tooltip rely on.
// Two tests:
//   1. pushActive scope RAII swap + auto-restore.
//   2. focus does NOT leak across managers (K-INV-D5-2 per-window focus).
// =============================================================================

#include "AYTest.h"
#include "AYUIManager.h"
#include "AYMockRenderer.h"

using namespace ayt::ui;

TEST_SUITE(AYUI_UIManager_PerWindow)

// -------------------------------------------------------------------------
// 1. pushActive must restore the previous manager when its scope exits.
//    Tests RAII swap semantics; verifies both the swap and the pop.
// -------------------------------------------------------------------------
TEST_CASE(test_push_active_scope_restores) {
    MockRenderer backendA;
    MockRenderer backendB;

    UIManager a;
    UIManager b;
    a.initialize(&backendA);
    b.initialize(&backendB);
    // After these two initializes, b is the most-recently-initialized
    // manager so g_activeUIManager == &b. Verify.
    CHECK(UIManager::tryGet() == &b);

    {
        UIManager::ActiveScope g(&a);
        CHECK(UIManager::tryGet() == &a);
        {
            // Nested scope — should restore the OUTER scope (a) on exit,
            // not the underlying nullptr. This is the test that locks
            // single-thread nested-push semantics.
            UIManager::ActiveScope inner(&a);   // same target as outer
            CHECK(UIManager::tryGet() == &a);
        }
        // Still inside outer scope — must still be &a.
        CHECK(UIManager::tryGet() == &a);
    }

    // Out of all scopes — restored to the state at first push.
    CHECK(UIManager::tryGet() == &b);

    b.shutdown();
    a.shutdown();
}

// -------------------------------------------------------------------------
// 2. Per-window focus isolation — K-INV-D5-2.
//
// Each UIManager owns its own _focusedWidget. setFocus on widget X in
// manager A must NOT bleed into manager B. The "swap + setFocus +
// pop" sequence mirrors what EditorChildWindowManager::routeKey would
// do for a keystroke arriving on a top-level child window.
// -------------------------------------------------------------------------
TEST_CASE(test_focus_isolation_between_managers) {
    MockRenderer backendA;
    MockRenderer backendB;
    UIManager a;
    UIManager b;
    a.initialize(&backendA);
    b.initialize(&backendB);

    // We need real focusable widgets — synthesize minimal Widget
    // instances. UIManager::setFocus takes any Widget*; it stores the
    // pointer and (for FocusableWidget) flips focus state. Base Widget
    // is fine for storage verification — we only read back the pointer.
    Widget wA;
    Widget wB;
    wA.setId("wA");
    wB.setId("wB");

    a.setFocus(&wA);
    CHECK(a.getFocusedWidget() == &wA);

    {
        UIManager::ActiveScope g(&b);
        b.setFocus(&wB);
        CHECK(b.getFocusedWidget() == &wB);
        // Inside the scope, A's focus must still be wA (no leak).
        CHECK(a.getFocusedWidget() == &wA);
    }

    // Outside the scope — b's focus is still on wB (it survived the
    // swap because nothing in the swap mutated it).
    CHECK(b.getFocusedWidget() == &wB);
    CHECK(a.getFocusedWidget() == &wA);

    b.shutdown();
    a.shutdown();
}

TEST_SUITE_END
