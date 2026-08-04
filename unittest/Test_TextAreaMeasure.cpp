#include "AYTest.h"
#include "AYTextArea.h"
#include "AYUIManager.h"
#include "AYMockRenderer.h"
#include "AYTextMeasure.h"
#include <iostream>

// =============================================================================
// PR-A1 — TextArea measureText 贯通
// -----------------------------------------------------------------------------
// TextArea pre-PR used literal `* 7.0f` for every text width site (selection
// rect, drawText bounds, caret x, IME underline, wrap-math, maxLineW). PR-A1
// routes all of them through `ayt::ui::measurePrefixWidth`, which prefers
// IRenderBackend::measureText when wired and falls back to per-char estimates
// otherwise. These tests pin both paths:
//   - Without a backend → fall back to the per-char estimate (unchanged
//     behaviour, no regression for unit tests / MockRenderer).
//   - With a backend that returns a known width → the rendered selection /
//     caret / underline reflect that width (proves the backend path is wired
//     end-to-end).
// =============================================================================

using namespace ayt::ui;
using namespace ayt::math;

namespace {

// Backend that returns a non-zero measureText width so we can prove the
// backend-aware path is reached. Width = nChars * kBackendCharWidth; that
// gives a deterministic value we can assert against in test bodies.
class WideMockRenderer : public MockRenderer {
public:
    static constexpr float kBackendCharWidth = 12.0f;  // bigger than 7 fallback

    IRenderBackend::TextMetrics measureText(const std::wstring& text,
                                            int fontSize,
                                            float maxWidth = 0.0f) const override {
        (void)fontSize; (void)maxWidth;
        IRenderBackend::TextMetrics m;
        m.width   = static_cast<float>(text.size()) * kBackendCharWidth;
        m.height  = static_cast<float>(fontSize);
        m.ascent  = m.height * 0.8f;
        m.descent = m.height * 0.2f;
        return m;
    }
};

}  // namespace

TEST_SUITE(AYUI_TextArea_Measure_A1)

TEST_CASE(textarea_measure_fallback_when_no_backend) {
    // No UIManager → measurePrefixWidth walks the per-char fallback.
    // ASCII: 7px/ch, non-ASCII: fontSize (=14).
    CHECK_FLOAT_EQ(ayt::ui::measurePrefixWidth(L"abc", 3), 21.0f, 1e-3f);
    CHECK_FLOAT_EQ(ayt::ui::measurePrefixWidth(L"abc", 0),  0.0f, 1e-3f);
    CHECK_FLOAT_EQ(ayt::ui::measurePrefixWidth(L"",    0),  0.0f, 1e-3f);
    CHECK_FLOAT_EQ(ayt::ui::measurePrefixWidth(L"aZ",  2), 14.0f, 1e-3f);
    // Clamp n > text.size().
    CHECK_FLOAT_EQ(ayt::ui::measurePrefixWidth(L"hi", 99), 14.0f, 1e-3f);
}

TEST_CASE(textarea_measure_uses_backend_when_wired) {
    UIManager ui;
    WideMockRenderer backend;
    ui.initialize(&backend);
    // Backend returns nChars * 12.0f — distinct from the 7px fallback.
    CHECK_FLOAT_EQ(
        ayt::ui::measurePrefixWidth(L"abcdef", 6, &backend),
        6.0f * WideMockRenderer::kBackendCharWidth, 1e-3f);
    CHECK_FLOAT_EQ(
        ayt::ui::measurePrefixWidth(L"a", 1, &backend),
        WideMockRenderer::kBackendCharWidth, 1e-3f);
    // Calling without an explicit backend also picks up the active one.
    CHECK_FLOAT_EQ(
        ayt::ui::measurePrefixWidth(L"ab", 2),
        2.0f * WideMockRenderer::kBackendCharWidth, 1e-3f);
    ui.shutdown();
}

TEST_CASE(textarea_caret_position_uses_backend_width) {
    UIManager ui;
    WideMockRenderer backend;
    ui.initialize(&backend);
    TextArea ta;
    ta.setText(L"hello");
    ta.setCaret(0, 5);  // end of "hello"
    ta.getDocumentAsFocusable()->setFocus(true);
    ta.render(backend);

    // Backend width 12px/char. For col 5 the prefix "hello" measures 60px,
    // and the document's kPaddingX = 6 gives an expected caret x of 66.
    // The fallback would be 5 * 7 + 6 = 41. We assert the caret x is in
    // the band [60, 75] to prove the backend path was used.
    //
    // PR-A1 R3 fix: identify the caret by its height (lineHeight - 2 ≈ 16)
    // and Y position (first line, so minY ≈ 1). The drawRect call
    // `{{x, y}, {1, lh-2}}` produces a degenerate negative-width rect
    // through the MockRenderer (an [ay-renderer-known-gaps] issue), so
    // we cannot use width as a filter — but the minX value is correct
    // (verified against the dumped draw calls).
    float caretX = -1.0f;
    bool found = false;
    for (const auto& dc : backend.getDrawCalls()) {
        if (dc.type == MockRenderer::DrawCall::Type::Rect &&
            dc.bounds.minY >= 0.5f && dc.bounds.minY <= 2.0f &&
            dc.bounds.height() >= 14.0f && dc.bounds.height() <= 18.0f) {
            caretX = dc.bounds.minX;
            found = true;
            break;
        }
    }
    CHECK(found);
    CHECK(caretX >= 60.0f);
    CHECK(caretX <= 75.0f);
    ui.shutdown();
}

TEST_CASE(textarea_selection_rect_uses_backend_width) {
    UIManager ui;
    WideMockRenderer backend;
    ui.initialize(&backend);
    TextArea ta;
    ta.setText(L"hello world");
    ta.getDocumentAsFocusable()->setFocus(true);
    // Select "world" (cols 6..11) on the same line.
    ta.setCaret(0, 11);
    ta.setSelection(0, 6, 0, 11);
    ta.render(backend);

    // The selection rect should have minX = 6 * 12 + 6 = 78 (with backend
    // width 12px/char + kPaddingX). Fallback would give 6 * 7 + 6 = 48.
    // We identify the selection by its colour band (alpha < 1) and
    // height (lh). MockRenderer's w = maxX - minX is computed at the
    // drawRect site using the actual emitted size, but the recorder
    // stores the min/max verbatim so minX is the trustworthy signal.
    float selMinX = -1.0f;
    bool found = false;
    for (const auto& dc : backend.getDrawCalls()) {
        // Selection rect: minX=78 (backend path), full line height, w
        // negative because of the MockRenderer [ay-renderer-known-gaps]
        // issue. minX is the trustworthy signal.
        if (dc.type == MockRenderer::DrawCall::Type::Rect &&
            dc.bounds.height() >= 16.0f && dc.bounds.height() <= 20.0f &&
            dc.bounds.minX >= 70.0f) {
            selMinX = dc.bounds.minX;
            found = true;
            break;
        }
    }
    CHECK(found);
    // Backend x0 = 6*12 + 6 = 78. Allow ±2px for floating-point.
    CHECK(selMinX >= 76.0f);
    CHECK(selMinX <= 80.0f);
    ui.shutdown();
}

TEST_CASE(textarea_wordwrap_uses_backend_width_for_em_proxy) {
    UIManager ui;
    WideMockRenderer backend;
    ui.initialize(&backend);
    TextArea ta;
    // One long line that, with wrap on, must split into >=2 visual rows.
    ta.setText(L"abcdefghij klmnopqrst uvwxyz");
    ta.setSize(FVector2(80.0f, 200.0f));
    ta.setWordWrap(false);
    const float hNoWrap = ta.getScrollView()->getContent()->getSize().y;

    ta.setWordWrap(true);
    const float hWrap = ta.getScrollView()->getContent()->getSize().y;

    // With the backend returning 12px/em (vs fallback 7), the wrap math
    // produces more visual rows for the same 80px viewport — the wrap
    // height must therefore be strictly greater.
    CHECK(hWrap > hNoWrap);
    ui.shutdown();
}

TEST_SUITE_END
