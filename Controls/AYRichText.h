#pragma once

// =============================================================================
// C-12 RichText: multi-run text with per-run color / font size.
// =============================================================================
//
// Architecture (v1):
//   RichText : LeafWidget
//     - _runs: std::vector<RichRun>   // text + per-run color + font size
//     - _defaultColor / _defaultFontSize
//     - _wrapWidth: float (0 = no wrap)
//
// onRender lays out runs left-to-right inside the widget bounds, calling
// drawText once per run. For wrap support (wrapWidth > 0), per-run widths
// come from renderer.measureText; if the backend returns 0 (default impl),
// we fall back to a character-count heuristic (run.text.size() * 7 px) so
// wrap still works in unit tests with MockRenderer.
//
// v1 limitations (deferred):
//   - No image runs (no drawImage in IRenderBackend).
//   - No bold / italic / underline runs (only color + size).
//   - No HTML / markdown parsing — callers must use addRun().
//   - Per-line Y positions are recomputed each frame (no cache).

#include "AYLeafWidget.h"
#include <string>
#include <vector>

namespace ayt::ui {

struct RichRun {
    std::wstring text;
    math::FVector4 color = math::FVector4(1.0f, 1.0f, 1.0f, 1.0f);
    int fontSize = 14;
    bool isImage = false;   // unused in v1 — kept for forward compat
};

class RichText : public LeafWidget {
public:
    RichText();
    ~RichText() override;

    void addRun(const std::wstring& text,
                const math::FVector4& color,
                int fontSize);
    void clearRuns();
    size_t getRunCount() const { return _runs.size(); }
    const RichRun& getRun(size_t i) const { return _runs[i]; }

    void setDefaultColor(const math::FVector4& c) { _defaultColor = c; }
    math::FVector4 getDefaultColor() const { return _defaultColor; }
    void setDefaultFontSize(int s) { _defaultFontSize = s; }
    int  getDefaultFontSize() const { return _defaultFontSize; }

    // 0 = no wrap; >0 = wrap to that pixel width.
    void setWrapWidth(float w) { _wrapWidth = w; markBoundsDirty(); }
    float getWrapWidth() const { return _wrapWidth; }

protected:
    void onRender(IRenderBackend& renderer) override;

private:
    // Helper: estimate pixel width of `text` at `fontSize` using the
    // backend's measureText; if zero (default impl), fall back to
    // `text.size() * 0.5 * fontSize` heuristic so unit tests still wrap.
    float measureRunWidth(IRenderBackend& renderer,
                          const std::wstring& text,
                          int fontSize) const;

    std::vector<RichRun> _runs;
    math::FVector4 _defaultColor = math::FVector4(1.0f, 1.0f, 1.0f, 1.0f);
    int   _defaultFontSize = 14;
    float _wrapWidth = 0.0f;
};

Widget* createRichTextWidget();

} // namespace ayt::ui