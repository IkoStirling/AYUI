// Test_MockBlendGradient.cpp — P1 MockRenderer recordings for blend mode
// and gradient corner colors. The border/shadow contract (3493 protection
// line) is asserted frozen: drawBorderRect still expands to the interface's
// inline 4/8-rect decomposition, drawRectShadow to one offset rect.

#include "AYUI/MockRenderer.h"
#include "AYTest.h"

TEST_SUITE(MockBlendGradientTests)

TEST_CASE(mock_blend_gradient_corner_colors_recorded)
{
    ayt::ui::MockRenderer r;
    const ayt::math::FVector4 tl(1.0f, 0.0f, 0.0f, 1.0f);
    const ayt::math::FVector4 tr(0.0f, 1.0f, 0.0f, 1.0f);
    const ayt::math::FVector4 bl(0.0f, 0.0f, 1.0f, 1.0f);
    const ayt::math::FVector4 br(1.0f, 1.0f, 1.0f, 1.0f);

    r.drawGradientRect(ayt::math::FRectangle(0, 0, 100, 60), tl, tr, bl, br);

    CHECK(r.getDrawCallCount() == 1);
    const auto& dc = r.getDrawCalls()[0];
    CHECK(dc.type == ayt::ui::MockRenderer::DrawCall::Rect);
    CHECK_FLOAT_EQ(dc.cornerColors[0].x, tl.x, 1e-5f);  // TL
    CHECK_FLOAT_EQ(dc.cornerColors[0].y, tl.y, 1e-5f);
    CHECK_FLOAT_EQ(dc.cornerColors[1].x, tr.x, 1e-5f);  // TR
    CHECK_FLOAT_EQ(dc.cornerColors[1].y, tr.y, 1e-5f);
    CHECK_FLOAT_EQ(dc.cornerColors[2].x, bl.x, 1e-5f);  // BL
    CHECK_FLOAT_EQ(dc.cornerColors[2].y, bl.y, 1e-5f);
    CHECK_FLOAT_EQ(dc.cornerColors[3].x, br.x, 1e-5f);  // BR
    CHECK_FLOAT_EQ(dc.cornerColors[3].y, br.y, 1e-5f);
    // color field keeps the average (legacy assertion compatibility).
    CHECK_FLOAT_EQ(dc.color.x, 0.5f, 1e-5f);
    CHECK_FLOAT_EQ(dc.color.y, 0.5f, 1e-5f);
    CHECK_FLOAT_EQ(dc.color.z, 0.5f, 1e-5f);
}

TEST_CASE(mock_blend_gradient_two_color_routes_four)
{
    ayt::ui::MockRenderer r;
    r.drawGradientRect(ayt::math::FRectangle(0, 0, 100, 60),
                       ayt::math::FVector4(0.1f, 0.2f, 0.3f, 1.0f),
                       ayt::math::FVector4(0.4f, 0.5f, 0.6f, 1.0f));

    CHECK(r.getDrawCallCount() == 1);
    const auto& dc = r.getDrawCalls()[0];
    CHECK_FLOAT_EQ(dc.cornerColors[0].x, 0.1f, 1e-5f);  // TL == top
    CHECK_FLOAT_EQ(dc.cornerColors[1].x, 0.1f, 1e-5f);  // TR == top
    CHECK_FLOAT_EQ(dc.cornerColors[2].x, 0.4f, 1e-5f);  // BL == bottom
    CHECK_FLOAT_EQ(dc.cornerColors[3].x, 0.4f, 1e-5f);  // BR == bottom
}

TEST_CASE(mock_blend_mode_stamped_per_call)
{
    ayt::ui::MockRenderer r;

    r.drawRect(ayt::math::FRectangle(0, 0, 10, 10), ayt::math::FVector4(1, 1, 1, 1));
    r.setBlendMode(ayt::ui::BlendMode::Additive);
    r.drawRect(ayt::math::FRectangle(20, 0, 30, 10), ayt::math::FVector4(1, 1, 1, 1));
    r.setBlendMode(ayt::ui::BlendMode::Normal);
    r.drawRect(ayt::math::FRectangle(40, 0, 50, 10), ayt::math::FVector4(1, 1, 1, 1));

    CHECK(r.getDrawCallCount() == 3);
    CHECK(r.getDrawCalls()[0].blendMode == ayt::ui::BlendMode::Normal);
    CHECK(r.getDrawCalls()[1].blendMode == ayt::ui::BlendMode::Additive);
    CHECK(r.getDrawCalls()[2].blendMode == ayt::ui::BlendMode::Normal);
}

TEST_CASE(mock_blend_mode_on_gradient_and_text)
{
    ayt::ui::MockRenderer r;

    r.setBlendMode(ayt::ui::BlendMode::Screen);
    r.drawGradientRect(ayt::math::FRectangle(0, 0, 100, 60),
                       ayt::math::FVector4(1, 0, 0, 1), ayt::math::FVector4(1, 0, 0, 1),
                       ayt::math::FVector4(0, 0, 1, 1), ayt::math::FVector4(0, 0, 1, 1));
    r.drawText(ayt::math::FRectangle(0, 0, 50, 16), L"Hi", 12,
               ayt::math::FVector4(1, 1, 1, 1));

    CHECK(r.getDrawCallCount() == 2);
    CHECK(r.getDrawCalls()[0].blendMode == ayt::ui::BlendMode::Screen);
    CHECK(r.getDrawCalls()[1].blendMode == ayt::ui::BlendMode::Screen);
}

TEST_CASE(mock_border_shadow_contract_frozen)
{
    ayt::ui::MockRenderer r;

    // Rounded border with r < half-min: 4 edges + 4 corners = 8 rects.
    r.drawBorderRect(ayt::math::FRectangle(0, 0, 100, 60),
                     ayt::math::FVector4(1, 1, 1, 1), 2.0f, 8.0f);
    CHECK(r.getDrawCallCount() == 8);

    // Square border: 4 edges, no corners.
    r.drawBorderRect(ayt::math::FRectangle(0, 0, 100, 60),
                     ayt::math::FVector4(1, 1, 1, 1), 2.0f, 0.0f);
    CHECK(r.getDrawCallCount() == 12);

    // Degenerate width >= half-min: single fill rect.
    r.drawBorderRect(ayt::math::FRectangle(0, 0, 100, 60),
                     ayt::math::FVector4(1, 1, 1, 1), 60.0f, 0.0f);
    CHECK(r.getDrawCallCount() == 13);

    // Shadow: exactly one offset rect, alpha color preserved.
    ayt::ui::IRenderBackend::ShadowStyle shadow;
    shadow.color = ayt::math::FVector4(0, 0, 0, 0.5f);
    shadow.offset = ayt::math::FVector2(3, 4);
    r.drawRectShadow(ayt::math::FRectangle(10, 10, 50, 40), shadow);
    CHECK(r.getDrawCallCount() == 14);
    const auto& dc = r.getDrawCalls()[13];
    CHECK(dc.type == ayt::ui::MockRenderer::DrawCall::Rect);
    CHECK_FLOAT_EQ(dc.bounds.minX, 13.0f, 1e-5f);
    CHECK_FLOAT_EQ(dc.bounds.minY, 14.0f, 1e-5f);
    CHECK_FLOAT_EQ(dc.bounds.maxX, 53.0f, 1e-5f);
    CHECK_FLOAT_EQ(dc.bounds.maxY, 44.0f, 1e-5f);
    CHECK_FLOAT_EQ(dc.color.w, 0.5f, 1e-5f);
}

TEST_SUITE_END
