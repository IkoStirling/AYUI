#include "AYImage.h"
#include "AYTextureRegistry.h"
#include "IAYRenderBackend.h"

namespace ayt::ui {

namespace {

// G10 — Global release callback for anonymous textures. Stored as a
// function pointer + captured state via std::function so it can be
// reset by clearReleaseCallback() between tests. Default = no-op so
// unit tests without a backend don't crash.
ImageReleaseCallback& globalReleaseCallback() {
    static ImageReleaseCallback s_cb;
    return s_cb;
}

} // anon

Image::Image()
    : _tex()
    , _color(1.0f, 1.0f, 1.0f, 1.0f)
    , _uv(0.0f, 0.0f, 1.0f, 1.0f)
{
}

Image::~Image() {
    // G10 — release whatever texture we held. releaseCurrent() routes
    // through the registry for named textures and through the global
    // callback for anonymous ones. Idempotent so the order of cleanup
    // doesn't matter.
    releaseCurrent();
}

void Image::releaseCurrent() {
    if (_tex.handle == nullptr && _tex.name.empty()) {
        return;
    }
    if (!_tex.name.empty()) {
        // Named: decrement the registry. If this is the LAST release,
        // the registry fires its own release callback (which the host
        // has installed to call backend.releaseTexture).
        TextureRegistry::get().release(_tex.name);
    } else if (globalReleaseCallback()) {
        // Anonymous with a live handle: fire the global callback.
        globalReleaseCallback()(_tex);
    }
    // Reset so double-release is a no-op.
    _tex.handle = nullptr;
    _tex.name.clear();
    _tex.width = 0;
    _tex.height = 0;
}

void Image::setTexture(const std::string& textureName) {
    if (textureName.empty()) {
        // Treat empty-string as "clear" so callers don't have to
        // special-case it. Mirrors the prior void* null pattern.
        releaseCurrent();
        return;
    }
    // Release whatever we held before — we don't want to leak the
    // previous refcount when the host swaps textures.
    releaseCurrent();
    _tex = TextureRegistry::get().acquire(textureName);
}

void Image::setTexture(const ImageTextureHandle& h) {
    releaseCurrent();
    if (!h.name.empty()) {
        // Caller is asking us to adopt a named handle. The registry
        // already holds the refcount; just take a copy of the data.
        _tex = h;
    } else {
        // Anonymous. We own it directly; ~Image will fire the release
        // callback (if installed).
        _tex = h;
    }
}

void Image::setTexture(void* rawHandle) {
    // Legacy void* path — anonymous, w/h = 0, no format change.
    releaseCurrent();
    _tex.handle = rawHandle;
}

void Image::onRender(IRenderBackend& renderer) {
    if (_size.x <= 0.0f || _size.y <= 0.0f) {
        return;
    }

    math::FRectangle bounds = getWorldBounds();
    if (bounds.maxX <= bounds.minX || bounds.maxY <= bounds.minY) {
        return;
    }

    if (_tex.handle != nullptr) {
        // G10 — backend hook is unchanged (still `void* handle` per
        // the IRenderBackend ABI); the typed wrapping is purely on our
        // side. uv comes from the widget as before.
        renderer.drawRect(bounds, _tex.handle, _uv);
    } else {
        renderer.drawRect(bounds, _color);
    }
}

// --- Static callback plumbing ---

void Image::setReleaseCallback(ImageReleaseCallback cb) {
    globalReleaseCallback() = std::move(cb);
}

bool Image::hasReleaseCallback() {
    return static_cast<bool>(globalReleaseCallback());
}

void Image::clearReleaseCallback() {
    globalReleaseCallback() = nullptr;
}

} // namespace ayt::ui
