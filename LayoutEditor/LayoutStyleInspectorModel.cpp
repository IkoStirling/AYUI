#include "AYUI/LayoutEditor/LayoutStyleInspectorModel.h"

#include "AYUI/Widget.h"

#include <unordered_set>

namespace ayt::ui {

LayoutStyleInspection LayoutStyleInspectorModel::inspect(
    const Widget* widget, const StyleSheet* sheet) const {
    LayoutStyleInspection result;
    if (widget == nullptr) return result;

    result.styleId = widget->getStyleId();
    if (result.styleId.empty()) {
        result.localOverrideCount = widget->getStyleTokenOverrides().size();
        for (const Widget* parent = widget->getParent(); parent != nullptr;
             parent = parent->getParent()) {
            result.inheritedOverrideCount +=
                parent->getStyleTokenOverrides().size();
        }
        result.source = result.localOverrideCount > 0
            ? LayoutStyleSource::LocalTokenOverride
            : (result.inheritedOverrideCount > 0
                ? LayoutStyleSource::InheritedTokenOverride
                : LayoutStyleSource::None);
        return result;
    }
    if (sheet == nullptr || !sheet->containsStyle(result.styleId)) {
        result.source = LayoutStyleSource::Missing;
        return result;
    }

    result.styleExists = true;
    result.computed = sheet->getComputedStyle(result.styleId);
    std::unordered_set<std::string> referencedTokens;
    for (const std::string* token : {&result.computed.bgToken,
                                     &result.computed.borderColorToken,
                                     &result.computed.textColorToken}) {
        if (token != nullptr && !token->empty()) referencedTokens.insert(*token);
    }
    for (const std::string& token : referencedTokens) {
        const auto local = widget->getStyleTokenOverrides().find(token);
        if (local != widget->getStyleTokenOverrides().end()) {
            ++result.localOverrideCount;
            if (token == result.computed.bgToken) {
                result.computed.backgroundColor = local->second;
            }
            if (token == result.computed.borderColorToken) {
                result.computed.borderColor = local->second;
                result.computed.border.color = local->second;
            }
            if (token == result.computed.textColorToken) {
                result.computed.textColor = local->second;
                result.computed.font.color = local->second;
            }
            continue;
        }
        for (const Widget* parent = widget->getParent(); parent != nullptr;
             parent = parent->getParent()) {
            const auto inherited = parent->getStyleTokenOverrides().find(token);
            if (inherited == parent->getStyleTokenOverrides().end()) continue;
            ++result.inheritedOverrideCount;
            if (token == result.computed.bgToken) {
                result.computed.backgroundColor = inherited->second;
            }
            if (token == result.computed.borderColorToken) {
                result.computed.borderColor = inherited->second;
                result.computed.border.color = inherited->second;
            }
            if (token == result.computed.textColorToken) {
                result.computed.textColor = inherited->second;
                result.computed.font.color = inherited->second;
            }
            break;
        }
    }
    result.source = result.localOverrideCount > 0
        ? LayoutStyleSource::LocalTokenOverride
        : (result.inheritedOverrideCount > 0
            ? LayoutStyleSource::InheritedTokenOverride
            : LayoutStyleSource::StyleSheet);
    return result;
}

math::FVector4 LayoutStyleInspectorModel::backgroundForState(
    const LayoutStyleInspection& inspection, StyleState state) const {
    if (inspection.styleExists && inspection.computed.backgroundStates.enabled) {
        return inspection.computed.backgroundStates.forState(state);
    }
    return inspection.computed.backgroundColor;
}

std::wstring LayoutStyleInspectorModel::sourceLabel(
    const LayoutStyleInspection& inspection) const {
    switch (inspection.source) {
    case LayoutStyleSource::Missing:
        return L"Missing style definition";
    case LayoutStyleSource::StyleSheet:
        return L"Style sheet";
    case LayoutStyleSource::InheritedTokenOverride:
        return L"Style sheet + inherited token overrides (" +
            std::to_wstring(inspection.inheritedOverrideCount) + L")";
    case LayoutStyleSource::LocalTokenOverride:
        return L"Local token overrides (" +
            std::to_wstring(inspection.localOverrideCount) + L")";
    case LayoutStyleSource::None:
    default:
        return L"Default widget appearance";
    }
}

} // namespace ayt::ui
