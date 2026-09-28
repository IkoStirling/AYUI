#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ayt::ui {

// Paragraph base direction. Auto follows the first strong character.
enum class TextDirection : uint8_t { Auto, LeftToRight, RightToLeft };

// One extended grapheme cluster in the source std::wstring. All public text
// indices remain std::wstring code-unit offsets, matching the rest of AYUI.
struct UnicodeTextCluster {
    size_t textStart = 0;
    size_t textLength = 0;
    uint8_t bidiLevel = 0;
    bool whitespace = false;
    bool hardBreak = false;
    bool softBreakAfter = false;

    bool rightToLeft() const { return (bidiLevel & 1u) != 0; }
};

struct UnicodeTextAnalysis {
    std::vector<UnicodeTextCluster> clusters;
    bool baseRightToLeft = false;
};

// Analyze grapheme boundaries, paragraph direction, bidi embedding levels and
// UAX #14-compatible soft line-break opportunities. Windows augments the
// portable rules with Uniscribe's current OS Unicode tables.
UnicodeTextAnalysis analyzeUnicodeText(
    const std::wstring& text,
    TextDirection direction = TextDirection::Auto);

// Strict UTF-8 conversion shared by committed text and IME paths. Invalid
// sequences are replaced with U+FFFD. On UTF-16 platforms supplementary
// code points are emitted as surrogate pairs, never as truncated wchar_t
// values. When requested, byteToTextOffset maps every UTF-8 byte offset (and
// the final end offset) to a std::wstring code-unit offset.
std::wstring decodeUtf8Text(
    const std::string& utf8,
    std::vector<size_t>* byteToTextOffset = nullptr);

/** @brief Encode UI code units as strict UTF-8, preserving embedded NULs.
 * UTF-16 surrogate pairs are combined; unpaired surrogates and invalid UTF-32
 * scalars become U+FFFD. No locale, platform API or normalization is involved.
 */
std::string encodeUtf8Text(const std::wstring& text);

// Grapheme-safe editing helpers. Indices are std::wstring code-unit offsets.
// floor/ceil keep an already-valid boundary unchanged; previous/next always
// move by one complete extended grapheme cluster when possible.
size_t floorGraphemeBoundary(const std::wstring& text, size_t offset);
size_t ceilGraphemeBoundary(const std::wstring& text, size_t offset);
size_t previousGraphemeBoundary(const std::wstring& text, size_t offset);
size_t nextGraphemeBoundary(const std::wstring& text, size_t offset);

// UAX #9 L2 visual reordering for a contiguous cluster range (normally one
// laid-out line). Returned values are indices into analysis.clusters.
std::vector<size_t> reorderUnicodeClusters(
    const UnicodeTextAnalysis& analysis,
    size_t firstCluster,
    size_t clusterCount);

} // namespace ayt::ui
