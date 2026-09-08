#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace ayt::ui {

enum class AuthoringProperty : std::uint8_t {
    Id,
    X,
    Y,
    Width,
    Height,
    Text,
    Texture,
    Items,
    TextHAlign,
    TextVAlign,
    Style,
    Checked,
    Password,
    ReadOnly,
    Controller,
    OnClick,
    OnToggled,
    OnValueChanged,
    OnTextChanged,
    OnSubmit,
    OnSelectionChanged,
    OnItemActivated,
    OnClose,
    Gravity,
    Spacing,
    Padding
};

enum class PropertySection : std::uint8_t {
    Identity,
    Transform,
    Content,
    Appearance,
    Layout,
    Interaction
};

struct PropertyFieldSchema {
    AuthoringProperty property;
    PropertySection section;
    const char* key;
    const char* rowId;
    const char* labelId;
    const char* controlId;
};

class PropertySchema {
public:
    PropertySchema() = default;
    PropertySchema(std::initializer_list<AuthoringProperty> properties);

    PropertySchema& add(AuthoringProperty property);
    PropertySchema& add(const PropertySchema& other);
    bool contains(AuthoringProperty property) const;
    bool hasSection(PropertySection section) const;
    std::vector<const PropertyFieldSchema*> fields() const;
    std::uint64_t mask() const { return _mask; }

private:
    std::uint64_t _mask = 0;
};

const PropertyFieldSchema& propertyFieldSchema(AuthoringProperty property);
const std::vector<PropertyFieldSchema>& allPropertyFieldSchemas();

} // namespace ayt::ui
