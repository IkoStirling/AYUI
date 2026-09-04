#pragma once

// B-3 — opaque JSON handle.
//
// DockArea's Phase-4 persistence (serializeDockTree / applyDockTree) needs
// to pass nested JSON between helper methods, but the public include path
// must not pull nlohmann/json.hpp into every consumer TU (it is a 25k-line
// header and its version is baked via the CMake `thirdparty_` private
// include directory). The helpers now receive a `JsonHandle` instead of a
// `nlohmann::json&`. The opaque void* is actually a pointer to a
// `nlohmann::json` allocated by DockArea.cpp's internal helpers.
//
// Lifetime: handles are non-owning borrowed references into a json value
// owned by the caller (a std::string holding the dumped text, a json value
// inside serializeDockTree/applyDockTree, etc.). Helpers never store them
// past the call. This keeps public surface clean while preserving the
// recursive serialize/deserialize structure.
//
// Only Controls/AYDockArea.cpp implements the cast helpers via the
// `dockjson::` free functions in `Controls/DockJsonImpl.h`.

#include <cstddef>

namespace ayt::ui {

class JsonHandle {
public:
    JsonHandle() = default;
    explicit JsonHandle(void* p) : p_(p) {}
    void* get() const { return p_; }
    bool valid() const { return p_ != nullptr; }
private:
    void* p_ = nullptr;
};

} // namespace ayt::ui
