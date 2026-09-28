#pragma once
#include "AYUI/Box.h"
#include <functional>
#include <string>
#include <vector>

namespace ayt::ui { class TextInput; class TextLabel; class ComboBox; }
namespace ayt::ui::authoring {
struct PropertyFieldOptions {
    std::string inputId, labelId, choiceId;
    float labelWidth = 102.0f;
    bool submitOnFocusLost = false;
    bool submitOnChoice = true;
};

/** @brief Owner-neutral text/enum property row for composing authoring forms.
 * Refresh is silent, unknown enum values are preserved, hidden/read-only fields
 * cannot submit. Validation and persistence/history belong to host callbacks.
 * Child pointers are borrowed until the normal Widget tree is destroyed.
 */
class PropertyField final : public HBox {
public:
    explicit PropertyField(PropertyFieldOptions options = {});
    void setTextValue(const std::wstring& label, const std::wstring& value,
                      bool readOnly = false);
    void setChoiceValue(const std::wstring& label, const std::wstring& value,
        const std::vector<std::wstring>& choices, bool allowEmpty = false,
        bool readOnly = false);
    std::wstring value() const;
    bool requestSubmit();
    void setValidator(std::function<bool(const std::wstring&, std::wstring&)> validator) {
        _validator = std::move(validator);
    }
    void setOnEdited(std::function<void()> callback) { _onEdited = std::move(callback); }
    void setOnSubmitted(std::function<void()> callback) { _onSubmitted = std::move(callback); }
    const std::wstring& validationError() const { return _error; }
    TextLabel* label() const { return _label; }
    TextInput* input() const { return _input; }
    ComboBox* choice() const { return _choice; }
private:
    void edited();
    TextLabel* _label = nullptr;
    TextInput* _input = nullptr;
    ComboBox* _choice = nullptr;
    bool _refreshing = false, _readOnly = false, _choiceMode = false, _submitting = false;
    std::wstring _error;
    std::function<bool(const std::wstring&, std::wstring&)> _validator;
    std::function<void()> _onEdited, _onSubmitted;
};
} // namespace ayt::ui::authoring
