#include "AYUI/LayoutEditor/PropertySchema.h"

#include <stdexcept>

namespace ayt::ui {
namespace {

constexpr std::uint64_t propertyBit(AuthoringProperty property) {
    return std::uint64_t{1} << static_cast<std::uint8_t>(property);
}

} // namespace

const std::vector<PropertyFieldSchema>& allPropertyFieldSchemas() {
    static const std::vector<PropertyFieldSchema> fields = {
        {AuthoringProperty::Id, PropertySection::Identity,
         "id", "row_prop_id", "lbl_prop_id", "prop_id"},
        {AuthoringProperty::X, PropertySection::Transform,
         "x", "row_prop_x", "lbl_prop_x", "prop_x"},
        {AuthoringProperty::Y, PropertySection::Transform,
         "y", "row_prop_y", "lbl_prop_y", "prop_y"},
        {AuthoringProperty::Width, PropertySection::Transform,
         "w", "row_prop_w", "lbl_prop_w", "prop_w"},
        {AuthoringProperty::Height, PropertySection::Transform,
         "h", "row_prop_h", "lbl_prop_h", "prop_h"},
        {AuthoringProperty::Text, PropertySection::Content,
         "text", "row_prop_text", "lbl_prop_text", "prop_text"},
        {AuthoringProperty::Texture, PropertySection::Content,
         "texture", "row_prop_texture", "lbl_prop_texture", "prop_texture"},
        {AuthoringProperty::Items, PropertySection::Content,
         "items", "row_prop_items", "lbl_prop_items", "prop_items"},
        {AuthoringProperty::TextHAlign, PropertySection::Content,
         "hAlign", "row_prop_text_halign", "lbl_prop_text_halign", "prop_text_halign"},
        {AuthoringProperty::TextVAlign, PropertySection::Content,
         "vAlign", "row_prop_text_valign", "lbl_prop_text_valign", "prop_text_valign"},
        {AuthoringProperty::Style, PropertySection::Appearance,
         "style", "row_prop_style", "lbl_prop_style", "prop_style_combo"},
        {AuthoringProperty::Checked, PropertySection::Appearance,
         "checked", "row_prop_checked", "lbl_prop_checked", "prop_checked"},
        {AuthoringProperty::Password, PropertySection::Appearance,
         "password", "row_prop_password", "lbl_prop_password", "prop_password"},
        {AuthoringProperty::ReadOnly, PropertySection::Appearance,
         "readOnly", "row_prop_readonly", "lbl_prop_readonly", "prop_readonly"},
        {AuthoringProperty::Controller, PropertySection::Interaction,
         "controller", "row_prop_controller", "lbl_prop_controller", "prop_controller"},
        {AuthoringProperty::OnClick, PropertySection::Interaction,
         "event:onClick", "row_prop_on_click", "lbl_prop_on_click", "prop_on_click"},
        {AuthoringProperty::OnToggled, PropertySection::Interaction,
         "event:onToggled", "row_prop_on_toggled", "lbl_prop_on_toggled", "prop_on_toggled"},
        {AuthoringProperty::OnValueChanged, PropertySection::Interaction,
         "event:onValueChanged", "row_prop_on_value_changed", "lbl_prop_on_value_changed", "prop_on_value_changed"},
        {AuthoringProperty::OnTextChanged, PropertySection::Interaction,
         "event:onTextChanged", "row_prop_on_text_changed", "lbl_prop_on_text_changed", "prop_on_text_changed"},
        {AuthoringProperty::OnSubmit, PropertySection::Interaction,
         "event:onSubmit", "row_prop_on_submit", "lbl_prop_on_submit", "prop_on_submit"},
        {AuthoringProperty::OnSelectionChanged, PropertySection::Interaction,
         "event:onSelectionChanged", "row_prop_on_selection_changed", "lbl_prop_on_selection_changed", "prop_on_selection_changed"},
        {AuthoringProperty::OnItemActivated, PropertySection::Interaction,
         "event:onItemActivated", "row_prop_on_item_activated", "lbl_prop_on_item_activated", "prop_on_item_activated"},
        {AuthoringProperty::OnClose, PropertySection::Interaction,
         "event:onClose", "row_prop_on_close", "lbl_prop_on_close", "prop_on_close"},
        {AuthoringProperty::Gravity, PropertySection::Layout,
         "gravity", "row_prop_gravity", "lbl_prop_gravity", "prop_gravity"},
        {AuthoringProperty::Spacing, PropertySection::Layout,
         "spacing", "row_prop_spacing", "lbl_prop_spacing", "prop_spacing"},
        {AuthoringProperty::Padding, PropertySection::Layout,
         "padding", "row_prop_padding", "lbl_prop_pad", "prop_pad_l"},
    };
    return fields;
}

const PropertyFieldSchema& propertyFieldSchema(AuthoringProperty property) {
    for (const PropertyFieldSchema& field : allPropertyFieldSchemas()) {
        if (field.property == property) return field;
    }
    throw std::out_of_range("unknown Layout Editor property");
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
