#include "GalleryChildBackend.h"
#include "aymath/MathDefs.h"

// MIRROR of AYEditor/demo/GdiRenderBackend.cpp — see the header comment
// for why two copies exist. GDI is Win32-only; guard the whole
// implementation so a non-Windows build of the demo compiles it empty.
#if defined(_WIN32)

namespace ayt::gallery {

using namespace ayt::math;

namespace {

UINT horizontalAlign(ayt::ui::IRenderBackend::TextStyle::Align align) {
    switch (align) {
    case ayt::ui::IRenderBackend::TextStyle::Align::Center: return DT_CENTER;
    case ayt::ui::IRenderBackend::TextStyle::Align::Right:  return DT_RIGHT;
    default: return DT_LEFT;
    }
}

} // namespace

GalleryChildBackend::GalleryChildBackend(HWND hwnd)
    : _hwnd(hwnd) {
}

void GalleryChildBackend::setDrawTarget(HDC hdc, int width, int height) {
    _hdc = hdc;
    _width = width;
    _height = height;
}

void GalleryChildBackend::beginFrame() {
}

void GalleryChildBackend::endFrame() {
}

void GalleryChildBackend::beginCanvas(const math::FRectangle& viewport) {
    if (!_hdc) {
        return;
    }
    drawRect(viewport, math::FVector4(0.10f, 0.10f, 0.11f, 1.0f));
}

void GalleryChildBackend::endCanvas() {
}

COLORREF GalleryChildBackend::toColorRef(const math::FVector4& color) {
    const int r = static_cast<int>(color.x * 255.0f);
    const int g = static_cast<int>(color.y * 255.0f);
    const int b = static_cast<int>(color.z * 255.0f);
    return RGB(r, g, b);
}

RECT GalleryChildBackend::toRect(const math::FRectangle& bounds) const {
    RECT rect{};
    rect.left = static_cast<LONG>(bounds.minX);
    rect.top = static_cast<LONG>(bounds.minY);
    rect.right = static_cast<LONG>(bounds.maxX);
    rect.bottom = static_cast<LONG>(bounds.maxY);
    return rect;
}

void GalleryChildBackend::drawRect(const math::FRectangle& bounds, const math::FVector4& color) {
    if (!_hdc) {
        return;
    }

    HBRUSH brush = CreateSolidBrush(toColorRef(color));
    RECT rect = toRect(bounds);
    FillRect(_hdc, &rect, brush);
    DeleteObject(brush);
}

void GalleryChildBackend::drawRect(const math::FRectangle& bounds, void* textureHandle,
                                   const math::FRectangle& uv) {
    AYUNREFERENCED_PARAM(textureHandle);
    AYUNREFERENCED_PARAM(uv);
    drawRect(bounds, math::FVector4(0.25f, 0.25f, 0.28f, 1.0f));
}

void GalleryChildBackend::drawText(const math::FRectangle& bounds, const std::wstring& text,
                                   int fontSize, const math::FVector4& color) {
    if (!_hdc || text.empty()) {
        return;
    }

    SetBkMode(_hdc, TRANSPARENT);
    SetTextColor(_hdc, toColorRef(color));

    HFONT font = CreateFontW(
        fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    HGDIOBJ oldFont = SelectObject(_hdc, font);
    RECT rect = toRect(bounds);
    DrawTextW(_hdc, text.c_str(), static_cast<int>(text.size()), &rect,
              DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | horizontalAlign(
                  ayt::ui::IRenderBackend::TextStyle::Align::Left));
    SelectObject(_hdc, oldFont);
    DeleteObject(font);
}

void GalleryChildBackend::drawWithAlpha(const math::FRectangle& bounds, void* textureHandle,
                                        float alpha) {
    AYUNREFERENCED_PARAM(alpha);
    drawRect(bounds, textureHandle, math::FRectangle(0.0f, 0.0f, 1.0f, 1.0f));
}

} // namespace ayt::gallery

#endif // _WIN32
