#pragma once

#include "AYUI/Style.h"
#include "AYUI/Theme.h"

#include <string>
#include <vector>

namespace ayt::ui {

enum class LayoutThemeTokenKind { Color, Float };

struct LayoutThemeTokenEntry {
    std::string key;
    LayoutThemeTokenKind kind = LayoutThemeTokenKind::Color;
    std::string value;
};

struct LayoutThemeStyleEntry {
    std::string fragment;
    std::string styleId;
};

// Lossless authoring projection over Theme JSON. Unknown fields are retained;
// mutations only touch tokens and explicitly selected style properties.
class LayoutThemeEditorModel {
public:
    bool load(const std::string& jsonText, std::string* error = nullptr);
    std::string serialize(bool pretty = true) const;
    void clear();

    std::vector<LayoutThemeTokenEntry> tokens() const;
    std::vector<LayoutThemeStyleEntry> styles() const;

    bool setColorToken(const std::string& key, const math::FVector4& value,
                       std::string* error = nullptr);
    bool setFloatToken(const std::string& key, float value,
                       std::string* error = nullptr);
    bool renameToken(const std::string& oldKey, const std::string& newKey,
                     std::string* error = nullptr);
    bool removeToken(const std::string& key, bool allowReferenced = false,
                     std::string* error = nullptr);
    std::size_t referenceCount(const std::string& key) const;

    bool setStyleProperty(const std::string& fragment,
                          const std::string& styleId,
                          const std::string& property,
                          const std::string& tokenOrLiteral,
                          std::string* error = nullptr);
    std::string styleProperty(const std::string& fragment,
                              const std::string& styleId,
                              const std::string& property) const;
    bool createStyle(const std::string& fragment, const std::string& styleId,
                     std::string* error = nullptr);
    bool duplicateStyle(const std::string& sourceFragment,
                        const std::string& sourceStyleId,
                        const std::string& destinationFragment,
                        const std::string& destinationStyleId,
                        std::string* error = nullptr);
    bool removeStyle(const std::string& fragment, const std::string& styleId,
                     std::string* error = nullptr);
    Theme buildPreviewTheme() const;

private:
    std::string _json = "{}";
};

} // namespace ayt::ui
