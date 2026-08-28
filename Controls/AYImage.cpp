#include "AYUI/Image.h"
#include "AYUI/TextureRegistry.h"
#include "AYUI/IRenderBackend.h"

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
    if (_tex.name == textureName) {
        syncNamedTexture();
        return;
    }
    markDirty();
    // Release whatever we held before — we don't want to leak the
    // previous refcount when the host swaps textures.
    releaseCurrent();
    _tex = TextureRegistry::get().acquire(textureName);
}

void Image::setTexture(const ImageTextureHandle& h) {
    if (!h.name.empty()) {
        TextureRegistry& registry = TextureRegistry::get();
        if (h.handle != nullptr) {
            registry.registerExternal(h.name, h.handle, h.width, h.height, h.format);
        }
        if (_tex.name == h.name) {
            syncNamedTexture();
            return;
        }
        releaseCurrent();
        markDirty();
        _tex = registry.acquire(h.name);
    } else {
        // Anonymous. We own it directly; ~Image will fire the release
        // callback (if installed).
        if (_tex.name.empty()
            && _tex.handle == h.handle
            && _tex.width == h.width
            && _tex.height == h.height
            && _tex.format == h.format) return;
        releaseCurrent();
        markDirty();
        _tex = h;
    }
}

void Image::setTexture(void* rawHandle) {
    // Legacy void* path — anonymous, w/h = 0, no format change.
    if (_tex.name.empty() && _tex.handle == rawHandle
        && _tex.width == 0 && _tex.height == 0) return;
    releaseCurrent();
    markDirty();
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

void Image::syncNamedTexture() {
    if (_tex.name.empty()) return;
    const ImageTextureHandle latest = TextureRegistry::get().lookup(_tex.name);
    if (latest.generation == _tex.generation
        && latest.handle == _tex.handle
        && latest.width == _tex.width
        && latest.height == _tex.height
        && latest.format == _tex.format) return;
    _tex = latest;
    markDirty();
}

void Image::render(IRenderBackend& renderer) {
    // Resolve before Widget::render decides whether its retained commands are
    // reusable. A late registration or hot reload therefore rebuilds the
    // textured command in the same frame and never submits the stale handle.
    syncNamedTexture();
    Widget::render(renderer);
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
