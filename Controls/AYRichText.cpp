#include "AYUI/RichText.h"
#include "AYUI/IRenderBackend.h"
#include <algorithm>

namespace ayt::ui {

RichText::RichText() {
    setSize(math::FVector2(200.0f, 24.0f));
}

RichText::~RichText() = default;

void RichText::addRun(const std::wstring& text,
                      const math::FVector4& color,
                      int fontSize) {
    RichRun r;
    r.text = text;
    r.color = color;
    r.fontSize = fontSize > 0 ? fontSize : _defaultFontSize;
    _runs.push_back(std::move(r));
    markBoundsDirty();
}

void RichText::clearRuns() {
    _runs.clear();
    markBoundsDirty();
}

float RichText::measureRunWidth(IRenderBackend& renderer,
                                const std::wstring& text,
                                int fontSize) const {
    if (text.empty()) return 0.0f;
    const auto m = renderer.measureText(text, fontSize);
    if (m.width > 0.0f) return m.width;
    // Fallback heuristic: ~0.5 em per character at fontSize. This keeps
    // wrap testable when the backend's measureText is a no-op (e.g.
    // MockRenderer's default which returns zero for all dimensions).
    return static_cast<float>(text.size()) * 0.5f *
           static_cast<float>(fontSize);
}

void RichText::onRender(IRenderBackend& renderer) {
    const math::FRectangle b = getWorldBounds();
    if (b.maxX <= b.minX || b.maxY <= b.minY) return;
    if (_runs.empty()) return;

    const float lineHeight = static_cast<float>(_defaultFontSize) * 1.4f;
    float x = b.minX;
    float y = b.minY;
    const float rightEdge = (_wrapWidth > 0.0f)
        ? b.minX + _wrapWidth
        : b.maxX;

    for (const RichRun& run : _runs) {
        if (run.text.empty()) continue;
        const float runW = measureRunWidth(renderer, run.text, run.fontSize);
        // Wrap: if adding this run would exceed wrapWidth, advance to
        // next line (only if wrap is enabled AND we have non-zero x).
        if (_wrapWidth > 0.0f && x > b.minX &&
            (x + runW) > rightEdge) {
            x = b.minX;
            y += lineHeight;
            if (y >= b.maxY) return;   // out of vertical space — bail
        }
        math::FRectangle textBounds(
            x, y, std::min(x + runW, b.maxX), y + lineHeight);
        renderer.drawText(textBounds, run.text, run.fontSize, run.color);
        x += runW;
    }
}

Widget* createRichTextWidget() { return new RichText(); }

} // namespace ayt::ui