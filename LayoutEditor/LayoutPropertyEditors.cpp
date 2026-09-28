#include "AYUI/LayoutEditor/LayoutPropertyEditors.h"
#include <AYUI/Authoring/NumericFields.h>
#include <AYUI/UnicodeText.h>

#include "AYUI/Button.h"
#include "AYUI/ColorPicker.h"
#include "AYUI/TextInput.h"
#include "AYUI/UIManager.h"

#include <algorithm>

namespace ayt::ui {

LayoutColorPropertyEditor::LayoutColorPropertyEditor(
    const std::string& valueControlId) {
    setPadding(0.0f, 0.0f, 0.0f, 0.0f);
    setSpacing(5.0f);

    _valueInput = new TextInput();
    _valueInput->setId(valueControlId);
    _valueInput->setStyleId("__le_input");
    _valueInput->setSize({0.0f, 30.0f});
    addWidget(_valueInput, 0.0f);

    _swatchButton = new Button();
    _swatchButton->setId(valueControlId + "_swatch");
    _swatchButton->setText(L"");
    _swatchButton->setSize({30.0f, 30.0f});
    _swatchButton->setOnClicked([this]() { openPicker(); });
    addWidget(_swatchButton, 30.0f);
    setColor(_color);
}

void LayoutColorPropertyEditor::setColor(const math::FVector4& color) {
    _color.x = std::clamp(color.x, 0.0f, 1.0f);
    _color.y = std::clamp(color.y, 0.0f, 1.0f);
    _color.z = std::clamp(color.z, 0.0f, 1.0f);
    _color.w = std::clamp(color.w, 0.0f, 1.0f);
    if (_valueInput != nullptr) {
        _valueInput->setText(ColorPicker::formatHexCode(_color));
    }
    if (_swatchButton != nullptr) {
        _swatchButton->setColor(_color);
        _swatchButton->setFallbackHoverColor(math::FVector4(
            std::min(1.0f, _color.x + 0.08f),
            std::min(1.0f, _color.y + 0.08f),
            std::min(1.0f, _color.z + 0.08f), _color.w));
    }
}

void LayoutColorPropertyEditor::openPicker() {
    if (_manager == nullptr || _swatchButton == nullptr) return;
    auto* picker = new ColorPicker();
    picker->setId(getId() + "_popup");
    picker->setSize({292.0f, 310.0f});
    const math::FRectangle anchor = _swatchButton->getWorldBounds();
    picker->setPosition({anchor.maxX - 292.0f, anchor.maxY + 4.0f});
    picker->setColor(_color, false);
    picker->setOnColorInteractionStarted([this]() {
        if (_onInteractionStarted) _onInteractionStarted();
    });
    picker->setOnColorChanged([this](const math::FVector4& value) {
        setColor(value);
        if (_onColorChanged) _onColorChanged(_color);
    });
    picker->setOnColorCommitted([this](const math::FVector4& value) {
        setColor(value);
        if (_onColorCommitted) _onColorCommitted(_color);
    });
    _manager->openPopup(_swatchButton, picker);
}

LayoutResourcePropertyEditor::LayoutResourcePropertyEditor(
    const std::string& valueControlId) {
    setPadding(0.0f, 0.0f, 0.0f, 0.0f);
    setSpacing(4.0f);

    _valueInput = new TextInput();
    _valueInput->setId(valueControlId);
    _valueInput->setStyleId("__le_input");
    _valueInput->setSize({0.0f, 30.0f});
    addWidget(_valueInput, 0.0f);

    _browseButton = new Button();
    _browseButton->setId("btn_pick_texture");
    _browseButton->setStyleId("__le_command");
    _browseButton->setText(L"…");
    _browseButton->setSize({30.0f, 30.0f});
    addWidget(_browseButton, 30.0f);

    _clearButton = new Button();
    _clearButton->setId("btn_clear_texture");
    _clearButton->setStyleId("__le_command");
    _clearButton->setText(L"×");
    _clearButton->setSize({30.0f, 30.0f});
    addWidget(_clearButton, 30.0f);
}

LayoutVectorPropertyEditor::LayoutVectorPropertyEditor(
    const std::vector<std::string>& componentControlIds,
    const std::vector<std::string>& componentLabels) {
    setPadding(0.0f, 0.0f, 0.0f, 0.0f);
    setSpacing(6.0f);
    std::vector<std::wstring> labels;
    for (const auto& label : componentLabels) labels.push_back(decodeUtf8Text(label));
    _inputs = authoring::addNumericInputs(*this, componentControlIds, labels,
                                         {"__le_input", 30.0f, false});
}

} // namespace ayt::ui
