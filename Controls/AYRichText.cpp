#include "AYUI/RichText.h"
#include "AYUI/Clipboard.h"
#include "AYUI/IRenderBackend.h"
#include "AYUI/UIKeyCode.h"
#include "AYUI/UIManager.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
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

void RichText::normalizeRun(RichRun& run) const {
    if (run.fontSize <= 0) run.fontSize = _defaultFontSize;
    run.fontWeight = std::clamp(run.fontWeight, 100, 900);
    if (run.isImage && run.inlineKind == RichInlineKind::None) {
        run.inlineKind = RichInlineKind::Image;
    }
    if (run.inlineKind != RichInlineKind::None) {
        run.text.assign(1, static_cast<wchar_t>(0xFFFC));
        run.inlineSize.x = std::max(1.0f, run.inlineSize.x);
        run.inlineSize.y = std::max(1.0f, run.inlineSize.y);
        run.isImage = run.inlineKind == RichInlineKind::Image;
    }
}

void RichText::syncInlineWidgetChildren() {
    std::vector<Widget*> required;
    for (const RichRun& run : _runs) {
        if (run.inlineKind == RichInlineKind::Widget && run.inlineWidget != nullptr
            && std::find(required.begin(), required.end(), run.inlineWidget) == required.end()) {
            required.push_back(run.inlineWidget);
        }
    }
    const std::vector<Widget*> current = getChildren();
    for (Widget* child : current) {
        if (std::find(required.begin(), required.end(), child) == required.end()) removeChild(child);
    }
    for (Widget* child : required) {
        if (child->getParent() != this) addChildExternal(child);
    }
}

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
    normalizeRun(normalized);
    _runs.push_back(std::move(normalized));
    syncInlineWidgetChildren();
    markBoundsDirty();
    markDirty();
}

void RichText::insertRun(size_t index, const RichRun& run) {
    RichRun normalized = run;
    normalizeRun(normalized);
    index = std::min(index, _runs.size());
    _runs.insert(_runs.begin() + static_cast<std::ptrdiff_t>(index),
                 std::move(normalized));
    syncInlineWidgetChildren();
    markBoundsDirty();
    markDirty();
}

bool RichText::setRun(size_t index, const RichRun& run) {
    if (index >= _runs.size()) return false;
    RichRun normalized = run;
    normalizeRun(normalized);
    _runs[index] = std::move(normalized);
    syncInlineWidgetChildren();
    markBoundsDirty();
    markDirty();
    return true;
}

bool RichText::removeRun(size_t index) {
    if (index >= _runs.size()) return false;
    _runs.erase(_runs.begin() + static_cast<std::ptrdiff_t>(index));
    syncInlineWidgetChildren();
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
    syncInlineWidgetChildren();
    _selectionAnchor = _caret = 0;
    markBoundsDirty();
    markDirty();
}

std::wstring RichText::getPlainText() const {
    std::wstring result;
    for (const RichRun& run : _runs) result += run.text;
    return result;
}

void RichText::setPlainText(const std::wstring& text) {
    _runs.clear();
    if (!text.empty()) {
        RichRun run;
        run.text = text;
        run.color = _defaultColor;
        run.fontSize = _defaultFontSize;
        _runs.push_back(std::move(run));
    }
    syncInlineWidgetChildren();
    _selectionAnchor = _caret = std::min(_caret, text.size());
    _undoStack.clear();
    _redoStack.clear();
    markBoundsDirty();
    markDirty();
    if (_onTextChanged) _onTextChanged(getPlainText());
}

void RichText::addInlineImage(void* textureHandle, const math::FVector2& size,
                              const std::wstring& altText,
                              const math::FRectangle& uv) {
    RichRun run;
    run.inlineKind = RichInlineKind::Image;
    run.inlineTexture = textureHandle;
    run.inlineSize = size;
    run.inlineAltText = altText;
    run.inlineUv = uv;
    addRun(run);
}

void RichText::addInlineWidget(Widget* widget, const math::FVector2& size,
                               const std::wstring& altText) {
    if (widget == nullptr) return;
    RichRun run;
    run.inlineKind = RichInlineKind::Widget;
    run.inlineWidget = widget;
    run.inlineSize = size;
    run.inlineAltText = altText;
    addRun(run);
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
            const bool inlineObject = run.inlineKind != RichInlineKind::None;
            atom.height = inlineObject ? run.inlineSize.y : std::max(1.0f,
                static_cast<float>(run.fontSize) * _lineHeight);
            atom.bidiLevel = cluster.bidiLevel;
            atom.whitespace = cluster.whitespace;
            atom.newline = cluster.hardBreak;
            // A style run may split a Unicode cluster. The cluster's line
            // break opportunity belongs only to its final piece; exposing it
            // on an earlier piece would permit wrapping inside a grapheme.
            atom.softBreakAfter = cluster.softBreakAfter && partLength == remaining;
            if (inlineObject) {
                atom.width = run.inlineSize.x;
            } else if (!atom.newline) {
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
            if (shapedAtoms[begin].newline || shapedAtoms[begin].synthetic
                || _runs[shapedAtoms[begin].runIndex].inlineKind != RichInlineKind::None) {
                ++begin;
                continue;
            }
            size_t end = begin + 1u;
            while (end < shapedAtoms.size() && !shapedAtoms[end].newline
                && !shapedAtoms[end].synthetic
                && _runs[shapedAtoms[end].runIndex].inlineKind == RichInlineKind::None
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
            const bool inlineObject = run.inlineKind != RichInlineKind::None;
            const float top = inlineObject
                ? y + std::max(0.0f, source.height - run.inlineSize.y) - run.inlineBaseline
                : y - run.baselineShift;
            const bool rtl = atom.rightToLeft();
            const bool logicalAdjacent = !result.fragments.empty() && !atom.synthetic
                && (rtl
                    ? atom.runTextStart + atom.textLength
                        == result.fragments.back().runTextStart
                    : result.fragments.back().runTextStart
                        + result.fragments.back().textLength == atom.runTextStart);
            const bool canMerge = !inlineObject && !result.fragments.empty()
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
                created.inlineObject = inlineObject;
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

void RichText::setSelectable(bool selectable) {
    _selectable = selectable;
    if (!_selectable) {
        _editable = false;
        clearSelection();
    }
    markDirty();
}

void RichText::setEditable(bool editable) {
    _editable = editable;
    if (editable) _selectable = true;
    markDirty();
}

void RichText::setSelection(size_t anchor, size_t caret) {
    const std::wstring text = getPlainText();
    _selectionAnchor = floorGraphemeBoundary(text, std::min(anchor, text.size()));
    _caret = floorGraphemeBoundary(text, std::min(caret, text.size()));
    _caretBlink = 0.0f;
    _caretVisible = true;
    markDirty();
}

void RichText::clearSelection() {
    _selectionAnchor = _caret;
    markDirty();
}

void RichText::selectAll() {
    _selectionAnchor = 0;
    _caret = getPlainText().size();
    markDirty();
}

std::wstring RichText::getSelectedText() const {
    if (!hasSelection()) return {};
    const std::wstring text = getPlainText();
    const size_t start = std::min(getSelectionStart(), text.size());
    const size_t end = std::min(getSelectionEnd(), text.size());
    return text.substr(start, end - start);
}

std::wstring RichText::getLinkTargetAt(size_t textIndex) const {
    size_t offset = 0;
    for (const RichRun& run : _runs) {
        const size_t length = run.text.size();
        if (textIndex >= offset && textIndex < offset + length
            && run.semanticKind == RichSemanticKind::Link) {
            return run.linkTarget;
        }
        offset += length;
    }
    return {};
}

RichRun RichText::insertionStyleAt(size_t index) const {
    size_t cursor = 0;
    for (const RichRun& run : _runs) {
        const size_t end = cursor + run.text.size();
        if (index <= end && run.inlineKind == RichInlineKind::None) {
            RichRun style = run;
            style.text.clear();
            return style;
        }
        cursor = end;
    }
    RichRun style;
    style.color = _defaultColor;
    style.fontSize = _defaultFontSize;
    return style;
}

bool RichText::replaceRange(size_t start, size_t end, const std::wstring& text,
                            bool recordUndo) {
    const std::wstring plain = getPlainText();
    start = floorGraphemeBoundary(plain, std::min(start, plain.size()));
    end = ceilGraphemeBoundary(plain, std::min(end, plain.size()));
    if (end < start) std::swap(start, end);
    if (start == end && text.empty()) return false;
    if (recordUndo) {
        _undoStack.push_back({_runs, _selectionAnchor, _caret});
        if (_undoStack.size() > 100u) _undoStack.erase(_undoStack.begin());
        _redoStack.clear();
    }

    RichRun insertion = insertionStyleAt(start);
    insertion.text = text;
    normalizeRun(insertion);
    insertion.inlineKind = RichInlineKind::None;
    insertion.isImage = false;
    insertion.inlineTexture = nullptr;
    insertion.inlineWidget = nullptr;

    std::vector<RichRun> next;
    bool inserted = text.empty();
    size_t runStart = 0;
    for (const RichRun& source : _runs) {
        const size_t runEnd = runStart + source.text.size();
        if (runEnd <= start) {
            next.push_back(source);
        } else if (runStart >= end) {
            if (!inserted) { next.push_back(insertion); inserted = true; }
            next.push_back(source);
        } else {
            if (start > runStart) {
                RichRun prefix = source;
                prefix.text = source.text.substr(0, start - runStart);
                next.push_back(std::move(prefix));
            }
            if (!inserted) { next.push_back(insertion); inserted = true; }
            if (end < runEnd) {
                RichRun suffix = source;
                suffix.text = source.text.substr(end - runStart);
                next.push_back(std::move(suffix));
            }
        }
        runStart = runEnd;
    }
    if (!inserted) next.push_back(std::move(insertion));
    next.erase(std::remove_if(next.begin(), next.end(), [](const RichRun& run) {
        return run.text.empty();
    }), next.end());
    _runs = std::move(next);
    syncInlineWidgetChildren();
    _caret = start + text.size();
    _selectionAnchor = _caret;
    markBoundsDirty();
    markDirty();
    if (_onTextChanged) _onTextChanged(getPlainText());
    return true;
}

bool RichText::replaceSelection(const std::wstring& text) {
    if (!_editable) return false;
    return replaceRange(getSelectionStart(), getSelectionEnd(), text, true);
}

bool RichText::undo() {
    if (_undoStack.empty()) return false;
    _redoStack.push_back({_runs, _selectionAnchor, _caret});
    EditSnapshot snapshot = std::move(_undoStack.back());
    _undoStack.pop_back();
    _runs = std::move(snapshot.runs);
    _selectionAnchor = snapshot.anchor;
    _caret = snapshot.caret;
    syncInlineWidgetChildren();
    markBoundsDirty();
    markDirty();
    if (_onTextChanged) _onTextChanged(getPlainText());
    return true;
}

bool RichText::redo() {
    if (_redoStack.empty()) return false;
    _undoStack.push_back({_runs, _selectionAnchor, _caret});
    EditSnapshot snapshot = std::move(_redoStack.back());
    _redoStack.pop_back();
    _runs = std::move(snapshot.runs);
    _selectionAnchor = snapshot.anchor;
    _caret = snapshot.caret;
    syncInlineWidgetChildren();
    markBoundsDirty();
    markDirty();
    if (_onTextChanged) _onTextChanged(getPlainText());
    return true;
}

void RichText::moveCaret(size_t next, bool extend) {
    const std::wstring text = getPlainText();
    next = floorGraphemeBoundary(text, std::min(next, text.size()));
    if (!extend) _selectionAnchor = next;
    _caret = next;
    _caretBlink = 0.0f;
    _caretVisible = true;
    markDirty();
}

bool RichText::onMouseButtonDown(const UIMouseEvent& e) {
    if (!_selectable || e.mouseButton != 0) return false;
    if (UIManager* ui = UIManager::tryGet()) ui->setFocus(this);
    IRenderBackend* backend = UIManager::tryGet() ? UIManager::tryGet()->backend() : nullptr;
    if (backend == nullptr) return true;
    const size_t index = hitTestTextIndex(*backend, e.mousePos);
    _pressedLinkTarget = getLinkTargetAt(index);
    const uint32_t mods = UIManager::tryGet()->getModifiers();
    const bool shift = (mods & 1u) != 0u;
    if (!shift) _selectionAnchor = index;
    _caret = index;
    _dragSelecting = true;
    _caretBlink = 0.0f;
    _caretVisible = true;
    markDirty();
    return true;
}

bool RichText::onMouseMove(const UIMouseEvent& e) {
    if (!_dragSelecting) return false;
    IRenderBackend* backend = UIManager::tryGet() ? UIManager::tryGet()->backend() : nullptr;
    if (backend == nullptr) return true;
    _caret = hitTestTextIndex(*backend, e.mousePos);
    markDirty();
    return true;
}

bool RichText::onMouseButtonUp(const UIMouseEvent& e) {
    if (e.mouseButton != 0 || !_dragSelecting) return false;
    _dragSelecting = false;
    IRenderBackend* backend = UIManager::tryGet() ? UIManager::tryGet()->backend() : nullptr;
    if (backend != nullptr && _selectionAnchor == _caret
        && !_pressedLinkTarget.empty()
        && getLinkTargetAt(hitTestTextIndex(*backend, e.mousePos)) == _pressedLinkTarget
        && _onLinkActivated) {
        _onLinkActivated(_pressedLinkTarget);
    }
    _pressedLinkTarget.clear();
    return true;
}

bool RichText::onTextInput(wchar_t ch) {
    if (!_editable || !_hasFocus || ch < 0x20) return false;
    return replaceSelection(std::wstring(1, ch));
}

bool RichText::onTextInputText(const std::wstring& text) {
    if (!_editable || !_hasFocus) return false;
    std::wstring accepted;
    accepted.reserve(text.size());
    for (wchar_t ch : text) {
        if (ch >= 0x20 || ch == L'\n' || ch == L'\t') accepted.push_back(ch);
    }
    return accepted.empty() ? true : replaceSelection(accepted);
}

bool RichText::onKeyDown(int keyCode) {
    if (!_hasFocus || !_selectable) return false;
    UIManager* ui = UIManager::tryGet();
    const uint32_t mods = ui != nullptr ? ui->getModifiers() : 0u;
    const bool shift = (mods & 1u) != 0u;
    const bool ctrl = (mods & 2u) != 0u;
    const std::wstring plain = getPlainText();
    if (ctrl) {
        if (keyCode == UIKey_A) { selectAll(); return true; }
        if (keyCode == UIKey_C) { (void)getClipboard().setText(getSelectedText()); return true; }
        if (keyCode == UIKey_Z && _editable) return shift ? redo() : undo();
        if (keyCode == UIKey_Y && _editable) return redo();
        if (keyCode == UIKey_X && _editable) {
            if (hasSelection() && getClipboard().setText(getSelectedText())) return replaceSelection({});
            return true;
        }
        if (keyCode == UIKey_V && _editable) {
            std::wstring clip;
            if (getClipboard().getText(clip)) (void)replaceSelection(clip);
            return true;
        }
        if (keyCode == UIKey_Left || keyCode == UIKey_Right) {
            size_t next = _caret;
            if (keyCode == UIKey_Left) {
                while (next > 0 && std::iswspace(plain[next - 1])) --next;
                while (next > 0 && !std::iswspace(plain[next - 1])) --next;
            } else {
                while (next < plain.size() && !std::iswspace(plain[next])) ++next;
                while (next < plain.size() && std::iswspace(plain[next])) ++next;
            }
            moveCaret(next, shift);
            return true;
        }
    }
    switch (keyCode) {
    case UIKey_Left:
        moveCaret(previousGraphemeBoundary(plain, _caret), shift); return true;
    case UIKey_Right:
        moveCaret(nextGraphemeBoundary(plain, _caret), shift); return true;
    case UIKey_Home:
        moveCaret(0, shift); return true;
    case UIKey_End:
        moveCaret(plain.size(), shift); return true;
    case UIKey_Up:
    case UIKey_Down: {
        IRenderBackend* backend = ui != nullptr ? ui->backend() : nullptr;
        if (backend == nullptr) return false;
        const math::FRectangle caret = getCaretRect(*backend, _caret);
        const float lineStep = std::max(1.0f, caret.maxY - caret.minY + _lineSpacing);
        const float targetY = (caret.minY + caret.maxY) * 0.5f
            + (keyCode == UIKey_Up ? -lineStep : lineStep);
        moveCaret(hitTestTextIndex(*backend,
            math::FVector2(caret.minX, targetY)), shift);
        return true;
    }
    case UIKey_Backspace:
        if (!_editable) return false;
        if (hasSelection()) return replaceSelection({});
        if (_caret > 0) return replaceRange(previousGraphemeBoundary(plain, _caret), _caret, {}, true);
        return true;
    case UIKey_Delete:
        if (!_editable) return false;
        if (hasSelection()) return replaceSelection({});
        if (_caret < plain.size()) return replaceRange(_caret, nextGraphemeBoundary(plain, _caret), {}, true);
        return true;
    case UIKey_Enter:
        return _editable ? replaceSelection(L"\n") : false;
    default:
        return false;
    }
}

bool RichText::onImeCompositionStart(const std::string& text, int caret) {
    if (!_editable || !_hasFocus) return false;
    _compositionStart = getSelectionStart();
    _compositionLength = 0;
    _composing = true;
    _undoStack.push_back({_runs, _selectionAnchor, _caret});
    if (_undoStack.size() > 100u) _undoStack.erase(_undoStack.begin());
    _redoStack.clear();
    const std::wstring preview = decodeUtf8Text(text);
    (void)replaceRange(getSelectionStart(), getSelectionEnd(), preview, false);
    _compositionLength = preview.size();
    _caret = _compositionStart + std::min<size_t>(std::max(caret, 0), preview.size());
    _selectionAnchor = _caret;
    return true;
}

bool RichText::onImeCompositionUpdate(const std::string& text, int caret) {
    if (!_composing) return onImeCompositionStart(text, caret);
    const std::wstring preview = decodeUtf8Text(text);
    (void)replaceRange(_compositionStart, _compositionStart + _compositionLength, preview, false);
    _compositionLength = preview.size();
    _caret = _compositionStart + std::min<size_t>(std::max(caret, 0), preview.size());
    _selectionAnchor = _caret;
    return true;
}

bool RichText::onImeCompositionEnd(const std::string& committed) {
    if (!_composing) return false;
    const std::wstring text = decodeUtf8Text(committed);
    _composing = false;
    const bool changed = replaceRange(_compositionStart,
        _compositionStart + _compositionLength, text, false);
    _compositionLength = 0;
    return changed || text.empty();
}

UiCursorHint RichText::getCursorHint() const {
    return _selectable ? UiCursorHint::Beam : UiCursorHint::Default;
}

void RichText::onFocusGained() {
    _caretBlink = 0.0f;
    _caretVisible = true;
    markDirty();
}

void RichText::onFocusLost() {
    _dragSelecting = false;
    _pressedLinkTarget.clear();
    _caretVisible = false;
    markDirty();
}

void RichText::tick(float dt) {
    CompoundFocusableWidget::tick(dt);
    if (_hasFocus && _editable) {
        _caretBlink += std::max(0.0f, dt);
        if (_caretBlink >= 0.5f) {
            _caretBlink = std::fmod(_caretBlink, 0.5f);
            _caretVisible = !_caretVisible;
            markDirty();
        }
    }
}

void RichText::layoutChildren() {
    IRenderBackend* backend = UIManager::tryGet() ? UIManager::tryGet()->backend() : nullptr;
    if (backend != nullptr) placeInlineWidgets(layout(*backend));
}

void RichText::placeInlineWidgets(const RichTextLayout& result) {
    const math::FRectangle own = getWorldBounds();
    for (const RichTextFragment& fragment : result.fragments) {
        if (!fragment.inlineObject || fragment.runIndex >= _runs.size()) continue;
        const RichRun& run = _runs[fragment.runIndex];
        if (run.inlineKind != RichInlineKind::Widget || run.inlineWidget == nullptr) continue;
        run.inlineWidget->setPosition(math::FVector2(
            fragment.bounds.minX - own.minX, fragment.bounds.minY - own.minY));
        run.inlineWidget->setSize(run.inlineSize);
    }
}

void RichText::onRender(IRenderBackend& renderer) {
    const RichTextLayout result = layout(renderer);
    placeInlineWidgets(result);
    if (result.fragments.empty()) return;
    renderer.pushClip(getWorldBounds());

    // Selection is painted below glyphs/inline content. Caret-stop geometry
    // comes from the same shaped layout, so ligatures and RTL ranges remain
    // aligned with what the renderer submits.
    if (hasSelection()) {
        const size_t selectionStart = getSelectionStart();
        const size_t selectionEnd = getSelectionEnd();
        for (const RichTextFragment& fragment : result.fragments) {
            const size_t fragmentStart = fragment.documentTextStart;
            const size_t fragmentEnd = fragmentStart + fragment.textLength;
            if (fragmentEnd <= selectionStart || fragmentStart >= selectionEnd
                || fragment.caretX.empty()) continue;
            const size_t begin = std::max(selectionStart, fragmentStart);
            const size_t end = std::min(selectionEnd, fragmentEnd);
            auto caretX = [&](size_t index) {
                size_t nearest = 0;
                size_t distance = std::numeric_limits<size_t>::max();
                for (size_t i = 0; i < fragment.caretTextIndices.size(); ++i) {
                    const size_t candidate = fragment.caretTextIndices[i];
                    const size_t d = candidate > index ? candidate - index : index - candidate;
                    if (d < distance) { distance = d; nearest = i; }
                }
                return fragment.caretX[nearest];
            };
            const float x0 = caretX(begin);
            const float x1 = caretX(end);
            renderer.drawRect(math::FRectangle(std::min(x0, x1), fragment.bounds.minY,
                std::max(x0, x1), fragment.bounds.maxY), _selectionColor);
        }
    }

    for (const RichTextFragment& fragment : result.fragments) {
        if (fragment.runIndex >= _runs.size()) continue;
        const RichRun& run = _runs[fragment.runIndex];
        if (fragment.inlineObject) {
            if (run.inlineKind == RichInlineKind::Image && run.inlineTexture != nullptr) {
                renderer.drawRect(fragment.bounds, run.inlineTexture, run.inlineUv);
            }
            continue;
        }
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
    if (_hasFocus && _editable && _caretVisible) {
        renderer.drawRect(getCaretRect(renderer, _caret), _caretColor);
    }
    renderer.popClip();
}

void RichText::renderChildren(IRenderBackend& renderer) {
    renderer.pushClip(getWorldBounds());
    Widget::renderChildren(renderer);
    renderer.popClip();
}

Widget* createRichTextWidget() { return new RichText(); }

} // namespace ayt::ui
