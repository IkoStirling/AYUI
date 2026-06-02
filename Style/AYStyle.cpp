#include "AYStyle.h"

namespace ayt::ui {

StyleSheet::StyleSheet() {
    _defaultStyle = StyleBuilder::makeDefault();
}

StyleSheet::~StyleSheet() {
}

bool StyleSheet::loadFromString(const char* data, size_t length) {
    AYUNREFERENCED_PARAM(data);
    AYUNREFERENCED_PARAM(length);
    // TODO: Parse style sheet format
    return false;
}

bool StyleSheet::loadFromFile(const char* filepath) {
    AYUNREFERENCED_PARAM(filepath);
    // TODO: Load and parse file
    return false;
}

const WidgetStyle* StyleSheet::getStyle(const std::string& styleId) const {
    auto it = _styles.find(styleId);
    if (it != _styles.end()) {
        return &it->second;
    }
    return &_defaultStyle;
}

void StyleSheet::setStyle(const std::string& styleId, const WidgetStyle& style) {
    _styles[styleId] = style;
}

WidgetStyle StyleSheet::getComputedStyle(const std::string& styleId) const {
    // TODO: Implement inheritance/computed style
    return *getStyle(styleId);
}

StyleManager::StyleManager()
    : _styleSheet(nullptr)
{
}

StyleManager& StyleManager::get() {
    static StyleManager instance;
    return instance;
}

void StyleManager::setStyleSheet(StyleSheet* sheet) {
    _styleSheet = sheet;
}

const WidgetStyle* StyleManager::getStyle(const std::string& styleId) const {
    if (_styleSheet) {
        return _styleSheet->getStyle(styleId);
    }
    return nullptr;
}

WidgetStyle StyleManager::getComputedStyle(const std::string& styleId) const {
    if (_styleSheet) {
        return _styleSheet->getComputedStyle(styleId);
    }
    return StyleBuilder::makeDefault();
}

void StyleManager::applyStyle(Widget* widget) const {
    AYUNREFERENCED_PARAM(widget);
    // TODO: Apply computed style to widget
}

WidgetStyle StyleBuilder::makeDefault() {
    WidgetStyle style;
    style.backgroundColor = math::FVector4(0.2f, 0.2f, 0.2f, 1.0f);
    style.textColor = math::FVector4(1.0f, 1.0f, 1.0f, 1.0f);
    style.borderColor = math::FVector4(0.3f, 0.3f, 0.3f, 1.0f);
    style.border.width = 1.0f;
    style.border.color = math::FVector4(0.5f, 0.5f, 0.5f, 1.0f);
    style.border.cornerRadius = 0.0f;
    style.font.fontFamily = "Arial";
    style.font.fontSize = 14;
    style.font.color = math::FVector4(1.0f, 1.0f, 1.0f, 1.0f);
    style.font.bold = false;
    style.font.italic = false;
    style.padding = math::FVector4(4.0f, 4.0f, 4.0f, 4.0f);
    style.minWidth = 0.0f;
    style.minHeight = 0.0f;
    style.maxWidth = 0.0f; // 0 means no limit
    style.maxHeight = 0.0f;
    return style;
}

WidgetStyle StyleBuilder::makeButton() {
    WidgetStyle style = makeDefault();
    style.backgroundColor = math::FVector4(0.3f, 0.3f, 0.3f, 1.0f);
    style.border.width = 1.0f;
    style.border.color = math::FVector4(0.5f, 0.5f, 0.5f, 1.0f);
    style.border.cornerRadius = 4.0f;
    style.padding = math::FVector4(8.0f, 4.0f, 8.0f, 4.0f);
    return style;
}

WidgetStyle StyleBuilder::makeTextLabel() {
    WidgetStyle style = makeDefault();
    style.backgroundColor = math::FVector4(0.0f, 0.0f, 0.0f, 0.0f);
    style.textColor = math::FVector4(1.0f, 1.0f, 1.0f, 1.0f);
    style.padding = math::FVector4(2.0f, 2.0f, 2.0f, 2.0f);
    return style;
}

WidgetStyle StyleBuilder::makeWindow() {
    WidgetStyle style = makeDefault();
    style.backgroundColor = math::FVector4(0.15f, 0.15f, 0.15f, 0.95f);
    style.border.width = 2.0f;
    style.border.color = math::FVector4(0.4f, 0.4f, 0.4f, 1.0f);
    style.border.cornerRadius = 6.0f;
    style.padding = math::FVector4(0.0f, 0.0f, 0.0f, 0.0f);
    return style;
}

WidgetStyle StyleBuilder::makePanel() {
    WidgetStyle style = makeDefault();
    style.backgroundColor = math::FVector4(0.2f, 0.2f, 0.2f, 1.0f);
    style.border.width = 1.0f;
    style.border.color = math::FVector4(0.3f, 0.3f, 0.3f, 1.0f);
    style.padding = math::FVector4(4.0f, 4.0f, 4.0f, 4.0f);
    return style;
}

} // namespace ayt::ui