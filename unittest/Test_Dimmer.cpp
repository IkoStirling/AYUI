#include "AYTest.h"
#include "AYDimmer.h"
#include "AYMockRenderer.h"

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_Dimmer)

// =============================================================================
// Phase D (D2) — Dimmer tests (PR-2)
// =============================================================================
//
// Coverage:
//   - Initial state (no callback wired, scrim = 50% black).
//   - render emits one Rect draw call with the scrim color.
//   - hitTest returns self for any point inside its bounds, null outside.
//   - onMouseButtonDown fires the dismiss sink and returns true (capture).
//   - dismiss callback wiring is idempotent / replaceable.
// =============================================================================

TEST_CASE(dimmer_initial_state) {
    Dimmer d;
    // Default scrim color is 50% black.
    CHECK(d.getScrimColor().z == 0.0f);
    CHECK(d.getScrimColor().w == 0.5f);
    // No callback wired.
    // (No getter for callback — exercise via onMouseButtonDown side-effect.)
}

TEST_CASE(dimmer_render_emits_scrim) {
    Dimmer d;
    d.setSize(FVector2(400.0f, 300.0f));
    d.setPosition(FVector2(0.0f, 0.0f));

    MockRenderer renderer;
    d.render(renderer);

    // At least one Rect with scrim color (default 0,0,0,0.5) appears.
    int hits = 0;
    for (const auto& dc : renderer.getDrawCalls()) {
        if (dc.type != MockRenderer::DrawCall::Rect) continue;
        if (std::abs(dc.color.x - 0.0f) < 1e-4f &&
            std::abs(dc.color.y - 0.0f) < 1e-4f &&
            std::abs(dc.color.z - 0.0f) < 1e-4f &&
            std::abs(dc.color.w - 0.5f) < 1e-4f) {
            ++hits;
        }
    }
    CHECK(hits >= 1);
}

TEST_CASE(dimmer_hit_test_always_returns_self_within_bounds) {
    Dimmer d;
    d.setSize(FVector2(400.0f, 300.0f));
    d.setPosition(FVector2(0.0f, 0.0f));

    // Center, corner-edge, anywhere inside → self.
    CHECK(d.hitTest(FVector2(200.0f, 150.0f)) == &d);
    CHECK(d.hitTest(FVector2(0.0f, 0.0f)) == &d);
    CHECK(d.hitTest(FVector2(399.0f, 299.0f)) == &d);
    // Outside (degenerate) — null.
    CHECK(d.hitTest(FVector2(-1.0f, -1.0f)) == nullptr);
    CHECK(d.hitTest(FVector2(500.0f, 500.0f)) == nullptr);
}

TEST_CASE(dimmer_on_mouse_button_down_fires_dismiss_callback) {
    Dimmer d;
    d.setSize(FVector2(100.0f, 100.0f));
    d.setPosition(FVector2(0.0f, 0.0f));

    int fireCount = 0;
    d.setOnDismiss([&]() { ++fireCount; });

    const UIMouseEvent e(FVector2(10.0f, 10.0f), 0);
    CHECK(d.onMouseButtonDown(e));   // returns true (eat + capture)
    CHECK(fireCount == 1);

    // Second click fires again.
    CHECK(d.onMouseButtonDown(e));
    CHECK(fireCount == 2);

    // Non-zero button is ignored (defensive — multi-button mice).
    const UIMouseEvent e2(FVector2(10.0f, 10.0f), 1);
    CHECK_FALSE(d.onMouseButtonDown(e2));
    CHECK(fireCount == 2);
}

TEST_CASE(dimmer_dismiss_callback_idempotent_replace) {
    Dimmer d;
    int a = 0, b = 0;
    d.setOnDismiss([&]() { ++a; });
    d.setOnDismiss([&]() { ++b; });   // replace
    d.onMouseButtonDown(UIMouseEvent(FVector2(5.0f, 5.0f), 0));
    CHECK(a == 0);   // replaced
    CHECK(b == 1);

    // Clear by setting nullptr.
    d.setOnDismiss(nullptr);
    d.onMouseButtonDown(UIMouseEvent(FVector2(5.0f, 5.0f), 0));
    CHECK(b == 1);   // unchanged
}

TEST_SUITE_END
