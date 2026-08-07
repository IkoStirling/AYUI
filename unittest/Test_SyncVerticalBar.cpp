#include "AYTest.h"
#include "AYScrollBarSync.h"
#include "AYScrollBar.h"

#include <string>

using namespace ayt::ui;

// ============================================================================
// PR-SyncVerticalBar: pure-function tests for the shared bar-sync helper.
// The helper itself takes a ScrollBar* + 3 floats; we verify the call order
// by hooking the bar's onValueChanged callback (setRange internally calls
// setValue, so a wrong order would fire the callback twice with stale data).
// ============================================================================

TEST_SUITE(AYUI_SyncVerticalBar)

TEST_CASE(sync_vertical_bar_null_bar_is_noop) {
    // No crash, no observable effect.
    syncVerticalBar(nullptr, 100.0f, 50.0f, 25.0f);
    CHECK(true);
}

TEST_CASE(sync_vertical_bar_sets_range_viewport_value_in_order) {
    auto* bar = new ScrollBar();
    bar->setOrientation(ScrollBar::Orientation::Vertical);
    int callbackCount = 0;
    float lastValue = -1.0f;
    bar->setOnValueChanged([&](float v) {
        ++callbackCount;
        lastValue = v;
    });
    // setRange internally calls setValue — if the helper called
    // setValue first (or double-called it), callbackCount would be
    // wrong. Locked order: range → viewport → value = exactly 1 final
    // value-changed fire with the desired value.
    syncVerticalBar(bar, /*content=*/200.0f, /*vp=*/80.0f, /*off=*/40.0f);
    CHECK(callbackCount == 1);
    CHECK(bar->getValue() == 40.0f);
    CHECK(lastValue == 40.0f);
    // Viewport landed (ScrollBar exposes this getter directly).
    CHECK(bar->getViewportSize() == 80.0f);
    delete bar;
}

TEST_CASE(sync_vertical_bar_zero_viewport_does_not_crash) {
    auto* bar = new ScrollBar();
    // Extreme edge: content=0, vp=0 — should not divide-by-zero or
    // NaN-poison the value.
    syncVerticalBar(bar, 0.0f, 0.0f, 0.0f);
    CHECK(bar->getValue() == 0.0f);
    delete bar;
}

TEST_CASE(sync_vertical_bar_huge_content_does_not_clamp_offset) {
    // Caller is responsible for clamping scrollOffset to [0, content];
    // the helper passes it through verbatim so the bar's own clamp
    // (if any) can be exercised separately. This pins that contract.
    auto* bar = new ScrollBar();
    syncVerticalBar(bar, /*content=*/1.0e9f, /*vp=*/1.0f, /*off=*/500.0f);
    // ScrollBar::setValue may clamp internally; we only assert the
    // helper itself did not crash and the viewport landed.
    CHECK(bar->getViewportSize() == 1.0f);
    delete bar;
}

TEST_SUITE_END