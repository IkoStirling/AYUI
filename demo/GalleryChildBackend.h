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
// GDI is Win32-only; the Gallery target is WIN32, so no platform guard
// is needed in this header (the .cpp guards its body for parity).

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

    HWND _hwnd = nullptr;
    HDC _hdc = nullptr;
    int _width = 0;
    int _height = 0;
};

} // namespace ayt::gallery
