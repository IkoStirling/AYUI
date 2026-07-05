#pragma once

#include "AYWidget.h"

namespace ayt::ui {

class Image : public CompoundWidget {
public:
    Image();
    virtual ~Image();

    void setTexture(void* textureHandle) { _textureHandle = textureHandle; }
    void* getTexture() const { return _textureHandle; }

    void setColor(const math::FVector4& color) { _color = color; }
    const math::FVector4& getColor() const { return _color; }

    void setUV(const math::FRectangle& uv) { _uv = uv; }
    const math::FRectangle& getUV() const { return _uv; }

    void performLayout() override;

    void onRender(IRenderBackend& renderer) override;

protected:
    void* _textureHandle;
    math::FVector4 _color;
    math::FRectangle _uv;
};

} // namespace ayt::ui