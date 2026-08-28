#include "AYTest.h"
#include "AYUI/UnicodeText.h"

using namespace ayt::ui;

TEST_SUITE(AYUI_UnicodeText)

TEST_CASE(unicode_grapheme_combining_mark_is_one_cluster) {
    const UnicodeTextAnalysis a = analyzeUnicodeText(L"e\x0301x");
    CHECK(a.clusters.size() == 2u);
    CHECK(a.clusters[0].textStart == 0u);
    CHECK(a.clusters[0].textLength == 2u);
    CHECK(a.clusters[1].textStart == 2u);
}

TEST_CASE(unicode_grapheme_emoji_zwj_sequence_is_one_cluster) {
    const std::wstring family = L"\U0001f469\u200d\U0001f4bb";
    const UnicodeTextAnalysis a = analyzeUnicodeText(family);
    CHECK(a.clusters.size() == 1u);
    CHECK(a.clusters[0].textLength == family.size());
}

TEST_CASE(unicode_grapheme_regional_indicators_pair_as_flags) {
    const std::wstring flags = L"\U0001f1e8\U0001f1f3\U0001f1fa\U0001f1f8";
    const UnicodeTextAnalysis a = analyzeUnicodeText(flags);
    CHECK(a.clusters.size() == 2u);
    CHECK(a.clusters[0].textLength * 2u == flags.size());
}

TEST_CASE(unicode_crlf_is_one_mandatory_break_cluster) {
    const UnicodeTextAnalysis a = analyzeUnicodeText(L"a\r\nb");
    CHECK(a.clusters.size() == 3u);
    CHECK(a.clusters[1].hardBreak);
    CHECK(a.clusters[1].textLength == 2u);
}

TEST_CASE(unicode_line_break_keeps_cjk_closing_punctuation_attached) {
    const UnicodeTextAnalysis a = analyzeUnicodeText(L"中文，继续");
    CHECK(a.clusters.size() == 5u);
    CHECK(!a.clusters[1].softBreakAfter);
    CHECK(a.clusters[2].softBreakAfter);
}

TEST_CASE(unicode_bidi_reorders_pure_rtl_line_visually) {
    const UnicodeTextAnalysis a = analyzeUnicodeText(
        L"\x05d0\x05d1\x05d2", TextDirection::RightToLeft);
    const std::vector<size_t> order = reorderUnicodeClusters(a, 0, a.clusters.size());
    CHECK(order.size() == 3u);
    CHECK(order[0] == 2u);
    CHECK(order[1] == 1u);
    CHECK(order[2] == 0u);
}

TEST_CASE(unicode_bidi_keeps_ltr_number_run_inside_rtl_paragraph) {
    const UnicodeTextAnalysis a = analyzeUnicodeText(
        L"\x05d0\x05d1 12", TextDirection::RightToLeft);
    size_t oddLevelCount = 0;
    size_t evenLevelCount = 0;
    for (const UnicodeTextCluster& cluster : a.clusters) {
        if (cluster.rightToLeft()) ++oddLevelCount;
        else ++evenLevelCount;
    }
    CHECK(a.baseRightToLeft);
    CHECK(oddLevelCount >= 2u);
    CHECK(evenLevelCount >= 2u);
}

TEST_CASE(unicode_utf8_decoder_preserves_supplementary_codepoint) {
    const std::wstring decoded = decodeUtf8Text("\xf0\x9f\x98\x80");
    CHECK(decoded == L"\U0001f600");
    const UnicodeTextAnalysis analysis = analyzeUnicodeText(decoded);
    CHECK(analysis.clusters.size() == 1u);
    CHECK(analysis.clusters[0].textLength == decoded.size());
}

TEST_CASE(unicode_utf8_decoder_replaces_overlong_and_surrogate_sequences) {
    const std::wstring decoded = decodeUtf8Text("\xc0\xaf\xed\xa0\x80");
    CHECK(!decoded.empty());
    for (wchar_t ch : decoded) CHECK(ch == static_cast<wchar_t>(0xfffd));
}

TEST_CASE(unicode_grapheme_edit_boundaries_never_split_cluster) {
    const std::wstring text = L"e\x0301\U0001f469\u200d\U0001f4bbx";
    const UnicodeTextAnalysis analysis = analyzeUnicodeText(text);
    CHECK(analysis.clusters.size() == 3u);
    const size_t firstEnd = analysis.clusters[0].textLength;
    const size_t secondEnd = firstEnd + analysis.clusters[1].textLength;
    CHECK(nextGraphemeBoundary(text, 0) == firstEnd);
    CHECK(previousGraphemeBoundary(text, secondEnd) == firstEnd);
    CHECK(floorGraphemeBoundary(text, firstEnd + 1u) == firstEnd);
    CHECK(ceilGraphemeBoundary(text, firstEnd + 1u) == secondEnd);
}

TEST_SUITE_END
