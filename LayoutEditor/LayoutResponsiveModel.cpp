#include "AYUI/LayoutEditor/LayoutResponsiveModel.h"

#include <algorithm>
#include <cmath>

namespace ayt::ui {

LayoutResponsiveModel::LayoutResponsiveModel() {
    _breakpoints = {
        {"Compact", L"Compact  < 600 DIP", 0.0f, 600.0f, 390.0f},
        {"Medium", L"Medium  600–1023 DIP", 600.0f, 1024.0f, 800.0f},
        {"Wide", L"Wide  ≥ 1024 DIP", 1024.0f, 0.0f, 1280.0f}
    };
}

bool LayoutResponsiveModel::validIndex(int index) const {
    return index >= 0 && index < static_cast<int>(_breakpoints.size());
}

int LayoutResponsiveModel::breakpointForWidth(float logicalWidth) const {
    if (!std::isfinite(logicalWidth)) return -1;
    for (size_t index = 0; index < _breakpoints.size(); ++index) {
        const LayoutBreakpoint& breakpoint = _breakpoints[index];
        if (logicalWidth >= breakpoint.minWidth &&
            (breakpoint.maxWidth <= 0.0f ||
             logicalWidth < breakpoint.maxWidth)) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

const ResponsiveLayoutRule* LayoutResponsiveModel::rule(
    const Widget& widget, int breakpointIndex) const {
    if (!validIndex(breakpointIndex)) return nullptr;
    const std::string& name = _breakpoints[
        static_cast<size_t>(breakpointIndex)].name;
    for (const ResponsiveLayoutRule& candidate :
         widget.getResponsiveLayoutRules()) {
        if (candidate.name == name) return &candidate;
    }
    return nullptr;
}

ResponsiveVisibility LayoutResponsiveModel::visibility(
    const Widget& widget, int breakpointIndex) const {
    const ResponsiveLayoutRule* found = rule(widget, breakpointIndex);
    return found != nullptr ? found->visibility
                            : ResponsiveVisibility::Inherit;
}

ResponsiveLayoutRule LayoutResponsiveModel::makeRule(
    int breakpointIndex) const {
    ResponsiveLayoutRule result;
    if (!validIndex(breakpointIndex)) return result;
    const LayoutBreakpoint& breakpoint = _breakpoints[
        static_cast<size_t>(breakpointIndex)];
    result.name = breakpoint.name;
    result.minParentWidth = breakpoint.minWidth;
    result.maxParentWidth = breakpoint.maxWidth;
    return result;
}

bool LayoutResponsiveModel::setVisibility(
    Widget& widget, int breakpointIndex,
    ResponsiveVisibility nextVisibility) const {
    if (!validIndex(breakpointIndex)) return false;
    std::vector<ResponsiveLayoutRule> rules =
        widget.getResponsiveLayoutRules();
    const std::string& name = _breakpoints[
        static_cast<size_t>(breakpointIndex)].name;
    auto found = std::find_if(
        rules.begin(), rules.end(), [&name](const ResponsiveLayoutRule& rule) {
            return rule.name == name;
        });
    if (found == rules.end()) {
        if (nextVisibility == ResponsiveVisibility::Inherit) return false;
        ResponsiveLayoutRule created = makeRule(breakpointIndex);
        created.visibility = nextVisibility;
        rules.push_back(std::move(created));
    } else {
        if (found->visibility == nextVisibility) return false;
        found->visibility = nextVisibility;
        if (found->visibility == ResponsiveVisibility::Inherit &&
            !found->overrideAnchors) {
            rules.erase(found);
        }
    }
    widget.setResponsiveLayoutRules(std::move(rules));
    return true;
}

bool LayoutResponsiveModel::captureAnchorOverride(
    Widget& widget, int breakpointIndex) const {
    if (!validIndex(breakpointIndex) || !widget.hasAnchorLayout()) return false;
    std::vector<ResponsiveLayoutRule> rules =
        widget.getResponsiveLayoutRules();
    const std::string& name = _breakpoints[
        static_cast<size_t>(breakpointIndex)].name;
    auto found = std::find_if(
        rules.begin(), rules.end(), [&name](const ResponsiveLayoutRule& rule) {
            return rule.name == name;
        });
    if (found == rules.end()) {
        rules.push_back(makeRule(breakpointIndex));
        found = std::prev(rules.end());
    }
    found->anchors = widget.getAnchorLayout();
    found->overrideAnchors = true;
    widget.setResponsiveLayoutRules(std::move(rules));
    return true;
}

bool LayoutResponsiveModel::clearRule(Widget& widget,
                                      int breakpointIndex) const {
    if (!validIndex(breakpointIndex)) return false;
    std::vector<ResponsiveLayoutRule> rules =
        widget.getResponsiveLayoutRules();
    const std::string& name = _breakpoints[
        static_cast<size_t>(breakpointIndex)].name;
    const auto next = std::remove_if(
        rules.begin(), rules.end(), [&name](const ResponsiveLayoutRule& rule) {
            return rule.name == name;
        });
    if (next == rules.end()) return false;
    rules.erase(next, rules.end());
    widget.setResponsiveLayoutRules(std::move(rules));
    return true;
}

float LayoutResponsiveModel::previewWidth(int breakpointIndex) const {
    return validIndex(breakpointIndex)
        ? _breakpoints[static_cast<size_t>(breakpointIndex)].previewWidth
        : 0.0f;
}

} // namespace ayt::ui
