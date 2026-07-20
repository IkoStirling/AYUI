#pragma once

#include <string>

namespace ayt::ui {

// G10 — Backend-agnostic pixel format. Concrete backends map these to
// native enums (OpenGL GL_RGBA8 / D3D11_FORMAT_R8G8B8A8_UNORM / etc.).
// v1 ships a small enough set that we don't need every BCn/ASTC variant;
// the goal is "enough for editor chrome + typical game UI", not exhaustive.
enum class TextureFormat {
    RGBA8,   // 8-bit-per-channel RGBA, the safe default
    RGB8,    // 8-bit RGB (no alpha)
    R8,      // single-channel grayscale (font atlases, masks)
    RG8,     // two-channel (used by some tangent-space normal atlases)
    DXT5,    // BC3 — common compression on desktop GPUs
    ETC2,    // ETC2 RGBA — common compression on mobile
};

// G10 — Typed texture handle. Replaces the bare `void* _textureHandle`
// in v1's Image so we can introspect dimensions + format, and so the
// lifetime story is uniform across backends.
//
// Two flavors:
//   * NAMED   — `_tex.name` non-empty. Owned via the global TextureRegistry
//               which ref-counts; last release fires the release callback.
//   * ANON    — `_tex.name` empty + `handle` non-null. Owned directly by the
//               Image; ~Image fires the release callback synchronously.
//
// The backend handle itself is still `void*` because it crosses the
// IRenderBackend ABI (every backend has its own concrete handle type and
// we don't want to template the whole widget tree on it).
struct ImageTextureHandle {
    void*               handle = nullptr;
    int                 width  = 0;
    int                 height = 0;
    TextureFormat       format = TextureFormat::RGBA8;
    std::string         name;        // empty → anonymous

    bool isValid() const { return handle != nullptr; }
    bool isNamed()  const { return !name.empty(); }
};

} // namespace ayt::ui
