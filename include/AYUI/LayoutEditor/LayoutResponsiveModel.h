#pragma once

#include "AYUI/Widget.h"

#include <string>
#include <vector>

namespace ayt::ui {

struct LayoutBreakpoint {
    std::string name;
    std::wstring label;
    float minWidth = 0.0f;
    float maxWidth = 0.0f;
    float previewWidth = 0.0f;
};

class LayoutResponsiveModel {
public:
    LayoutResponsiveModel();

    const std::vector<LayoutBreakpoint>& breakpoints() const {
        return _breakpoints;
    }
    int breakpointForWidth(float logicalWidth) const;
    const ResponsiveLayoutRule* rule(const Widget& widget,
                                     int breakpointIndex) const;
    ResponsiveVisibility visibility(const Widget& widget,
                                    int breakpointIndex) const;

    bool setVisibility(Widget& widget, int breakpointIndex,
                       ResponsiveVisibility visibility) const;
    bool captureAnchorOverride(Widget& widget, int breakpointIndex) const;
    bool clearRule(Widget& widget, int breakpointIndex) const;
    float previewWidth(int breakpointIndex) const;

private:
    bool validIndex(int index) const;
    ResponsiveLayoutRule makeRule(int breakpointIndex) const;

    std::vector<LayoutBreakpoint> _breakpoints;
};

} // namespace ayt::ui
