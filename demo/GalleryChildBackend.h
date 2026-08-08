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

#include "aymath/MathDefs.h"
#include "aymath/MathTypes.h"
#include "IAYRenderBackend.h"

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <Windows.h>

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

private:
    static COLORREF toColorRef(const math::FVector4& color);
    RECT toRect(const math::FRectangle& bounds) const;
    void ensureBackbuffer(int width, int height);
    void releaseBackbuffer();
    HFONT fontForSize(int fontSize);

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
};

} // namespace ayt::gallery
