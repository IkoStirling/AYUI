#pragma once

// B-3 — internal helpers that cast an opaque ayt::ui::JsonHandle back to
// a real nlohmann::json reference. Only the .cpp files that legitimately
// need nlohmann include this header. Public consumers never see it.

#include "AYUI/DockJsonHandle.h"
#include <nlohmann/json.hpp>

namespace ayt::ui {

inline nlohmann::json& jsonRef(JsonHandle h) {
    return *static_cast<nlohmann::json*>(h.get());
}

inline const nlohmann::json& jsonRefConst(JsonHandle h) {
    return *static_cast<const nlohmann::json*>(h.get());
}

} // namespace ayt::ui
