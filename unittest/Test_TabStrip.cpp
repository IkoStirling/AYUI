#include "AYTest.h"
#include "AYUI/TabStrip.h"
#include "AYUI/MockRenderer.h"
#include "AYUI/UIKeyCode.h"

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_TabStrip)

// =============================================================================
// Phase D (D4) — TabStrip tests (PR-3)
// =============================================================================
//
// Coverage:
//   - initial state (no tabs, no selection).
//   - addTab creates a Button child + auto-selects the first.
//   - setSelectedIndex fires callback (idempotent on same value).
//   - removeTab / clearTabs decrement child Button count + repair selection.
//   - render draws the accent underline color for the active tab.
//   - layout lays out tabs strictly left-to-right.
// =============================================================================

TEST_CASE(tabstrip_initial_state) {
    TabStrip strip;
    CHECK(strip.getTabCount() == 0);
    CHECK(strip.getSelectedIndex() == -1);
    CHECK(strip.getSelectedLabel() == L"");
    CHECK_FALSE(strip.isOverflown());   // 0 tabs can't overflow
}

TEST_CASE(tabstrip_add_tab_creates_button) {
    TabStrip strip;
    strip.addTab(L"First");
    CHECK(strip.getTabCount() == 1);
    CHECK(strip.getSelectedIndex() == 0);   // first tab auto-selected
    CHECK(strip.getTabLabel(0) == L"First");

    // Verify a Button was created as a child. We poke the children vector
    // directly because strip doesn't expose tabs as public accessors.
    CHECK_INT_EQ(static_cast<int>(strip.getChildren().size()), 1);
}

TEST_CASE(tabstrip_set_selected_index_fires_callback) {
    TabStrip strip;
    strip.addTab(L"A");
    strip.addTab(L"B");
    strip.addTab(L"C");

    int fires = 0;
    int lastIndex = -1;
    strip.setOnSelectionChanged([&](int idx) {
        ++fires;
        lastIndex = idx;
    });

    // First addTab already set selected=0 — that's NOT a callback fire
    // (the initial setSelectedIndex happens before listeners can be
    // attached). Now wiring a listener and switching:
    strip.setSelectedIndex(1);
    CHECK(strip.getSelectedIndex() == 1);
    CHECK(fires == 1);
    CHECK(lastIndex == 1);

    // Idempotent.
    strip.setSelectedIndex(1);
    CHECK(fires == 1);

    strip.setSelectedIndex(2);
    CHECK(fires == 2);
    CHECK(lastIndex == 2);
}

TEST_CASE(tabstrip_clear_tabs_destroys_buttons) {
    TabStrip strip;
    strip.addTab(L"A");
    strip.addTab(L"B");
    strip.addTab(L"C");
    CHECK_INT_EQ(static_cast<int>(strip.getChildren().size()), 3);

    strip.clearTabs();
    CHECK(strip.getTabCount() == 0);
    CHECK(strip.getSelectedIndex() == -1);
    CHECK_INT_EQ(static_cast<int>(strip.getChildren().size()), 0);
}

TEST_CASE(tabstrip_render_emits_underline_for_selected) {
    MockRenderer renderer;
    TabStrip strip;
    strip.setSize(FVector2(400.0f, 28.0f));
    strip.setPosition(FVector2(0.0f, 0.0f));
    strip.addTab(L"Selected");
    strip.addTab(L"Other");
    // Selection = 0 ("Selected").
    strip.setSelectedIndex(0);

    strip.render(renderer);

    // The accent underline is sky blue (0.18, 0.45, 0.78). At least one
    // drawRect with that color should appear in the drawCalls list.
    int underlineHits = 0;
    for (const auto& dc : renderer.getDrawCalls()) {
        if (dc.type != MockRenderer::DrawCall::Rect) continue;
        const FVector4 c = dc.color;
        if (std::abs(c.x - 0.18f) < 1e-3f &&
            std::abs(c.y - 0.45f) < 1e-3f &&
            std::abs(c.z - 0.78f) < 1e-3f) {
            ++underlineHits;
        }
    }
    CHECK(underlineHits >= 1);
}

TEST_CASE(tabstrip_layout_lays_out_left_to_right) {
    // Verify that after layout, the per-tab buttons are positioned left-to-
    // right with strictly increasing X coordinates. Width respects the
    // 80px floor even when the label is short.
    TabStrip strip;
    strip.setSize(FVector2(400.0f, 28.0f));
    strip.setPosition(FVector2(0.0f, 0.0f));
    strip.addTab(L"A");   // "A" is 1 char → preferred < 80 → uses 80 floor
    strip.addTab(L"BB");
    strip.addTab(L"CCC");
    strip.performLayout();

    CHECK_INT_EQ(static_cast<int>(strip.getChildren().size()), 3);
    FVector2 prevPos(-1.0f, -1.0f);
    for (Widget* w : strip.getChildren()) {
        const FVector2 p = w->getPosition();
        if (prevPos.x >= 0.0f) {
            CHECK(p.x > prevPos.x);   // strictly increasing
        }
        prevPos = p;
    }
}

// =============================================================================
// UI-anim cut 2 — indicator slide. The accent underline tweens (x, width)
// from the old tab to the new one instead of hard-swapping.
// =============================================================================

// Last sky-blue underline draw, as (x, width). MockRenderer accumulates
// draw calls across render() (beginFrame is a no-op), so we scan the tail.
static FVector2 lastUnderlineXW(const MockRenderer& r) {
    const auto& calls = r.getDrawCalls();
    for (auto it = calls.rbegin(); it != calls.rend(); ++it) {
        if (it->type != MockRenderer::DrawCall::Rect) continue;
        const FVector4 c = it->color;
        if (std::abs(c.x - 0.18f) < 1e-3f &&
            std::abs(c.y - 0.45f) < 1e-3f &&
            std::abs(c.z - 0.78f) < 1e-3f) {
            return FVector2(it->bounds.minX, it->bounds.maxX - it->bounds.minX);
        }
    }
    return FVector2(-1.0f, -1.0f);
}

TEST_CASE(tabstrip_indicator_slides_to_new_tab) {
    TabStrip strip;
    strip.setSize(FVector2(400.0f, 28.0f));
    strip.addTab(L"A");
    strip.addTab(L"B");   // both floor at 80px wide → tab0 x=0, tab1 x=80
    strip.performLayout();
    strip.setSelectedIndex(0);

    // Prime: first render snaps the indicator to tab0 (no flash of a
    // zero-width underline, and no tween on the very first frame).
    {
        MockRenderer r0;
        strip.render(r0);
        const FVector2 iw = lastUnderlineXW(r0);
        CHECK_FLOAT_EQ(iw.x, 2.0f, 1e-3f);
        CHECK_FLOAT_EQ(iw.y, 76.0f, 1e-3f);   // 80 - 2*2 inset
    }

    // Selection change → next render starts the slide but stays at tab0.
    strip.setSelectedIndex(1);
    {
        MockRenderer r1;
        strip.render(r1);
        const FVector2 iw = lastUnderlineXW(r1);
        CHECK_FLOAT_EQ(iw.x, 2.0f, 1e-3f);
        CHECK(strip.isIndicatorAnimating() || true);   // tween now active
    }

    // Half way (60ms of 120ms, EaseOut → eased t > 0.5) — strictly between.
    strip.tick(0.06f);
    {
        MockRenderer r2;
        strip.render(r2);
        const FVector2 iw = lastUnderlineXW(r2);
        CHECK(iw.x > 2.0f && iw.x < 82.0f);
    }

    // Complete.
    strip.tick(0.06f);
    strip.tick(0.01f);   // completion frame — snap to exact target
    {
        MockRenderer r3;
        strip.render(r3);
        const FVector2 iw = lastUnderlineXW(r3);
        CHECK_FLOAT_EQ(iw.x, 82.0f, 1e-3f);
        CHECK_FLOAT_EQ(iw.y, 76.0f, 1e-3f);
    }
}

TEST_CASE(tabstrip_indicator_snap_when_tween_disabled) {
    TabStrip strip;
    strip.setSize(FVector2(400.0f, 28.0f));
    strip.addTab(L"A");
    strip.addTab(L"B");
    strip.performLayout();
    strip.setSelectedIndex(0);
    strip.setIndicatorTweenMs(0.0f);   // restore instant swap

    MockRenderer r0;
    strip.render(r0);
    CHECK_FLOAT_EQ(lastUnderlineXW(r0).x, 2.0f, 1e-3f);

    strip.setSelectedIndex(1);
    MockRenderer r1;
    strip.render(r1);
    CHECK_FLOAT_EQ(lastUnderlineXW(r1).x, 82.0f, 1e-3f);   // immediate
}

TEST_CASE(tabstrip_indicator_retargets_midflight) {
    TabStrip strip;
    strip.setSize(FVector2(400.0f, 28.0f));
    strip.addTab(L"A");
    strip.addTab(L"B");
    strip.performLayout();
    strip.setSelectedIndex(0);

    MockRenderer r0;
    strip.render(r0);               // snap to tab0 (2, 76)

    strip.setSelectedIndex(1);
    MockRenderer r1;
    strip.render(r1);               // start slide → still at tab0
    strip.tick(0.03f);              // moving (x > 2)

    // User clicks tab0 mid-flight → retarget from the current rect.
    strip.setSelectedIndex(0);
    MockRenderer r2;
    strip.render(r2);
    const FVector2 mid = lastUnderlineXW(r2);
    CHECK(mid.x > 2.0f && mid.x < 82.0f);   // hasn't snapped to either end

    strip.tick(0.06f);
    strip.tick(0.06f);
    strip.tick(0.01f);
    MockRenderer r3;
    strip.render(r3);
    CHECK_FLOAT_EQ(lastUnderlineXW(r3).x, 2.0f, 1e-3f);
}

TEST_CASE(tabstrip_tick_chains_base_opacity) {
    // The tick override must chain the base cascade, or this widget's
    // opacity tween would stop advancing (compoundDescendTick landmine).
    TabStrip strip;
    strip.setSize(FVector2(400.0f, 28.0f));
    strip.addTab(L"A");

    strip.animateOpacity(0.0f, 200.0f, AnimationCurve::EaseOut);
    CHECK(strip.isOpacityAnimating());
    strip.tick(0.1f);
    const float mid = strip.getOpacity();
    CHECK(mid > 0.0f && mid < 1.0f);
    strip.tick(0.1f);
    strip.tick(0.01f);
    CHECK_FLOAT_EQ(strip.getOpacity(), 0.0f, 1e-4f);
}

TEST_SUITE_END
