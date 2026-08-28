#include "AYUI/RichText.h"
#include "AYUI/IRenderBackend.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <limits>

namespace ayt::ui {
namespace {

struct LayoutAtom {
    size_t runIndex = 0;
    size_t runTextStart = 0;
    size_t documentTextStart = 0;
    size_t textLength = 1;
    std::wstring text;
    float width = 0.0f;
    float height = 0.0f;
    bool whitespace = false;
    bool newline = false;
};

struct AtomLine {
    std::vector<LayoutAtom> atoms;
    float width = 0.0f;
    float height = 0.0f;
    bool paragraphEnd = false;
};

bool isBreakSpace(wchar_t ch) { return ch == L' ' || ch == L'\t'; }

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
    _runs.push_back(std::move(normalized));
    markBoundsDirty();
    markDirty();
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
                                 int fontSize) const {
    if (text.empty()) return 0.0f;
    const auto metrics = renderer.measureText(text, fontSize);
    if (metrics.width > 0.0f) return metrics.width;
    return static_cast<float>(text.size()) * 0.5f * static_cast<float>(fontSize);
}

RichTextLayout RichText::layout(IRenderBackend& renderer) const {
    RichTextLayout result;
    const math::FRectangle bounds = getWorldBounds();
    const float boundsWidth = std::max(0.0f, bounds.maxX - bounds.minX);
    float availableWidth = boundsWidth;
    if (_wrapWidth > 0.0f) availableWidth = std::min(availableWidth, _wrapWidth);
    const bool wrap = _wrapMode != RichTextWrapMode::NoWrap && availableWidth > 0.0f;

    std::vector<LayoutAtom> atoms;
    size_t documentIndex = 0;
    for (size_t runIndex = 0; runIndex < _runs.size(); ++runIndex) {
        const RichRun& run = _runs[runIndex];
        for (size_t i = 0; i < run.text.size();) {
            LayoutAtom atom;
            atom.runIndex = runIndex;
            atom.runTextStart = i;
            atom.documentTextStart = documentIndex;
            const wchar_t first = run.text[i];
#if WCHAR_MAX <= 0xffff
            if (first >= 0xd800 && first <= 0xdbff && i + 1 < run.text.size()
                && run.text[i + 1] >= 0xdc00 && run.text[i + 1] <= 0xdfff) {
                atom.textLength = 2;
            }
#endif
            atom.text = run.text.substr(i, atom.textLength);
            atom.newline = first == L'\n' || first == L'\r';
            if (first == L'\r' && i + 1 < run.text.size() && run.text[i + 1] == L'\n') {
                atom.textLength = 2;
                atom.text = L"\r\n";
            }
            atom.whitespace = isBreakSpace(first);
            atom.height = std::max(1.0f, static_cast<float>(run.fontSize) * _lineHeight);
            if (!atom.newline) {
                atom.width = measureTextWidth(renderer, atom.text, run.fontSize)
                           + std::max(0.0f, run.letterSpacing);
            }
            atoms.push_back(atom);
            i += atom.textLength;
            documentIndex += atom.textLength;
        }
    }

    std::vector<AtomLine> lines;
    AtomLine current;
    const auto recompute = [](AtomLine& line) {
        line.width = 0.0f;
        line.height = 0.0f;
        for (const LayoutAtom& atom : line.atoms) {
            line.width += atom.width;
            line.height = std::max(line.height, atom.height);
        }
    };
    auto finishLine = [&](bool paragraphEnd) {
        recompute(current);
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
            && current.width + atom.width > availableWidth) {
            if (_wrapMode == RichTextWrapMode::Word) {
                size_t breakIndex = current.atoms.size();
                while (breakIndex > 0 && !current.atoms[breakIndex - 1].whitespace) {
                    --breakIndex;
                }
                if (breakIndex > 0 && breakIndex < current.atoms.size()) {
                    std::vector<LayoutAtom> tail(current.atoms.begin() + breakIndex,
                                                 current.atoms.end());
                    while (!current.atoms.empty() && current.atoms.back().whitespace) {
                        current.atoms.pop_back();
                    }
                    finishLine(false);
                    while (!tail.empty() && tail.front().whitespace) tail.erase(tail.begin());
                    current.atoms = std::move(tail);
                    recompute(current);
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
            // Keep the first line even when its glyph box is taller than the
            // viewport; rendering clips it. Dropping it entirely made a
            // short RichText widget appear blank instead of partially visible.
            if (verticalLines > 0
                && occupied + next > boundsHeight + 0.001f) break;
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
        const size_t styleIndex = last.atoms.empty() ? _runs.size() - 1
                                                     : last.atoms.back().runIndex;
        const RichRun& styleRun = _runs[styleIndex];
        LayoutAtom ellipsis;
        ellipsis.runIndex = styleIndex;
        ellipsis.runTextStart = std::numeric_limits<size_t>::max();
        ellipsis.documentTextStart = documentIndex;
        ellipsis.text = L"\x2026";
        ellipsis.width = measureTextWidth(renderer, ellipsis.text, styleRun.fontSize);
        ellipsis.height = std::max(1.0f, static_cast<float>(styleRun.fontSize) * _lineHeight);
        while (!last.atoms.empty() && wrap
               && last.width + ellipsis.width > availableWidth) {
            last.atoms.pop_back();
            recompute(last);
        }
        last.atoms.push_back(ellipsis);
        recompute(last);
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
        for (const LayoutAtom& atom : source.atoms) {
            const RichRun& run = _runs[atom.runIndex];
            const float atomWidth = atom.width + (atom.whitespace ? gapExtra : 0.0f);
            const float top = y - run.baselineShift;
            // A justified whitespace atom owns extra advance that drawText()
            // cannot encode inside a merged string. Keep justified atoms as
            // separate fragments so the next glyph starts after that advance.
            const bool canMerge = !result.fragments.empty()
                && gapExtra <= 0.0001f
                && result.fragments.back().lineIndex == lineIndex
                && result.fragments.back().runIndex == atom.runIndex
                && atom.runTextStart != std::numeric_limits<size_t>::max()
                && result.fragments.back().runTextStart
                     + result.fragments.back().textLength == atom.runTextStart;
            if (canMerge) {
                RichTextFragment& fragment = result.fragments.back();
                fragment.textLength += atom.textLength;
                fragment.bounds.maxX = x + atomWidth;
                fragment.bounds.minY = std::min(fragment.bounds.minY, top);
                fragment.bounds.maxY = std::max(fragment.bounds.maxY, top + source.height);
            } else {
                RichTextFragment fragment;
                fragment.runIndex = atom.runIndex;
                fragment.runTextStart = atom.runTextStart;
                fragment.textLength = atom.textLength;
                fragment.documentTextStart = atom.documentTextStart;
                fragment.lineIndex = lineIndex;
                fragment.bounds = math::FRectangle(x, top, x + atomWidth,
                                                   top + source.height);
                result.fragments.push_back(fragment);
            }
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
    const float width = closest->bounds.maxX - closest->bounds.minX;
    const float fraction = width > 0.0f
        ? std::clamp((point.x - closest->bounds.minX) / width, 0.0f, 1.0f) : 0.0f;
    return closest->documentTextStart
        + static_cast<size_t>(std::lround(fraction * closest->textLength));
}

math::FRectangle RichText::getCaretRect(IRenderBackend& renderer,
                                         size_t textIndex) const {
    const RichTextLayout result = layout(renderer);
    for (const RichTextFragment& fragment : result.fragments) {
        const size_t end = fragment.documentTextStart + fragment.textLength;
        if (textIndex >= fragment.documentTextStart && textIndex <= end) {
            const float t = fragment.textLength > 0
                ? static_cast<float>(textIndex - fragment.documentTextStart)
                    / static_cast<float>(fragment.textLength) : 0.0f;
            const float x = fragment.bounds.minX
                + (fragment.bounds.maxX - fragment.bounds.minX) * t;
            return math::FRectangle(x, fragment.bounds.minY, x + 1.0f,
                                    fragment.bounds.maxY);
        }
    }
    if (!result.fragments.empty()) {
        const auto& last = result.fragments.back().bounds;
        return math::FRectangle(last.maxX, last.minY, last.maxX + 1.0f, last.maxY);
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
        IRenderBackend::TextStyle style;
        style.color = run.color;
        style.bold = run.bold;
        style.italic = run.italic;
        style.letterSpacing = static_cast<int>(std::lround(run.letterSpacing));
        style.valign = IRenderBackend::TextStyle::VAlign::Top;
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
