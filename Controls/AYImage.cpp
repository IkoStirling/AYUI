#include "AYImage.h"

namespace ayt::ui {

Image::Image()
    : _textureHandle(nullptr)
    , _color(1.0f, 1.0f, 1.0f, 1.0f)
    , _uv(0.0f, 0.0f, 1.0f, 1.0f)
{
}

Image::~Image() {
}

} // namespace ayt::ui