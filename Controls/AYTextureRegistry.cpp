#include "AYUI/TextureRegistry.h"

namespace ayt::ui {

TextureRegistry& TextureRegistry::get() {
    // Local-static singleton. Same pattern as AYUIManager's overlay
    // singleton — single-threaded, lives for the program lifetime.
    // Caller is the UI thread; first use constructs, shutdown clears.
    static TextureRegistry s_instance;
    return s_instance;
}

void TextureRegistry::registerExternal(const std::string& name,
                                       void* handle, int width, int height,
                                       TextureFormat fmt) {
    if (name.empty()) {
        // An anonymous entry would defeat the registry's whole purpose
        // (name-based lookup). Refuse silently — host code path bug.
        return;
    }
    auto it = _entries.find(name);
    if (it != _entries.end()) {
        // Idempotent: only overwrite the texture data when something
        // changed. Hosts may re-register after a hot-reload.
        it->second.tex.handle = handle;
        it->second.tex.width  = width;
        it->second.tex.height = height;
        it->second.tex.format = fmt;
        return;
    }
    Entry e;
    e.tex.handle = handle;
    e.tex.width  = width;
    e.tex.height = height;
    e.tex.format = fmt;
    e.tex.name   = name;
    e.refcount   = 0;
    _entries.emplace(name, std::move(e));
}

ImageTextureHandle TextureRegistry::acquire(const std::string& name) {
    ImageTextureHandle out;
    if (name.empty()) {
        return out;
    }
    auto it = _entries.find(name);
    if (it == _entries.end()) {
        // Pre-register: create an entry with handle=null. Host is
        // expected to follow with registerExternal() before render.
        Entry e;
        e.tex.name   = name;
        e.refcount   = 1;
        _entries.emplace(name, std::move(e));
        out.name = name;
        return out;
    }
    ++it->second.refcount;
    out = it->second.tex;   // by-value: caller doesn't hold a map ref
    return out;
}

void TextureRegistry::release(const std::string& name) {
    if (name.empty()) return;
    auto it = _entries.find(name);
    if (it == _entries.end()) return;
    if (it->second.refcount == 0) {
        // Already at zero — silent no-op (host bug if it happens, but
        // we won't UAF on it).
        return;
    }
    --it->second.refcount;
    if (it->second.refcount == 0) {
        if (_onRelease && it->second.tex.handle != nullptr) {
            _onRelease(it->second.tex);
        }
        _entries.erase(it);
    }
}

bool TextureRegistry::has(const std::string& name) const {
    return _entries.find(name) != _entries.end();
}

size_t TextureRegistry::refcount(const std::string& name) const {
    auto it = _entries.find(name);
    return (it == _entries.end()) ? 0 : it->second.refcount;
}

void TextureRegistry::clearAll() {
    // Fire release for every entry with a live backend handle so the
    // host gets a chance to free GPU memory. Order doesn't matter;
    // each entry is independent.
    for (auto& kv : _entries) {
        if (_onRelease && kv.second.tex.handle != nullptr) {
            _onRelease(kv.second.tex);
        }
    }
    _entries.clear();
}

} // namespace ayt::ui
