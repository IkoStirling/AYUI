#include "AYImage.h"
#include "IAYRenderBackend.h"

namespace ayt::ui {

Image::Image()
    : _textureHandle(nullptr)
    , _color(1.0f, 1.0f, 1.0f, 1.0f)
    , _uv(0.0f, 0.0f, 1.0f, 1.0f)
{
}

Image::~Image() {
}

void Image::onRender(IRenderBackend& renderer) {
    if (_size.x <= 0.0f || _size.y <= 0.0f) {
        return;
    }

    math::FRectangle bounds = getWorldBounds();
    if (bounds.maxX <= bounds.minX || bounds.maxY <= bounds.minY) {
        return;
    }

    if (_textureHandle != nullptr) {
        renderer.drawRect(bounds, _textureHandle, _uv);
    } else {
        renderer.drawRect(bounds, _color);
    }
}

} // namespace ayt::ui