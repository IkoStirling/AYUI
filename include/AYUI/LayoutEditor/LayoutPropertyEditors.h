#pragma once

#include "AYUI/Box.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace ayt::ui {

class Button;
class TextInput;
class UIManager;

// Compact authoring-only field controls used by schema-generated Inspector
// rows. They deliberately expose ordinary AYUI children so legacy chrome and
// LayoutEditorSession bindings can continue to address the value controls by
// stable ID while the richer presentation stays encapsulated.
class LayoutColorPropertyEditor final : public HBox {
public:
    explicit LayoutColorPropertyEditor(const std::string& valueControlId);

    TextInput* valueInput() const { return _valueInput; }
    Button* swatchButton() const { return _swatchButton; }

    void setColor(const math::FVector4& color);
    const math::FVector4& color() const { return _color; }
    void setManager(UIManager* manager) { _manager = manager; }
    void setOnInteractionStarted(std::function<void()> callback) {
        _onInteractionStarted = std::move(callback);
    }
    void setOnColorChanged(
        std::function<void(const math::FVector4&)> callback) {
        _onColorChanged = std::move(callback);
    }
    void setOnColorCommitted(
        std::function<void(const math::FVector4&)> callback) {
        _onColorCommitted = std::move(callback);
    }

    void openPicker();

private:
    math::FVector4 _color{1.0f, 1.0f, 1.0f, 1.0f};
    UIManager* _manager = nullptr;
    TextInput* _valueInput = nullptr;
    Button* _swatchButton = nullptr;
    std::function<void()> _onInteractionStarted;
    std::function<void(const math::FVector4&)> _onColorChanged;
    std::function<void(const math::FVector4&)> _onColorCommitted;
};

class LayoutResourcePropertyEditor final : public HBox {
public:
    explicit LayoutResourcePropertyEditor(const std::string& valueControlId);

    TextInput* valueInput() const { return _valueInput; }
    Button* browseButton() const { return _browseButton; }
    Button* clearButton() const { return _clearButton; }

private:
    TextInput* _valueInput = nullptr;
    Button* _browseButton = nullptr;
    Button* _clearButton = nullptr;
};

class LayoutVectorPropertyEditor final : public HBox {
public:
    LayoutVectorPropertyEditor(
        const std::vector<std::string>& componentControlIds,
        const std::vector<std::string>& componentLabels);

    const std::vector<TextInput*>& inputs() const { return _inputs; }

private:
    std::vector<TextInput*> _inputs;
};

} // namespace ayt::ui
