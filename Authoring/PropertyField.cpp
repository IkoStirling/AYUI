#include "AYUI/Authoring/PropertyField.h"
#include "AYUI/TextLabel.h"
#include "AYUI/TextInput.h"
#include "AYUI/ComboBox.h"
#include <algorithm>

namespace ayt::ui::authoring {
PropertyField::PropertyField(PropertyFieldOptions options) {
    setSpacing(4.0f);
    setPadding(0, 0, 0, 0);
    _label = new TextLabel();
    _label->setId(options.labelId);
    _label->setFontSize(11);
    _label->setTextColor({0.67f, 0.71f, 0.78f, 1.0f});
    _label->setVerticalAlignment(TextLabel::VAlignment::Center);
    _input = new TextInput();
    _input->setId(options.inputId);
    _choice = new ComboBox();
    _choice->setId(options.choiceId);
    _choice->setVisible(false);
    _input->setOnTextChanged([this](const std::wstring&) { edited(); });
    _input->setOnSubmit([this](const std::wstring&) { (void)requestSubmit(); });
    if (options.submitOnFocusLost) {
        _input->setOnFocusLostNotify([this] { (void)requestSubmit(); });
    }
    _choice->setOnSelectionChanged([this, submit = options.submitOnChoice](int) {
        if (_refreshing || _readOnly || !isVisible()) return;
        edited();
        if (submit) (void)requestSubmit();
    });
    addWidget(_label, options.labelWidth);
    addWidget(_input, 0);
    addWidget(_choice, 0);
}
void PropertyField::edited() {
    if (_refreshing || _readOnly || !isVisible()) return;
    _error.clear();
    if (_onEdited) _onEdited();
}
void PropertyField::setTextValue(const std::wstring& label, const std::wstring& value,
                               bool readOnly) {
    _refreshing = true;
    setVisible(!label.empty());
    if (label.empty()) { _refreshing = false; return; }
    _readOnly = readOnly;
    _choiceMode = false;
    _error.clear();
    setVisible(!label.empty());
    _label->setText(label);
    _input->setVisible(true);
    _input->setReadOnly(readOnly);
    _choice->setVisible(false);
    _input->setText(value);
    _refreshing = false;
}
void PropertyField::setChoiceValue(const std::wstring& label, const std::wstring& value,
    const std::vector<std::wstring>& choices, bool allowEmpty, bool readOnly) {
    _refreshing = true;
    setVisible(!label.empty());
    if (label.empty()) { _refreshing = false; return; }
    _readOnly = readOnly;
    _choiceMode = true;
    _error.clear();
    setVisible(!label.empty());
    _label->setText(label);
    _input->setVisible(false);
    _choice->setVisible(true);
    _choice->setEnabled(!readOnly);
    std::vector<std::wstring> items;
    if (allowEmpty) items.emplace_back();
    for (const auto& item : choices) {
        if (std::find(items.begin(), items.end(), item) == items.end()) items.push_back(item);
    }
    if (!value.empty() && std::find(items.begin(), items.end(), value) == items.end()) {
        items.push_back(value);
    }
    _choice->setItems(items);
    const auto found = std::find(items.begin(), items.end(), value);
    _choice->setSelectedIndex(found == items.end() ? (items.empty() ? -1 : 0)
        : static_cast<int>(std::distance(items.begin(), found)));
    _refreshing = false;
}
std::wstring PropertyField::value() const {
    return _choiceMode ? _choice->getSelectedItem() : _input->getText();
}
bool PropertyField::requestSubmit() {
    if (_refreshing || _readOnly || _submitting || !isVisible() || !_onSubmitted) return false;
    _submitting = true;
    struct Reset { bool& flag; ~Reset() { flag = false; } } reset{_submitting};
    _error.clear();
    if (_validator && !_validator(value(), _error)) return false;
    _onSubmitted();
    return true;
}
} // namespace ayt::ui::authoring
