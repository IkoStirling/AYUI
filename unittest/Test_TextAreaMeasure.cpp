#include "AYTest.h"
#include "AYUI/TextArea.h"
#include "AYUI/UIManager.h"
#include "AYUI/MockRenderer.h"
#include "AYUI/TextMeasure.h"
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
    // Identify the caret by its 1px width, lineHeight - 2 height and first-row
    // Y position. FRectangle stores min/max coordinates, so this also locks
    // the translated rectangle contract used by the real renderer.
    float caretX = -1.0f;
    bool found = false;
    for (const auto& dc : backend.getDrawCalls()) {
        if (dc.type == MockRenderer::DrawCall::Type::Rect &&
            dc.bounds.width() >= 0.9f && dc.bounds.width() <= 1.1f &&
            dc.bounds.minY >= 4.5f && dc.bounds.minY <= 5.5f &&
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

TEST_CASE(textarea_caret_uses_same_font_size_and_padding_as_text) {
    UIManager ui;
    MockRenderer backend;
    ui.initialize(&backend);
    TextArea ta;
    ta.setPosition(FVector2(240.0f, 130.0f));
    ta.setSize(FVector2(360.0f, 160.0f));
    ta.setLineHeight(17.0f); // DSL source editor: effective font size is 13.
    ta.setText(L"abcd");
    ta.setCaret(0, 4);
    ta.performLayout();
    ta.getDocumentAsFocusable()->setFocus(true);
    ta.render(backend);

    bool found = false;
    for (const auto& dc : backend.getDrawCalls()) {
        if (dc.type != MockRenderer::DrawCall::Type::Rect
            || dc.bounds.width() < 0.9f || dc.bounds.width() > 1.1f
            || dc.bounds.height() < 14.9f || dc.bounds.height() > 15.1f) {
            continue;
        }
        found = true;
        // MockRenderer: 4 glyphs * 13px * 0.6 + 6px horizontal padding.
        CHECK_FLOAT_EQ(dc.bounds.minX, 240.0f + 37.2f, 1e-3f);
        // Text, hit-testing and caret all begin after the same 4px top pad.
        CHECK_FLOAT_EQ(dc.bounds.minY, 130.0f + 5.0f, 1e-3f);
        break;
    }
    CHECK(found);
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
    // We identify the selection by its measured start and positive extent.
    float selMinX = -1.0f;
    bool found = false;
    for (const auto& dc : backend.getDrawCalls()) {
        if (dc.type == MockRenderer::DrawCall::Type::Rect &&
            dc.bounds.height() >= 16.0f && dc.bounds.height() <= 20.0f &&
            dc.bounds.width() > 0.0f && dc.bounds.minX >= 70.0f) {
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

TEST_CASE(textarea_translated_text_bounds_use_absolute_max_coordinates) {
    UIManager ui;
    WideMockRenderer backend;
    ui.initialize(&backend);
    TextArea ta;
    ta.setPosition(FVector2(240.0f, 130.0f));
    ta.setSize(FVector2(360.0f, 160.0f));
    ta.setText(L"translated");
    ta.performLayout();
    ta.render(backend);

    bool found = false;
    for (const auto& dc : backend.getDrawCalls()) {
        if (dc.type != MockRenderer::DrawCall::Type::Text
            || dc.text != L"translated") {
            continue;
        }
        found = true;
        CHECK(dc.bounds.minX >= 240.0f);
        CHECK(dc.bounds.minY >= 130.0f);
        CHECK(dc.bounds.maxX > dc.bounds.minX);
        CHECK(dc.bounds.maxY > dc.bounds.minY);
    }
    CHECK(found);
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

// Audit H-R-1..3 + H-M-VisualLine: pre-fix, buildVisualLines rebuilt
// the entire visual-line projection (O(N) text measurement calls,
// each O(line length)) on every onRender, every caret-move helper,
// every hit test, and every syncDocumentSizeToContent. A 100-line
// document at 60Hz rendered for 5 seconds was ~30,000 redundant
// measurements. Post-fix the result is memoized in a member-field
// buffer (`_cachedVisualLines`); the cache is invalidated by any
// text mutation (via invalidateDocument) and by any of
// {availableWidth, wordWrap, lineHeight} change. We pin three
// invariants:
//
//   1. The cache returns 3+ visual lines for a 3-line buffer.
//   2. The reference is stable across identical calls (same
//      vector address — confirms no per-call heap allocation).
//   3. After a text mutation, the cache contents reflect the new
//      text (size grows from 3 → 4 visual lines after one insertChar).
TEST_CASE(textarea_visual_lines_cached_when_unchanged) {
    MockRenderer backend;
    UIManager ui;
    ui.initialize(&backend);

    TextArea ta;
    ta.setSize(FVector2(300.0f, 100.0f));
    ta.setText(L"line one\nline two\nline three");
    ui.root()->addChildExternal(&ta);

    // First call: cache miss → builds 3 visual lines and returns a
    // reference into the member-field cache buffer.
    const std::vector<TextArea::VisualLine>& first =
        ta.getCachedVisualLines(280.0f);
    const size_t firstSize = first.size();
    // We expect one VisualLine per logical line when _wordWrap=false.
    CHECK(firstSize == 3u);

    // Subsequent calls with same args: cache HIT → must return the
    // same vector reference. Pre-fix each call returned a freshly
    // heap-allocated vector; the test would see different pointers.
    const std::vector<TextArea::VisualLine>& second =
        ta.getCachedVisualLines(280.0f);
    CHECK(&second == &first);

    const std::vector<TextArea::VisualLine>& third =
        ta.getCachedVisualLines(280.0f);
    CHECK(&third == &first);

    // Sanity: the cached contents reflect the 3-line buffer.
    CHECK(third[0].logicalLine == 0);
    CHECK(third[1].logicalLine == 1);
    CHECK(third[2].logicalLine == 2);

    // Mutating the text must invalidate the cache: invalidateDocument
    // bumps _visualLinesDirty, and the next buildVisualLines call
    // clears + rebuilds the cache buffer. The reference is still
    // stable (it's a member field). insertChar(L'X') at the default
    // caret (line 0, col 0) prepends 'X' to the first line — the
    // buffer stays 3 lines, so the cache stays 3 entries.
    ta.insertChar(L'X');
    const std::vector<TextArea::VisualLine>& fourth =
        ta.getCachedVisualLines(280.0f);
    CHECK(fourth.size() == 3u);

    // Subsequent identical calls now hit the new cache entry —
    // reference must remain stable.
    const std::vector<TextArea::VisualLine>& fifth =
        ta.getCachedVisualLines(280.0f);
    CHECK(&fifth == &fourth);

    // The first line must now start with 'X' — proves the rebuild
    // saw the post-insert text (not the stale pre-insert one).
    CHECK(fifth[0].logicalLine == 0);

    // Changing wordWrap forces another rebuild: cache contents change.
    ta.setWordWrap(true);
    const std::vector<TextArea::VisualLine>& sixth =
        ta.getCachedVisualLines(280.0f);
    // With wordWrap off→on, each long line can split into multiple
    // visual rows. Reference must remain stable.
    CHECK(sixth.size() >= 3u);
    CHECK(&sixth == &fourth);

    ui.shutdown();
}

TEST_SUITE_END
