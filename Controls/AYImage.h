#pragma once

#include "AYLeafWidget.h"
#include "AYImageTexture.h"
#include <functional>
#include <string>

namespace ayt::ui {

// Forward decl so the public header doesn't pull in the registry.
class TextureRegistry;

// G10 — callback type used by Image to release an anonymous backend
// texture. Fires from ~Image when the handle is anonymous (no name)
// AND a callback is installed. Global because every Image routes
// lifetime through the same path.
using ImageReleaseCallback = std::function<void(const ImageTextureHandle&)>;

// Image is a leaf renderer (no children, no child layout, no hit-test
// descent). It used to extend CompoundWidget — that was a violation of the
// R-6 invariant ("leaf widgets MUST NOT host children"). LeafWidget is the
// correct base; it shares the same no-op performLayout pattern Image was
// already using.
//
// G10 — Texture handle is now a typed ImageTextureHandle (width/height/
// format/name) instead of raw void*. Lifetime contract:
//
//   * NAMED texture (handle.name non-empty):
//       Image holds a refcount in TextureRegistry("ui/icon_save").
//       ~Image calls TextureRegistry::release(name) → refcount--.
//       Last release fires the registry's release callback.
//   * ANONYMOUS texture (handle.name empty, handle.handle non-null):
//       Image is the sole owner. ~Image fires the global
//       Image::setReleaseCallback() if installed.
//
// Host wires the global callback once at startup, e.g.:
//
//     Image::setReleaseCallback([&backend](const ImageTextureHandle& h) {
//         backend.releaseTexture(h.handle);
//     });
//
// Or installs a TextureRegistry release callback for shared textures.
class Image : public LeafWidget {
public:
    Image();
    virtual ~Image();

    // Set a NAMED texture — ref-counted through TextureRegistry. Safe
    // to call multiple times: releases the previous name first.
    void setTexture(const std::string& textureName);

    // Set a fully-typed handle (anonymous or named). If `h.name` is
    // empty, the handle is owned directly by this Image; if non-empty,
    // the registry holds the refcount and Image is a borrower.
    void setTexture(const ImageTextureHandle& h);

    // Legacy overload — accepts a raw pointer and creates an anonymous
    // handle (name empty, w/h = 0). Kept so existing call sites
    // (`image.setTexture(ptr)`) keep compiling.
    void setTexture(void* rawHandle);

    const ImageTextureHandle& getTexture() const { return _tex; }
    const std::string& getTextureName() const { return _tex.name; }
    bool hasTexture() const { return _tex.handle != nullptr; }

    void setColor(const math::FVector4& color) { _color = color; }
    const math::FVector4& getColor() const { return _color; }

    void setUV(const math::FRectangle& uv) { _uv = uv; }
    const math::FRectangle& getUV() const { return _uv; }

    void onRender(IRenderBackend& renderer) override;

    // G10 — Global release callback for ANONYMOUS textures. Host
    // installs once at startup; the default is a no-op so unit tests
    // without a backend don't crash.
    static void setReleaseCallback(ImageReleaseCallback cb);
    static bool hasReleaseCallback();
    // Test helper: reset the global callback to a no-op. Tests call
    // this in their dtor / fixture teardown to avoid leaking state
    // across cases.
    static void clearReleaseCallback();

protected:
    ImageTextureHandle _tex;
    math::FVector4 _color;
    math::FRectangle _uv;

private:
    // Release whatever _tex currently holds. Called from ~Image and
    // from setTexture() when replacing an existing handle. Idempotent.
    void releaseCurrent();
};

} // namespace ayt::ui
