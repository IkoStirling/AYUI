#pragma once
#include "AYUI/Box.h"
#include <functional>
#include <string>
#include <vector>

namespace ayt::ui { class TextInput; class TextLabel; }
namespace ayt::ui::authoring {
struct NumericInputOptions {
    std::string styleId;
    float height = 28.0f;
    bool numericScrub = true;
};
// Low-level shared construction for existing property rows, preserving their ABI.
std::vector<TextInput*> addNumericInputs(HBox& row, const std::vector<std::string>& ids,
    const std::vector<std::wstring>& labels, NumericInputOptions options = {});

/** @brief Reusable component-value/tangent fields, independent of resource types.
 * Hosts own validation/history. readValues rejects incomplete/nonfinite input atomically.
 * setValues is a silent refresh; the unit label is presentation-only, not a conversion.
 * Child IDs remain supplied by the caller. Destroy through the normal Widget tree.
 */
class NumericFields final : public HBox {
public:
    NumericFields(const std::vector<std::string>& ids, const std::vector<std::wstring>& labels = {});
    bool setValues(const std::vector<float>& values, bool readOnly = false);
    bool readValues(std::size_t count, std::vector<float>& output) const;
    void setUnit(const std::wstring& unit);
    void setOnSubmitted(std::function<void()> callback) { _onSubmitted = std::move(callback); }
    const std::vector<TextInput*>& inputs() const noexcept { return _inputs; }
    std::size_t componentCount() const noexcept { return _width; }
private:
    std::vector<TextInput*> _inputs;
    TextLabel* _unit = nullptr;
    std::function<void()> _onSubmitted;
    std::size_t _width = 0u;
    bool _readOnly = false, _refreshing = false;
};
} // namespace ayt::ui::authoring
