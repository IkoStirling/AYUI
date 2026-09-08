#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace ayt::ui {

class LayoutResourceCatalog;
class StyleSheet;
class Widget;

enum class LayoutDiagnosticSeverity { Info, Warning, Error };

enum class LayoutDiagnosticCode {
    EmptyId,
    InvalidId,
    DuplicateId,
    NonFiniteGeometry,
    NonPositiveSize,
    MissingStyle,
    MissingTexture,
    EventWithoutController,
    MissingAccessibleName,
    InvalidAnchors,
    OutsideParent,
    GridSlotOverlap,
};

struct LayoutDiagnostic {
    LayoutDiagnosticSeverity severity = LayoutDiagnosticSeverity::Info;
    LayoutDiagnosticCode code = LayoutDiagnosticCode::EmptyId;
    Widget* widget = nullptr;
    std::string widgetId;
    std::wstring message;
};

struct LayoutValidationContext {
    const LayoutResourceCatalog* textureCatalog = nullptr;
    const StyleSheet* styleSheet = nullptr;
    bool reportAnonymousWidgets = true;
    bool reportOutsideParent = true;
};

class LayoutValidationModel {
public:
    void run(const std::vector<Widget*>& authoredWidgets,
             const LayoutValidationContext& context = {});

    const std::vector<LayoutDiagnostic>& diagnostics() const {
        return _diagnostics;
    }
    size_t errorCount() const { return _errorCount; }
    size_t warningCount() const { return _warningCount; }
    bool hasErrors() const { return _errorCount != 0; }
    std::vector<std::wstring> displayLabels() const;

private:
    void add(LayoutDiagnosticSeverity severity, LayoutDiagnosticCode code,
             Widget* widget, std::wstring message);

    std::vector<LayoutDiagnostic> _diagnostics;
    size_t _errorCount = 0;
    size_t _warningCount = 0;
};

} // namespace ayt::ui
