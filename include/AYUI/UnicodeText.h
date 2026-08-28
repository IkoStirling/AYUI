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

// UAX #9 L2 visual reordering for a contiguous cluster range (normally one
// laid-out line). Returned values are indices into analysis.clusters.
std::vector<size_t> reorderUnicodeClusters(
    const UnicodeTextAnalysis& analysis,
    size_t firstCluster,
    size_t clusterCount);

} // namespace ayt::ui
