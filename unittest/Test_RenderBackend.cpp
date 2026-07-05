#include "AYTest.h"
#include "AYMockRenderer.h"

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_RenderBackend)

TEST_CASE(draw_border_rect_uses_edge_strips) {
    MockRenderer renderer;
    const FRectangle bounds(10.0f, 20.0f, 110.0f, 52.0f);
    const FVector4 borderColor(0.12f, 0.12f, 0.12f, 1.0f);

    renderer.drawBorderRect(bounds, borderColor, 1.0f, 0.0f);

    const auto& calls = renderer.getDrawCalls();
    CHECK(calls.size() == 4u);

    const float fullArea = (bounds.maxX - bounds.minX) * (bounds.maxY - bounds.minY);
    for (const MockRenderer::DrawCall& call : calls) {
        CHECK(call.type == MockRenderer::DrawCall::Rect);
        const float area = (call.bounds.maxX - call.bounds.minX)
                         * (call.bounds.maxY - call.bounds.minY);
        CHECK(area < fullArea);
    }
}

TEST_CASE(draw_border_rect_with_corner_radius_adds_corner_patches) {
    MockRenderer renderer;
    const FRectangle bounds(0.0f, 0.0f, 100.0f, 32.0f);

    renderer.drawBorderRect(bounds, FVector4(1.0f, 0.0f, 0.0f, 1.0f), 1.0f, 2.0f);

    CHECK(renderer.getDrawCalls().size() == 8u);
}

TEST_SUITE_END
