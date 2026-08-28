#include "AYUI/UnicodeText.h"

#include <algorithm>
#include <climits>
#include <limits>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <Windows.h>
#  include <usp10.h>
#endif

namespace ayt::ui {
namespace {

struct CodePoint {
    uint32_t value = 0;
    size_t start = 0;
    size_t length = 1;
};

std::vector<CodePoint> decode(const std::wstring& text) {
    std::vector<CodePoint> result;
    result.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        CodePoint cp;
        cp.start = i;
        cp.value = static_cast<uint32_t>(text[i]);
#if WCHAR_MAX <= 0xffff
        if (cp.value >= 0xd800u && cp.value <= 0xdbffu && i + 1 < text.size()) {
            const uint32_t low = static_cast<uint32_t>(text[i + 1]);
            if (low >= 0xdc00u && low <= 0xdfffu) {
                cp.value = 0x10000u + ((cp.value - 0xd800u) << 10)
                         + (low - 0xdc00u);
                cp.length = 2;
            }
        }
#endif
        result.push_back(cp);
        i += cp.length;
    }
    return result;
}

bool inRange(uint32_t cp, uint32_t first, uint32_t last) {
    return cp >= first && cp <= last;
}

bool isControl(uint32_t cp) {
    return cp <= 0x001fu || inRange(cp, 0x007fu, 0x009fu)
        || cp == 0x2028u || cp == 0x2029u;
}

bool isExtend(uint32_t cp) {
    return inRange(cp, 0x0300u, 0x036fu)
        || inRange(cp, 0x0483u, 0x0489u)
        || inRange(cp, 0x0591u, 0x05bdu)
        || cp == 0x05bfu || inRange(cp, 0x05c1u, 0x05c2u)
        || inRange(cp, 0x0610u, 0x061au)
        || inRange(cp, 0x064bu, 0x065fu)
        || cp == 0x0670u || inRange(cp, 0x06d6u, 0x06edu)
        || inRange(cp, 0x0711u, 0x0711u)
        || inRange(cp, 0x0730u, 0x074au)
        || inRange(cp, 0x07a6u, 0x07b0u)
        || inRange(cp, 0x07ebu, 0x07f3u)
        || inRange(cp, 0x0816u, 0x082du)
        || inRange(cp, 0x0859u, 0x085bu)
        || inRange(cp, 0x08d3u, 0x0902u)
        || inRange(cp, 0x093au, 0x093cu)
        || cp == 0x094du || inRange(cp, 0x0951u, 0x0957u)
        || inRange(cp, 0x0962u, 0x0963u)
        || inRange(cp, 0x1ab0u, 0x1affu)
        || inRange(cp, 0x1dc0u, 0x1dffu)
        || inRange(cp, 0x20d0u, 0x20ffu)
        || inRange(cp, 0xfe00u, 0xfe0fu)
        || inRange(cp, 0xfe20u, 0xfe2fu)
        || inRange(cp, 0x1f3fbu, 0x1f3ffu)
        || inRange(cp, 0xe0100u, 0xe01efu);
}

bool isSpacingMark(uint32_t cp) {
    return cp == 0x0903u || inRange(cp, 0x093bu, 0x0940u)
        || inRange(cp, 0x0949u, 0x094cu) || inRange(cp, 0x0982u, 0x0983u)
        || inRange(cp, 0x09beu, 0x09c0u) || inRange(cp, 0x0a3eu, 0x0a40u)
        || inRange(cp, 0x0abeu, 0x0ac0u) || inRange(cp, 0x0b3eu, 0x0b40u)
        || inRange(cp, 0x0bbeu, 0x0bc2u) || inRange(cp, 0x0c01u, 0x0c03u)
        || inRange(cp, 0x0cc0u, 0x0cc4u) || inRange(cp, 0x0d3eu, 0x0d40u)
        || inRange(cp, 0x0f3eu, 0x0f3fu) || inRange(cp, 0x102bu, 0x1031u)
        || inRange(cp, 0x17b6u, 0x17c8u);
}

bool isPrepend(uint32_t cp) {
    return inRange(cp, 0x0600u, 0x0605u) || cp == 0x06ddu
        || cp == 0x070fu || inRange(cp, 0x0890u, 0x0891u)
        || cp == 0x08e2u || cp == 0x110bdu || cp == 0x110cdu;
}

bool isRegionalIndicator(uint32_t cp) {
    return inRange(cp, 0x1f1e6u, 0x1f1ffu);
}

bool isExtendedPictographic(uint32_t cp) {
    return inRange(cp, 0x1f000u, 0x1faffu)
        || inRange(cp, 0x2600u, 0x27bfu)
        || inRange(cp, 0x2300u, 0x23ffu);
}

enum class HangulClass { None, L, V, T, LV, LVT };

HangulClass hangulClass(uint32_t cp) {
    if (inRange(cp, 0x1100u, 0x115fu) || inRange(cp, 0xa960u, 0xa97cu)) {
        return HangulClass::L;
    }
    if (inRange(cp, 0x1160u, 0x11a7u) || inRange(cp, 0xd7b0u, 0xd7c6u)) {
        return HangulClass::V;
    }
    if (inRange(cp, 0x11a8u, 0x11ffu) || inRange(cp, 0xd7cbu, 0xd7fbu)) {
        return HangulClass::T;
    }
    if (inRange(cp, 0xac00u, 0xd7a3u)) {
        return ((cp - 0xac00u) % 28u) == 0u ? HangulClass::LV
                                            : HangulClass::LVT;
    }
    return HangulClass::None;
}

bool shouldJoinGrapheme(const std::vector<CodePoint>& cps, size_t index) {
    if (index == 0) return false;
    const uint32_t prev = cps[index - 1].value;
    const uint32_t curr = cps[index].value;
    if (prev == 0x000du && curr == 0x000au) return true;
    if (isControl(prev) || isControl(curr) || prev == 0x000du || prev == 0x000au
        || curr == 0x000du || curr == 0x000au) return false;

    const HangulClass a = hangulClass(prev);
    const HangulClass b = hangulClass(curr);
    if (a == HangulClass::L && (b == HangulClass::L || b == HangulClass::V
        || b == HangulClass::LV || b == HangulClass::LVT)) return true;
    if ((a == HangulClass::LV || a == HangulClass::V)
        && (b == HangulClass::V || b == HangulClass::T)) return true;
    if ((a == HangulClass::LVT || a == HangulClass::T) && b == HangulClass::T) {
        return true;
    }
    if (isExtend(curr) || curr == 0x200du || isSpacingMark(curr)) return true;
    if (isPrepend(prev)) return true;

    if (isExtendedPictographic(curr)) {
        size_t j = index;
        if (j > 0 && cps[j - 1].value == 0x200du) {
            --j;
            while (j > 0 && isExtend(cps[j - 1].value)) --j;
            if (j > 0 && isExtendedPictographic(cps[j - 1].value)) return true;
        }
    }
    if (isRegionalIndicator(prev) && isRegionalIndicator(curr)) {
        size_t count = 0;
        for (size_t j = index; j > 0 && isRegionalIndicator(cps[j - 1].value); --j) {
            ++count;
        }
        return (count & 1u) != 0u;
    }
    return false;
}

bool isWhitespace(uint32_t cp) {
    return cp == 0x0009u || cp == 0x0020u || cp == 0x1680u
        || inRange(cp, 0x2000u, 0x200au) || cp == 0x205fu || cp == 0x3000u;
}

bool isRtlStrong(uint32_t cp) {
    return inRange(cp, 0x0590u, 0x08ffu)
        || inRange(cp, 0xfb1du, 0xfdffu)
        || inRange(cp, 0xfe70u, 0xfeffu)
        || inRange(cp, 0x10800u, 0x10fffu)
        || inRange(cp, 0x1e800u, 0x1eeffu);
}

bool isEuropeanOrArabicNumber(uint32_t cp) {
    return inRange(cp, 0x0030u, 0x0039u) || inRange(cp, 0x0660u, 0x0669u)
        || inRange(cp, 0x06f0u, 0x06f9u);
}

bool isCjk(uint32_t cp) {
    return inRange(cp, 0x2e80u, 0x9fffu) || inRange(cp, 0xac00u, 0xd7a3u)
        || inRange(cp, 0xf900u, 0xfaffu) || inRange(cp, 0x20000u, 0x323afu)
        || isExtendedPictographic(cp);
}

bool isOpeningPunctuation(uint32_t cp) {
    return cp == L'(' || cp == L'[' || cp == L'{' || cp == 0x2018u
        || cp == 0x201cu || cp == 0x3008u || cp == 0x300au
        || cp == 0x300cu || cp == 0x300eu || cp == 0x3010u
        || cp == 0x3014u || cp == 0x3016u;
}

bool isClosingPunctuation(uint32_t cp) {
    return cp == L')' || cp == L']' || cp == L'}' || cp == L',' || cp == L'.'
        || cp == L'!' || cp == L'?' || cp == L':' || cp == L';'
        || cp == 0x2019u || cp == 0x201du || cp == 0x3001u || cp == 0x3002u
        || cp == 0xff0cu || cp == 0xff0eu || cp == 0xff01u || cp == 0xff1fu
        || cp == 0x3009u || cp == 0x300bu || cp == 0x300du || cp == 0x300fu
        || cp == 0x3011u || cp == 0x3015u || cp == 0x3017u;
}

bool isBreakPunctuation(uint32_t cp) {
    return cp == L'-' || cp == L'/' || cp == 0x2010u || cp == 0x2013u
        || cp == 0x2014u;
}

size_t codePointIndexAt(const std::vector<CodePoint>& cps, size_t textIndex) {
    const auto it = std::upper_bound(cps.begin(), cps.end(), textIndex,
        [](size_t value, const CodePoint& cp) { return value < cp.start; });
    if (it == cps.begin()) return 0;
    return static_cast<size_t>((it - cps.begin()) - 1);
}

void assignPortableBidi(UnicodeTextAnalysis& out,
                        const std::vector<CodePoint>& cps,
                        TextDirection requested) {
    bool baseRtl = requested == TextDirection::RightToLeft;
    if (requested == TextDirection::Auto) {
        for (const CodePoint& cp : cps) {
            if (isRtlStrong(cp.value)) { baseRtl = true; break; }
            if (!isWhitespace(cp.value) && !isControl(cp.value)
                && !isEuropeanOrArabicNumber(cp.value)) break;
        }
    }
    out.baseRightToLeft = baseRtl;
    const uint8_t base = baseRtl ? 1u : 0u;
    for (UnicodeTextCluster& cluster : out.clusters) {
        const size_t cpIndex = codePointIndexAt(cps, cluster.textStart);
        const uint32_t cp = cps.empty() ? 0u : cps[cpIndex].value;
        if (isRtlStrong(cp)) cluster.bidiLevel = 1u;
        else if (isEuropeanOrArabicNumber(cp)) cluster.bidiLevel = baseRtl ? 2u : 0u;
        else if (!isWhitespace(cp) && !isControl(cp)) cluster.bidiLevel = baseRtl ? 2u : 0u;
        else cluster.bidiLevel = base;
    }
}

#if defined(_WIN32)
void augmentWithUniscribe(const std::wstring& text,
                          TextDirection requested,
                          UnicodeTextAnalysis& out) {
    if (text.empty() || text.size() > static_cast<size_t>(INT_MAX)) return;
    const int length = static_cast<int>(text.size());
    std::vector<SCRIPT_ITEM> items(static_cast<size_t>(length) + 2u);
    SCRIPT_STATE state{};
    if (requested == TextDirection::RightToLeft) state.uBidiLevel = 1;
    int itemCount = 0;
    HRESULT hr = ::ScriptItemize(text.data(), length, length + 1, nullptr,
                                 requested == TextDirection::Auto ? nullptr : &state,
                                 items.data(), &itemCount);
    if (FAILED(hr) || itemCount <= 0) return;

    std::vector<uint8_t> levels(static_cast<size_t>(length),
                                requested == TextDirection::RightToLeft ? 1u : 0u);
    std::vector<SCRIPT_LOGATTR> attrs(static_cast<size_t>(length));
    for (int item = 0; item < itemCount; ++item) {
        const int begin = items[static_cast<size_t>(item)].iCharPos;
        const int end = items[static_cast<size_t>(item + 1)].iCharPos;
        if (begin < 0 || end <= begin || end > length) continue;
        const uint8_t level = static_cast<uint8_t>(items[static_cast<size_t>(item)].a.s.uBidiLevel);
        std::fill(levels.begin() + begin, levels.begin() + end, level);
        ::ScriptBreak(text.data() + begin, end - begin,
                      &items[static_cast<size_t>(item)].a, attrs.data() + begin);
    }

    if (requested == TextDirection::Auto) {
        out.baseRightToLeft = !items.empty()
            && (items.front().a.s.uBidiLevel & 1u) != 0u;
    } else {
        out.baseRightToLeft = requested == TextDirection::RightToLeft;
    }
    for (UnicodeTextCluster& cluster : out.clusters) {
        const size_t start = std::min(cluster.textStart, levels.size() - 1u);
        cluster.bidiLevel = levels[start];
        // SCRIPT_LOGATTR describes a caret/break opportunity *before* the
        // corresponding UTF-16 code unit. The opportunity after this cluster
        // therefore lives on the next code unit, not its final code unit.
        const size_t after = cluster.textStart + cluster.textLength;
        if (after < attrs.size() && attrs[after].fSoftBreak) {
            cluster.softBreakAfter = true;
        }
    }
}
#endif

} // namespace

UnicodeTextAnalysis analyzeUnicodeText(const std::wstring& text,
                                       TextDirection direction) {
    UnicodeTextAnalysis out;
    const std::vector<CodePoint> cps = decode(text);
    if (cps.empty()) {
        out.baseRightToLeft = direction == TextDirection::RightToLeft;
        return out;
    }

    for (size_t i = 0; i < cps.size();) {
        const size_t first = i;
        ++i;
        while (i < cps.size() && shouldJoinGrapheme(cps, i)) ++i;
        UnicodeTextCluster cluster;
        cluster.textStart = cps[first].start;
        const CodePoint& last = cps[i - 1];
        cluster.textLength = last.start + last.length - cluster.textStart;
        cluster.whitespace = isWhitespace(cps[first].value);
        cluster.hardBreak = cps[first].value == 0x000au || cps[first].value == 0x000du
            || cps[first].value == 0x2028u || cps[first].value == 0x2029u;
        out.clusters.push_back(cluster);
    }

    for (size_t i = 0; i < out.clusters.size(); ++i) {
        UnicodeTextCluster& cluster = out.clusters[i];
        const size_t cpIndex = codePointIndexAt(cps, cluster.textStart);
        const uint32_t cp = cps[cpIndex].value;
        cluster.softBreakAfter = cluster.hardBreak || cluster.whitespace
            || isBreakPunctuation(cp);
        if (i + 1 < out.clusters.size()) {
            const size_t nextCpIndex = codePointIndexAt(cps, out.clusters[i + 1].textStart);
            const uint32_t next = cps[nextCpIndex].value;
            if (isOpeningPunctuation(cp) || isClosingPunctuation(next)) {
                cluster.softBreakAfter = false;
            } else if (isClosingPunctuation(cp) || (isCjk(cp) && isCjk(next))) {
                cluster.softBreakAfter = true;
            }
        }
    }

    assignPortableBidi(out, cps, direction);
#if defined(_WIN32)
    augmentWithUniscribe(text, direction, out);
#endif
    return out;
}

std::vector<size_t> reorderUnicodeClusters(const UnicodeTextAnalysis& analysis,
                                           size_t firstCluster,
                                           size_t clusterCount) {
    std::vector<size_t> order;
    if (firstCluster >= analysis.clusters.size() || clusterCount == 0) return order;
    const size_t end = std::min(analysis.clusters.size(), firstCluster + clusterCount);
    order.reserve(end - firstCluster);
    uint8_t maxLevel = 0;
    uint8_t minOdd = std::numeric_limits<uint8_t>::max();
    for (size_t i = firstCluster; i < end; ++i) {
        order.push_back(i);
        const uint8_t level = analysis.clusters[i].bidiLevel;
        maxLevel = std::max(maxLevel, level);
        if ((level & 1u) != 0u) minOdd = std::min(minOdd, level);
    }
    if (minOdd == std::numeric_limits<uint8_t>::max()) return order;
    for (int level = static_cast<int>(maxLevel); level >= static_cast<int>(minOdd); --level) {
        size_t i = 0;
        while (i < order.size()) {
            while (i < order.size()
                && analysis.clusters[order[i]].bidiLevel < level) ++i;
            const size_t begin = i;
            while (i < order.size()
                && analysis.clusters[order[i]].bidiLevel >= level) ++i;
            std::reverse(order.begin() + static_cast<std::ptrdiff_t>(begin),
                         order.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }
    return order;
}

} // namespace ayt::ui
