// AYUI-Audit-2026-08-26: drawCard round-trip tests.
//
// The base IRenderBackend::drawCard default impl expands a card into
// shadow → fill → border sub-calls, which made it impossible for tests
// to observe that a caller actually used drawCard (vs. manually calling
// drawRect + drawBorderRect). The MockRenderer override records the
// full CardStyle into a single DrawCall::Card entry. These tests verify
// the recorded payload for each layer.
#include "AYTest.h"
#include "AYUI/MockRenderer.h"
#include "AYUI/IRenderBackend.h"

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_CardStyle_Render)

TEST_CASE(draw_card_records_single_card_call) {
    // AYUI-Audit-2026-08-26: smoke test — drawCard must produce exactly
    // ONE recorded call (not three shadow+fill+border sub-calls).
    MockRenderer renderer;
    IRenderBackend::CardStyle style;
    style.fillColor = FVector4(0.20f, 0.40f, 0.60f, 1.0f);

    const FRectangle bounds(0.0f, 0.0f, 100.0f, 50.0f);
    renderer.drawCard(bounds, style);

    const auto& calls = renderer.getDrawCalls();
    CHECK(calls.size() == 1u);
    CHECK(calls[0].type == MockRenderer::DrawCall::Card);
    // FRectangle has no operator== — compare fields explicitly.
    CHECK_FLOAT_EQ(calls[0].bounds.minX, bounds.minX, 1e-4f);
    CHECK_FLOAT_EQ(calls[0].bounds.minY, bounds.minY, 1e-4f);
    CHECK_FLOAT_EQ(calls[0].bounds.maxX, bounds.maxX, 1e-4f);
    CHECK_FLOAT_EQ(calls[0].bounds.maxY, bounds.maxY, 1e-4f);
}

TEST_CASE(draw_card_records_fill_layer) {
    // AYUI-Audit-2026-08-26: fillColor rides in BOTH `color` (post
    // opacity tint) and `cardFillColor` (raw style value). Tests that
    // verify the opacity multiplication path use `color`; tests that
    // verify the raw payload use `cardFillColor`.
    MockRenderer renderer;
    IRenderBackend::CardStyle style;
    style.fillColor = FVector4(0.10f, 0.20f, 0.30f, 0.80f);
    style.cornerRadius = IRenderBackend::CornerRadii(4.0f);

    renderer.drawCard(FRectangle(0.0f, 0.0f, 100.0f, 50.0f), style);

    const auto& calls = renderer.getDrawCalls();
    CHECK(calls.size() == 1u);
    CHECK(calls[0].cardFillColor == style.fillColor);
    CHECK(calls[0].color.w       == 0.80f);  // opacity stack at 1.0
}

TEST_CASE(draw_card_records_border_layer) {
    // AYUI-Audit-2026-08-26: borderColor + borderWidth +
    // borderPosition must round-trip through the recorded card fields.
    MockRenderer renderer;
    IRenderBackend::CardStyle style;
    style.borderColor    = FVector4(0.0f, 1.0f, 0.0f, 1.0f);
    style.borderWidth    = 2.0f;
    style.borderPosition = IRenderBackend::BorderStyle::Position::Inside;

    renderer.drawCard(FRectangle(0.0f, 0.0f, 100.0f, 50.0f), style);

    const auto& calls = renderer.getDrawCalls();
    CHECK(calls.size() == 1u);
    CHECK(calls[0].cardBorderColor    == style.borderColor);
    CHECK(calls[0].cardBorderWidth    == style.borderWidth);
    CHECK(calls[0].cardBorderPosition == IRenderBackend::BorderStyle::Position::Inside);
}

TEST_CASE(draw_card_records_shadow_layer) {
    // AYUI-Audit-2026-08-26: shadowColor + shadowOffset +
    // shadowBlurRadius must round-trip through the recorded card fields.
    MockRenderer renderer;
    IRenderBackend::CardStyle style;
    style.shadowColor       = FVector4(0.0f, 0.0f, 0.0f, 0.4f);
    style.shadowOffset      = FVector2(2.0f, 3.0f);
    style.shadowBlurRadius  = 6.5f;

    renderer.drawCard(FRectangle(0.0f, 0.0f, 100.0f, 50.0f), style);

    const auto& calls = renderer.getDrawCalls();
    CHECK(calls.size() == 1u);
    CHECK(calls[0].cardShadowColor      == style.shadowColor);
    CHECK(calls[0].cardShadowOffset     == style.shadowOffset);
    CHECK(calls[0].cardShadowBlurRadius == style.shadowBlurRadius);
}

TEST_CASE(draw_card_records_per_corner_radii) {
    // AYUI-Audit-2026-08-26: CornerRadii (TL, TR, BR, BL) must be
    // recorded in slot order. Tests assert each slot independently so
    // a per-corner swap regression is caught.
    MockRenderer renderer;
    IRenderBackend::CardStyle style;
    style.cornerRadius = IRenderBackend::CornerRadii(2.0f, 4.0f, 8.0f, 16.0f);

    renderer.drawCard(FRectangle(0.0f, 0.0f, 100.0f, 50.0f), style);

    const auto& calls = renderer.getDrawCalls();
    CHECK(calls.size() == 1u);
    CHECK(calls[0].cardRadii[0] == 2.0f);   // topLeft
    CHECK(calls[0].cardRadii[1] == 4.0f);   // topRight
    CHECK(calls[0].cardRadii[2] == 8.0f);   // bottomRight
    CHECK(calls[0].cardRadii[3] == 16.0f);  // bottomLeft
}

TEST_CASE(draw_card_records_all_layers_together) {
    // AYUI-Audit-2026-08-26: combination smoke test — fill + border +
    // shadow + per-corner radii all set in one card, all recorded.
    MockRenderer renderer;
    IRenderBackend::CardStyle style;
    style.fillColor          = FVector4(0.50f, 0.50f, 0.50f, 1.0f);
    style.borderColor        = FVector4(0.10f, 0.10f, 0.10f, 1.0f);
    style.borderWidth        = 1.5f;
    style.borderPosition     = IRenderBackend::BorderStyle::Position::Center;
    style.shadowColor        = FVector4(0.0f, 0.0f, 0.0f, 0.25f);
    style.shadowOffset       = FVector2(1.0f, 2.0f);
    style.shadowBlurRadius   = 3.0f;
    style.cornerRadius       = IRenderBackend::CornerRadii(5.0f);

    renderer.drawCard(FRectangle(0.0f, 0.0f, 200.0f, 100.0f), style);

    const auto& calls = renderer.getDrawCalls();
    CHECK(calls.size() == 1u);
    CHECK(calls[0].type             == MockRenderer::DrawCall::Card);
    CHECK(calls[0].cardFillColor    == style.fillColor);
    CHECK(calls[0].cardBorderColor  == style.borderColor);
    CHECK(calls[0].cardBorderWidth  == style.borderWidth);
    CHECK(calls[0].cardBorderPosition == IRenderBackend::BorderStyle::Position::Center);
    CHECK(calls[0].cardShadowColor  == style.shadowColor);
    CHECK(calls[0].cardShadowOffset == style.shadowOffset);
    CHECK(calls[0].cardShadowBlurRadius == style.shadowBlurRadius);
    CHECK(calls[0].cardRadii[0]     == 5.0f);
}

TEST_CASE(draw_card_transparent_layers_are_still_recorded) {
    // AYUI-Audit-2026-08-26: even when fillColor / shadowColor /
    // borderColor have alpha = 0 (visually a no-op layer), the
    // drawCard call MUST still be recorded as one Card entry — the
    // caller's intent (use drawCard, not drawRect) is what matters for
    // hit-testing / drawcall-count assertions.
    MockRenderer renderer;
    IRenderBackend::CardStyle style;  // all defaults: alpha 0 everywhere
    renderer.drawCard(FRectangle(0.0f, 0.0f, 100.0f, 50.0f), style);

    const auto& calls = renderer.getDrawCalls();
    CHECK(calls.size() == 1u);
    CHECK(calls[0].type == MockRenderer::DrawCall::Card);
}

TEST_CASE(draw_rect_with_border_routes_through_drawBorderRect) {
    // AYUI-Audit-2026-08-26: MockRenderer overrides drawRect(border)
    // and forwards to drawBorderRect (NOT through the IRenderBackend
    // drawCard path — see MockRenderer.cpp:159). This test pins that
    // behavior so a future refactor that swaps the order doesn't
    // silently break the visual. drawBorderRect default impl emits
    // 4 edge strips + optional 4 corner patches.
    MockRenderer renderer;
    IRenderBackend::BorderStyle border;
    border.color        = FVector4(0.1f, 0.2f, 0.3f, 1.0f);
    border.width        = 2.0f;
    border.cornerRadius = 4.0f;

    renderer.drawRect(FRectangle(0.0f, 0.0f, 100.0f, 50.0f), border);

    const auto& calls = renderer.getDrawCalls();
    // With cornerRadius=4 and a non-trivial border, we expect at
    // least 4 edge strips + 4 corner patches = 8 Rect records.
    CHECK(calls.size() >= 4u);
    int nonRectCallCount = 0;
    for (const MockRenderer::DrawCall& call : calls) {
        if (call.type != MockRenderer::DrawCall::Rect) {
            ++nonRectCallCount;
        }
    }
    CHECK(nonRectCallCount == 0);
}

TEST_SUITE_END
