#pragma once

#include "AYLeafWidget.h"

namespace ayt::ui {

// Image is a leaf renderer (no children, no child layout, no hit-test
// descent). It used to extend CompoundWidget — that was a violation of the
// R-6 invariant ("leaf widgets MUST NOT host children"). LeafWidget is the
// correct base; it shares the same no-op performLayout pattern Image was
// already using.
class Image : public LeafWidget {
public:
    Image();
    virtual ~Image();

    void setTexture(void* textureHandle) { _textureHandle = textureHandle; }
    void* getTexture() const { return _textureHandle; }

    void setColor(const math::FVector4& color) { _color = color; }
    const math::FVector4& getColor() const { return _color; }

    void setUV(const math::FRectangle& uv) { _uv = uv; }
    const math::FRectangle& getUV() const { return _uv; }

    // No performLayout override — inherits LeafWidget's no-op. Image is a
    // pure leaf renderer; size/position come from the parent layout pass.

    void onRender(IRenderBackend& renderer) override;

protected:
    void* _textureHandle;
    math::FVector4 _color;
    math::FRectangle _uv;
};

} // namespace ayt::ui