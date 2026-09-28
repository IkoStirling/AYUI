#include <AYUI/Authoring/DiagnosticsPanel.h>
#include <AYUI/TextArea.h>
#include <AYUI/ComboBox.h>
#include <AYUI/Button.h>
#include <algorithm>
#include <sstream>

namespace ayt::ui::authoring {
DiagnosticsPanel::DiagnosticsPanel(std::size_t visibleLimit) : _limit(std::max<std::size_t>(1, visibleLimit)) {
    setPadding(0, 0, 0, 0); setSpacing(2);
    _toolbar = new HBox(); _toolbar->setPadding(0, 0, 0, 0); _toolbar->setSpacing(4);
    _filterControl = new ComboBox(); _filterControl->setItems({L"All", L"Errors", L"Warnings", L"Info"});
    _filterControl->setSelectedIndex(0);
    _filterControl->setOnSelectionChanged([this](int index) {
        if (_refreshing || index < 0 || index > 3) return;
        setFilter(index == 0 ? std::optional<DiagnosticSeverity>{} : index == 1 ? DiagnosticSeverity::Error
            : index == 2 ? DiagnosticSeverity::Warning : DiagnosticSeverity::Info);
    });
    _toolbar->addWidget(_filterControl, 84);
    _expand = new Button(); _expand->setOnClicked([this] { setExpanded(!_expanded); });
    _toolbar->addWidget(_expand, 100);
    _locator = new ComboBox(); _locator->setOnSelectionChanged([this](int index) {
        if (_refreshing || !_onLocate || index < 0 || static_cast<std::size_t>(index) >= _locatable.size()) return;
        const auto target = _entries[_locatable[static_cast<std::size_t>(index)]].target;
        _onLocate(target);
        // Location is an action, not a persistent selection; allow repeating it.
        _refreshing = true; _locator->setSelectedIndex(-1); _refreshing = false;
    });
    _toolbar->addWidget(_locator, 0); _toolbar->setVisible(false); addWidget(_toolbar, 24);
    _text = new TextArea(); _text->setReadOnly(true); _text->setWordWrap(false); addWidget(_text, 0);
}
void DiagnosticsPanel::setEntries(std::vector<DiagnosticEntry> entries, std::wstring heading) {
    if (!_plain && _entries == entries && _heading == heading) return;
    _plain = false; _entries = std::move(entries); _heading = std::move(heading); refresh();
}
void DiagnosticsPanel::setReport(const std::wstring& text) {
    _plain = true; _entries.clear(); _visible.clear(); _locatable.clear(); _toolbar->setVisible(false);
    if (_text->getText() != text) _text->setText(text);
}
const std::wstring& DiagnosticsPanel::reportText() const { return _text->getText(); }
void DiagnosticsPanel::setFilter(std::optional<DiagnosticSeverity> filter) { _filter = filter; if (!_plain) refresh(); }
void DiagnosticsPanel::setExpanded(bool expanded) { _expanded = expanded; if (!_plain) refresh(); }
void DiagnosticsPanel::setOnLocate(std::function<void(const std::string&)> callback) {
    _onLocate = std::move(callback); if (!_plain) refresh();
}
bool DiagnosticsPanel::locateVisible(std::size_t index) {
    if (!_onLocate || index >= _visible.size()) return false;
    const auto target = _entries[_visible[index]].target;
    if (target.empty()) return false;
    _onLocate(target); return true;
}
void DiagnosticsPanel::refresh() {
    _refreshing = true; _toolbar->setVisible(true);
    _filterControl->setSelectedIndex(!_filter ? 0 : *_filter == DiagnosticSeverity::Error ? 1
        : *_filter == DiagnosticSeverity::Warning ? 2 : 3);
    _visible.clear(); _locatable.clear();
    std::wostringstream text;
    if (!_heading.empty()) text << _heading << L"\n";
    std::size_t matches = 0;
    std::vector<std::wstring> locations;
    for (std::size_t i = 0; i < _entries.size(); ++i) {
        const auto& entry = _entries[i];
        if (_filter && *_filter != entry.severity) continue;
        ++matches;
        if (!_expanded && _visible.size() >= _limit) continue;
        _visible.push_back(i);
        text << (entry.severity == DiagnosticSeverity::Error ? L"ERROR "
            : entry.severity == DiagnosticSeverity::Warning ? L"WARN  " : L"INFO  ");
        if (!entry.code.empty()) text << L"[" << entry.code << L"] ";
        text << entry.message << L"\n";
        if (!entry.target.empty()) {
            _locatable.push_back(i); locations.push_back(L"Locate: " + (entry.code.empty() ? entry.message : entry.code));
        }
    }
    const auto remaining = matches - _visible.size();
    if (remaining) text << L"... " << remaining << L" more issue(s)";
    const auto value = text.str();
    if (_text->getText() != value) _text->setText(value);
    const auto expandText = _expanded ? L"Collapse" : L"Show all";
    if (_expand->getText() != expandText) _expand->setText(expandText);
    _expand->setVisible(matches > _limit);
    _locator->setItems(locations); _locator->setSelectedIndex(-1);
    _locator->setVisible(_onLocate && !locations.empty());
    _refreshing = false;
}
} // namespace ayt::ui::authoring
