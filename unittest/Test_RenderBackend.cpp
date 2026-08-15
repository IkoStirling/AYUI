#include "AYTest.h"
#include "AYUI/MockRenderer.h"

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

TEST_CASE(add_colored_quad_default_falls_back_to_draw_rect) {
    // Default IRenderBackend implementation must not retain the quad in a
    // batch — it must immediately emit a drawRect and return false so
    // backends without batching remain drop-in compatible.
    MockRenderer renderer;
    const FRectangle bounds(0.0f, 0.0f, 50.0f, 25.0f);
    const FVector4 color(0.4f, 0.5f, 0.6f, 1.0f);

    const bool batched = renderer.addColoredQuad(bounds, color);

    CHECK(batched == false);
    CHECK(renderer.getDrawCalls().size() == 1u);
    CHECK(renderer.getDrawCalls().front().type == MockRenderer::DrawCall::Rect);
    CHECK(renderer.getDrawCalls().front().color == color);
}

TEST_CASE(add_textured_quad_default_falls_back_to_draw_rect) {
    MockRenderer renderer;
    const FRectangle bounds(0.0f, 0.0f, 64.0f, 64.0f);
    const FRectangle uv(0.0f, 0.0f, 0.5f, 0.5f);
    int dummyHandle = 0;

    const bool batched = renderer.addTexturedQuad(bounds, &dummyHandle, uv);

    CHECK(batched == false);
    CHECK(renderer.getDrawCalls().size() == 1u);
    CHECK(renderer.getDrawCalls().front().type == MockRenderer::DrawCall::Image);
    CHECK(renderer.getDrawCalls().front().texture == &dummyHandle);
}

TEST_CASE(flush_batches_default_is_noop) {
    // Default flushBatches() must not produce any drawcall or side effect.
    MockRenderer renderer;
    renderer.drawRect(FRectangle(0, 0, 10, 10), FVector4(1, 1, 1, 1));
    const size_t callsBefore = renderer.getDrawCalls().size();

    renderer.flushBatches();

    CHECK(renderer.getDrawCalls().size() == callsBefore);
}

TEST_SUITE_END
