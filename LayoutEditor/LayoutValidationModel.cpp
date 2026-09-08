#include "AYUI/LayoutEditor/LayoutValidationModel.h"

#include "AYUI/Box.h"
#include "AYUI/Button.h"
#include "AYUI/CheckBox.h"
#include "AYUI/GridPanel.h"
#include "AYUI/Image.h"
#include "AYUI/InteractiveWidget.h"
#include "AYUI/LayoutEditor/LayoutResourceCatalog.h"
#include "AYUI/RadioButton.h"
#include "AYUI/Style.h"
#include "AYUI/Widget.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace ayt::ui {
namespace {

bool validIdSyntax(const std::string& id) {
    if (id.empty() || id.rfind("__le", 0) == 0) return false;
    const auto first = static_cast<unsigned char>(id.front());
    if (!(std::isalpha(first) || first == '_')) return false;
    return std::all_of(id.begin(), id.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '_' || ch == '-' || ch == '.' ||
               ch == ':';
    });
}

bool hasInferredAccessibleName(const Widget* widget) {
    if (widget == nullptr) return false;
    if (!widget->getAccessibilityLabel().empty()) return true;
    if (const auto* button = dynamic_cast<const Button*>(widget)) {
        return !button->getText().empty();
    }
    if (const auto* check = dynamic_cast<const CheckBox*>(widget)) {
        return !check->getText().empty();
    }
    if (const auto* radio = dynamic_cast<const RadioButton*>(widget)) {
        return !radio->getText().empty();
    }
    return false;
}

std::wstring widgetName(const Widget* widget) {
    if (widget == nullptr || widget->getId().empty()) return L"(anonymous)";
    return std::wstring(widget->getId().begin(), widget->getId().end());
}

bool overlaps(int aRow, int aCol, const GridPanel::CellInfo& a,
              int bRow, int bCol, const GridPanel::CellInfo& b) {
    return aRow < bRow + b.rowSpan && bRow < aRow + a.rowSpan &&
           aCol < bCol + b.colSpan && bCol < aCol + a.colSpan;
}

} // namespace

void LayoutValidationModel::add(LayoutDiagnosticSeverity severity,
                                LayoutDiagnosticCode code, Widget* widget,
                                std::wstring message) {
    LayoutDiagnostic diagnostic;
    diagnostic.severity = severity;
    diagnostic.code = code;
    diagnostic.widget = widget;
    if (widget != nullptr) diagnostic.widgetId = widget->getId();
    diagnostic.message = std::move(message);
    _diagnostics.push_back(std::move(diagnostic));
    if (severity == LayoutDiagnosticSeverity::Error) ++_errorCount;
    if (severity == LayoutDiagnosticSeverity::Warning) ++_warningCount;
}

void LayoutValidationModel::run(
    const std::vector<Widget*>& authoredWidgets,
    const LayoutValidationContext& context) {
    _diagnostics.clear();
    _errorCount = 0;
    _warningCount = 0;

    std::unordered_map<std::string, size_t> idCounts;
    std::unordered_set<Widget*> authored;
    for (Widget* widget : authoredWidgets) {
        if (widget == nullptr) continue;
        authored.insert(widget);
        if (!widget->getId().empty()) ++idCounts[widget->getId()];
    }

    for (Widget* widget : authoredWidgets) {
        if (widget == nullptr) continue;
        const std::wstring name = widgetName(widget);
        const std::string& id = widget->getId();
        if (id.empty()) {
            if (context.reportAnonymousWidgets) {
                add(LayoutDiagnosticSeverity::Warning,
                    LayoutDiagnosticCode::EmptyId, widget,
                    L"Anonymous widget cannot be referenced by controllers");
            }
        } else if (!validIdSyntax(id)) {
            add(LayoutDiagnosticSeverity::Error,
                LayoutDiagnosticCode::InvalidId, widget,
                L"Invalid or editor-reserved ID: " + name);
        } else if (idCounts[id] > 1u) {
            add(LayoutDiagnosticSeverity::Error,
                LayoutDiagnosticCode::DuplicateId, widget,
                L"Duplicate widget ID: " + name);
        }

        const math::FVector2& position = widget->getPosition();
        const math::FVector2& size = widget->getSize();
        if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
            !std::isfinite(size.x) || !std::isfinite(size.y)) {
            add(LayoutDiagnosticSeverity::Error,
                LayoutDiagnosticCode::NonFiniteGeometry, widget,
                name + L" has non-finite geometry");
        } else if (size.x <= 0.0f || size.y <= 0.0f) {
            add(LayoutDiagnosticSeverity::Error,
                LayoutDiagnosticCode::NonPositiveSize, widget,
                name + L" has a non-positive size");
        }

        const std::string& styleId = widget->getStyleId();
        if (!styleId.empty() && context.styleSheet != nullptr &&
            !context.styleSheet->containsStyle(styleId)) {
            add(LayoutDiagnosticSeverity::Warning,
                LayoutDiagnosticCode::MissingStyle, widget,
                name + L" references missing style: " +
                    std::wstring(styleId.begin(), styleId.end()));
        }

        if (const auto* image = dynamic_cast<const Image*>(widget)) {
            const std::string& texture = image->getTextureName();
            if (!texture.empty() && context.textureCatalog != nullptr &&
                !context.textureCatalog->contains(texture)) {
                add(LayoutDiagnosticSeverity::Warning,
                    LayoutDiagnosticCode::MissingTexture, widget,
                    name + L" references a missing texture resource");
            }
        }

        if (!widget->getEventBindings().empty() &&
            widget->getControllerId().empty()) {
            add(LayoutDiagnosticSeverity::Warning,
                LayoutDiagnosticCode::EventWithoutController, widget,
                name + L" binds events without a controller ID");
        }

        if (dynamic_cast<InteractiveWidget*>(widget) != nullptr &&
            !widget->isAccessibilityHidden() &&
            !hasInferredAccessibleName(widget)) {
            add(LayoutDiagnosticSeverity::Warning,
                LayoutDiagnosticCode::MissingAccessibleName, widget,
                name + L" has no accessible name");
        }

        if (widget->hasAnchorLayout()) {
            const AnchorLayout& anchor = widget->getAnchorLayout();
            if (anchor.anchorMin.x > anchor.anchorMax.x ||
                anchor.anchorMin.y > anchor.anchorMax.y ||
                anchor.anchorMin.x < 0.0f || anchor.anchorMin.y < 0.0f ||
                anchor.anchorMax.x > 1.0f || anchor.anchorMax.y > 1.0f) {
                add(LayoutDiagnosticSeverity::Error,
                    LayoutDiagnosticCode::InvalidAnchors, widget,
                    name + L" has invalid anchor bounds");
            }
        }

        const auto& responsive = widget->getResponsiveLayoutRules();
        for (size_t index = 0; index < responsive.size(); ++index) {
            const ResponsiveLayoutRule& rule = responsive[index];
            if (rule.maxParentWidth > 0.0f &&
                rule.maxParentWidth <= rule.minParentWidth) {
                add(LayoutDiagnosticSeverity::Error,
                    LayoutDiagnosticCode::InvalidResponsiveRange, widget,
                    name + L" has an invalid responsive width range");
            }
            if (rule.overrideAnchors && !widget->hasAnchorLayout()) {
                add(LayoutDiagnosticSeverity::Warning,
                    LayoutDiagnosticCode::ResponsiveAnchorWithoutBase, widget,
                    name + L" overrides responsive anchors without a base anchor");
            }
            const float maxWidth = rule.maxParentWidth <= 0.0f
                ? std::numeric_limits<float>::max() : rule.maxParentWidth;
            for (size_t other = index + 1; other < responsive.size(); ++other) {
                const ResponsiveLayoutRule& candidate = responsive[other];
                const float candidateMax = candidate.maxParentWidth <= 0.0f
                    ? std::numeric_limits<float>::max()
                    : candidate.maxParentWidth;
                if (rule.minParentWidth < candidateMax &&
                    candidate.minParentWidth < maxWidth) {
                    add(LayoutDiagnosticSeverity::Warning,
                        LayoutDiagnosticCode::OverlappingResponsiveRules,
                        widget,
                        name + L" has overlapping responsive width rules");
                }
            }
        }

        Widget* parent = widget->getParent();
        if (context.reportOutsideParent && parent != nullptr &&
            authored.find(parent) != authored.end() &&
            !widget->isLayoutPositionManaged()) {
            const math::FVector2& parentSize = parent->getSize();
            if (position.x < 0.0f || position.y < 0.0f ||
                position.x + size.x > parentSize.x ||
                position.y + size.y > parentSize.y) {
                add(LayoutDiagnosticSeverity::Warning,
                    LayoutDiagnosticCode::OutsideParent, widget,
                    name + L" extends outside its parent bounds");
            }
        }

        auto* grid = dynamic_cast<GridPanel*>(widget);
        if (grid == nullptr) continue;
        struct Occupied {
            int row = 0;
            int col = 0;
            const GridPanel::CellInfo* cell = nullptr;
        };
        std::vector<Occupied> cells;
        for (int row = 0; row < grid->getRowCount(); ++row) {
            for (int col = 0; col < grid->getColumnCount(); ++col) {
                const GridPanel::CellInfo* cell = grid->findCell(row, col);
                if (cell != nullptr && cell->widget != nullptr) {
                    cells.push_back({row, col, cell});
                }
            }
        }
        bool reported = false;
        for (size_t a = 0; a < cells.size() && !reported; ++a) {
            for (size_t b = a + 1; b < cells.size(); ++b) {
                if (overlaps(cells[a].row, cells[a].col, *cells[a].cell,
                             cells[b].row, cells[b].col, *cells[b].cell)) {
                    add(LayoutDiagnosticSeverity::Error,
                        LayoutDiagnosticCode::GridSlotOverlap, widget,
                        name + L" contains overlapping grid slots");
                    reported = true;
                    break;
                }
            }
        }
    }
}

std::vector<std::wstring> LayoutValidationModel::displayLabels() const {
    std::vector<std::wstring> labels;
    labels.reserve(_diagnostics.size());
    for (const LayoutDiagnostic& diagnostic : _diagnostics) {
        const wchar_t* prefix = diagnostic.severity == LayoutDiagnosticSeverity::Error
            ? L"[E] " : (diagnostic.severity == LayoutDiagnosticSeverity::Warning
                ? L"[W] " : L"[I] ");
        labels.push_back(std::wstring(prefix) + diagnostic.message);
    }
    if (labels.empty()) labels.push_back(L"No authoring issues");
    return labels;
}

} // namespace ayt::ui
