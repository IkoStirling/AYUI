#pragma once

// GDI per-HWND render backend for Gallery child windows (DockCard
// promote → independent top-level window).
//
// MIRROR of AYEditor/demo/GdiRenderBackend.h — the two copies evolve in
// lockstep (AYUI's demo cannot depend on AYEditor; both hosts need a
// plain-GDI backend because bgfx is process-singleton-bound to the
// primary window's nwh and cannot switch HWNDs per frame). Only the
// namespace differs (ayt::gallery).
//
// Draws into an offscreen bitmap and BitBlts once per frame — painting
// directly to the window DC caused visible flicker (and felt like a
// low framerate) because WM_PAINT / class brush cleared mid-frame while
// FillRect/DrawText streamed to the front buffer.

#include "AYMath/MathDefs.h"
#include "AYMath/MathTypes.h"
#include "AYUI/IRenderBackend.h"

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <Windows.h>
#include <unordered_map>
#include <vector>

namespace ayt::gallery {

using namespace ayt::math;

class GalleryChildBackend : public ayt::ui::IRenderBackend {
public:
    explicit GalleryChildBackend(HWND hwnd);
    ~GalleryChildBackend() override;

    void setDrawTarget(HDC hdc, int width, int height);

    void beginFrame() override;
    void endFrame() override;
    void beginCanvas(const math::FRectangle& viewport) override;
    void endCanvas() override;

    void drawRect(const math::FRectangle& bounds, const math::FVector4& color) override;
    void drawRect(const math::FRectangle& bounds, void* textureHandle,
                  const math::FRectangle& uv) override;
    void drawText(const math::FRectangle& bounds, const std::wstring& text, int fontSize,
                  const math::FVector4& color) override;
    void drawWithAlpha(const math::FRectangle& bounds, void* textureHandle, float alpha) override;

    void pushClip(const math::FRectangle& bounds) override;
    void popClip() override;

    PathHandle createPath() override;
    void releasePath(PathHandle path) override;
    void addPathContour(PathHandle path, const math::FVector2* points,
                        int count, bool closed,
                        ayt::ui::PathWinding winding) override;
    void setPathFillColor(PathHandle path, const math::FVector4& color) override;
    void setPathStrokeColor(PathHandle path, const math::FVector4& color) override;
    void setPathStrokeWidth(PathHandle path, float width) override;
    void setPathStrokeStyle(PathHandle path, ayt::ui::PathStrokeCap cap,
                            ayt::ui::PathStrokeJoin join,
                            float miterLimit) override;
    void drawPath(PathHandle path,
                  ayt::ui::PathFillMode mode) override;

private:
    static COLORREF toColorRef(const math::FVector4& color);
    RECT toRect(const math::FRectangle& bounds) const;
    void ensureBackbuffer(int width, int height);
    void releaseBackbuffer();
    HFONT fontForSize(int fontSize);

    struct PathContour {
        std::vector<POINT> points;
        bool closed = false;
    };
    struct PathState {
        std::vector<PathContour> contours;
        COLORREF fillColor = RGB(255, 255, 255);
        COLORREF strokeColor = RGB(255, 255, 255);
        float strokeWidth = 1.0f;
        ayt::ui::PathStrokeCap cap = ayt::ui::PathStrokeCap::Butt;
        ayt::ui::PathStrokeJoin join = ayt::ui::PathStrokeJoin::Miter;
        float miterLimit = 4.0f;
    };

    HWND _hwnd = nullptr;
    HDC _windowDc = nullptr;   // front buffer (from GetDC)
    HDC _hdc = nullptr;        // draw target (= mem DC when buffering)
    HDC _memDc = nullptr;
    HBITMAP _bitmap = nullptr;
    HBITMAP _oldBitmap = nullptr;
    HFONT _font = nullptr;
    int _fontSize = 0;
    int _width = 0;
    int _height = 0;
    int _bbWidth = 0;
    int _bbHeight = 0;
    int _nextPathId = 1;
    std::vector<int> _clipStates;
    std::unordered_map<int, PathState> _paths;
};

} // namespace ayt::gallery
