#include "AYTest.h"
#include "AYUI/MockRenderer.h"
#include "AYUI/SvgIcon.h"

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
    int nonRectCallCount = 0;
    int fullAreaCallCount = 0;
    for (const MockRenderer::DrawCall& call : calls) {
        if (call.type != MockRenderer::DrawCall::Rect) {
            ++nonRectCallCount;
        }
        const float area = (call.bounds.maxX - call.bounds.minX)
                         * (call.bounds.maxY - call.bounds.minY);
        if (area >= fullArea) {
            ++fullAreaCallCount;
        }
    }
    CHECK(nonRectCallCount == 0);
    CHECK(fullAreaCallCount == 0);
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

TEST_SUITE(AYUI_SvgIcon)

TEST_CASE(svg_outline_path_preserves_contour_and_round_stroke_style) {
    const char* source = R"svg(
        <svg xmlns="http://www.w3.org/2000/svg" width="24" height="24"
             viewBox="0 0 24 24" fill="none" stroke="currentColor"
             stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
          <path d="M19.95 11a8 8 0 1 0 -.5 4m.5 5v-5h-5" />
        </svg>)svg";
    std::string error;
    const auto document = SvgDocument::parse(source, &error);
    CHECK(document != nullptr);
    CHECK(error.empty());
    CHECK(document->getPathCount() == 1u);

    MockRenderer renderer;
    CHECK(document->draw(renderer, FRectangle(10, 20, 34, 44),
                         FVector4(0.2f, 0.6f, 1.0f, 1.0f)));
    const auto& calls = renderer.getDrawCalls();
    CHECK(calls.size() == 1u);
    CHECK(calls[0].type == MockRenderer::DrawCall::Path);
    CHECK(calls[0].intParam1 == static_cast<int>(PathFillMode::Stroke));
    // The rotate glyph contains an arc subpath plus a separate arrowhead
    // subpath after the second relative move command.
    CHECK(calls[0].pathContourCount == 2u);
    CHECK(calls[0].pathStrokeCap == PathStrokeCap::Round);
    CHECK(calls[0].pathStrokeJoin == PathStrokeJoin::Round);
    CHECK_FLOAT_EQ(calls[0].floatParam1, 2.0f, 1e-4f);
    CHECK_FLOAT_EQ(calls[0].color.x, 0.2f, 1e-4f);
}

TEST_CASE(svg_path_parser_supports_complete_command_family) {
    const char* source = R"svg(
        <svg viewBox="0 0 32 32" fill="none" stroke="#ffffff"
             stroke-width="1.5" stroke-linecap="square" stroke-linejoin="bevel">
          <path d="M2 2 L6 2 H10 V6 C10 8 12 8 12 10 S14 12 16 10
                   Q18 8 20 10 T24 10 A3 2 25 0 1 28 14 z" />
        </svg>)svg";
    std::string error;
    const auto document = SvgDocument::parse(source, &error);
    CHECK(document != nullptr);
    CHECK(error.empty());

    MockRenderer renderer;
    renderer.setUiScale(2.0f);
    CHECK(document->draw(renderer, FRectangle(0, 0, 64, 64), FVector4(1, 0, 0, 1)));
    CHECK(renderer.getDrawCalls().size() == 1u);
    const auto& call = renderer.getDrawCalls().front();
    CHECK(call.pathContourCount == 1u);
    CHECK(call.pathStrokeCap == PathStrokeCap::Square);
    CHECK(call.pathStrokeJoin == PathStrokeJoin::Bevel);
    CHECK_FLOAT_EQ(call.floatParam1, 3.0f, 1e-4f);
}

TEST_CASE(svg_filled_paths_use_current_color_and_preserve_paint_order) {
    const char* source = R"svg(
        <svg viewBox="0 0 24 24" fill="currentColor">
          <path d="M5 4h6v16h-6z" />
          <path d="M13 4h6v16h-6z" />
        </svg>)svg";
    const auto document = SvgDocument::parse(source);
    CHECK(document != nullptr);

    MockRenderer renderer;
    const FVector4 tint(0.8f, 0.3f, 0.1f, 0.75f);
    CHECK(document->draw(renderer, FRectangle(0, 0, 24, 24), tint));
    CHECK(renderer.getDrawCalls().size() == 2u);
    CHECK(renderer.getDrawCalls()[0].intParam1 == static_cast<int>(PathFillMode::Fill));
    CHECK(renderer.getDrawCalls()[1].intParam1 == static_cast<int>(PathFillMode::Fill));
    CHECK_FLOAT_EQ(renderer.getDrawCalls()[0].color.w, 0.75f, 1e-4f);
}

TEST_CASE(svg_rejects_unsupported_transform_instead_of_silently_drawing_wrong) {
    std::string error;
    const auto document = SvgDocument::parse(
        R"svg(<svg viewBox="0 0 24 24"><path transform="rotate(20)" d="M0 0L4 4"/></svg>)svg",
        &error);
    CHECK(document == nullptr);
    CHECK(error.find("transform") != std::string::npos);
}

TEST_CASE(svg_icon_retained_display_list_replays_vector_recipe) {
    const auto document = SvgDocument::parse(
        R"svg(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M6 4v16l14 -8z"/></svg>)svg");
    CHECK(document != nullptr);

    SvgIcon icon;
    icon.setDocument(document);
    icon.setSize(FVector2(24, 24));
    icon.setColor(FVector4(0.4f, 0.8f, 0.2f, 1.0f));
    MockRenderer renderer;
    icon.render(renderer);
    CHECK(renderer.getDrawCalls().size() == 1u);

    renderer.beginFrame();
    icon.render(renderer);
    CHECK(renderer.getDrawCalls().size() == 1u);
    CHECK(renderer.getDrawCalls()[0].type == MockRenderer::DrawCall::Path);
    CHECK_FLOAT_EQ(renderer.getDrawCalls()[0].color.y, 0.8f, 1e-4f);
}

TEST_SUITE_END
