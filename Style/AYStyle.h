#pragma once

#include "aymath/MathTypes.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace ayt::ui {

// Forward declarations
class Widget;

// Style property types
struct ColorStyle {
    math::FVector4 normal;
    math::FVector4 hovered;
    math::FVector4 pressed;
    math::FVector4 disabled;
};

struct BorderStyle {
    float width;
    math::FVector4 color;
    float cornerRadius;
};

struct FontStyle {
    std::string fontFamily;
    int fontSize;
    math::FVector4 color;
    bool bold;
    bool italic;
};

// Complete style for a widget
struct WidgetStyle {
    math::FVector4 backgroundColor;
    math::FVector4 textColor;
    math::FVector4 borderColor;
    BorderStyle border;
    FontStyle font;
    math::FVector4 padding;
    float minWidth;
    float minHeight;
    float maxWidth;
    float maxHeight;
};

class StyleSheet {
public:
    StyleSheet();
    ~StyleSheet();

    // Load from file or string
    bool loadFromString(const char* data, size_t length);
    bool loadFromFile(const char* filepath);

    // Get style by ID
    const WidgetStyle* getStyle(const std::string& styleId) const;

    // Add or update style
    void setStyle(const std::string& styleId, const WidgetStyle& style);

    // Merge styles (for inheritance)
    WidgetStyle getComputedStyle(const std::string& styleId) const;

private:
    std::unordered_map<std::string, WidgetStyle> _styles;
    WidgetStyle _defaultStyle;

    // Parse helpers
    void parseStyle(const char* data, size_t length);
    void parseProperty(const std::string& styleId, const std::string& name, const std::string& value);
};

class StyleManager {
public:
    static StyleManager& get();

    void setStyleSheet(StyleSheet* sheet);
    StyleSheet* getStyleSheet() const { return _styleSheet; }

    const WidgetStyle* getStyle(const std::string& styleId) const;
    WidgetStyle getComputedStyle(const std::string& styleId) const;

    void applyStyle(Widget* widget) const;

private:
    StyleManager();
    StyleManager(const StyleManager&) = delete;
    StyleManager& operator=(const StyleManager&) = delete;

    StyleSheet* _styleSheet;
};

// Helper to define styles in code
struct StyleBuilder {
    static WidgetStyle makeDefault();
    static WidgetStyle makeButton();
    static WidgetStyle makeTextLabel();
    static WidgetStyle makeWindow();
    static WidgetStyle makePanel();
};

} // namespace ayt::ui