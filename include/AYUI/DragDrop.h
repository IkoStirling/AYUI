#pragma once

#include <string>

namespace ayt::ui {

// =============================================================================
// G12 — Drag & Drop payload (POD).
// =============================================================================
//
// A drag session carries a single DragPayload from source to target. The
// payload is intentionally opaque + tagged:
//
//   - `kind`     : dispatch key. Hosts compare `kind == "FilePath"` etc.
//                  This is the primary contract between source and target.
//   - `text`     : optional human-readable label. UIManager's drag ghost
//                  displays it during the drag (R-3 best-effort text).
//   - `data`     : opaque host-typed pointer. Source decides how to fill
//                  it (e.g. an index into a vector, a heap-allocated struct
//                  with lifetime extending the drag session, a window-
//                  owned resource). Target decides how to interpret it.
//                  UIManager never dereferences this pointer — it just
//                  shuttles it.
//   - `userData` : convenience int slot for trivially-copyable payloads
//                  (e.g. a single index) so hosts don't have to heap-
//                  allocate just to drag.
//
// Mirrors the "opaque handle + tag" convention already used elsewhere in
// this codebase (cf. AYEventSystem byte-memcpy payloads, but main-thread
// only here so no serialization overhead).
//
// `isEmpty()` is the gate UIManager uses to refuse a payload-less drag
// (beginDrag with empty payload is a programming error but not a crash).
// =============================================================================

struct DragPayload {
    std::string  kind;
    std::wstring text;
    void*        data     = nullptr;
    int          userData = 0;

    bool isEmpty() const {
        return kind.empty()
            && text.empty()
            && data == nullptr
            && userData == 0;
    }
};

} // namespace ayt::ui