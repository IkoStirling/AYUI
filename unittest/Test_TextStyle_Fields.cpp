// AYUI-Audit-2026-08-26: TextStyle field recording tests.
//
// The styled drawText(bounds, text, fontSize, TextStyle) overload used
// to forward to the no-style overload and silently drop every TextStyle
// field except `color`. These tests build a MockRenderer, set each
// TextStyle field on the input, invoke drawText, and assert the
// corresponding DrawCall field recorded the value rather than dropping
// it. Each field has its own TEST_CASE so a regression pinpoints which
// field lost its payload.
#include "AYTest.h"
#include "AYUI/MockRenderer.h"
#include "AYUI/IRenderBackend.h"

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_TextStyle_Fields)

TEST_CASE(styled_draw_text_records_color) {
    // AYUI-Audit-2026-08-26: baseline check — even after the audit
    // change, the simple `color` slot must still flow through (regressed
    // check: previous behavior also did this correctly).
    MockRenderer renderer;
    IRenderBackend::TextStyle style;
    style.color = FVector4(0.20f, 0.40f, 0.60f, 0.80f);

    const FRectangle bounds(0.0f, 0.0f, 100.0f, 20.0f);
    renderer.drawText(bounds, L"hello", 14, style);

    const auto& calls = renderer.getDrawCalls();
    CHECK(calls.size() == 1u);
    CHECK(calls[0].type == MockRenderer::DrawCall::Text);
    CHECK(calls[0].text == L"hello");
    CHECK(calls[0].fontSize == 14);
    CHECK(calls[0].bounds == bounds);
    // Color is multiplied by the opacity stack (default 1.0); so the
    // alpha remains 0.80. Use a direct tolerance comparison rather
    // than ayt::math::abs to avoid an extra include dependency.
    const float alpha = calls[0].color.w;
    CHECK(alpha > 0.79f);
    CHECK(alpha < 0.81f);
}

TEST_CASE(styled_draw_text_records_outline) {
    // AYUI-Audit-2026-08-26: outlineColor + outlineWidth were previously
    // dropped on the floor; they must now flow through to the recorded
    // DrawCall.
    MockRenderer renderer;
    IRenderBackend::TextStyle style;
    style.outlineColor = FVector4(0.10f, 0.20f, 0.30f, 1.0f);
    style.outlineWidth = 2.5f;

    renderer.drawText(FRectangle(0.0f, 0.0f, 100.0f, 20.0f), L"x", 12, style);

    const auto& calls = renderer.getDrawCalls();
    CHECK(calls.size() == 1u);
    CHECK(calls[0].outlineColor == style.outlineColor);
    CHECK(calls[0].outlineWidth == style.outlineWidth);
}

TEST_CASE(styled_draw_text_records_shadow) {
    // AYUI-Audit-2026-08-26: shadowColor + shadowOffset +
    // shadowBlurRadius were previously dropped.
    MockRenderer renderer;
    IRenderBackend::TextStyle style;
    style.shadowColor       = FVector4(0.0f, 0.0f, 0.0f, 0.5f);
    style.shadowOffset      = FVector2(3.0f, 4.0f);
    style.shadowBlurRadius  = 7.5f;

    renderer.drawText(FRectangle(0.0f, 0.0f, 100.0f, 20.0f), L"y", 12, style);

    const auto& calls = renderer.getDrawCalls();
    CHECK(calls.size() == 1u);
    CHECK(calls[0].shadowColor      == style.shadowColor);
    CHECK(calls[0].shadowOffset     == style.shadowOffset);
    CHECK(calls[0].shadowBlurRadius == style.shadowBlurRadius);
}

TEST_CASE(styled_draw_text_records_spacing_and_wrap) {
    // AYUI-Audit-2026-08-26: letterSpacing + lineSpacing + wrapToBounds
    // were previously dropped. Asserting each one of these pins the
    // wrapping/measurement contract for measurement-using widgets
    // (TextArea, RichText, etc.).
    MockRenderer renderer;
    IRenderBackend::TextStyle style;
    style.letterSpacing = 3;
    style.lineSpacing   = 5;
    style.wrapToBounds  = true;

    renderer.drawText(FRectangle(0.0f, 0.0f, 200.0f, 40.0f), L"z", 12, style);

    const auto& calls = renderer.getDrawCalls();
    CHECK(calls.size() == 1u);
    CHECK(calls[0].letterSpacing == 3);
    CHECK(calls[0].lineSpacing   == 5);
    CHECK(calls[0].wrapToBounds  == true);
}

TEST_CASE(styled_draw_text_records_align_and_valign) {
    // AYUI-Audit-2026-08-26: align + valign were previously dropped —
    // alignment was the only field besides color that callers could
    // observe (via legacy centering behavior), but the explicit enum
    // value wasn't carried. Tests must now be able to read it back.
    MockRenderer renderer;
    IRenderBackend::TextStyle style;
    style.align  = IRenderBackend::TextStyle::Align::Center;
    style.valign = IRenderBackend::TextStyle::VAlign::Bottom;

    renderer.drawText(FRectangle(0.0f, 0.0f, 100.0f, 20.0f), L"a", 12, style);

    const auto& calls = renderer.getDrawCalls();
    CHECK(calls.size() == 1u);
    CHECK(calls[0].align  == IRenderBackend::TextStyle::Align::Center);
    CHECK(calls[0].valign == IRenderBackend::TextStyle::VAlign::Bottom);
}

TEST_CASE(styled_draw_text_keeps_default_textStyle_snapshot) {
    // AYUI-Audit-2026-08-26: the legacy `textStyle` snapshot slot on
    // DrawCall is still populated so old tests reading it (e.g. the
    // textStyle-aware assertions in Test_RenderBackend) keep passing.
    MockRenderer renderer;
    IRenderBackend::TextStyle style;
    style.color         = FVector4(0.1f, 0.2f, 0.3f, 1.0f);
    style.outlineWidth  = 1.5f;
    style.letterSpacing = 2;

    renderer.drawText(FRectangle(0.0f, 0.0f, 100.0f, 20.0f), L"b", 12, style);

    const auto& calls = renderer.getDrawCalls();
    CHECK(calls.size() == 1u);
    CHECK(calls[0].textStyle.color         == style.color);
    CHECK(calls[0].textStyle.outlineWidth  == style.outlineWidth);
    CHECK(calls[0].textStyle.letterSpacing == style.letterSpacing);
}

TEST_CASE(styled_draw_text_does_not_call_base_color_overload) {
    // AYUI-Audit-2026-08-26: the previous impl forwarded to the simple
    // color overload, which would record a second Rect/Text call with
    // the default TextStyle. Now we record exactly one Text call.
    MockRenderer renderer;
    IRenderBackend::TextStyle style;
    style.color = FVector4(0.5f, 0.5f, 0.5f, 1.0f);
    renderer.drawText(FRectangle(0.0f, 0.0f, 50.0f, 20.0f), L"only one", 12, style);
    CHECK(renderer.getDrawCalls().size() == 1u);
    CHECK(renderer.getDrawCalls()[0].type == MockRenderer::DrawCall::Text);
}

TEST_SUITE_END