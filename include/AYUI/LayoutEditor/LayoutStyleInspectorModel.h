#pragma once

#include "AYUI/Style.h"

#include <cstddef>
#include <string>

namespace ayt::ui {

class Widget;

enum class LayoutStyleSource {
    None,
    Missing,
    StyleSheet,
    InheritedTokenOverride,
    LocalTokenOverride,
};

struct LayoutStyleInspection {
    std::string styleId;
    LayoutStyleSource source = LayoutStyleSource::None;
    bool styleExists = false;
    size_t localOverrideCount = 0;
    size_t inheritedOverrideCount = 0;
    WidgetStyle computed{};
};

class LayoutStyleInspectorModel {
public:
    LayoutStyleInspection inspect(const Widget* widget,
                                  const StyleSheet* sheet) const;
    math::FVector4 backgroundForState(
        const LayoutStyleInspection& inspection,
        StyleState state) const;
    std::wstring sourceLabel(const LayoutStyleInspection& inspection) const;
};

} // namespace ayt::ui
