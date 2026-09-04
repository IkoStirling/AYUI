#pragma once

#include "AYUI/Widget.h"
#include "AYUI/DockJsonHandle.h"
#include <string>
#include <vector>

namespace ayt { namespace ui {

class WidgetSerializer {
public:
    static std::string serialize(Widget* root, bool pretty = true);
    static std::string serializeWidget(Widget* widget);
    static Widget* deserialize(const std::string& jsonStr);

private:
    static void serializeWidgetToJson(Widget* widget, JsonHandle j);
};

} } // namespace ayt::ui
