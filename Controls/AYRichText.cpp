#include "AYUI/RichText.h"
#include "AYUI/IRenderBackend.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ayt::ui {
namespace {

struct LayoutAtom {
    size_t analysisIndex = 0;
    size_t runIndex = 0;
    size_t runTextStart = 0;
    size_t documentTextStart = 0;
    size_t textLength = 0;
    std::wstring text;
    float width = 0.0f;
    float height = 0.0f;
    uint8_t bidiLevel = 0;
    bool whitespace = false;
    bool newline = false;
    bool softBreakAfter = false;
    bool synthetic = false;

    bool rightToLeft() const { return (bidiLevel & 1u) != 0u; }
};

struct AtomLine {
    std::vector<LayoutAtom> atoms;
    float width = 0.0f;
    float height = 0.0f;
    bool paragraphEnd = false;
};

IRenderBackend::TextStyle makeTextStyle(const RichRun& run,
                                         TextDirection direction) {
    IRenderBackend::TextStyle style;
    style.color = run.color;
    style.bold = run.bold;
    style.italic = run.italic;
    style.fontFamily = run.fontFamily;
    style.fontWeight = std::clamp(run.bold && run.fontWeight < 600
        ? 700 : run.fontWeight, 100, 900);
    style.direction = direction;
    style.language = run.language;
    style.letterSpacing = static_cast<int>(std::lround(run.letterSpacing));
    style.valign = IRenderBackend::TextStyle::VAlign::Top;
    return style;
}

void recomputeLine(AtomLine& line) {
    line.width = 0.0f;
    line.height = 0.0f;
    for (const LayoutAtom& atom : line.atoms) {
        line.width += atom.width;
        line.height = std::max(line.height, atom.height);
    }
}

std::vector<size_t> visualOrder(const AtomLine& line) {
    std::vector<size_t> order(line.atoms.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    if (order.empty()) return order;
    uint8_t maxLevel = 0;
    uint8_t minOdd = std::numeric_limits<uint8_t>::max();
    for (const LayoutAtom& atom : line.atoms) {
        maxLevel = std::max(maxLevel, atom.bidiLevel);
        if (atom.rightToLeft()) minOdd = std::min(minOdd, atom.bidiLevel);
    }
    if (minOdd == std::numeric_limits<uint8_t>::max()) return order;
    for (int level = static_cast<int>(maxLevel); level >= static_cast<int>(minOdd); --level) {
        size_t i = 0;
        while (i < order.size()) {
            while (i < order.size() && line.atoms[order[i]].bidiLevel < level) ++i;
            const size_t begin = i;
            while (i < order.size() && line.atoms[order[i]].bidiLevel >= level) ++i;
            std::reverse(order.begin() + static_cast<std::ptrdiff_t>(begin),
                         order.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }
    return order;
}

void appendCaretStop(RichTextFragment& fragment, size_t textIndex, float x) {
    if (!fragment.caretTextIndices.empty()
        && fragment.caretTextIndices.back() == textIndex
        && std::abs(fragment.caretX.back() - x) < 0.001f) return;
    fragment.caretTextIndices.push_back(textIndex);
    fragment.caretX.push_back(x);
}

} // namespace

RichText::RichText() { setSize(math::FVector2(200.0f, 24.0f)); }
RichText::~RichText() = default;

void RichText::addRun(const std::wstring& text, const math::FVector4& color,
                      int fontSize) {
    RichRun run;
    run.text = text;
    run.color = color;
    run.fontSize = fontSize > 0 ? fontSize : _defaultFontSize;
    addRun(run);
}

void RichText::addRun(const RichRun& run) {
    RichRun normalized = run;
    if (normalized.fontSize <= 0) normalized.fontSize = _defaultFontSize;
    normalized.fontWeight = std::clamp(normalized.fontWeight, 100, 900);
    _runs.push_back(std::move(normalized));
    markBoundsDirty();
    markDirty();
}

void RichText::insertRun(size_t index, const RichRun& run) {
    RichRun normalized = run;
    if (normalized.fontSize <= 0) normalized.fontSize = _defaultFontSize;
    normalized.fontWeight = std::clamp(normalized.fontWeight, 100, 900);
    index = std::min(index, _runs.size());
    _runs.insert(_runs.begin() + static_cast<std::ptrdiff_t>(index),
                 std::move(normalized));
    markBoundsDirty();
    markDirty();
}

bool RichText::setRun(size_t index, const RichRun& run) {
    if (index >= _runs.size()) return false;
    RichRun normalized = run;
    if (normalized.fontSize <= 0) normalized.fontSize = _defaultFontSize;
    normalized.fontWeight = std::clamp(normalized.fontWeight, 100, 900);
    _runs[index] = std::move(normalized);
    markBoundsDirty();
    markDirty();
    return true;
}

bool RichText::removeRun(size_t index) {
    if (index >= _runs.size()) return false;
    _runs.erase(_runs.begin() + static_cast<std::ptrdiff_t>(index));
    markBoundsDirty();
    markDirty();
    return true;
}

bool RichText::moveRun(size_t fromIndex, size_t toIndex) {
    if (fromIndex >= _runs.size() || toIndex >= _runs.size() ||
        fromIndex == toIndex) {
        return false;
    }
    RichRun moving = std::move(_runs[fromIndex]);
    _runs.erase(_runs.begin() + static_cast<std::ptrdiff_t>(fromIndex));
    _runs.insert(_runs.begin() + static_cast<std::ptrdiff_t>(toIndex),
                 std::move(moving));
    markBoundsDirty();
    markDirty();
    return true;
}

void RichText::clearRuns() {
    _runs.clear();
    markBoundsDirty();
    markDirty();
}

std::wstring RichText::getPlainText() const {
    std::wstring result;
    for (const RichRun& run : _runs) result += run.text;
    return result;
}

void RichText::setWrapWidth(float width) {
    _wrapWidth = std::max(0.0f, width);
    _wrapMode = _wrapWidth > 0.0f ? RichTextWrapMode::Word
                                  : RichTextWrapMode::NoWrap;
    markBoundsDirty();
    markDirty();
}

float RichText::measureTextWidth(IRenderBackend& renderer,
                                 const std::wstring& text,
                                 const RichRun& run,
                                 TextDirection direction) const {
    if (text.empty()) return 0.0f;
    const auto metrics = renderer.measureText(text, run.fontSize,
                                              makeTextStyle(run, direction));
    if (metrics.width > 0.0f) return metrics.width;
    return static_cast<float>(text.size()) * 0.5f
        * static_cast<float>(run.fontSize);
}

RichTextLayout RichText::layout(IRenderBackend& renderer) const {
    RichTextLayout result;
    const math::FRectangle bounds = getWorldBounds();
    const float boundsWidth = std::max(0.0f, bounds.maxX - bounds.minX);
    float availableWidth = boundsWidth;
    if (_wrapWidth > 0.0f) availableWidth = std::min(availableWidth, _wrapWidth);
    const bool wrap = _wrapMode != RichTextWrapMode::NoWrap && availableWidth > 0.0f;

    const std::wstring plainText = getPlainText();
    const UnicodeTextAnalysis analysis = analyzeUnicodeText(plainText, _textDirection);
    std::vector<size_t> runDocumentStarts(_runs.size() + 1u, 0u);
    for (size_t i = 0; i < _runs.size(); ++i) {
        runDocumentStarts[i + 1] = runDocumentStarts[i] + _runs[i].text.size();
    }

    std::vector<LayoutAtom> atoms;
    for (size_t clusterIndex = 0; clusterIndex < analysis.clusters.size(); ++clusterIndex) {
        const UnicodeTextCluster& cluster = analysis.clusters[clusterIndex];
        size_t position = cluster.textStart;
        size_t remaining = cluster.textLength;
        while (remaining > 0 && !_runs.empty()) {
            auto upper = std::upper_bound(runDocumentStarts.begin(),
                                          runDocumentStarts.end(), position);
            size_t runIndex = upper == runDocumentStarts.begin() ? 0u
                : static_cast<size_t>((upper - runDocumentStarts.begin()) - 1);
            if (runIndex >= _runs.size()) runIndex = _runs.size() - 1u;
            const size_t runEnd = runDocumentStarts[runIndex + 1u];
            const size_t partLength = std::min(remaining, runEnd - position);
            if (partLength == 0) break;
            const RichRun& run = _runs[runIndex];
            LayoutAtom atom;
            atom.analysisIndex = clusterIndex;
            atom.runIndex = runIndex;
            atom.runTextStart = position - runDocumentStarts[runIndex];
            atom.documentTextStart = position;
            atom.textLength = partLength;
            atom.text = plainText.substr(position, partLength);
            atom.height = std::max(1.0f,
                static_cast<float>(run.fontSize) * _lineHeight);
            atom.bidiLevel = cluster.bidiLevel;
            atom.whitespace = cluster.whitespace;
            atom.newline = cluster.hardBreak;
            // A style run may split a Unicode cluster. The cluster's line
            // break opportunity belongs only to its final piece; exposing it
            // on an earlier piece would permit wrapping inside a grapheme.
            atom.softBreakAfter = cluster.softBreakAfter && partLength == remaining;
            if (!atom.newline) {
                const TextDirection atomDirection = atom.rightToLeft()
                    ? TextDirection::RightToLeft : TextDirection::LeftToRight;
                atom.width = measureTextWidth(renderer, atom.text, run, atomDirection);
            }
            atoms.push_back(std::move(atom));
            position += partLength;
            remaining -= partLength;
        }
    }

    // Shape only final visual-line spans. Shaping the full paragraph before
    // wrapping gives Arabic joining and ligatures context that drawText no
    // longer has after the line is split, so measured and submitted advances
    // can diverge. This helper is applied after line breaking and again after
    // any overflow repair.
    auto shapeAtomSpan = [&](std::vector<LayoutAtom>& shapedAtoms) {
        for (size_t begin = 0; begin < shapedAtoms.size();) {
            if (shapedAtoms[begin].newline || shapedAtoms[begin].synthetic) {
                ++begin;
                continue;
            }
            size_t end = begin + 1u;
            while (end < shapedAtoms.size() && !shapedAtoms[end].newline
                && !shapedAtoms[end].synthetic
                && shapedAtoms[end].runIndex == shapedAtoms[begin].runIndex
                && shapedAtoms[end].rightToLeft() == shapedAtoms[begin].rightToLeft()
                && shapedAtoms[end - 1].runTextStart + shapedAtoms[end - 1].textLength
                    == shapedAtoms[end].runTextStart) ++end;
            const RichRun& run = _runs[shapedAtoms[begin].runIndex];
            const size_t localStart = shapedAtoms[begin].runTextStart;
            const size_t localEnd = shapedAtoms[end - 1].runTextStart
                + shapedAtoms[end - 1].textLength;
            const std::wstring span = run.text.substr(localStart, localEnd - localStart);
            const TextDirection spanDirection = shapedAtoms[begin].rightToLeft()
                ? TextDirection::RightToLeft : TextDirection::LeftToRight;
            const auto shaped = renderer.shapeText(span, run.fontSize,
                                                   makeTextStyle(run, spanDirection));
            if (!shaped.clusters.empty()) {
                for (size_t i = begin; i < end; ++i) shapedAtoms[i].width = 0.0f;
                for (const auto& shapedCluster : shaped.clusters) {
                    const size_t sourceBegin = localStart + shapedCluster.sourceStart;
                    const size_t sourceEnd = sourceBegin + shapedCluster.sourceLength;
                    std::vector<size_t> covered;
                    for (size_t i = begin; i < end; ++i) {
                        const size_t atomBegin = shapedAtoms[i].runTextStart;
                        const size_t atomEnd = atomBegin + shapedAtoms[i].textLength;
                        if (atomBegin < sourceEnd && sourceBegin < atomEnd) {
                            covered.push_back(i);
                        }
                    }
                    if (covered.empty()) continue;
                    const float clusterWidth = std::abs(shapedCluster.xEnd
                                                        - shapedCluster.xStart);
                    const float share = clusterWidth / static_cast<float>(covered.size());
                    for (size_t i : covered) shapedAtoms[i].width += share;
                }
            }
            begin = end;
        }
    };

    std::vector<AtomLine> lines;
    AtomLine current;
    auto finishLine = [&](bool paragraphEnd) {
        recomputeLine(current);
        if (current.height <= 0.0f) {
            current.height = static_cast<float>(_defaultFontSize) * _lineHeight;
        }
        current.paragraphEnd = paragraphEnd;
        lines.push_back(std::move(current));
        current = AtomLine{};
    };

    for (const LayoutAtom& atom : atoms) {
        if (atom.newline) {
            finishLine(true);
            continue;
        }
        if (wrap && !current.atoms.empty()
            && current.atoms.back().analysisIndex != atom.analysisIndex
            && current.width + atom.width > availableWidth) {
            if (_wrapMode == RichTextWrapMode::Word) {
                size_t breakAfter = std::numeric_limits<size_t>::max();
                for (size_t i = 0; i < current.atoms.size(); ++i) {
                    if (current.atoms[i].softBreakAfter) breakAfter = i;
                }
                if (breakAfter != std::numeric_limits<size_t>::max()) {
                    std::vector<LayoutAtom> tail(
                        current.atoms.begin() + static_cast<std::ptrdiff_t>(breakAfter + 1u),
                        current.atoms.end());
                    current.atoms.erase(
                        current.atoms.begin() + static_cast<std::ptrdiff_t>(breakAfter + 1u),
                        current.atoms.end());
                    while (!current.atoms.empty() && current.atoms.back().whitespace) {
                        current.atoms.pop_back();
                    }
                    finishLine(false);
                    while (!tail.empty() && tail.front().whitespace) tail.erase(tail.begin());
                    current.atoms = std::move(tail);
                    recomputeLine(current);
                } else {
                    finishLine(false);
                }
            } else {
                finishLine(false);
            }
        }
        current.atoms.push_back(atom);
        current.width += atom.width;
        current.height = std::max(current.height, atom.height);
    }
    if (!current.atoms.empty() || lines.empty()) finishLine(true);

    auto shapeLine = [&](AtomLine& line) {
        shapeAtomSpan(line.atoms);
        recomputeLine(line);
        if (line.height <= 0.0f) {
            line.height = static_cast<float>(_defaultFontSize) * _lineHeight;
        }
    };
    for (AtomLine& line : lines) shapeLine(line);

    // A final-line shape can be wider than the pre-wrap estimate. Repair only
    // overflowing lines, preserving word opportunities and never splitting
    // pieces that belong to the same grapheme cluster.
    if (wrap) {
        for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
            while (lines[lineIndex].width > availableWidth + 0.001f
                   && lines[lineIndex].atoms.size() > 1u) {
                AtomLine& line = lines[lineIndex];
                size_t split = line.atoms.size() - 1u;
                const size_t tailCluster = line.atoms[split].analysisIndex;
                while (split > 0u
                       && line.atoms[split - 1u].analysisIndex == tailCluster) --split;
                if (_wrapMode == RichTextWrapMode::Word) {
                    for (size_t i = split; i > 0u; --i) {
                        if (line.atoms[i - 1u].softBreakAfter) {
                            split = i;
                            break;
                        }
                    }
                }
                if (split == 0u) break;
                AtomLine tail;
                const bool wasParagraphEnd = line.paragraphEnd;
                tail.atoms.assign(
                    line.atoms.begin() + static_cast<std::ptrdiff_t>(split),
                    line.atoms.end());
                line.atoms.erase(
                    line.atoms.begin() + static_cast<std::ptrdiff_t>(split),
                    line.atoms.end());
                while (!line.atoms.empty() && line.atoms.back().whitespace) {
                    line.atoms.pop_back();
                }
                while (!tail.atoms.empty() && tail.atoms.front().whitespace) {
                    tail.atoms.erase(tail.atoms.begin());
                }
                line.paragraphEnd = false;
                tail.paragraphEnd = wasParagraphEnd;
                shapeLine(line);
                shapeLine(tail);
                lines.insert(lines.begin()
                    + static_cast<std::ptrdiff_t>(lineIndex + 1u), std::move(tail));
            }
        }
    }

    const size_t allowedLines = _maxLines == 0 ? lines.size()
                                                : std::min(_maxLines, lines.size());
    if (allowedLines < lines.size()) {
        lines.resize(allowedLines);
        result.truncated = true;
    }
    const float boundsHeight = std::max(0.0f, bounds.maxY - bounds.minY);
    if (boundsHeight > 0.0f) {
        float occupied = 0.0f;
        size_t verticalLines = 0;
        for (; verticalLines < lines.size(); ++verticalLines) {
            const float next = lines[verticalLines].height
                + (verticalLines == 0 ? 0.0f : _lineSpacing);
            if (verticalLines > 0 && occupied + next > boundsHeight + 0.001f) break;
            occupied += next;
        }
        if (verticalLines < lines.size()) {
            lines.resize(verticalLines);
            result.truncated = true;
        }
    }

    if (result.truncated && _overflow == RichTextOverflow::Ellipsis
        && !lines.empty() && !_runs.empty()) {
        AtomLine& last = lines.back();
        const size_t styleIndex = last.atoms.empty() ? _runs.size() - 1u
                                                     : last.atoms.back().runIndex;
        const RichRun& styleRun = _runs[styleIndex];
        LayoutAtom ellipsis;
        ellipsis.analysisIndex = std::numeric_limits<size_t>::max();
        ellipsis.runIndex = styleIndex;
        ellipsis.runTextStart = std::numeric_limits<size_t>::max();
        ellipsis.documentTextStart = plainText.size();
        ellipsis.text = L"\x2026";
        ellipsis.synthetic = true;
        ellipsis.bidiLevel = last.atoms.empty()
            ? (analysis.baseRightToLeft ? 1u : 0u) : last.atoms.back().bidiLevel;
        const TextDirection ellipsisDirection = ellipsis.rightToLeft()
            ? TextDirection::RightToLeft : TextDirection::LeftToRight;
        ellipsis.width = measureTextWidth(renderer, ellipsis.text, styleRun,
                                          ellipsisDirection);
        ellipsis.height = std::max(1.0f,
            static_cast<float>(styleRun.fontSize) * _lineHeight);
        while (!last.atoms.empty() && wrap
               && last.width + ellipsis.width > availableWidth) {
            last.atoms.pop_back();
            shapeLine(last);
        }
        last.atoms.push_back(ellipsis);
        shapeLine(last);
    }

    float totalHeight = 0.0f;
    float maxWidth = 0.0f;
    for (size_t i = 0; i < lines.size(); ++i) {
        totalHeight += lines[i].height + (i == 0 ? 0.0f : _lineSpacing);
        maxWidth = std::max(maxWidth, lines[i].width);
    }
    float y = bounds.minY;
    if (_verticalAlignment == RichTextVerticalAlignment::Center) {
        y += std::max(0.0f, (boundsHeight - totalHeight) * 0.5f);
    } else if (_verticalAlignment == RichTextVerticalAlignment::Bottom) {
        y += std::max(0.0f, boundsHeight - totalHeight);
    }

    for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        AtomLine& source = lines[lineIndex];
        if (lineIndex > 0) y += _lineSpacing;
        float x = bounds.minX;
        if (_alignment == RichTextAlignment::Center) {
            x += std::max(0.0f, (availableWidth - source.width) * 0.5f);
        } else if (_alignment == RichTextAlignment::Right) {
            x += std::max(0.0f, availableWidth - source.width);
        }
        size_t gapCount = 0;
        if (_alignment == RichTextAlignment::Justify && !source.paragraphEnd
            && source.width < availableWidth) {
            for (const LayoutAtom& atom : source.atoms) if (atom.whitespace) ++gapCount;
        }
        const float gapExtra = gapCount > 0
            ? (availableWidth - source.width) / static_cast<float>(gapCount) : 0.0f;

        RichTextLine line;
        line.firstFragment = result.fragments.size();
        line.width = gapCount > 0 ? availableWidth : source.width;
        line.height = source.height;
        line.y = y;
        const std::vector<size_t> order = visualOrder(source);
        for (size_t atomIndex : order) {
            const LayoutAtom& atom = source.atoms[atomIndex];
            const RichRun& run = _runs[atom.runIndex];
            const float atomWidth = atom.width + (atom.whitespace ? gapExtra : 0.0f);
            const float top = y - run.baselineShift;
            const bool rtl = atom.rightToLeft();
            const bool logicalAdjacent = !result.fragments.empty() && !atom.synthetic
                && (rtl
                    ? atom.runTextStart + atom.textLength
                        == result.fragments.back().runTextStart
                    : result.fragments.back().runTextStart
                        + result.fragments.back().textLength == atom.runTextStart);
            const bool canMerge = !result.fragments.empty()
                && gapExtra <= 0.0001f
                && result.fragments.back().lineIndex == lineIndex
                && result.fragments.back().runIndex == atom.runIndex
                && result.fragments.back().rightToLeft == rtl
                && logicalAdjacent;
            RichTextFragment* fragment = nullptr;
            if (canMerge) {
                fragment = &result.fragments.back();
                fragment->textLength += atom.textLength;
                if (rtl) {
                    fragment->runTextStart = atom.runTextStart;
                    fragment->documentTextStart = atom.documentTextStart;
                }
                fragment->bounds.maxX = x + atomWidth;
                fragment->bounds.minY = std::min(fragment->bounds.minY, top);
                fragment->bounds.maxY = std::max(fragment->bounds.maxY,
                                                  top + source.height);
            } else {
                RichTextFragment created;
                created.runIndex = atom.runIndex;
                created.runTextStart = atom.runTextStart;
                created.textLength = atom.textLength;
                created.documentTextStart = atom.documentTextStart;
                created.lineIndex = lineIndex;
                created.bidiLevel = atom.bidiLevel;
                created.rightToLeft = rtl;
                created.bounds = math::FRectangle(x, top, x + atomWidth,
                                                   top + source.height);
                result.fragments.push_back(std::move(created));
                fragment = &result.fragments.back();
            }
            const size_t leftTextIndex = rtl
                ? atom.documentTextStart + atom.textLength : atom.documentTextStart;
            const size_t rightTextIndex = rtl
                ? atom.documentTextStart : atom.documentTextStart + atom.textLength;
            appendCaretStop(*fragment, leftTextIndex, x);
            appendCaretStop(*fragment, rightTextIndex, x + atomWidth);
            x += atomWidth;
        }
        line.fragmentCount = result.fragments.size() - line.firstFragment;
        result.lines.push_back(line);
        y += source.height;
    }
    result.contentSize = math::FVector2(maxWidth, totalHeight);
    return result;
}

math::FVector2 RichText::measureContent(IRenderBackend& renderer) const {
    return layout(renderer).contentSize;
}

size_t RichText::hitTestTextIndex(IRenderBackend& renderer,
                                  const math::FVector2& point) const {
    const RichTextLayout result = layout(renderer);
    if (result.fragments.empty()) return 0;
    const RichTextFragment* closest = &result.fragments.front();
    float best = std::numeric_limits<float>::max();
    for (const RichTextFragment& fragment : result.fragments) {
        const float dx = point.x < fragment.bounds.minX ? fragment.bounds.minX - point.x
            : (point.x > fragment.bounds.maxX ? point.x - fragment.bounds.maxX : 0.0f);
        const float dy = point.y < fragment.bounds.minY ? fragment.bounds.minY - point.y
            : (point.y > fragment.bounds.maxY ? point.y - fragment.bounds.maxY : 0.0f);
        if (dx + dy < best) { best = dx + dy; closest = &fragment; }
    }
    if (closest->caretX.empty()) return closest->documentTextStart;
    size_t bestStop = 0;
    float bestDistance = std::numeric_limits<float>::max();
    for (size_t i = 0; i < closest->caretX.size(); ++i) {
        const float distance = std::abs(point.x - closest->caretX[i]);
        if (distance < bestDistance) { bestDistance = distance; bestStop = i; }
    }
    return closest->caretTextIndices[bestStop];
}

math::FRectangle RichText::getCaretRect(IRenderBackend& renderer,
                                         size_t textIndex) const {
    const RichTextLayout result = layout(renderer);
    const RichTextFragment* nearest = nullptr;
    size_t nearestStop = 0;
    size_t nearestDistance = std::numeric_limits<size_t>::max();
    for (const RichTextFragment& fragment : result.fragments) {
        for (size_t i = 0; i < fragment.caretTextIndices.size(); ++i) {
            const size_t candidate = fragment.caretTextIndices[i];
            if (candidate == textIndex) {
                const float x = fragment.caretX[i];
                return math::FRectangle(x, fragment.bounds.minY, x + 1.0f,
                                        fragment.bounds.maxY);
            }
            const size_t distance = candidate > textIndex ? candidate - textIndex
                                                          : textIndex - candidate;
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearest = &fragment;
                nearestStop = i;
            }
        }
    }
    if (nearest != nullptr) {
        const float x = nearest->caretX[nearestStop];
        return math::FRectangle(x, nearest->bounds.minY, x + 1.0f,
                                nearest->bounds.maxY);
    }
    const auto b = getWorldBounds();
    return math::FRectangle(b.minX, b.minY, b.minX + 1.0f,
                            b.minY + static_cast<float>(_defaultFontSize) * _lineHeight);
}

void RichText::onRender(IRenderBackend& renderer) {
    const RichTextLayout result = layout(renderer);
    if (result.fragments.empty()) return;
    renderer.pushClip(getWorldBounds());
    for (const RichTextFragment& fragment : result.fragments) {
        if (fragment.runIndex >= _runs.size()) continue;
        const RichRun& run = _runs[fragment.runIndex];
        const std::wstring text = fragment.runTextStart == std::numeric_limits<size_t>::max()
            ? std::wstring(L"\x2026")
            : run.text.substr(fragment.runTextStart, fragment.textLength);
        const TextDirection direction = fragment.rightToLeft
            ? TextDirection::RightToLeft : TextDirection::LeftToRight;
        IRenderBackend::TextStyle style = makeTextStyle(run, direction);
        renderer.drawText(fragment.bounds, text, run.fontSize, style);
        if (run.underline || run.strikethrough) {
            const float thickness = std::max(1.0f, run.fontSize / 14.0f);
            if (run.underline) {
                const float underlineY = fragment.bounds.maxY - thickness;
                renderer.drawRect(math::FRectangle(fragment.bounds.minX, underlineY,
                    fragment.bounds.maxX, underlineY + thickness), run.color);
            }
            if (run.strikethrough) {
                const float strikeY = (fragment.bounds.minY + fragment.bounds.maxY) * 0.5f;
                renderer.drawRect(math::FRectangle(fragment.bounds.minX, strikeY,
                    fragment.bounds.maxX, strikeY + thickness), run.color);
            }
        }
    }
    renderer.popClip();
}

Widget* createRichTextWidget() { return new RichText(); }

} // namespace ayt::ui
