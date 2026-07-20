#pragma once

#include "AYImageTexture.h"
#include <functional>
#include <string>
#include <unordered_map>

namespace ayt::ui {

// G10 — TextureRegistry. Ref-counted, name-keyed shared texture store.
//
// Use case: an editor loads `ui/icon_save.png` once, then several Image
// widgets (toolbar button + menu item + list row icon) all share the
// same backend texture handle. Without a registry, every Image would
// either each load + own their own copy (waste of VRAM) or share raw
// `void*` and leak on the last drop because nobody knows when "last" is.
//
// Lifecycle:
//   1. Host loads the texture via whatever backend path → backend.handle
//   2. Host calls registry.registerExternal("ui/icon_save", handle, w, h, fmt)
//   3. Each Image widget that wants to use it calls
//          registry.acquire("ui/icon_save")
//      → bumps refcount, returns a typed ImageTextureHandle with `name` set
//   4. On ~Image, registry.release("ui/icon_save") decrements; when refcount
//      hits 0, the registered ReleaseCallback fires (host's responsibility
//      to call backend.releaseTexture(handle)).
//
// Thread safety: v1 single-threaded — every call assumes the UI thread.
// (The game-thread / render-thread split will need a queue; not in scope.)
class TextureRegistry {
public:
    using ReleaseCallback = std::function<void(const ImageTextureHandle&)>;

    static TextureRegistry& get();

    // Register a backend-supplied texture under `name`. Idempotent: a
    // second register with the same name + same handle is a no-op; a
    // second register with the same name + DIFFERENT handle is a host
    // bug — we keep the original and log nothing (callers must debug).
    // Refcount starts at 0; callers bump via acquire().
    void registerExternal(const std::string& name,
                          void* handle, int width, int height,
                          TextureFormat fmt);

    // Bump refcount + return the current entry's handle. If the name
    // isn't registered yet, creates an entry with refcount=1 and an
    // empty (handle=null) ImageTextureHandle — caller is expected to
    // registerExternal() before render. Returns by value so the caller
    // can stash it in an Image without holding a reference into the map.
    ImageTextureHandle acquire(const std::string& name);

    // Decrement refcount. When it hits 0, fires the release callback
    // (if set) and erases the entry. No-op if the name is unknown.
    void release(const std::string& name);

    bool   has(const std::string& name) const;
    size_t refcount(const std::string& name) const;
    size_t size() const { return _entries.size(); }

    // Install a release callback. Fires when the LAST refcount is
    // dropped, before the entry is erased. Host uses this to call
    // backend.releaseTexture(handle). Idempotent — set once, kept for
    // the registry's lifetime.
    void setReleaseCallback(ReleaseCallback cb) { _onRelease = std::move(cb); }
    bool hasReleaseCallback() const { return static_cast<bool>(_onRelease); }

    // Test-only / host-only escape hatch: drop everything. Used by
    // shutdown paths and the test harness so test order doesn't leak
    // state. Fires the release callback for every entry with a live
    // backend handle.
    void clearAll();

private:
    TextureRegistry() = default;
    TextureRegistry(const TextureRegistry&) = delete;
    TextureRegistry& operator=(const TextureRegistry&) = delete;

    struct Entry {
        ImageTextureHandle tex;
        size_t refcount = 0;
    };
    std::unordered_map<std::string, Entry> _entries;
    ReleaseCallback _onRelease;
};

} // namespace ayt::ui
