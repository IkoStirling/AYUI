#include "AYUI/Authoring/NumericFields.h"
#include "AYUI/Authoring/AuthoringPrimitives.h"
#include <AYUI/TextInput.h>
#include <AYUI/TextLabel.h>

namespace ayt::ui::authoring {
std::vector<TextInput*> addNumericInputs(HBox& row, const std::vector<std::string>& ids,
    const std::vector<std::wstring>& labels, NumericInputOptions options) {
    std::vector<TextInput*> inputs;
    inputs.reserve(ids.size());
    for (std::size_t i = 0; i < ids.size(); ++i) {
        auto* input = new TextInput();
        input->setId(ids[i]);
        if (!options.styleId.empty()) input->setStyleId(options.styleId);
        if (i < labels.size()) input->setPlaceholder(labels[i]);
        input->setSize({0, options.height});
        input->setNumericScrubEnabled(options.numericScrub);
        row.addWidget(input, 0);
        inputs.push_back(input);
    }
    return inputs;
}
NumericFields::NumericFields(const std::vector<std::string>& ids, const std::vector<std::wstring>& labels) {
    setPadding(0, 0, 0, 0);
    setSpacing(4);
    _inputs = addNumericInputs(*this, ids, labels);
    _width = _inputs.size();
    for (auto* input : _inputs) input->setOnSubmit([this](const std::wstring&) {
        if (!_refreshing && !_readOnly && _onSubmitted) _onSubmitted();
    });
    _unit = new TextLabel();
    _unit->setFontSize(11);
    _unit->setVerticalAlignment(TextLabel::VAlignment::Center);
    _unit->setVisible(false);
    addWidget(_unit, 68);
}
bool NumericFields::setValues(const std::vector<float>& values, bool readOnly) {
    if (values.size() > _inputs.size()
        || std::any_of(values.begin(), values.end(), [](float value) { return !std::isfinite(value); })) return false;
    _refreshing = true;
    _width = values.size();
    _readOnly = readOnly;
    for (std::size_t i = 0; i < _inputs.size(); ++i) {
        auto* input = _inputs[i];
        const auto text = i < _width ? formatNumber(values[i]) : std::wstring{};
        input->setVisible(i < _width);
        if (input->isReadOnly() != readOnly) input->setReadOnly(readOnly);
        if (input->getText() != text) input->setText(text);
    }
    _refreshing = false;
    return true;
}
bool NumericFields::readValues(std::size_t count, std::vector<float>& output) const {
    if (_readOnly || count == 0u || count > _width) return false;
    std::vector<float> values;
    values.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto value = parseFiniteFloat(_inputs[i]->getText());
        if (!value) return false;
        values.push_back(*value);
    }
    output = std::move(values);
    return true;
}
void NumericFields::setUnit(const std::wstring& unit) {
    if (_unit->getText() != unit) _unit->setText(unit);
    _unit->setVisible(!unit.empty());
}
} // namespace ayt::ui::authoring
