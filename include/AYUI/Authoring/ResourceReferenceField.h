#pragma once
#include <AYUI/Box.h>
#include <functional>
#include <string>

namespace ayt::ui { class TextInput; class Button; class TextLabel; }
namespace ayt::ui::authoring {
struct ResourceReferenceResult { bool accepted = false; std::wstring message; };
struct ResourceReferenceOptions {
    std::wstring label, placeholder, loadCaption = L"Load";
    std::string inputId;
    float labelWidth = 64, loadWidth = 48;
};
/** @brief Reusable reference input, optional picker and owner-validated load request.
 * Paths remain opaque. Silent refresh never loads; cancelled picks never mutate.
 * Hosts own file dialogs, type/path validation, loading, persistence and history.
 * Destroy the control through the normal Widget tree.
 */
class ResourceReferenceField final : public HBox {
public:
    using Load = std::function<ResourceReferenceResult(const std::wstring&)>;
    explicit ResourceReferenceField(ResourceReferenceOptions options = {});
    void setPath(const std::wstring& path);
    void setOnLoad(Load callback);
    void setPicker(std::function<std::wstring()> picker);
    void setReadOnly(bool value);
    void setStatus(const ResourceReferenceResult& result);
    bool requestLoad();
    bool requestPick();
    TextInput* input() const noexcept { return _input; }
    Button* loadButton() const noexcept { return _load; }
    Button* pickButton() const noexcept { return _pick; }
    TextLabel* statusLabel() const noexcept { return _status; }
private:
    void refreshEnabled();
    TextInput* _input = nullptr;
    Button* _load = nullptr;
    Button* _pick = nullptr;
    TextLabel* _status = nullptr;
    Load _onLoad;
    std::function<std::wstring()> _picker;
    bool _readOnly = false, _requesting = false;
};
} // namespace ayt::ui::authoring
