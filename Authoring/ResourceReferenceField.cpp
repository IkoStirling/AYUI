#include <AYUI/Authoring/ResourceReferenceField.h>
#include <AYUI/TextInput.h>
#include <AYUI/TextLabel.h>
#include <AYUI/Button.h>

namespace ayt::ui::authoring {
ResourceReferenceField::ResourceReferenceField(ResourceReferenceOptions options) {
    setPadding(0, 0, 0, 0); setSpacing(4);
    if (!options.label.empty()) {
        auto* label = new TextLabel(); label->setText(options.label); label->setFontSize(11);
        label->setVerticalAlignment(TextLabel::VAlignment::Center); addWidget(label, options.labelWidth);
    }
    _input = new TextInput(); _input->setId(options.inputId);
    _input->setPlaceholder(options.placeholder);
    _input->setOnSubmit([this](const auto&) { requestLoad(); }); addWidget(_input, 0);
    _pick = new Button(); _pick->setText(L"Pick"); _pick->setPadding(7, 3, 7, 3);
    _pick->setOnClicked([this] { requestPick(); }); addWidget(_pick, 40);
    _load = new Button(); _load->setText(options.loadCaption); _load->setPadding(7, 3, 7, 3);
    _load->setOnClicked([this] { requestLoad(); }); addWidget(_load, options.loadWidth);
    _status = new TextLabel(); _status->setFontSize(10);
    _status->setVerticalAlignment(TextLabel::VAlignment::Center); _status->setVisible(false);
    addWidget(_status, 54); refreshEnabled();
}
void ResourceReferenceField::setPath(const std::wstring& path) {
    if (_input->getText() != path) _input->setText(path);
}
void ResourceReferenceField::setOnLoad(Load callback) { _onLoad = std::move(callback); refreshEnabled(); }
void ResourceReferenceField::setPicker(std::function<std::wstring()> picker) { _picker = std::move(picker); refreshEnabled(); }
void ResourceReferenceField::setReadOnly(bool value) { _readOnly = value; refreshEnabled(); }
void ResourceReferenceField::refreshEnabled() {
    _input->setReadOnly(_readOnly);
    _load->setEnabled(!_readOnly && static_cast<bool>(_onLoad));
    _pick->setVisible(static_cast<bool>(_picker)); _pick->setEnabled(!_readOnly && static_cast<bool>(_picker));
}
void ResourceReferenceField::setStatus(const ResourceReferenceResult& result) {
    // Full failure details remain available via the host's status/diagnostics UI.
    const std::wstring text = result.message.empty() ? L"" : result.accepted ? L"Ready" : L"Failed";
    if (_status->getText() != text) _status->setText(text);
    _status->setTextColor(result.accepted ? ayt::math::FVector4{0.42f, 0.78f, 0.50f, 1}
                                        : ayt::math::FVector4{0.95f, 0.42f, 0.35f, 1});
    _status->setVisible(!text.empty());
}
bool ResourceReferenceField::requestLoad() {
    if (_readOnly || _requesting || !_onLoad) return false;
    _requesting = true;
    ResourceReferenceResult result;
    try { result = _onLoad(_input->getText()); }
    catch (...) { _requesting = false; throw; }
    _requesting = false; setStatus(result); return result.accepted;
}
bool ResourceReferenceField::requestPick() {
    if (_readOnly || _requesting || !_picker) return false;
    _requesting = true;
    std::wstring path;
    try { path = _picker(); }
    catch (...) { _requesting = false; throw; }
    _requesting = false;
    if (path.empty()) return false;
    setPath(path); return requestLoad();
}
} // namespace ayt::ui::authoring
