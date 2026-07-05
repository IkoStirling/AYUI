#pragma once

#include "AYWidget.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace ayt { namespace ui {
using json = nlohmann::json;

class WidgetSerializer {
public:
    static std::string serialize(Widget* root, bool pretty = true);
    static std::string serializeWidget(Widget* widget);
    static Widget* deserialize(const std::string& jsonStr);

private:
    static void serializeWidgetToJson(Widget* widget, json& j);
};

} } // namespace ayt::ui
