#include "AYUI/LayoutEditor/PropertySchema.h"

#include <stdexcept>

namespace ayt::ui {
namespace {

constexpr std::uint64_t propertyBit(AuthoringProperty property) {
    return std::uint64_t{1} << static_cast<std::uint8_t>(property);
}

PropertyFieldSchema field(
    AuthoringProperty property, PropertySection section, const char* key,
    const char* rowId, const char* labelId, const char* controlId,
    PropertyEditorKind editorKind = PropertyEditorKind::Text,
    bool hasRange = false, float minimum = 0.0f, float maximum = 0.0f,
    float step = 0.0f,
    std::initializer_list<const char*> enumOptions = {}) {
    PropertyFieldSchema result{property, section, key, rowId, labelId,
                               controlId, editorKind, hasRange, minimum,
                               maximum, step};
    result.enumOptions.reserve(enumOptions.size());
    for (const char* option : enumOptions) {
        result.enumOptions.emplace_back(option != nullptr ? option : "");
    }
    return result;
}

} // namespace

const std::vector<PropertyFieldSchema>& allPropertyFieldSchemas() {
    static const std::vector<PropertyFieldSchema> fields = {
        field(AuthoringProperty::Id, PropertySection::Identity,
              "id", "row_prop_id", "lbl_prop_id", "prop_id"),
        field(AuthoringProperty::X, PropertySection::Transform,
              "x", "row_prop_x", "lbl_prop_x", "prop_x",
              PropertyEditorKind::Number, false, 0.0f, 0.0f, 1.0f),
        field(AuthoringProperty::Y, PropertySection::Transform,
              "y", "row_prop_y", "lbl_prop_y", "prop_y",
              PropertyEditorKind::Number, false, 0.0f, 0.0f, 1.0f),
        field(AuthoringProperty::Width, PropertySection::Transform,
              "w", "row_prop_w", "lbl_prop_w", "prop_w",
              PropertyEditorKind::Number, true, 8.0f, 16384.0f, 1.0f),
        field(AuthoringProperty::Height, PropertySection::Transform,
              "h", "row_prop_h", "lbl_prop_h", "prop_h",
              PropertyEditorKind::Number, true, 8.0f, 16384.0f, 1.0f),
        field(AuthoringProperty::Text, PropertySection::Content,
              "text", "row_prop_text", "lbl_prop_text", "prop_text",
              PropertyEditorKind::MultilineText),
        field(AuthoringProperty::Texture, PropertySection::Content,
              "texture", "row_prop_texture", "lbl_prop_texture", "prop_texture",
              PropertyEditorKind::Resource),
        field(AuthoringProperty::Items, PropertySection::Content,
              "items", "row_prop_items", "lbl_prop_items", "prop_items",
              PropertyEditorKind::MultilineText),
        field(AuthoringProperty::TextHAlign, PropertySection::Content,
              "hAlign", "row_prop_text_halign", "lbl_prop_text_halign",
              "prop_text_halign", PropertyEditorKind::Enum, false, 0.0f,
              0.0f, 0.0f, {"Left", "Center", "Right"}),
        field(AuthoringProperty::TextVAlign, PropertySection::Content,
              "vAlign", "row_prop_text_valign", "lbl_prop_text_valign",
              "prop_text_valign", PropertyEditorKind::Enum, false, 0.0f,
              0.0f, 0.0f, {"Top", "Center", "Bottom"}),
        field(AuthoringProperty::Style, PropertySection::Appearance,
              "style", "row_prop_style", "lbl_prop_style", "prop_style_combo",
              PropertyEditorKind::Enum),
        field(AuthoringProperty::Checked, PropertySection::Appearance,
              "checked", "row_prop_checked", "lbl_prop_checked", "prop_checked",
              PropertyEditorKind::Boolean, false, 0.0f, 0.0f, 0.0f,
              {"false", "true"}),
        field(AuthoringProperty::Password, PropertySection::Appearance,
              "password", "row_prop_password", "lbl_prop_password",
              "prop_password", PropertyEditorKind::Boolean, false, 0.0f,
              0.0f, 0.0f, {"false", "true"}),
        field(AuthoringProperty::ReadOnly, PropertySection::Appearance,
              "readOnly", "row_prop_readonly", "lbl_prop_readonly",
              "prop_readonly", PropertyEditorKind::Boolean, false, 0.0f,
              0.0f, 0.0f, {"false", "true"}),
        field(AuthoringProperty::Controller, PropertySection::Interaction,
              "controller", "row_prop_controller", "lbl_prop_controller",
              "prop_controller"),
        field(AuthoringProperty::OnClick, PropertySection::Interaction,
              "event:onClick", "row_prop_on_click", "lbl_prop_on_click",
              "prop_on_click"),
        field(AuthoringProperty::OnToggled, PropertySection::Interaction,
              "event:onToggled", "row_prop_on_toggled", "lbl_prop_on_toggled",
              "prop_on_toggled"),
        field(AuthoringProperty::OnValueChanged, PropertySection::Interaction,
              "event:onValueChanged", "row_prop_on_value_changed",
              "lbl_prop_on_value_changed", "prop_on_value_changed"),
        field(AuthoringProperty::OnTextChanged, PropertySection::Interaction,
              "event:onTextChanged", "row_prop_on_text_changed",
              "lbl_prop_on_text_changed", "prop_on_text_changed"),
        field(AuthoringProperty::OnSubmit, PropertySection::Interaction,
              "event:onSubmit", "row_prop_on_submit", "lbl_prop_on_submit",
              "prop_on_submit"),
        field(AuthoringProperty::OnSelectionChanged, PropertySection::Interaction,
              "event:onSelectionChanged", "row_prop_on_selection_changed",
              "lbl_prop_on_selection_changed", "prop_on_selection_changed"),
        field(AuthoringProperty::OnItemActivated, PropertySection::Interaction,
              "event:onItemActivated", "row_prop_on_item_activated",
              "lbl_prop_on_item_activated", "prop_on_item_activated"),
        field(AuthoringProperty::OnClose, PropertySection::Interaction,
              "event:onClose", "row_prop_on_close", "lbl_prop_on_close",
              "prop_on_close"),
        field(AuthoringProperty::Gravity, PropertySection::Layout,
              "gravity", "row_prop_gravity", "lbl_prop_gravity", "prop_gravity",
              PropertyEditorKind::Enum, false, 0.0f, 0.0f, 0.0f,
              {"TopLeft", "TopCenter", "TopRight", "CenterLeft", "Center",
               "CenterRight", "BottomLeft", "BottomCenter", "BottomRight"}),
        field(AuthoringProperty::Spacing, PropertySection::Layout,
              "spacing", "row_prop_spacing", "lbl_prop_spacing", "prop_spacing",
              PropertyEditorKind::Number, true, 0.0f, 1024.0f, 1.0f),
        field(AuthoringProperty::Padding, PropertySection::Layout,
              "padding", "row_prop_padding", "lbl_prop_pad", "prop_pad_l",
              PropertyEditorKind::Number, true, 0.0f, 4096.0f, 1.0f),
        field(AuthoringProperty::ValueMin, PropertySection::Content,
              "min", "row_prop_min", "lbl_prop_min", "prop_min",
              PropertyEditorKind::Number, false, 0.0f, 0.0f, 0.1f),
        field(AuthoringProperty::ValueMax, PropertySection::Content,
              "max", "row_prop_max", "lbl_prop_max", "prop_max",
              PropertyEditorKind::Number, false, 0.0f, 0.0f, 0.1f),
        field(AuthoringProperty::Value, PropertySection::Content,
              "value", "row_prop_value", "lbl_prop_value", "prop_value",
              PropertyEditorKind::Number, false, 0.0f, 0.0f, 0.1f),
        field(AuthoringProperty::ImageTint, PropertySection::Appearance,
              "imageTint", "row_prop_image_tint", "lbl_prop_image_tint",
              "prop_image_tint", PropertyEditorKind::Color),
        field(AuthoringProperty::ImageUvMinX, PropertySection::Content,
              "uvMinX", "row_prop_uv_min_x", "lbl_prop_uv_min_x",
              "prop_uv_min_x", PropertyEditorKind::Number, false, 0.0f, 0.0f, 0.01f),
        field(AuthoringProperty::ImageUvMinY, PropertySection::Content,
              "uvMinY", "row_prop_uv_min_y", "lbl_prop_uv_min_y",
              "prop_uv_min_y", PropertyEditorKind::Number, false, 0.0f, 0.0f, 0.01f),
        field(AuthoringProperty::ImageUvMaxX, PropertySection::Content,
              "uvMaxX", "row_prop_uv_max_x", "lbl_prop_uv_max_x",
              "prop_uv_max_x", PropertyEditorKind::Number, false, 0.0f, 0.0f, 0.01f),
        field(AuthoringProperty::ImageUvMaxY, PropertySection::Content,
              "uvMaxY", "row_prop_uv_max_y", "lbl_prop_uv_max_y",
              "prop_uv_max_y", PropertyEditorKind::Number, false, 0.0f, 0.0f, 0.01f),
        field(AuthoringProperty::SelectionMode, PropertySection::Interaction,
              "selectionMode", "row_prop_selection_mode", "lbl_prop_selection_mode",
              "prop_selection_mode", PropertyEditorKind::Enum, false, 0.0f,
              0.0f, 0.0f, {"Single", "Extended"}),
        field(AuthoringProperty::ItemHeight, PropertySection::Layout,
              "itemHeight", "row_prop_item_height", "lbl_prop_item_height",
              "prop_item_height", PropertyEditorKind::Number, true, 8.0f,
              512.0f, 1.0f),
        field(AuthoringProperty::TileWidth, PropertySection::Layout,
              "tileWidth", "row_prop_tile_width", "lbl_prop_tile_width",
              "prop_tile_width", PropertyEditorKind::Number, true, 16.0f,
              2048.0f, 1.0f),
        field(AuthoringProperty::TileHeight, PropertySection::Layout,
              "tileHeight", "row_prop_tile_height", "lbl_prop_tile_height",
              "prop_tile_height", PropertyEditorKind::Number, true, 16.0f,
              2048.0f, 1.0f),
        field(AuthoringProperty::TileSpacing, PropertySection::Layout,
              "tileSpacing", "row_prop_tile_spacing", "lbl_prop_tile_spacing",
              "prop_tile_spacing", PropertyEditorKind::Number, true, 0.0f,
              512.0f, 1.0f),
        field(AuthoringProperty::VerticalScrollBarVisibility, PropertySection::Layout,
              "verticalScrollBarVisibility", "row_prop_vscroll_visibility",
              "lbl_prop_vscroll_visibility", "prop_vscroll_visibility",
              PropertyEditorKind::Enum, false, 0.0f, 0.0f, 0.0f,
              {"Auto", "Always", "Hidden"}),
        field(AuthoringProperty::HorizontalScrollBarVisibility, PropertySection::Layout,
              "horizontalScrollBarVisibility", "row_prop_hscroll_visibility",
              "lbl_prop_hscroll_visibility", "prop_hscroll_visibility",
              PropertyEditorKind::Enum, false, 0.0f, 0.0f, 0.0f,
              {"Auto", "Always", "Hidden"}),
        field(AuthoringProperty::TabOverflowMode, PropertySection::Layout,
              "overflowMode", "row_prop_tab_overflow", "lbl_prop_tab_overflow",
              "prop_tab_overflow", PropertyEditorKind::Enum, false, 0.0f,
              0.0f, 0.0f, {"Scroll", "Compress", "Clip"}),
        field(AuthoringProperty::MinTabWidth, PropertySection::Layout,
              "minTabWidth", "row_prop_min_tab_width", "lbl_prop_min_tab_width",
              "prop_min_tab_width", PropertyEditorKind::Number, true, 24.0f,
              2048.0f, 1.0f),
        field(AuthoringProperty::GridRows, PropertySection::Layout,
              "gridRows", "row_prop_grid_rows", "lbl_prop_grid_rows",
              "prop_grid_rows", PropertyEditorKind::Integer, true, 1.0f,
              256.0f, 1.0f),
        field(AuthoringProperty::GridColumns, PropertySection::Layout,
              "gridColumns", "row_prop_grid_columns", "lbl_prop_grid_columns",
              "prop_grid_columns", PropertyEditorKind::Integer, true, 1.0f,
              256.0f, 1.0f),
        field(AuthoringProperty::GridSpacingX, PropertySection::Layout,
              "gridSpacingX", "row_prop_grid_spacing_x", "lbl_prop_grid_spacing_x",
              "prop_grid_spacing_x", PropertyEditorKind::Number, true, 0.0f,
              1024.0f, 1.0f),
        field(AuthoringProperty::GridSpacingY, PropertySection::Layout,
              "gridSpacingY", "row_prop_grid_spacing_y", "lbl_prop_grid_spacing_y",
              "prop_grid_spacing_y", PropertyEditorKind::Number, true, 0.0f,
              1024.0f, 1.0f),
        field(AuthoringProperty::RichTextWrapMode, PropertySection::Layout,
              "richWrapMode", "row_prop_rich_wrap", "lbl_prop_rich_wrap",
              "prop_rich_wrap", PropertyEditorKind::Enum, false, 0.0f,
              0.0f, 0.0f, {"NoWrap", "Word", "Character"}),
        field(AuthoringProperty::RichTextOverflow, PropertySection::Layout,
              "richOverflow", "row_prop_rich_overflow", "lbl_prop_rich_overflow",
              "prop_rich_overflow", PropertyEditorKind::Enum, false, 0.0f,
              0.0f, 0.0f, {"Clip", "Ellipsis"}),
        field(AuthoringProperty::LineHeight, PropertySection::Layout,
              "lineHeight", "row_prop_line_height", "lbl_prop_line_height",
              "prop_line_height", PropertyEditorKind::Number, true, 0.1f,
              16.0f, 0.1f),
        field(AuthoringProperty::MaxLines, PropertySection::Layout,
              "maxLines", "row_prop_max_lines", "lbl_prop_max_lines",
              "prop_max_lines", PropertyEditorKind::Integer, true, 0.0f,
              4096.0f, 1.0f),
    };
    return fields;
}

const PropertyFieldSchema& propertyFieldSchema(AuthoringProperty property) {
    for (const PropertyFieldSchema& field : allPropertyFieldSchemas()) {
        if (field.property == property) return field;
    }
    throw std::out_of_range("unknown Layout Editor property");
}

const PropertyFieldSchema* findPropertyFieldSchema(const std::string& key) {
    for (const PropertyFieldSchema& field : allPropertyFieldSchemas()) {
        if (key == field.key) return &field;
    }
    return nullptr;
}

PropertySchema::PropertySchema(
    std::initializer_list<AuthoringProperty> properties) {
    for (AuthoringProperty property : properties) add(property);
}

PropertySchema& PropertySchema::add(AuthoringProperty property) {
    _mask |= propertyBit(property);
    return *this;
}

PropertySchema& PropertySchema::add(const PropertySchema& other) {
    _mask |= other._mask;
    return *this;
}

bool PropertySchema::contains(AuthoringProperty property) const {
    return (_mask & propertyBit(property)) != 0;
}

bool PropertySchema::hasSection(PropertySection section) const {
    for (const PropertyFieldSchema& field : allPropertyFieldSchemas()) {
        if (field.section == section && contains(field.property)) return true;
    }
    return false;
}

std::vector<const PropertyFieldSchema*> PropertySchema::fields() const {
    std::vector<const PropertyFieldSchema*> result;
    for (const PropertyFieldSchema& field : allPropertyFieldSchemas()) {
        if (contains(field.property)) result.push_back(&field);
    }
    return result;
}

} // namespace ayt::ui
