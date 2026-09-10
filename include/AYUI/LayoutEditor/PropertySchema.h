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
    Padding,
    ValueMin,
    ValueMax,
    Value,
    ImageTint,
    ImageUvMinX,
    ImageUvMinY,
    ImageUvMaxX,
    ImageUvMaxY,
    SelectionMode,
    ItemHeight,
    TileWidth,
    TileHeight,
    TileSpacing,
    VerticalScrollBarVisibility,
    HorizontalScrollBarVisibility,
    TabOverflowMode,
    MinTabWidth,
    GridRows,
    GridColumns,
    GridSpacingX,
    GridSpacingY,
    RichTextWrapMode,
    RichTextOverflow,
    LineHeight,
    MaxLines
};

enum class PropertySection : std::uint8_t {
    Identity,
    Transform,
    Content,
    Appearance,
    Layout,
    Interaction
};

// The schema describes both availability and the editor control contract.
// LayoutEditorSession still hosts the concrete chrome, but it no longer owns
// duplicated enum lists or numeric intent for schema-backed fields.
enum class PropertyEditorKind : std::uint8_t {
    Text,
    MultilineText,
    Number,
    Integer,
    Boolean,
    Enum,
    Color,
    Resource
};

enum class PropertyRowKind : std::uint8_t {
    Standard,
    Vector4
};

struct PropertyFieldSchema {
    AuthoringProperty property;
    PropertySection section;
    const char* key;
    const char* rowId;
    const char* labelId;
    const char* controlId;
    PropertyEditorKind editorKind = PropertyEditorKind::Text;
    bool hasRange = false;
    float minimum = 0.0f;
    float maximum = 0.0f;
    float step = 0.0f;
    std::vector<std::string> enumOptions;
    const char* displayName = nullptr;
    PropertyRowKind rowKind = PropertyRowKind::Standard;
    float rowHeight = 32.0f;
    // Standard rows use controlId. Vector4 rows use these component IDs and
    // labels, allowing compound property chrome to remain schema-owned.
    std::vector<std::string> componentControlIds;
    std::vector<std::string> componentLabels;
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
const PropertyFieldSchema* findPropertyFieldSchema(const std::string& key);
const std::vector<PropertyFieldSchema>& allPropertyFieldSchemas();

} // namespace ayt::ui
