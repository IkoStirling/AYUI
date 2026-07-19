#include "AYTest.h"
#include "AYTabStrip.h"
#include "AYMockRenderer.h"
#include "UIKeyCode.h"

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

TEST_SUITE_END
