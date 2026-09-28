#pragma once
#include <AYUI/Box.h>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ayt::ui { class TextArea; class ComboBox; class Button; }
namespace ayt::ui::authoring {
enum class DiagnosticSeverity { Info, Warning, Error };
struct DiagnosticEntry {
    DiagnosticSeverity severity = DiagnosticSeverity::Info;
    std::wstring code, message;
    std::string target;
    bool operator==(const DiagnosticEntry& rhs) const {
        return severity == rhs.severity && code == rhs.code && message == rhs.message && target == rhs.target;
    }
};
/** @brief Resource-neutral read-only diagnostics with filtering, expansion and location requests.
 * Hosts generate severity/code/message and validate opaque targets before locating them.
 * Refresh never locates or mutates resources. Plain reports keep their original text.
 * Destroy through the normal Widget tree; no validation or bake work runs here.
 */
class DiagnosticsPanel final : public VBox {
public:
    explicit DiagnosticsPanel(std::size_t visibleLimit = 6);
    void setEntries(std::vector<DiagnosticEntry> entries, std::wstring heading = {});
    void setReport(const std::wstring& text);
    const std::wstring& reportText() const;
    void setFilter(std::optional<DiagnosticSeverity> filter);
    void setExpanded(bool expanded);
    void setOnLocate(std::function<void(const std::string&)> callback);
    bool locateVisible(std::size_t index);
    ComboBox* filterControl() const noexcept { return _filterControl; }
    ComboBox* locationControl() const noexcept { return _locator; }
    Button* expandButton() const noexcept { return _expand; }
private:
    void refresh();
    std::vector<DiagnosticEntry> _entries;
    std::vector<std::size_t> _visible, _locatable;
    std::wstring _heading;
    std::optional<DiagnosticSeverity> _filter;
    std::function<void(const std::string&)> _onLocate;
    HBox* _toolbar = nullptr;
    TextArea* _text = nullptr;
    ComboBox* _filterControl = nullptr;
    ComboBox* _locator = nullptr;
    Button* _expand = nullptr;
    std::size_t _limit;
    bool _expanded = false, _refreshing = false, _plain = true;
};
} // namespace ayt::ui::authoring
