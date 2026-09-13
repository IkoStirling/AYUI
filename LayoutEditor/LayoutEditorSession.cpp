#include "AYUI/LayoutEditor/LayoutEditorSession.h"

#include "AYUI/LayoutEditor/LayoutCanvasViewport.h"
#include "AYUI/LayoutEditor/LayoutPropertyEditors.h"
#include "AYUI/LayoutEditor/WidgetAuthoringRegistry.h"

#include "AYUI/Box.h"
#include "AYUI/Button.h"
#include "AYUI/CheckBox.h"
#include "AYUI/Clipboard.h"
#include "AYUI/ColorPicker.h"
#include "AYUI/ComboBox.h"
#include "AYUI/GridPanel.h"
#include "AYUI/Image.h"
#include "AYUI/IRenderBackend.h"
#include "AYUI/InteractiveWidget.h"
#include "AYUI/LayoutLoader.h"
#include "AYUI/ListView.h"
#include "AYUI/Modal.h"
#include "AYUI/ModalDialog.h"
#include "AYUI/Panel.h"
#include "AYUI/ProgressBar.h"
#include "AYUI/RadioButton.h"
#include "AYUI/RichText.h"
#include "AYUI/ScrollView.h"
#include "AYUI/Separator.h"
#include "AYUI/Slider.h"
#include "AYUI/Spinner.h"
#include "AYUI/Style.h"
#include "AYUI/TabControl.h"
#include "AYUI/TabStrip.h"
#include "AYUI/TextArea.h"
#include "AYUI/TextInput.h"
#include "AYUI/TextLabel.h"
#include "AYUI/TileView.h"
#include "AYUI/Tooltip.h"
#include "AYUI/TreeView.h"
#include "AYUI/Window.h"
#include "AYUI/Menu.h"
#include "AYUI/MenuBar.h"
#include "AYUI/MenuItem.h"
#include "AYUI/ScrollBar.h"
#include "AYUI/SvgIcon.h"
#include "AYUI/Widget.h"
#include "AYUI/WidgetFactory.h"
#include "AYUI/UIKeyCode.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <codecvt>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <locale>
#include <sstream>
#include <unordered_set>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <Windows.h>
#endif

namespace ayt::ui {
namespace {

constexpr float kHandleHit = 8.0f;
constexpr float kMinWidgetSize = 8.0f;

bool usesGenericSchemaBinding(AuthoringProperty property) {
    switch (property) {
    case AuthoringProperty::ValueMin:
    case AuthoringProperty::ValueMax:
    case AuthoringProperty::Value:
    case AuthoringProperty::ImageTint:
    case AuthoringProperty::ImageUvMinX:
    case AuthoringProperty::ImageUvMinY:
    case AuthoringProperty::ImageUvMaxX:
    case AuthoringProperty::ImageUvMaxY:
    case AuthoringProperty::SelectionMode:
    case AuthoringProperty::ItemHeight:
    case AuthoringProperty::TileWidth:
    case AuthoringProperty::TileHeight:
    case AuthoringProperty::TileSpacing:
    case AuthoringProperty::VerticalScrollBarVisibility:
    case AuthoringProperty::HorizontalScrollBarVisibility:
    case AuthoringProperty::TabOverflowMode:
    case AuthoringProperty::MinTabWidth:
    case AuthoringProperty::GridRows:
    case AuthoringProperty::GridColumns:
    case AuthoringProperty::GridSpacingX:
    case AuthoringProperty::GridSpacingY:
    case AuthoringProperty::RichTextWrapMode:
    case AuthoringProperty::RichTextOverflow:
    case AuthoringProperty::LineHeight:
    case AuthoringProperty::MaxLines:
        return true;
    default:
        return false;
    }
}

std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) {
        return {};
    }
#if defined(_WIN32)
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                        static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) {
        return {};
    }
    std::wstring out(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                          out.data(), n);
    return out;
#else
    if constexpr (sizeof(wchar_t) == 2) {
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
        return converter.from_bytes(s);
    } else {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        return converter.from_bytes(s);
    }
#endif
}

std::string wideToUtf8(const std::wstring& s) {
    if (s.empty()) {
        return {};
    }
#if defined(_WIN32)
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(),
                                        static_cast<int>(s.size()), nullptr, 0,
                                        nullptr, nullptr);
    if (n <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                          out.data(), n, nullptr, nullptr);
    return out;
#else
    if constexpr (sizeof(wchar_t) == 2) {
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
        return converter.to_bytes(s);
    } else {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        return converter.to_bytes(s);
    }
#endif
}

std::wstring formatFloat(float v) {
    std::wostringstream oss;
    oss << v;
    return oss.str();
}

bool parseFloat(const std::wstring& s, float& out) {
    try {
        size_t idx = 0;
        out = std::stof(wideToUtf8(s), &idx);
        return idx > 0;
    } catch (...) {
        return false;
    }
}

std::wstring trimWide(std::wstring value) {
    const auto first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {};
    const auto last = value.find_last_not_of(L" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::wstring lowerWide(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return value;
}

bool parseInt(const std::wstring& s, int& out) {
    try {
        const std::string encoded = wideToUtf8(trimWide(s));
        size_t index = 0;
        out = std::stoi(encoded, &index);
        return index == encoded.size();
    } catch (...) {
        return false;
    }
}

std::vector<std::wstring> splitItems(const std::wstring& value) {
    std::vector<std::wstring> items;
    std::wstring current;
    for (const wchar_t ch : value) {
        if (ch == L'|' || ch == L'\n') {
            current = trimWide(std::move(current));
            if (!current.empty()) items.push_back(std::move(current));
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    current = trimWide(std::move(current));
    if (!current.empty()) items.push_back(std::move(current));
    return items;
}

std::wstring joinItems(const std::vector<std::wstring>& items) {
    std::wstring result;
    for (const std::wstring& item : items) {
        if (!result.empty()) result += L" | ";
        result += item;
    }
    return result;
}

std::wstring formatColorHex(const math::FVector4& color) {
    auto byte = [](float value) {
        return static_cast<unsigned int>(std::lround(
            std::clamp(value, 0.0f, 1.0f) * 255.0f));
    };
    wchar_t value[10]{};
    std::swprintf(value, sizeof(value) / sizeof(value[0]),
                 L"#%02X%02X%02X%02X", byte(color.x), byte(color.y),
                 byte(color.z), byte(color.w));
    return value;
}

bool parseColorHex(std::wstring value, math::FVector4& out) {
    value = trimWide(std::move(value));
    if (!value.empty() && value.front() == L'#') value.erase(value.begin());
    if (value.size() != 6u && value.size() != 8u) return false;
    try {
        const unsigned long packed = std::stoul(value, nullptr, 16);
        const bool hasAlpha = value.size() == 8u;
        const unsigned int r = hasAlpha ? (packed >> 24u) & 0xffu
                                        : (packed >> 16u) & 0xffu;
        const unsigned int g = hasAlpha ? (packed >> 16u) & 0xffu
                                        : (packed >> 8u) & 0xffu;
        const unsigned int b = hasAlpha ? (packed >> 8u) & 0xffu
                                        : packed & 0xffu;
        const unsigned int a = hasAlpha ? packed & 0xffu : 0xffu;
        out = math::FVector4(r / 255.0f, g / 255.0f, b / 255.0f,
                             a / 255.0f);
        return true;
    } catch (...) {
        return false;
    }
}

std::vector<std::wstring> propertyOptionLabels(AuthoringProperty property) {
    const PropertyFieldSchema& schema = propertyFieldSchema(property);
    std::vector<std::wstring> labels;
    labels.reserve(schema.enumOptions.size());
    for (const std::string& option : schema.enumOptions) {
        labels.push_back(utf8ToWide(option));
    }
    return labels;
}

std::string readFileToString(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return {};
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

bool writeStringToFile(const std::string& path, const std::string& text) {
    if (path.empty()) return false;
    std::error_code error;
    const std::filesystem::path parent =
        std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) return false;
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.flush();
    return file.good();
}

struct DesignerGridEntry {
    Widget* widget = nullptr;
    int rowSpan = 1;
    int colSpan = 1;
    GridPanel::HAlign hAlign = GridPanel::HAlign::Fill;
    GridPanel::VAlign vAlign = GridPanel::VAlign::Fill;
};

std::vector<DesignerGridEntry> collectDesignerGridEntries(
    GridPanel* grid, Widget* omit = nullptr) {
    std::vector<DesignerGridEntry> entries;
    if (grid == nullptr) return entries;
    for (int row = 0; row < grid->getRowCount(); ++row) {
        for (int col = 0; col < grid->getColumnCount(); ++col) {
            const GridPanel::CellInfo* cell = grid->findCell(row, col);
            if (cell == nullptr || cell->widget == nullptr ||
                cell->widget == omit) {
                continue;
            }
            entries.push_back({cell->widget, cell->rowSpan, cell->colSpan,
                               cell->hAlign, cell->vAlign});
        }
    }
    return entries;
}

int designerGridIndexOf(GridPanel* grid, Widget* widget) {
    if (grid == nullptr || widget == nullptr) return -1;
    const int cols = grid->getColumnCount();
    for (int row = 0; row < grid->getRowCount(); ++row) {
        for (int col = 0; col < cols; ++col) {
            const GridPanel::CellInfo* cell = grid->findCell(row, col);
            if (cell != nullptr && cell->widget == widget) {
                return row * cols + col;
            }
        }
    }
    return -1;
}

bool rebuildDesignerGrid(GridPanel* grid, Widget* inserted,
                         size_t insertIndex) {
    if (grid == nullptr || inserted == nullptr) return false;

    std::vector<DesignerGridEntry> entries =
        collectDesignerGridEntries(grid, inserted);
    if (insertIndex > entries.size()) insertIndex = entries.size();
    entries.insert(entries.begin() + static_cast<std::ptrdiff_t>(insertIndex),
                   DesignerGridEntry{inserted});

    int columns = grid->getColumnCount();
    int rows = grid->getRowCount();
    if (columns <= 0) columns = 2;
    if (rows <= 0) rows = 2;
    while (rows * columns < static_cast<int>(entries.size())) ++rows;

    // Capture first, then detach every occupied cell. Re-applying the
    // captured metadata preserves authored spans/alignment while assigning
    // the new child to the next row-major slot.
    for (int row = 0; row < grid->getRowCount(); ++row) {
        for (int col = 0; col < grid->getColumnCount(); ++col) {
            if (grid->getCell(row, col) != nullptr) grid->clearCell(row, col);
        }
    }
    grid->setRowCount(rows);
    grid->setColumnCount(columns);
    for (size_t i = 0; i < entries.size(); ++i) {
        DesignerGridEntry& entry = entries[i];
        const int row = static_cast<int>(i) / columns;
        const int col = static_cast<int>(i) % columns;
        entry.widget->setLayoutPositionManaged(true);
        entry.widget->setLayoutSizeManaged(true);
        grid->setCell(row, col, entry.widget, entry.rowSpan, entry.colSpan,
                      entry.hAlign, entry.vAlign);
    }
    grid->performLayout();
    return inserted->getParent() == grid;
}

math::FVector2 nextDesignerFreePosition(Widget* parent, Widget* inserted) {
    constexpr float kInset = 16.0f;
    constexpr float kGap = 8.0f;
    if (parent == nullptr || inserted == nullptr) return {24.0f, 24.0f};

    const math::FVector2 parentSize = parent->getSize();
    const math::FVector2 childSize = inserted->getSize();
    const float maxX = std::max(kInset, parentSize.x - childSize.x - kInset);
    const float maxY = std::max(kInset, parentSize.y - childSize.y - kInset);
    auto overlaps = [parent, inserted, childSize, kGap](float x, float y) {
        const math::FRectangle candidate(
            x, y, x + childSize.x, y + childSize.y);
        for (Widget* sibling : parent->getChildren()) {
            if (sibling == nullptr || sibling == inserted ||
                !sibling->isVisible()) {
                continue;
            }
            const math::FVector2 pos = sibling->getPosition();
            const math::FVector2 size = sibling->getSize();
            const math::FRectangle occupied(
                pos.x - kGap, pos.y - kGap,
                pos.x + size.x + kGap, pos.y + size.y + kGap);
            if (candidate.minX < occupied.maxX &&
                candidate.maxX > occupied.minX &&
                candidate.minY < occupied.maxY &&
                candidate.maxY > occupied.minY) {
                return true;
            }
        }
        return false;
    };

    for (float y = kInset; y <= maxY + 0.5f; y += kGap) {
        for (float x = kInset; x <= maxX + 0.5f; x += kGap) {
            if (!overlaps(x, y)) return {x, y};
        }
    }

    // A full absolute container still gets deterministic cascade placement;
    // drag-create remains the route for choosing an exact overlapping point.
    const size_t ordinal = parent->getChildren().size();
    const float offset = static_cast<float>(ordinal % 8u) * kGap;
    return {std::min(maxX, kInset + offset),
            std::min(maxY, kInset + offset)};
}

void installLayoutEditorChromeStyles() {
    StyleSheet* sheet = StyleManager::get().getStyleSheet();
    if (sheet == nullptr) return;

    auto surface = [](const math::FVector4& background,
                      const math::FVector4& border,
                      float borderWidth = 0.0f,
                      float radius = 0.0f) {
        WidgetStyle style = StyleBuilder::makeDefault();
        style.backgroundColor = background;
        style.borderColor = border;
        style.border.color = border;
        style.border.width = borderWidth;
        style.border.cornerRadius = radius;
        return style;
    };
    auto text = [](int size, const math::FVector4& color, bool bold) {
        WidgetStyle style = StyleBuilder::makeTextLabel();
        style.textColor = color;
        style.font.color = color;
        style.font.fontSize = size;
        style.font.bold = bold;
        return style;
    };

    sheet->setStyle("__le_root", surface(
        math::FVector4(0.055f, 0.061f, 0.075f, 1.0f),
        math::FVector4(0.055f, 0.061f, 0.075f, 1.0f)));
    sheet->setStyle("__le_titlebar", surface(
        math::FVector4(0.082f, 0.090f, 0.108f, 1.0f),
        math::FVector4(0.16f, 0.18f, 0.22f, 1.0f), 0.0f));
    sheet->setStyle("__le_commandbar", surface(
        math::FVector4(0.070f, 0.077f, 0.093f, 1.0f),
        math::FVector4(0.15f, 0.17f, 0.20f, 1.0f), 0.0f));
    sheet->setStyle("__le_sidebar", surface(
        math::FVector4(0.073f, 0.080f, 0.096f, 1.0f),
        math::FVector4(0.15f, 0.17f, 0.20f, 1.0f), 1.0f));
    sheet->setStyle("__le_canvas", surface(
        math::FVector4(0.105f, 0.115f, 0.135f, 1.0f),
        math::FVector4(0.20f, 0.23f, 0.28f, 1.0f), 1.0f, 5.0f));
    sheet->setStyle("__le_statusbar", surface(
        math::FVector4(0.060f, 0.067f, 0.080f, 1.0f),
        math::FVector4(0.14f, 0.16f, 0.19f, 1.0f), 0.0f));

    WidgetStyle command = StyleBuilder::makeButton();
    command.backgroundColor = math::FVector4(0.14f, 0.15f, 0.18f, 1.0f);
    command.borderColor = math::FVector4(0.24f, 0.27f, 0.32f, 1.0f);
    command.border.color = command.borderColor;
    command.border.width = 1.0f;
    command.border.cornerRadius = 5.0f;
    command.textColor = math::FVector4(0.88f, 0.90f, 0.94f, 1.0f);
    command.font.color = command.textColor;
    command.backgroundStates.enabled = true;
    command.backgroundStates.normal = command.backgroundColor;
    command.backgroundStates.hovered = math::FVector4(0.20f, 0.22f, 0.27f, 1.0f);
    command.backgroundStates.pressed = math::FVector4(0.10f, 0.31f, 0.55f, 1.0f);
    command.backgroundStates.disabled = math::FVector4(0.10f, 0.11f, 0.13f, 0.7f);
    command.backgroundTransition.enabled = true;
    command.backgroundTransition.durationMs = 90.0f;
    sheet->setStyle("__le_command", command);

    WidgetStyle primary = command;
    primary.backgroundColor = math::FVector4(0.10f, 0.36f, 0.66f, 1.0f);
    primary.backgroundStates.normal = primary.backgroundColor;
    primary.backgroundStates.hovered = math::FVector4(0.13f, 0.44f, 0.78f, 1.0f);
    primary.backgroundStates.pressed = math::FVector4(0.07f, 0.29f, 0.55f, 1.0f);
    primary.borderColor = math::FVector4(0.22f, 0.52f, 0.86f, 1.0f);
    primary.border.color = primary.borderColor;
    sheet->setStyle("__le_primary", primary);

    WidgetStyle palette = command;
    palette.backgroundColor = math::FVector4(0.095f, 0.105f, 0.125f, 1.0f);
    palette.backgroundStates.normal = palette.backgroundColor;
    palette.backgroundStates.hovered = math::FVector4(0.15f, 0.18f, 0.23f, 1.0f);
    palette.backgroundStates.pressed = math::FVector4(0.10f, 0.32f, 0.58f, 1.0f);
    sheet->setStyle("__le_palette", palette);

    WidgetStyle menuAnchor = command;
    menuAnchor.backgroundColor = math::FVector4(0.082f, 0.090f, 0.108f, 1.0f);
    menuAnchor.backgroundStates.normal = menuAnchor.backgroundColor;
    menuAnchor.backgroundStates.hovered = math::FVector4(0.15f, 0.18f, 0.23f, 1.0f);
    menuAnchor.backgroundStates.pressed = math::FVector4(0.10f, 0.32f, 0.58f, 1.0f);
    menuAnchor.border.width = 0.0f;
    sheet->setStyle("__le_menu_anchor", menuAnchor);

    WidgetStyle input = surface(
        math::FVector4(0.050f, 0.055f, 0.067f, 1.0f),
        math::FVector4(0.20f, 0.23f, 0.28f, 1.0f), 1.0f, 4.0f);
    input.textColor = math::FVector4(0.88f, 0.90f, 0.94f, 1.0f);
    input.font.color = input.textColor;
    input.font.fontSize = 12;
    sheet->setStyle("__le_input", input);

    sheet->setStyle("__le_heading", text(
        13, math::FVector4(0.92f, 0.94f, 0.98f, 1.0f), true));
    sheet->setStyle("__le_title", text(
        16, math::FVector4(0.94f, 0.96f, 1.0f, 1.0f), true));
    sheet->setStyle("__le_label", text(
        12, math::FVector4(0.70f, 0.74f, 0.80f, 1.0f), false));
    sheet->setStyle("__le_muted", text(
        11, math::FVector4(0.48f, 0.53f, 0.61f, 1.0f), false));
    sheet->setStyle("__le_accent", text(
        12, math::FVector4(0.36f, 0.68f, 1.0f, 1.0f), true));
    sheet->setStyle("__le_warning", text(
        11, math::FVector4(1.0f, 0.66f, 0.28f, 1.0f), false));
    sheet->setStyle("__le_success", text(
        11, math::FVector4(0.35f, 0.82f, 0.56f, 1.0f), false));
}

} // namespace

LayoutEditorSession::LayoutEditorSession()
    : _docRoot(_documentModel.rootRef()),
      _documentPath(_documentModel.pathRef()),
      _dirty(_documentModel.dirtyRef()),
      _selected(_selectionModel.primaryRef()),
      _selection(_selectionModel.items()),
      _commandStack(64) {}

LayoutEditorSession::~LayoutEditorSession() {
    detach();
}

bool LayoutEditorSession::attach(UIManager& ui) {
    return attach(ui, nullptr);
}

bool LayoutEditorSession::attach(UIManager& ui, Widget* chromeRoot) {
    detach();
    installLayoutEditorChromeStyles();
    _ui = &ui;
    _chromeRoot = chromeRoot;
    _ui->disableLayoutHotReload();

    _canvasHost = findChromeById("canvas_host");
    if (_canvasHost == nullptr) {
        std::fprintf(stderr,
            "[LayoutEditorSession] canvas_host not found — load layout_editor.ui.json first\n");
        _ui = nullptr;
        return false;
    }

    _docLoader.setWidgetFactory(&WidgetFactory::get());
    ensureSchemaPropertyChrome();
    wireChrome();
    ensureCanvasViewport();

    if (_canvasViewport == nullptr || _canvasViewport->getChildren().empty()) {
        ensureEmptyDocument();
    } else {
        // The viewport owns exactly the authored document root. Selection
        // chrome remains a sibling above it in canvas_host.
        _documentModel.setRoot(nullptr);
        for (Widget* c : _canvasViewport->getChildren()) {
            if (!isEditorOverlay(c)) {
                _documentModel.setRoot(c);
                break;
            }
        }
        if (_docRoot != nullptr) {
            freezeDocumentInteraction(_docRoot);
        } else {
            ensureEmptyDocument();
        }
    }

    ensureSelectionChrome();
    ensureHierDropChrome();
    // Complete the initial model population once, after the authored root and
    // every chrome pointer exist. wireChrome() deliberately performs binding
    // only; running these synchronizers there as well caused two complete
    // Inspector passes and two asset scans during first open.
    refreshTextureResources();
    syncPreviewControls();
    refreshHierarchy();
    syncPropertyStrip();
    syncSelectionChrome();
    setStatus(L"Ready — Ctrl multi-select | C/V/D | arrows | G snap | Ctrl+wheel zoom | MMB pan | empty-drag marquee");
    refreshWindowTitle();
    return true;
}

Widget* LayoutEditorSession::findChromeById(const std::string& id) const {
    if (id.empty()) {
        return nullptr;
    }
    if (_chromeRoot == nullptr) {
        return _ui != nullptr ? _ui->findById(id) : nullptr;
    }
    std::function<Widget*(Widget*)> find = [&](Widget* node) -> Widget* {
        if (node == nullptr) return nullptr;
        if (node->getId() == id) return node;
        for (Widget* child : node->getChildren()) {
            if (Widget* match = find(child)) return match;
        }
        return nullptr;
    };
    return find(_chromeRoot);
}

void LayoutEditorSession::detach() {
    if (_animationTimelineView != nullptr) {
        _animationTimelineView->clearCallbacks();
    }
    if (_animationRuntimePreview.has_value()) {
        _animationRuntimePreview->cancel();
    }
    _animationRuntimePreview.reset();
    _animationPreviewBaseline.reset();
    if (_mode == Mode::Interact) {
        _mode = Mode::Edit;
        restoreDocumentInteraction(_docRoot);
    }
    destroySelectionChrome();
    destroyHierDropChrome();
    destroyPaletteGhost();
    _dragMode = DragMode::None;
    _dragTarget = nullptr;
    _toolDrag = ToolDrag::None;
    _spaceDown = false;
    _paletteType.clear();
    _hierDragWidget = nullptr;
    _commandStack.end();
    _deferred = DeferredAction::None;
    _consumeNextPointerUp = false;
    _ui = nullptr;
    _canvasHost = nullptr;
    _canvasViewport = nullptr;
    _documentModel.setRoot(nullptr);
    _selectionModel.clear();
    _inspectorSelection = nullptr;
    _hierarchy = nullptr;
    _hierarchySearch = nullptr;
    _hierarchySummary = nullptr;
    _hierarchyCol = nullptr;
    _chromeRoot = nullptr;
    _propStyleCombo = nullptr;
    _propTextHAlign = nullptr;
    _propTextVAlign = nullptr;
    _propChecked = nullptr;
    _propPassword = nullptr;
    _propReadOnly = nullptr;
    _propGravity = nullptr;
    _selOutlines.clear();
    _marqueeBox = nullptr;
    _safeAreaBox = nullptr;
    _clipboardItems.clear();
    _structuredModel.bind(nullptr);
    _textureCatalog.setEntries({});
    _interactionStates.clear();
    _interactionSnapshot.reset();
    _mode = Mode::Edit;
    _styleIds.clear();
    _viewZoom = 1.0f;
    _viewPan = math::FVector2(0.0f, 0.0f);
    _snapEnabled = true;
    _gridSize = 8.0f;
    _propId = _propX = _propY = _propW = _propH = _propText = nullptr;
    _propTexture = _propItems = _propController = nullptr;
    _propOnClick = _propOnToggled = _propOnValueChanged = nullptr;
    _propOnTextChanged = _propOnSubmit = nullptr;
    _propOnSelectionChanged = _propOnItemActivated = _propOnClose = nullptr;
    _propSpacing = nullptr;
    _propPadL = _propPadT = _propPadR = _propPadB = nullptr;
    _status = nullptr;
    _structuredList = nullptr;
    _structuredText = nullptr;
    _structuredFontSize = nullptr;
    _structuredColor = nullptr;
    _textureResourceList = nullptr;
    _textureSearch = nullptr;
    _textureStatus = nullptr;
    _stylePreviewStateCombo = nullptr;
    _stylePreviewSwatch = nullptr;
    _styleSourceStatus = nullptr;
    _styleColorStatus = nullptr;
    _validationList = nullptr;
    _validationStatus = nullptr;
    _suppressValidation = false;
    _validationDirty = true;
    _validationLabels.clear();
    _reuseList = nullptr;
    _reuseName = nullptr;
    _reuseStatus = nullptr;
    _suppressReuse = false;
    _externalComponentList = nullptr;
    _externalComponentSearch = nullptr;
    _externalComponentCategoryFilter = nullptr;
    _externalComponentId = nullptr;
    _externalComponentDisplayName = nullptr;
    _externalComponentCategory = nullptr;
    _externalComponentDescription = nullptr;
    _externalComponentTags = nullptr;
    _externalComponentStatus = nullptr;
    _externalComponentFilteredIndices.clear();
    _suppressExternalComponents = false;
    _themeTokenList = nullptr;
    _themeTokenKey = nullptr;
    _themeTokenValue = nullptr;
    _themeTokenSwatch = nullptr;
    _themeStyleList = nullptr;
    _themeStyleFragment = nullptr;
    _themeStyleId = nullptr;
    _themeStyleProperty = nullptr;
    _themeStyleBinding = nullptr;
    _themeEditorStatus = nullptr;
    _suppressThemeEditor = false;
    _projectRefactorKind = nullptr;
    _projectRefactorOld = nullptr;
    _projectRefactorNew = nullptr;
    _projectRefactorPreview = nullptr;
    _projectRefactorStatus = nullptr;
    _suppressProjectRefactor = false;
    _responsiveBreakpoint = nullptr;
    _responsiveVisibility = nullptr;
    _responsiveStatus = nullptr;
    _suppressResponsive = false;
    _responsiveAuthoringIndex = 0;
    _animationClipList = nullptr;
    _animationClipName = nullptr;
    _animationTrackList = nullptr;
    _animationProperty = nullptr;
    _animationKeyList = nullptr;
    _animationTime = nullptr;
    _animationCurve = nullptr;
    _animationRepeat = nullptr;
    _animationYoyo = nullptr;
    _animationImportance = nullptr;
    _animationStatus = nullptr;
    _animationTransportStatus = nullptr;
    _animationTimelineHost = nullptr;
    _animationTimelineView = nullptr;
    _suppressAnimation = false;
    _animationClipIndex = -1;
    _animationTrackIndex = -1;
    _animationKeyIndex = -1;
    _animationPreviewClipIndex = -1;
    _animationPreviewTimeMs = 0.0f;
    _animationPreviewLoop = false;
    _animationKeyDragActive = false;
    _animationKeyDragChanged = false;
    _animationKeyDragSnapshot.reset();
    _animationKeyDragClip = -1;
    _animationKeyDragTrack = -1;
    _animationKeyDragKey = -1;
    _animationKeyDragOriginalKey = -1;
    _stylePreviewState = StyleState::Normal;
    _previewPreset = nullptr;
    _previewDpi = nullptr;
    _previewWidth = nullptr;
    _previewHeight = nullptr;
    _previewSafeL = _previewSafeT = _previewSafeR = _previewSafeB = nullptr;
    _hierarchyIndex.clear();
    _commandStack.clear();
    _documentPath.clear();
    _reuseLibrary.clear();
    _animationLibrary.clear();
    _dirty = false;
}

std::wstring LayoutEditorSession::localizedText(
    std::string_view key, std::wstring_view fallback) const
{
    if (_ui == nullptr) return std::wstring(fallback);
    const UILayoutLoader::TextResolver& resolver =
        _ui->loader().textResolver();
    return resolver ? resolver(key, fallback) : std::wstring(fallback);
}

void LayoutEditorSession::retranslateChrome()
{
    if (_ui == nullptr || _chromeRoot == nullptr) return;
    _ui->loader().retranslate(_chromeRoot);
    for (const PropertyFieldSchema& schema : allPropertyFieldSchemas()) {
        if (schema.displayName == nullptr) continue;
        if (auto* label = dynamic_cast<TextLabel*>(
                findChromeById(schema.labelId))) {
            label->setText(localizedText(
                std::string("ui.") + "editor.ui_designer.property_field."
                    + schema.key,
                utf8ToWide(schema.displayName)));
        }
    }
    syncPreviewControls();
    syncHierarchySummary();
    syncAnimationTransportStatus();
    syncAnimationEditor();
    updatePropPanelVisibility();
    _ui->invalidateLayout();
    _ui->layout();
}

void LayoutEditorSession::pumpDeferred(float deltaSeconds) {
    syncCanvasViewportGeometry();
    syncAnimationTimelineGeometry();
    advanceAnimationPreview(deltaSeconds);
    if (_validationDirty) refreshValidation();
    const DeferredAction action = _deferred;
    _deferred = DeferredAction::None;
    switch (action) {
    case DeferredAction::Open:
        if (_openPicker) {
            const std::string path = _openPicker();
            if (!path.empty()) {
                open(path);
            }
        }
        break;
    case DeferredAction::Save:
        if (!save()) {
            setStatus(L"Save failed (use Save As?)");
        }
        break;
    case DeferredAction::SaveAs:
        if (!_savePicker) {
            setStatus(L"No Save As picker");
            break;
        }
        {
            const std::string path = _savePicker();
            if (!path.empty()) {
                saveAs(path);
            }
        }
        break;
    case DeferredAction::None:
    default:
        break;
    }
    syncSelectionChrome();
}

void LayoutEditorSession::setTexturePreviewLoader(TexturePreviewLoader loader) {
    _texturePreviewLoader = std::move(loader);
    rehydrateRuntimePresentation(_docRoot);
    syncTextureBrowser();
}

void LayoutEditorSession::setTextureResourceProvider(
    TextureResourceProvider provider) {
    _textureResourceProvider = std::move(provider);
    // Hosts configure providers while constructing their controller, before
    // the Designer chrome is attached. Scanning at that point did the same
    // recursive asset walk again from wireChrome(), blocking the editor UI
    // twice on first open. Preserve immediate refresh for live/reconfigured
    // sessions, but let attach() own the initial scan.
    if (_ui != nullptr) refreshTextureResources();
}

void LayoutEditorSession::setMode(Mode mode) {
    if (_mode == mode) return;
    if (mode == Mode::Interact) stopAnimationPreview();
    cancelToolDrag();
    _dragMode = DragMode::None;
    _dragTarget = nullptr;
    endMutation();
    if (mode == Mode::Interact) {
        if (_ui != nullptr) _ui->setFocus(nullptr);
        _interactionSnapshot = captureSnapshot();
        _mode = Mode::Interact;
        restoreDocumentInteraction(_docRoot);
        setStatus(L"Interact preview — controls are live; F6 or Esc returns to Edit");
    } else {
        const std::optional<Snapshot> restore = _interactionSnapshot;
        _interactionSnapshot.reset();
        _mode = Mode::Edit;
        if (restore.has_value()) {
            restoreSnapshot(*restore);
        } else {
            freezeDocumentInteraction(_docRoot);
        }
        setStatus(L"Edit mode — preview interaction state was rolled back");
    }
    syncPreviewControls();
    syncSelectionChrome();
    if (_ui != nullptr) {
        _ui->invalidateLayout();
        _ui->layout();
    }
}

void LayoutEditorSession::setPreviewPreset(int index) {
    if (_docRoot != nullptr && !previewOverridesDocumentSize()) {
        _authoredRootSize = _docRoot->getSize();
    }
    if (!_previewModel.selectPreset(index)) return;
    applyPreviewToDocument();
    syncPreviewControls();
    syncSelectionChrome();
}

void LayoutEditorSession::setPreviewSettings(
    const LayoutPreviewSettings& settings) {
    if (_docRoot != nullptr && !previewOverridesDocumentSize()) {
        _authoredRootSize = _docRoot->getSize();
    }
    _previewModel.setCustom(settings);
    applyPreviewToDocument();
    syncPreviewControls();
    syncSelectionChrome();
}

void LayoutEditorSession::setSafeAreaVisible(bool visible) {
    _previewModel.setSafeAreaVisible(visible);
    syncPreviewControls();
    syncPreviewChrome();
    if (_ui != nullptr) _ui->invalidateLayout();
}

bool LayoutEditorSession::previewOverridesDocumentSize() const {
    return !_previewModel.settings().followDocumentSize;
}

math::FVector2 LayoutEditorSession::authoredRootSize() const {
    return _docRoot != nullptr && !previewOverridesDocumentSize()
        ? _docRoot->getSize() : _authoredRootSize;
}

void LayoutEditorSession::applyPreviewToDocument() {
    if (_docRoot == nullptr) return;
    if (!previewOverridesDocumentSize()) {
        _docRoot->setSize(_authoredRootSize);
    } else {
        _docRoot->setSize(_previewModel.logicalSize(_authoredRootSize));
    }
    _docRoot->performLayout();
    if (_ui != nullptr) {
        _ui->invalidateLayout();
        _ui->layout();
    }
    syncPreviewChrome();
    syncResponsiveEditor();
}

bool LayoutEditorSession::defineReusableBlock(const std::string& name) {
    if (_selected == nullptr || _selection.size() != 1u) {
        setStatus(L"Select exactly one widget to create a reusable block");
        return false;
    }
    LayoutReuseLibrary updated = _reuseLibrary;
    std::string error;
    if (!updated.define(name, _selected, &error)) {
        setStatus(utf8ToWide(error));
        return false;
    }
    pushUndo(LayoutEditKind::Reusable, "Save reusable block");
    _reuseLibrary = std::move(updated);
    markDirty(true);
    syncReuseEditor();
    setStatus(utf8ToWide("Reusable block saved: " + name));
    return true;
}

bool LayoutEditorSession::insertReusableBlock(const std::string& name) {
    Widget* parent = pickParentForAdd();
    Widget* created = _reuseLibrary.instantiate(name);
    if (parent == nullptr || created == nullptr) {
        setStatus(utf8ToWide("Reusable block unavailable: " + name));
        destroyWidgetTree(created);
        return false;
    }
    remintTreeIds(created);
    pushUndo(LayoutEditKind::Insert, "Insert reusable block");
    if (!placeNewWidget(created, parent, -1, nullptr)) {
        _commandStack.discardLastUndo();
        destroyWidgetTree(created);
        return false;
    }
    rehydrateRuntimePresentation(created);
    freezeDocumentInteraction(created);
    markDirty(true);
    refreshHierarchy();
    select(created, false);
    setStatus(utf8ToWide("Inserted reusable block: " + name));
    return true;
}

bool LayoutEditorSession::removeReusableBlock(const std::string& name) {
    if (_reuseLibrary.find(name) == nullptr) return false;
    pushUndo(LayoutEditKind::Reusable, "Remove reusable block");
    if (!_reuseLibrary.remove(name)) {
        _commandStack.discardLastUndo();
        return false;
    }
    markDirty(true);
    syncReuseEditor();
    setStatus(utf8ToWide("Reusable block removed: " + name));
    return true;
}

void LayoutEditorSession::setExternalComponentLibraryPath(std::string path) {
    if (_externalComponentLibraryPath == path) return;
    _externalComponentLibraryPath = std::move(path);
    refreshExternalComponentLibrary();
}

void LayoutEditorSession::setProjectRefactorKinds(
    std::vector<LayoutProjectRefactorKind> kinds) {
    _projectRefactorKinds = std::move(kinds);
    syncProjectRefactorEditor();
}

bool LayoutEditorSession::refreshExternalComponentLibrary() {
    _externalComponentLibrary.clear();
    if (_externalComponentLibraryPath.empty()) {
        syncExternalComponentEditor();
        return true;
    }
    const std::string encoded = readFileToString(_externalComponentLibraryPath);
    if (encoded.empty()) {
        syncExternalComponentEditor();
        return true;
    }
    std::string error;
    if (!_externalComponentLibrary.deserialize(encoded, &error)) {
        setStatus(L"Component library rejected — " + utf8ToWide(error));
        syncExternalComponentEditor();
        return false;
    }
    syncExternalComponentEditor();
    return true;
}

bool LayoutEditorSession::saveExternalComponentLibrary() {
    if (_externalComponentLibraryPath.empty()) {
        setStatus(L"Project component library path is not configured");
        return false;
    }
    if (!writeStringToFile(_externalComponentLibraryPath,
                           _externalComponentLibrary.serialize(true))) {
        setStatus(L"Project component library could not be saved");
        return false;
    }
    return true;
}

bool LayoutEditorSession::defineExternalComponent(
    const std::string& id, const std::string& displayName,
    const std::string& category, const std::string& description,
    const std::vector<std::string>& tags) {
    if (_selected == nullptr || _selection.size() != 1u) {
        setStatus(L"Select exactly one Widget to create a project component");
        return false;
    }
    LayoutComponentLibrary updated = _externalComponentLibrary;
    std::string error;
    if (!updated.define(id, displayName, category, _selected, &error,
                        description, tags)) {
        setStatus(L"Project component rejected — " + utf8ToWide(error));
        return false;
    }
    const LayoutComponentLibrary previous = _externalComponentLibrary;
    _externalComponentLibrary = std::move(updated);
    if (!saveExternalComponentLibrary()) {
        _externalComponentLibrary = previous;
        return false;
    }
    syncExternalComponentEditor();
    setStatus(L"Project component saved — " + utf8ToWide(id));
    return true;
}

bool LayoutEditorSession::insertExternalComponent(const std::string& id) {
    Widget* instance = _externalComponentLibrary.instantiate(id);
    if (instance == nullptr) {
        setStatus(L"Project component could not be instantiated");
        return false;
    }
    Widget* parent = pickParentForAdd();
    if (parent == nullptr) {
        destroyWidgetTree(instance);
        return false;
    }
    remintTreeIds(instance);
    pushUndo(LayoutEditKind::Insert, "Insert project component");
    if (!placeNewWidget(instance, parent, -1, nullptr)) {
        _commandStack.discardLastUndo();
        destroyWidgetTree(instance);
        return false;
    }
    rehydrateRuntimePresentation(instance);
    freezeDocumentInteraction(instance);
    markDirty(true);
    refreshHierarchy();
    select(instance, false);
    setStatus(L"Project component inserted — " + utf8ToWide(id));
    return true;
}

bool LayoutEditorSession::removeExternalComponent(const std::string& id) {
    LayoutComponentLibrary updated = _externalComponentLibrary;
    if (!updated.remove(id)) return false;
    const LayoutComponentLibrary previous = _externalComponentLibrary;
    _externalComponentLibrary = std::move(updated);
    if (!saveExternalComponentLibrary()) {
        _externalComponentLibrary = previous;
        return false;
    }
    syncExternalComponentEditor();
    setStatus(L"Project component removed — " + utf8ToWide(id));
    return true;
}

bool LayoutEditorSession::openThemeDocument(const std::string& path) {
    const std::string encoded = readFileToString(path);
    if (encoded.empty()) {
        setStatus(L"Theme file could not be read");
        return false;
    }
    LayoutThemeEditorModel loaded;
    std::string error;
    if (!loaded.load(encoded, &error)) {
        setStatus(L"Theme rejected — " + utf8ToWide(error));
        return false;
    }
    _themeEditorModel = std::move(loaded);
    _themeDocumentPath = path;
    _themeDocumentDirty = false;
    syncThemeEditor();
    setStatus(L"Theme opened — " + utf8ToWide(path));
    return true;
}

bool LayoutEditorSession::saveThemeDocument() {
    if (_themeDocumentPath.empty()) {
        setStatus(L"Open a Theme JSON file before saving");
        return false;
    }
    if (!writeStringToFile(_themeDocumentPath,
                           _themeEditorModel.serialize(true))) {
        setStatus(L"Theme file could not be saved");
        return false;
    }
    _themeDocumentDirty = false;
    syncThemeEditor();
    setStatus(L"Theme saved — " + utf8ToWide(_themeDocumentPath));
    return true;
}

void LayoutEditorSession::applyThemeToken() {
    if (_themeTokenKey == nullptr || _themeTokenValue == nullptr) return;
    const std::string key = wideToUtf8(trimWide(_themeTokenKey->getText()));
    const std::wstring value = trimWide(_themeTokenValue->getText());
    std::string error;
    bool changed = false;
    if (key.rfind("color.", 0) == 0) {
        math::FVector4 color;
        changed = parseColorHex(value, color)
            && _themeEditorModel.setColorToken(key, color, &error);
        if (!changed && error.empty()) error = "Color token expects #RRGGBB or #RRGGBBAA";
    } else {
        float number = 0.0f;
        changed = parseFloat(value, number)
            && _themeEditorModel.setFloatToken(key, number, &error);
        if (!changed && error.empty()) error = "Float token expects a number";
    }
    if (!changed) {
        setStatus(L"Theme token rejected — " + utf8ToWide(error));
        return;
    }
    _themeDocumentDirty = true;
    syncThemeEditor(key);
}

void LayoutEditorSession::openThemeTokenColorPicker() {
    if (_ui == nullptr || _themeTokenSwatch == nullptr
        || _themeTokenKey == nullptr || _themeTokenValue == nullptr) return;
    const std::string key = wideToUtf8(trimWide(_themeTokenKey->getText()));
    if (key.rfind("color.", 0) != 0) {
        setStatus(L"Select or enter a color.* token before opening the picker");
        return;
    }
    math::FVector4 initial;
    if (!parseColorHex(trimWide(_themeTokenValue->getText()), initial)) {
        initial = _themeTokenSwatch->getColor();
    }
    auto* picker = new ColorPicker();
    picker->setId("theme_token_color_picker");
    picker->setSize({292.0f, 310.0f});
    const math::FRectangle anchor = _themeTokenSwatch->getWorldBounds();
    picker->setPosition({anchor.maxX - 292.0f, anchor.maxY + 4.0f});
    picker->setColor(initial, false);
    picker->setOnColorChanged([this](const math::FVector4& value) {
        if (_themeTokenValue != nullptr) {
            _themeTokenValue->setText(ColorPicker::formatHexCode(value));
        }
        if (_themeTokenSwatch != nullptr) _themeTokenSwatch->setColor(value);
    });
    picker->setOnColorCommitted([this](const math::FVector4& value) {
        if (_themeTokenValue != nullptr) {
            _themeTokenValue->setText(ColorPicker::formatHexCode(value));
        }
        applyThemeToken();
    });
    _ui->openPopup(_themeTokenSwatch, picker);
}

void LayoutEditorSession::renameThemeToken() {
    if (_themeTokenList == nullptr || _themeTokenKey == nullptr) return;
    const std::vector<LayoutThemeTokenEntry> entries = _themeEditorModel.tokens();
    const int index = _themeTokenList->getSelectedIndex();
    if (index < 0 || index >= static_cast<int>(entries.size())) return;
    const std::string replacement = wideToUtf8(
        trimWide(_themeTokenKey->getText()));
    std::string error;
    if (!_themeEditorModel.renameToken(
            entries[static_cast<size_t>(index)].key, replacement, &error)) {
        setStatus(L"Theme token rename rejected — " + utf8ToWide(error));
        return;
    }
    _themeDocumentDirty = true;
    syncThemeEditor(replacement);
}

void LayoutEditorSession::removeThemeToken() {
    if (_themeTokenList == nullptr) return;
    const std::vector<LayoutThemeTokenEntry> entries = _themeEditorModel.tokens();
    const int index = _themeTokenList->getSelectedIndex();
    if (index < 0 || index >= static_cast<int>(entries.size())) return;
    std::string error;
    if (!_themeEditorModel.removeToken(
            entries[static_cast<size_t>(index)].key, false, &error)) {
        setStatus(L"Theme token removal rejected — " + utf8ToWide(error));
        return;
    }
    _themeDocumentDirty = true;
    syncThemeEditor();
}

void LayoutEditorSession::applyThemeStyleBinding() {
    if (_themeStyleList == nullptr || _themeStyleProperty == nullptr
        || _themeStyleBinding == nullptr) return;
    const std::vector<LayoutThemeStyleEntry> entries = _themeEditorModel.styles();
    const int styleIndex = _themeStyleList->getSelectedIndex();
    const int propertyIndex = _themeStyleProperty->getSelectedIndex();
    if (styleIndex < 0 || styleIndex >= static_cast<int>(entries.size())
        || propertyIndex < 0 || propertyIndex > 2) return;
    static const char* properties[] = {
        "backgroundColor", "borderColor", "textColor"
    };
    const LayoutThemeStyleEntry& style = entries[static_cast<size_t>(styleIndex)];
    std::string error;
    if (!_themeEditorModel.setStyleProperty(
            style.fragment, style.styleId, properties[propertyIndex],
            wideToUtf8(trimWide(_themeStyleBinding->getText())), &error)) {
        setStatus(L"Theme style binding rejected — " + utf8ToWide(error));
        return;
    }
    _themeDocumentDirty = true;
    syncThemeEditor({}, style.fragment + "/" + style.styleId);
}

void LayoutEditorSession::createThemeStyle() {
    if (_themeStyleFragment == nullptr || _themeStyleId == nullptr) return;
    const std::string fragment = wideToUtf8(trimWide(
        _themeStyleFragment->getText()));
    const std::string styleId = wideToUtf8(trimWide(_themeStyleId->getText()));
    std::string error;
    if (!_themeEditorModel.createStyle(fragment, styleId, &error)) {
        setStatus(L"Theme style creation rejected — " + utf8ToWide(error));
        return;
    }
    _themeDocumentDirty = true;
    syncThemeEditor({}, fragment + "/" + styleId);
}

void LayoutEditorSession::duplicateThemeStyle() {
    if (_themeStyleList == nullptr || _themeStyleFragment == nullptr
        || _themeStyleId == nullptr) return;
    const std::vector<LayoutThemeStyleEntry> entries = _themeEditorModel.styles();
    const int index = _themeStyleList->getSelectedIndex();
    if (index < 0 || index >= static_cast<int>(entries.size())) return;
    const std::string fragment = wideToUtf8(trimWide(
        _themeStyleFragment->getText()));
    const std::string styleId = wideToUtf8(trimWide(_themeStyleId->getText()));
    const auto& source = entries[static_cast<size_t>(index)];
    std::string error;
    if (!_themeEditorModel.duplicateStyle(
            source.fragment, source.styleId, fragment, styleId, &error)) {
        setStatus(L"Theme style duplication rejected — " + utf8ToWide(error));
        return;
    }
    _themeDocumentDirty = true;
    syncThemeEditor({}, fragment + "/" + styleId);
}

void LayoutEditorSession::removeThemeStyle() {
    if (_themeStyleList == nullptr) return;
    const std::vector<LayoutThemeStyleEntry> entries = _themeEditorModel.styles();
    const int index = _themeStyleList->getSelectedIndex();
    if (index < 0 || index >= static_cast<int>(entries.size())) return;
    const auto& style = entries[static_cast<size_t>(index)];
    std::string error;
    if (!_themeEditorModel.removeStyle(style.fragment, style.styleId, &error)) {
        setStatus(L"Theme style removal rejected — " + utf8ToWide(error));
        return;
    }
    _themeDocumentDirty = true;
    syncThemeEditor();
}

void LayoutEditorSession::setResponsiveVisibility(
    int breakpointIndex, ResponsiveVisibility visibility) {
    if (_selected == nullptr || _selected == _docRoot) return;
    pushUndo(LayoutEditKind::Responsive, "Set responsive visibility");
    if (!_responsiveModel.setVisibility(
            *_selected, breakpointIndex, visibility)) {
        _commandStack.discardLastUndo();
        return;
    }
    markDirty(true);
    if (_ui != nullptr) {
        _ui->invalidateLayout();
        _ui->layout();
    }
    syncResponsiveEditor();
    syncSelectionChrome();
}

void LayoutEditorSession::captureResponsiveAnchors(int breakpointIndex) {
    if (_selected == nullptr || !canUseAnchorLayout(_selected) ||
        !_selected->hasAnchorLayout()) {
        setStatus(L"Choose a base Anchor preset before capturing an override");
        return;
    }
    pushUndo(LayoutEditKind::Responsive, "Capture responsive anchors");
    if (!_responsiveModel.captureAnchorOverride(
            *_selected, breakpointIndex)) {
        _commandStack.discardLastUndo();
        return;
    }
    markDirty(true);
    syncResponsiveEditor();
    setStatus(L"Responsive anchor override captured");
}

void LayoutEditorSession::clearResponsiveRule(int breakpointIndex) {
    if (_selected == nullptr || _selected == _docRoot) return;
    pushUndo(LayoutEditKind::Responsive, "Clear responsive rule");
    if (!_responsiveModel.clearRule(*_selected, breakpointIndex)) {
        _commandStack.discardLastUndo();
        return;
    }
    markDirty(true);
    if (_ui != nullptr) {
        _ui->invalidateLayout();
        _ui->layout();
    }
    syncResponsiveEditor();
    syncSelectionChrome();
}

void LayoutEditorSession::previewResponsiveBreakpoint(int breakpointIndex) {
    const float width = _responsiveModel.previewWidth(breakpointIndex);
    if (width <= 0.0f) return;
    LayoutPreviewSettings settings = _previewModel.settings();
    const float scale = std::max(0.25f, settings.dpiScale);
    const math::FVector2 currentLogical =
        _previewModel.logicalSize(_authoredRootSize);
    settings.pixelWidth = width * scale;
    settings.pixelHeight = std::max(1.0f, currentLogical.y) * scale;
    settings.followDocumentSize = false;
    setPreviewSettings(settings);
    setStatus(L"Previewing " + _responsiveModel.breakpoints()[
        static_cast<size_t>(breakpointIndex)].label);
}

bool LayoutEditorSession::createAnimationClip(const std::string& name) {
    stopAnimationPreview();
    UIAnimationLibrary updated = _animationLibrary;
    std::string error;
    const int index = updated.addClip(name, &error);
    if (index < 0) {
        setStatus(utf8ToWide(error));
        return false;
    }
    pushUndo(LayoutEditKind::Animation, "Create animation clip");
    _animationLibrary = std::move(updated);
    _animationClipIndex = index;
    _animationTrackIndex = -1;
    _animationKeyIndex = -1;
    markDirty(true);
    syncAnimationEditor();
    setStatus(utf8ToWide("Animation clip created: " + name));
    return true;
}

bool LayoutEditorSession::removeAnimationClip(int clipIndex) {
    stopAnimationPreview();
    UIAnimationLibrary updated = _animationLibrary;
    if (!updated.removeClip(clipIndex)) return false;
    pushUndo(LayoutEditKind::Animation, "Remove animation clip");
    _animationLibrary = std::move(updated);
    _animationClipIndex = std::min(
        clipIndex, static_cast<int>(_animationLibrary.size()) - 1);
    _animationTrackIndex = -1;
    _animationKeyIndex = -1;
    markDirty(true);
    syncAnimationEditor();
    setStatus(L"Animation clip removed");
    return true;
}

bool LayoutEditorSession::addAnimationTrack(
    int clipIndex, UIAnimationProperty property) {
    stopAnimationPreview();
    if (_selected == nullptr || _selection.size() != 1u ||
        _selected->getId().empty()) {
        setStatus(L"Select one Widget with an ID before binding a track");
        return false;
    }
    UIAnimationLibrary updated = _animationLibrary;
    std::string error;
    const int trackIndex = updated.addTrack(
        clipIndex, _selected->getId(), property, &error);
    if (trackIndex < 0) {
        setStatus(utf8ToWide(error));
        return false;
    }
    pushUndo(LayoutEditKind::Animation, "Bind animation track");
    _animationLibrary = std::move(updated);
    _animationClipIndex = clipIndex;
    _animationTrackIndex = trackIndex;
    _animationKeyIndex = -1;
    markDirty(true);
    syncAnimationEditor();
    setStatus(L"Animation track bound to selected Widget");
    return true;
}

bool LayoutEditorSession::removeAnimationTrack(
    int clipIndex, int trackIndex) {
    stopAnimationPreview();
    UIAnimationLibrary updated = _animationLibrary;
    if (!updated.removeTrack(clipIndex, trackIndex)) return false;
    pushUndo(LayoutEditKind::Animation, "Remove animation track");
    _animationLibrary = std::move(updated);
    _animationClipIndex = clipIndex;
    const auto& tracks = _animationLibrary.clips()[
        static_cast<size_t>(clipIndex)].tracks;
    _animationTrackIndex = std::min(
        trackIndex, static_cast<int>(tracks.size()) - 1);
    _animationKeyIndex = -1;
    markDirty(true);
    syncAnimationEditor();
    setStatus(L"Animation track removed");
    return true;
}

bool LayoutEditorSession::captureAnimationKeyframe(
    int clipIndex, int trackIndex, float timeMs, AnimationCurve curve) {
    stopAnimationPreview();
    if (clipIndex < 0 ||
        clipIndex >= static_cast<int>(_animationLibrary.size())) return false;
    const auto& tracks = _animationLibrary.clips()[
        static_cast<size_t>(clipIndex)].tracks;
    if (trackIndex < 0 || trackIndex >= static_cast<int>(tracks.size()))
        return false;
    const UIAnimationTrack& track = tracks[static_cast<size_t>(trackIndex)];
    Widget* target = findInDocument(track.targetId);
    if (target == nullptr) {
        setStatus(L"Animation target no longer exists");
        return false;
    }

    UIAnimationKeyframe keyframe;
    keyframe.timeMs = timeMs;
    keyframe.curve = curve;
    keyframe.hasSpringParameters = curve == AnimationCurve::Spring;
    keyframe.hasBezierParameters = curve == AnimationCurve::CubicBezier;
    if (track.property == UIAnimationProperty::Opacity) {
        keyframe.value.x = target->getOpacity();
    } else if (track.property == UIAnimationProperty::Position) {
        keyframe.value.x = target->getPosition().x;
        keyframe.value.y = target->getPosition().y;
    } else {
        keyframe.value.x = target->getSize().x;
        keyframe.value.y = target->getSize().y;
    }

    UIAnimationLibrary updated = _animationLibrary;
    const int keyIndex = updated.upsertKeyframe(
        clipIndex, trackIndex, std::move(keyframe));
    if (keyIndex < 0) return false;
    pushUndo(LayoutEditKind::Animation, "Capture animation keyframe");
    _animationLibrary = std::move(updated);
    _animationClipIndex = clipIndex;
    _animationTrackIndex = trackIndex;
    _animationKeyIndex = keyIndex;
    markDirty(true);
    syncAnimationEditor();
    setStatus(L"Animation keyframe captured from the target Widget");
    return true;
}

bool LayoutEditorSession::setAnimationKeyframeCurve(
    int clipIndex, int trackIndex, int keyframeIndex, AnimationCurve curve,
    const CubicBezierParameters& bezier,
    const SpringParameters& spring) {
    stopAnimationPreview();
    UIAnimationLibrary updated = _animationLibrary;
    if (!updated.setKeyframeCurve(
            clipIndex, trackIndex, keyframeIndex, curve, bezier, spring)) {
        return false;
    }
    if (updated.serialize(false) == _animationLibrary.serialize(false)) {
        return false;
    }
    pushUndo(LayoutEditKind::Animation, "Edit animation curve");
    _animationLibrary = std::move(updated);
    _animationClipIndex = clipIndex;
    _animationTrackIndex = trackIndex;
    _animationKeyIndex = keyframeIndex;
    markDirty(true);
    syncAnimationEditor();
    setStatus(L"Animation curve parameters updated");
    return true;
}

bool LayoutEditorSession::removeAnimationKeyframe(
    int clipIndex, int trackIndex, int keyframeIndex) {
    stopAnimationPreview();
    UIAnimationLibrary updated = _animationLibrary;
    if (!updated.removeKeyframe(clipIndex, trackIndex, keyframeIndex))
        return false;
    pushUndo(LayoutEditKind::Animation, "Remove animation keyframe");
    _animationLibrary = std::move(updated);
    _animationClipIndex = clipIndex;
    _animationTrackIndex = trackIndex;
    const auto& keys = _animationLibrary.clips()[
        static_cast<size_t>(clipIndex)].tracks[
            static_cast<size_t>(trackIndex)].keyframes;
    _animationKeyIndex = std::min(
        keyframeIndex, static_cast<int>(keys.size()) - 1);
    markDirty(true);
    syncAnimationEditor();
    setStatus(L"Animation keyframe removed");
    return true;
}

bool LayoutEditorSession::setAnimationPlayback(
    int clipIndex, int repeatCount, bool yoyo,
    AnimationImportance importance) {
    stopAnimationPreview();
    UIAnimationLibrary updated = _animationLibrary;
    if (!updated.setPlayback(clipIndex, repeatCount, yoyo, importance))
        return false;
    const UIAnimationClip& before = _animationLibrary.clips()[
        static_cast<size_t>(clipIndex)];
    const UIAnimationClip& after = updated.clips()[
        static_cast<size_t>(clipIndex)];
    if (before.repeatCount == after.repeatCount && before.yoyo == after.yoyo &&
        before.importance == after.importance) return false;
    pushUndo(LayoutEditKind::Animation, "Edit animation playback");
    _animationLibrary = std::move(updated);
    markDirty(true);
    syncAnimationEditor();
    return true;
}

bool LayoutEditorSession::previewAnimationFrame(
    int clipIndex, float timeMs) {
    if (clipIndex < 0 || clipIndex >= static_cast<int>(_animationLibrary.size()) ||
        _animationLibrary.clips()[static_cast<size_t>(clipIndex)].tracks.empty()) {
        setStatus(L"Select an animation clip with at least one track");
        return false;
    }
    if (_animationPreviewBaseline.has_value() &&
        _animationPreviewClipIndex != clipIndex) {
        stopAnimationPreview();
    }
    if (!_animationPreviewBaseline.has_value() &&
        !captureAnimationPreviewBaseline(clipIndex)) {
        return false;
    }
    if (_animationRuntimePreview.has_value()) {
        _animationRuntimePreview->cancel();
        _animationRuntimePreview.reset();
    }
    _animationClipIndex = clipIndex;

    std::size_t unresolved = 0;
    AnimationTimeline timeline = _animationLibrary.createTimeline(
        clipIndex,
        [this](const std::string& id) { return findInDocument(id); },
        &unresolved);
    timeline.seek(timeMs);
    _animationPreviewTimeMs = timeline.getCurrentTimeMs();
    _animationRuntimePreview = std::move(timeline);
    relayoutAfterAnimationSample();
    if (_animationTimelineView != nullptr) {
        _animationTimelineView->setCurrentTimeMs(_animationPreviewTimeMs);
    }
    if (_animationTime != nullptr) {
        _animationTime->setText(formatFloat(_animationPreviewTimeMs));
    }
    setChromeEnabled("btn_animation_reset", true);
    setStatus(L"Animation scrub preview at " +
              std::to_wstring(static_cast<int>(
                  std::lround(_animationPreviewTimeMs))) +
              L" ms" + (unresolved > 0u ? L" (missing tracks skipped)" : L""));
    return true;
}

void LayoutEditorSession::previewAnimationKeyframeDrag(float timeMs) {
    if (!_animationKeyDragActive || !_animationRuntimePreview.has_value() ||
        _animationKeyDragClip < 0 ||
        _animationKeyDragClip >= static_cast<int>(_animationLibrary.size())) {
        return;
    }
    const UIAnimationClip& clip = _animationLibrary.clips()[
        static_cast<size_t>(_animationKeyDragClip)];
    if (_animationKeyDragTrack < 0 ||
        _animationKeyDragTrack >= static_cast<int>(clip.tracks.size())) {
        return;
    }
    const UIAnimationTrack& track = clip.tracks[
        static_cast<size_t>(_animationKeyDragTrack)];
    if (_animationKeyDragKey < 0 ||
        _animationKeyDragKey >= static_cast<int>(track.keyframes.size())) {
        return;
    }

    const float nextTime = std::clamp(
        std::isfinite(timeMs) ? timeMs : 0.0f,
        0.0f, animationDisplayDurationMs(&clip));
    // Other tracks are unchanged during a one-key retime, so the evaluator
    // created at drag begin remains valid for them. At the moved key's exact
    // time, this track must equal the key value; apply it directly after the
    // shared evaluator samples the other tracks.
    _animationRuntimePreview->seek(nextTime);
    _animationPreviewTimeMs = nextTime;
    const UIAnimationKeyframe& key = track.keyframes[
        static_cast<size_t>(_animationKeyDragKey)];
    if (Widget* target = findInDocument(track.targetId)) {
        if (track.property == UIAnimationProperty::Opacity) {
            target->setOpacity(key.value.x);
        } else if (track.property == UIAnimationProperty::Position) {
            target->setPosition({key.value.x, key.value.y});
        } else {
            target->setSize({key.value.x, key.value.y});
        }
    }
    relayoutAfterAnimationSample();
    if (_animationTimelineView != nullptr) {
        _animationTimelineView->setCurrentTimeMs(_animationPreviewTimeMs);
    }
    if (_animationTime != nullptr) {
        _animationTime->setText(formatFloat(_animationPreviewTimeMs));
    }
    setChromeEnabled("btn_animation_reset", true);
    syncAnimationTransportStatus();
}

bool LayoutEditorSession::captureAnimationPreviewBaseline(int clipIndex) {
    if (clipIndex < 0 || clipIndex >= static_cast<int>(_animationLibrary.size()))
        return false;
    std::vector<AnimationPreviewValue> values;
    for (const UIAnimationTrack& track :
         _animationLibrary.clips()[static_cast<size_t>(clipIndex)].tracks) {
        Widget* target = findInDocument(track.targetId);
        if (target == nullptr) continue;
        AnimationPreviewValue value;
        value.targetId = track.targetId;
        value.property = track.property;
        if (track.property == UIAnimationProperty::Opacity) {
            value.value.x = target->getOpacity();
        } else if (track.property == UIAnimationProperty::Position) {
            value.value.x = target->getPosition().x;
            value.value.y = target->getPosition().y;
        } else {
            value.value.x = target->getSize().x;
            value.value.y = target->getSize().y;
        }
        values.push_back(std::move(value));
    }
    _animationPreviewBaseline = std::move(values);
    _animationPreviewClipIndex = clipIndex;
    _animationPreviewTimeMs = 0.0f;
    return true;
}

void LayoutEditorSession::relayoutAfterAnimationSample() {
    if (_docRoot != nullptr) _docRoot->performLayout();
    if (_ui != nullptr) {
        _ui->invalidateLayout();
        _ui->layout();
    }
    syncSelectionChrome();
}

bool LayoutEditorSession::playAnimationPreview(int clipIndex) {
    if (clipIndex < 0 || clipIndex >= static_cast<int>(_animationLibrary.size()) ||
        _animationLibrary.clips()[static_cast<size_t>(clipIndex)].tracks.empty()) {
        setStatus(L"Select an animation clip with at least one track");
        return false;
    }
    if (_animationRuntimePreview.has_value() &&
        _animationPreviewClipIndex == clipIndex &&
        _animationRuntimePreview->isPaused()) {
        _animationRuntimePreview->resume();
        syncAnimationEditor();
        setStatus(L"Animation preview resumed");
        return true;
    }
    if (_animationPreviewBaseline.has_value() &&
        _animationPreviewClipIndex != clipIndex) {
        stopAnimationPreview();
    }
    if (!_animationPreviewBaseline.has_value() &&
        !captureAnimationPreviewBaseline(clipIndex)) {
        return false;
    }
    if (_animationRuntimePreview.has_value()) {
        _animationRuntimePreview->cancel();
    }
    std::size_t unresolved = 0;
    AnimationTimeline timeline = _animationLibrary.createTimeline(
        clipIndex,
        [this](const std::string& id) { return findInDocument(id); },
        &unresolved);
    if (_animationPreviewLoop) {
        timeline.setRepeatCount(AnimationTimeline::RepeatForever);
    }
    timeline.play();
    _animationRuntimePreview = std::move(timeline);
    _animationPreviewClipIndex = clipIndex;
    _animationPreviewTimeMs = _animationRuntimePreview->getCurrentTimeMs();
    relayoutAfterAnimationSample();
    syncAnimationEditor();
    setStatus(std::wstring(L"Animation preview playing") +
              (unresolved > 0u ? L" (missing tracks skipped)" : L""));
    return true;
}

void LayoutEditorSession::pauseAnimationPreview() {
    if (!_animationRuntimePreview.has_value() ||
        _animationRuntimePreview->getState() !=
            AnimationPlaybackState::Running) return;
    _animationRuntimePreview->pause();
    syncAnimationEditor();
    setStatus(L"Animation preview paused");
}

void LayoutEditorSession::stopAnimationPreview() {
    if (!_animationPreviewBaseline.has_value()) return;
    if (_animationRuntimePreview.has_value()) {
        _animationRuntimePreview->cancel();
        _animationRuntimePreview.reset();
    }
    std::vector<AnimationPreviewValue> baseline =
        std::move(*_animationPreviewBaseline);
    _animationPreviewBaseline.reset();
    for (const AnimationPreviewValue& value : baseline) {
        Widget* target = findInDocument(value.targetId);
        if (target == nullptr) continue;
        if (value.property == UIAnimationProperty::Opacity) {
            target->setOpacity(value.value.x);
        } else if (value.property == UIAnimationProperty::Position) {
            target->setPosition({value.value.x, value.value.y});
        } else {
            target->setSize({value.value.x, value.value.y});
        }
    }
    _animationPreviewClipIndex = -1;
    _animationPreviewTimeMs = 0.0f;
    relayoutAfterAnimationSample();
    if (_animationTimelineView != nullptr) {
        _animationTimelineView->setCurrentTimeMs(0.0f);
    }
    syncAnimationEditor();
    setStatus(L"Animation preview reset to authored values");
}

void LayoutEditorSession::advanceAnimationPreview(float deltaSeconds) {
    if (!_animationRuntimePreview.has_value() ||
        _animationRuntimePreview->getState() !=
            AnimationPlaybackState::Running ||
        !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) return;
    const AnimationPlaybackState before =
        _animationRuntimePreview->getState();
    _animationRuntimePreview->tick(deltaSeconds);
    _animationPreviewTimeMs = _animationRuntimePreview->getCurrentTimeMs();
    relayoutAfterAnimationSample();
    if (_animationTimelineView != nullptr) {
        _animationTimelineView->setCurrentTimeMs(_animationPreviewTimeMs);
    }
    syncAnimationTransportStatus();
    if (before != AnimationPlaybackState::Completed &&
        _animationRuntimePreview->getState() ==
            AnimationPlaybackState::Completed) {
        syncAnimationEditor();
        setStatus(L"Animation preview completed");
    }
}

void LayoutEditorSession::setAnimationPreviewLoop(bool enabled) {
    if (_animationPreviewLoop == enabled) return;
    _animationPreviewLoop = enabled;
    if (_animationRuntimePreview.has_value() &&
        _animationRuntimePreview->isRunning()) {
        int authoredRepeatCount = 0;
        if (_animationPreviewClipIndex >= 0 &&
            _animationPreviewClipIndex < static_cast<int>(
                _animationLibrary.size())) {
            authoredRepeatCount = _animationLibrary.clips()[
                static_cast<size_t>(_animationPreviewClipIndex)].repeatCount;
        }
        _animationRuntimePreview->setRepeatCount(
            enabled ? AnimationTimeline::RepeatForever
                    : authoredRepeatCount);
    }
    if (auto* button = dynamic_cast<Button*>(
            findChromeById("btn_animation_loop"))) {
        button->setText(enabled
            ? localizedText("ui.editor.ui_designer.loop_on", L"Loop: On")
            : localizedText("ui.editor.ui_designer.loop_off", L"Loop: Off"));
    }
    syncAnimationEditor();
}

bool LayoutEditorSession::isAnimationPreviewPlaying() const {
    return _animationRuntimePreview.has_value() &&
        _animationRuntimePreview->getState() == AnimationPlaybackState::Running;
}

bool LayoutEditorSession::isAnimationPreviewPaused() const {
    return _animationRuntimePreview.has_value() &&
        _animationRuntimePreview->getState() == AnimationPlaybackState::Paused;
}

bool LayoutEditorSession::beginAnimationKeyframeDrag(
    int clipIndex, int trackIndex, int keyframeIndex) {
    if (_animationKeyDragActive || clipIndex < 0 ||
        clipIndex >= static_cast<int>(_animationLibrary.size())) return false;
    const auto& tracks = _animationLibrary.clips()[
        static_cast<size_t>(clipIndex)].tracks;
    if (trackIndex < 0 || trackIndex >= static_cast<int>(tracks.size()) ||
        keyframeIndex < 0 || keyframeIndex >= static_cast<int>(
            tracks[static_cast<size_t>(trackIndex)].keyframes.size())) {
        return false;
    }
    stopAnimationPreview();
    Snapshot before = captureSnapshot();
    _animationKeyDragSnapshot = before;
    _commandStack.begin(std::move(before), LayoutEditKind::Animation,
                        "Move animation keyframe");
    _animationKeyDragActive = true;
    _animationKeyDragChanged = false;
    _animationKeyDragClip = clipIndex;
    _animationKeyDragTrack = trackIndex;
    _animationKeyDragKey = keyframeIndex;
    _animationKeyDragOriginalKey = keyframeIndex;
    _animationClipIndex = clipIndex;
    _animationTrackIndex = trackIndex;
    _animationKeyIndex = keyframeIndex;
    (void)previewAnimationFrame(
        clipIndex,
        tracks[static_cast<size_t>(trackIndex)].keyframes[
            static_cast<size_t>(keyframeIndex)].timeMs);
    return true;
}

int LayoutEditorSession::updateAnimationKeyframeDrag(float timeMs) {
    if (!_animationKeyDragActive) return -1;
    const auto& keys = _animationLibrary.clips()[
        static_cast<size_t>(_animationKeyDragClip)].tracks[
            static_cast<size_t>(_animationKeyDragTrack)].keyframes;
    if (_animationKeyDragKey < 0 ||
        _animationKeyDragKey >= static_cast<int>(keys.size())) return -1;
    const float nextTime = std::max(0.0f,
        std::isfinite(timeMs) ? std::round(timeMs) : 0.0f);
    if (std::fabs(keys[static_cast<size_t>(_animationKeyDragKey)].timeMs -
                  nextTime) < 0.001f) {
        return _animationKeyDragKey;
    }
    const int moved = _animationLibrary.moveKeyframeTime(
        _animationKeyDragClip, _animationKeyDragTrack,
        _animationKeyDragKey, nextTime);
    if (moved < 0) return -1;
    _animationKeyDragKey = moved;
    _animationKeyIndex = moved;
    _animationKeyDragChanged = true;
    markDirty(true);
    syncAnimationTimelineTrack(_animationKeyDragTrack);
    previewAnimationKeyframeDrag(nextTime);
    return moved;
}

void LayoutEditorSession::endAnimationKeyframeDrag() {
    if (!_animationKeyDragActive) return;
    if (_animationKeyDragChanged) {
        endMutation();
        (void)previewAnimationFrame(
            _animationKeyDragClip, _animationPreviewTimeMs);
        syncAnimationEditor();
        setStatus(L"Animation keyframe moved");
    } else {
        _commandStack.discardLastUndo();
    }
    _animationKeyDragActive = false;
    _animationKeyDragChanged = false;
    _animationKeyDragSnapshot.reset();
    _animationKeyDragClip = -1;
    _animationKeyDragTrack = -1;
    _animationKeyDragKey = -1;
    _animationKeyDragOriginalKey = -1;
}

void LayoutEditorSession::cancelAnimationKeyframeDrag() {
    if (!_animationKeyDragActive) return;

    const bool changed = _animationKeyDragChanged;
    const int clipIndex = _animationKeyDragClip;
    const int trackIndex = _animationKeyDragTrack;
    const int keyIndex = _animationKeyDragOriginalKey;
    std::optional<Snapshot> rollback = std::move(_animationKeyDragSnapshot);

    if (_animationPreviewBaseline.has_value()) stopAnimationPreview();
    _commandStack.discardLastUndo();
    _animationKeyDragActive = false;
    _animationKeyDragChanged = false;
    _animationKeyDragSnapshot.reset();
    _animationKeyDragClip = -1;
    _animationKeyDragTrack = -1;
    _animationKeyDragKey = -1;
    _animationKeyDragOriginalKey = -1;

    if (changed && rollback.has_value()) {
        restoreSnapshot(*rollback);
        _animationClipIndex = clipIndex;
        _animationTrackIndex = trackIndex;
        _animationKeyIndex = keyIndex;
    }
    syncAnimationEditor();
    setStatus(L"Animation keyframe move cancelled");
}

void LayoutEditorSession::refreshTextureResources() {
    std::vector<LayoutTextureResource> entries;
    if (_textureResourceProvider) {
        try {
            entries = _textureResourceProvider();
        } catch (...) {
            setStatus(L"Texture resource scan failed");
        }
    }
    _textureCatalog.setEntries(std::move(entries));
    rehydrateRuntimePresentation(_docRoot);
    syncTextureBrowser();
    refreshValidation();
}

void LayoutEditorSession::setInteractionContracts(
    std::vector<LayoutControllerContract> controllers) {
    _interactionRegistry.replace(std::move(controllers));
    _interactionContractsConfigured = true;
    refreshValidation();
}

void LayoutEditorSession::clearInteractionContracts() {
    _interactionRegistry.clear();
    _interactionContractsConfigured = false;
    refreshValidation();
}

void LayoutEditorSession::setInteractionContractProvider(
    InteractionContractProvider provider) {
    _interactionContractProvider = std::move(provider);
    if (_interactionContractProvider) {
        refreshInteractionContracts();
    } else {
        clearInteractionContracts();
    }
}

void LayoutEditorSession::refreshInteractionContracts() {
    if (!_interactionContractProvider) {
        refreshValidation();
        return;
    }
    try {
        setInteractionContracts(_interactionContractProvider());
    } catch (...) {
        setStatus(L"Controller contract refresh failed");
    }
}

void LayoutEditorSession::bindPropField(const char* id, const char* field,
                                        TextInput*& slot, bool numericScrub) {
    slot = dynamic_cast<TextInput*>(findChromeById(id));
    if (slot == nullptr) {
        return;
    }
    const std::string fieldName(field);
    TextInput* ti = slot;

    ti->setOnSubmit([this, fieldName, ti](const std::wstring& /*value*/) {
        commitPropField(fieldName, ti);
    });
    ti->setOnFocusLostNotify([this, fieldName, ti]() {
        commitPropField(fieldName, ti);
        endPropertyMutation();
    });
    ti->setOnTextChanged({});

    if (numericScrub) {
        ti->setNumericScrubEnabled(true);
        ti->setOnNumericScrub([this, fieldName, ti](double value) {
            if (_suppressProp) {
                return;
            }
            float v = static_cast<float>(value);
            if (const PropertyFieldSchema* schema =
                    findPropertyFieldSchema(fieldName)) {
                if (schema->hasRange) {
                    v = std::clamp(v, schema->minimum, schema->maximum);
                }
                if (schema->editorKind == PropertyEditorKind::Integer) {
                    v = std::round(v);
                }
            }
            beginPropertyMutation(fieldName);
            applyProperty(fieldName, formatFloat(v));
            _suppressProp = true;
            ti->setText(formatFloat(v));
            _suppressProp = false;
        });
    }
}

void LayoutEditorSession::commitPropField(const std::string& field,
                                          TextInput* slot) {
    if (_suppressProp || slot == nullptr) {
        return;
    }
    if (field == "id") {
        const std::string candidate = wideToUtf8(trimWide(slot->getText()));
        std::wstring reason;
        if (!validateWidgetId(candidate, _selected, &reason)) {
            setStatus(L"ID rejected — " + reason);
            _suppressProp = true;
            slot->setText(_selected != nullptr
                ? utf8ToWide(_selected->getId()) : std::wstring{});
            _suppressProp = false;
            return;
        }
        if (_selected != nullptr && candidate == _selected->getId()) {
            return;
        }
    }
    beginPropertyMutation(field);
    applyProperty(field, slot->getText());
}

bool LayoutEditorSession::validateWidgetId(const std::string& candidate,
                                           Widget* edited,
                                           std::wstring* reason) const {
    return _documentModel.validateId(candidate, edited, reason);
}

void LayoutEditorSession::wireChrome() {
    if (_ui == nullptr) {
        return;
    }

    auto bindBtn = [this](const char* id, std::function<void()> fn) {
        if (auto* b = dynamic_cast<Button*>(findChromeById(id))) {
            b->setOnClicked(std::move(fn));
        }
    };

    auto bindMenuItem = [this](const char* menuKey,
                               const char* itemKey,
                               std::function<void()> fn) {
        auto* bar = dynamic_cast<MenuBar*>(findChromeById("designer_menubar"));
        if (bar == nullptr) return false;
        for (size_t menuIndex = 0; menuIndex < bar->getMenuCount(); ++menuIndex) {
            Menu* menu = bar->getMenu(menuIndex);
            if (menu == nullptr
                || menu->getLocalizationKey("title") != menuKey) continue;
            for (size_t itemIndex = 0; itemIndex < menu->getItemCount(); ++itemIndex) {
                MenuItem* item = menu->getItem(itemIndex);
                if (item != nullptr
                    && item->getLocalizationKey("text") == itemKey) {
                    item->setOnActivate(std::move(fn));
                    return true;
                }
            }
        }
        return false;
    };

    if (auto* bar = dynamic_cast<MenuBar*>(findChromeById("designer_menubar"))) {
        for (Widget* child : bar->getChildren()) {
            if (auto* anchor = dynamic_cast<Button*>(child)) {
                anchor->setStyleId("__le_menu_anchor");
                anchor->setPadding(10.0f, 4.0f, 10.0f, 4.0f);
            }
        }
    }

    bindBtn("btn_open", [this]() { _deferred = DeferredAction::Open; });
    bindBtn("btn_save", [this]() { _deferred = DeferredAction::Save; });
    bindBtn("btn_save_as", [this]() { _deferred = DeferredAction::SaveAs; });
    bindBtn("btn_undo", [this]() { undo(); });
    bindBtn("btn_redo", [this]() { redo(); });
    bindBtn("btn_delete", [this]() { deleteSelected(); });

    bindMenuItem("ui.editor.ui_designer.menu.file",
                 "ui.editor.ui_designer.action.open",
                 [this]() { _deferred = DeferredAction::Open; });
    bindMenuItem("ui.editor.ui_designer.menu.file",
                 "ui.editor.ui_designer.action.save",
                 [this]() { _deferred = DeferredAction::Save; });
    bindMenuItem("ui.editor.ui_designer.menu.file",
                 "ui.editor.ui_designer.action.save_as",
                 [this]() { _deferred = DeferredAction::SaveAs; });
    bindMenuItem("ui.editor.ui_designer.menu.edit",
                 "ui.editor.ui_designer.action.undo", [this]() { undo(); });
    bindMenuItem("ui.editor.ui_designer.menu.edit",
                 "ui.editor.ui_designer.action.redo", [this]() { redo(); });
    bindMenuItem("ui.editor.ui_designer.menu.edit",
                 "ui.editor.ui_designer.action.copy", [this]() { copySelection(); });
    bindMenuItem("ui.editor.ui_designer.menu.edit",
                 "ui.editor.ui_designer.action.paste", [this]() { pasteClipboard(); });
    bindMenuItem("ui.editor.ui_designer.menu.edit",
                 "ui.editor.ui_designer.action.duplicate", [this]() { duplicateSelection(); });
    bindMenuItem("ui.editor.ui_designer.menu.edit",
                 "ui.editor.ui_designer.action.delete", [this]() { deleteSelected(); });
    bindMenuItem("ui.editor.ui_designer.menu.edit",
                 "ui.editor.ui_designer.action.select_all", [this]() { selectAll(); });
    bindMenuItem("ui.editor.ui_designer.menu.edit",
                 "ui.editor.ui_designer.action.move_up", [this]() { reorderSelected(-1); });
    bindMenuItem("ui.editor.ui_designer.menu.edit",
                 "ui.editor.ui_designer.action.move_down", [this]() { reorderSelected(1); });
    bindMenuItem("ui.editor.ui_designer.menu.view",
                 "ui.editor.ui_designer.action.interact_preview", [this]() {
        setMode(_mode == Mode::Edit ? Mode::Interact : Mode::Edit);
    });
    bindMenuItem("ui.editor.ui_designer.menu.view",
                 "ui.editor.ui_designer.action.safe_area", [this]() {
        setSafeAreaVisible(!_previewModel.settings().showSafeArea);
    });
    bindMenuItem("ui.editor.ui_designer.menu.view",
                 "ui.editor.ui_designer.action.validate_layout", [this]() {
        refreshValidation();
        setStatus(_validationModel.hasErrors()
            ? L"Validation completed with errors"
            : L"Validation completed");
    });
    auto runProjectWorkflow = [this](const ProjectWorkflowAction& action) {
        if (action == nullptr) {
            setStatus(L"This workflow action requires an editor project host");
            return;
        }
        if (_documentPath.empty()) {
            setStatus(L"Save the UI Layout before using project workflow actions");
            return;
        }
        std::string message;
        const bool ok = action(_documentPath, message);
        setStatus(utf8ToWide((ok ? std::string{} : "Workflow failed — ")
            + (message.empty() ? std::string("No details") : message)));
    };
    bindMenuItem("ui.editor.ui_designer.menu.workflow",
                 "ui.editor.ui_designer.action.open_owning_screen", [this, runProjectWorkflow]() {
        runProjectWorkflow(_openOwningFlowAction);
    });
    bindMenuItem("ui.editor.ui_designer.menu.workflow",
                 "ui.editor.ui_designer.action.complete_flow_signals", [this, runProjectWorkflow]() {
        runProjectWorkflow(_completeFlowSignalsAction);
    });
    bindMenuItem("ui.editor.ui_designer.menu.workflow",
                 "ui.editor.ui_designer.action.safe_rename", [this]() {
        revealProjectRefactorEditor();
    });

    // Palette buttons are driven by onPointer* (click + drag-create).
    // Keep click handlers as a fallback if pointer routing misses them.
    for (const WidgetAuthoringDescriptor& entry :
         WidgetAuthoringRegistry::get().descriptors()) {
        if (entry.paletteButtonId.empty()) continue;
        auto* button = dynamic_cast<Button*>(
            findChromeById(entry.paletteButtonId));
        if (button == nullptr) continue;
        const std::string typeName = entry.typeName;
        button->setOnClicked([this, typeName]() { addWidget(typeName); });
        button->setIconDocument(entry.icon);
        button->setIconSize(17.0f);
        button->setIconGap(10.0f);
        button->setIconColor(math::FVector4(0.50f, 0.74f, 1.0f, 1.0f));
        button->setPadding(12.0f, 6.0f, 10.0f, 6.0f);
    }

    bindBtn("btn_pick_texture", [this]() { chooseTexture(); });
    bindBtn("btn_clear_texture", [this]() { clearTexture(); });

    bindBtn("btn_align_left", [this]() { alignSelection(AlignMode::Left); });
    bindBtn("btn_align_hcenter", [this]() { alignSelection(AlignMode::HCenter); });
    bindBtn("btn_align_right", [this]() { alignSelection(AlignMode::Right); });
    bindBtn("btn_align_top", [this]() { alignSelection(AlignMode::Top); });
    bindBtn("btn_align_vcenter", [this]() { alignSelection(AlignMode::VCenter); });
    bindBtn("btn_align_bottom", [this]() { alignSelection(AlignMode::Bottom); });
    bindBtn("btn_dist_h", [this]() { alignSelection(AlignMode::DistributeH); });
    bindBtn("btn_dist_v", [this]() { alignSelection(AlignMode::DistributeV); });
    for (int vertical = 0; vertical < 4; ++vertical) {
        for (int horizontal = 0; horizontal < 4; ++horizontal) {
            const std::string id = "btn_anchor_" +
                std::to_string(vertical) + std::to_string(horizontal);
            bindBtn(id.c_str(), [this, horizontal, vertical]() {
                const bool snapWidget = _ui != nullptr &&
                    (_ui->getModifiers() &
                     (1u << (UIKey_Control - UIKey_Shift))) != 0u;
                setAnchorPreset(
                    static_cast<AnchorAxisMode>(horizontal),
                    static_cast<AnchorAxisMode>(vertical), snapWidget);
            });
        }
    }
    bindBtn("btn_anchor_none", [this]() { clearSelectedAnchors(); });
    bindBtn("btn_move_up", [this]() { reorderSelected(-1); });
    bindBtn("btn_move_down", [this]() { reorderSelected(1); });
    bindBtn("btn_snap", [this]() { toggleSnap(); });

    _hierarchyCol = findChromeById("hierarchy_col");
    if (_chromeRoot == nullptr) {
        _chromeRoot = findChromeById("layout_editor_root");
    }
    _hierarchy = dynamic_cast<ListView*>(findChromeById("list_hierarchy"));
    _hierarchySearch = dynamic_cast<TextInput*>(
        findChromeById("hierarchy_search"));
    _hierarchySummary = dynamic_cast<TextLabel*>(
        findChromeById("lbl_hierarchy_summary"));
    if (_hierarchySearch != nullptr) {
        _hierarchySearch->setOnTextChanged([this](const std::wstring&) {
            refreshHierarchy();
        });
    }
    bindBtn("btn_hierarchy_search_clear", [this]() {
        if (_hierarchySearch != nullptr) _hierarchySearch->setText(L"");
        if (_hierarchySearch != nullptr && _ui != nullptr) {
            _ui->setFocus(_hierarchySearch);
        }
    });
    if (_hierarchy == nullptr) {
        std::fprintf(stderr,
            "[LayoutEditorSession] list_hierarchy not found\n");
        setStatus(L"DBG: list_hierarchy NOT FOUND");
    } else {
        _hierarchy->setSelectionMode(ListView::SelectionMode::Single);
        _hierarchy->setOnSelectionChanged([this](int index) {
            if (_suppressHierarchy || index < 0 ||
                index >= static_cast<int>(_hierarchyIndex.size())) {
                return;
            }
            // Honor Ctrl/Shift so orphaned ListView mouse-ups stay consistent
            // with onPointerDown's additive path.
            select(_hierarchyIndex[static_cast<size_t>(index)],
                   modifiersAdditive());
        });
    }
    bindPropField("prop_id", "id", _propId, false);
    bindPropField("prop_x", "x", _propX, true);
    bindPropField("prop_y", "y", _propY, true);
    bindPropField("prop_w", "w", _propW, true);
    bindPropField("prop_h", "h", _propH, true);
    bindPropField("prop_anchor_min_x", "anchorMinX", _propAnchorMinX, true);
    bindPropField("prop_anchor_min_y", "anchorMinY", _propAnchorMinY, true);
    bindPropField("prop_anchor_max_x", "anchorMaxX", _propAnchorMaxX, true);
    bindPropField("prop_anchor_max_y", "anchorMaxY", _propAnchorMaxY, true);
    bindPropField("prop_offset_min_x", "offsetMinX", _propOffsetMinX, true);
    bindPropField("prop_offset_min_y", "offsetMinY", _propOffsetMinY, true);
    bindPropField("prop_offset_max_x", "offsetMaxX", _propOffsetMaxX, true);
    bindPropField("prop_offset_max_y", "offsetMaxY", _propOffsetMaxY, true);
    bindPropField("prop_pivot_x", "pivotX", _propPivotX, true);
    bindPropField("prop_pivot_y", "pivotY", _propPivotY, true);
    bindPropField("prop_text", "text", _propText, false);
    bindPropField("prop_texture", "texture", _propTexture, false);
    bindPropField("prop_items", "items", _propItems, false);
    bindPropField("prop_controller", "controller", _propController, false);
    bindPropField("prop_on_click", "event:onClick", _propOnClick, false);
    bindPropField("prop_on_toggled", "event:onToggled", _propOnToggled, false);
    bindPropField("prop_on_value_changed", "event:onValueChanged",
                  _propOnValueChanged, false);
    bindPropField("prop_on_text_changed", "event:onTextChanged",
                  _propOnTextChanged, false);
    bindPropField("prop_on_submit", "event:onSubmit", _propOnSubmit, false);
    bindPropField("prop_on_selection_changed", "event:onSelectionChanged",
                  _propOnSelectionChanged, false);
    bindPropField("prop_on_item_activated", "event:onItemActivated",
                  _propOnItemActivated, false);
    bindPropField("prop_on_close", "event:onClose", _propOnClose, false);
    bindPropField("prop_spacing", "spacing", _propSpacing, true);
    bindPropField("prop_pad_l", "padL", _propPadL, true);
    bindPropField("prop_pad_t", "padT", _propPadT, true);
    bindPropField("prop_pad_r", "padR", _propPadR, true);
    bindPropField("prop_pad_b", "padB", _propPadB, true);

    _propStyleCombo = dynamic_cast<ComboBox*>(
        findChromeById("prop_style_combo"));
    if (_propStyleCombo != nullptr) {
        _propStyleCombo->setOnSelectionChanged([this](int index) {
            if (_suppressStyleCombo || _suppressProp || index < 0 ||
                index >= static_cast<int>(_styleIds.size())) {
                return;
            }
            beginPropertyMutation("style");
            applyProperty("style", utf8ToWide(_styleIds[static_cast<size_t>(index)]));
            endPropertyMutation();
        });
        syncStyleCombo();
    }

    _stylePreviewStateCombo = dynamic_cast<ComboBox*>(
        findChromeById("style_preview_state"));
    _stylePreviewSwatch = dynamic_cast<Panel*>(
        findChromeById("style_preview_swatch"));
    _styleSourceStatus = dynamic_cast<TextLabel*>(
        findChromeById("style_source_status"));
    _styleColorStatus = dynamic_cast<TextLabel*>(
        findChromeById("style_color_status"));
    if (_stylePreviewStateCombo != nullptr) {
        _stylePreviewStateCombo->setItems(
            {L"Normal", L"Hovered", L"Pressed", L"Disabled"});
        _stylePreviewStateCombo->setOnSelectionChanged([this](int index) {
            if (_suppressProp || index < 0 || index > 3) return;
            _stylePreviewState = static_cast<StyleState>(index);
            syncStyleInspector();
        });
    }
    bindBtn("btn_reset_style", [this]() {
        if (_selected == nullptr) return;
        beginPropertyMutation("style");
        applyProperty("style", L"");
        endPropertyMutation();
        syncPropertyStrip();
    });

    _propTextHAlign = dynamic_cast<ComboBox*>(
        findChromeById("prop_text_halign"));
    _propTextVAlign = dynamic_cast<ComboBox*>(
        findChromeById("prop_text_valign"));
    if (_propTextHAlign != nullptr) {
        _propTextHAlign->setItems(
            propertyOptionLabels(AuthoringProperty::TextHAlign));
        _propTextHAlign->setOnSelectionChanged([this](int index) {
            if (_suppressProp || index < 0 || index > 2) {
                return;
            }
            const PropertyFieldSchema& schema =
                propertyFieldSchema(AuthoringProperty::TextHAlign);
            beginPropertyMutation("hAlign");
            applyProperty("hAlign", utf8ToWide(
                schema.enumOptions[static_cast<size_t>(index)]));
            endPropertyMutation();
        });
    }
    if (_propTextVAlign != nullptr) {
        _propTextVAlign->setItems(
            propertyOptionLabels(AuthoringProperty::TextVAlign));
        _propTextVAlign->setOnSelectionChanged([this](int index) {
            if (_suppressProp || index < 0 || index > 2) {
                return;
            }
            const PropertyFieldSchema& schema =
                propertyFieldSchema(AuthoringProperty::TextVAlign);
            beginPropertyMutation("vAlign");
            applyProperty("vAlign", utf8ToWide(
                schema.enumOptions[static_cast<size_t>(index)]));
            endPropertyMutation();
        });
    }

    bindBoolCombo(_propChecked, "prop_checked", "checked");
    bindBoolCombo(_propPassword, "prop_password", "password");
    bindBoolCombo(_propReadOnly, "prop_readonly", "readOnly");
    bindGravityCombo();
    bindSchemaPropertyFields();

    _structuredList = dynamic_cast<ListView*>(
        findChromeById("structured_items"));
    if (_structuredList != nullptr) {
        _structuredList->setSelectionMode(ListView::SelectionMode::Single);
        _structuredList->setOnSelectionChanged([this](int index) {
            if (_suppressStructured) return;
            _structuredModel.setSelectedIndex(index);
            syncStructuredEditor();
        });
    }
    _structuredText = dynamic_cast<TextInput*>(
        findChromeById("structured_text"));
    _structuredFontSize = dynamic_cast<TextInput*>(
        findChromeById("structured_font_size"));
    _structuredColor = dynamic_cast<TextInput*>(
        findChromeById("structured_color"));
    for (TextInput* field : {_structuredText, _structuredFontSize,
                             _structuredColor}) {
        if (field == nullptr) continue;
        field->setOnSubmit([this](const std::wstring&) {
            if (_structuredModel.kind() == LayoutStructuredKind::RichText)
                commitStructuredRichStyle();
            else commitStructuredText();
        });
        field->setOnFocusLostNotify([this]() {
            if (_suppressStructured) return;
            if (_structuredModel.kind() == LayoutStructuredKind::RichText)
                commitStructuredRichStyle();
            else commitStructuredText();
        });
    }
    bindBtn("btn_structured_add", [this]() { structuredAdd(false); });
    bindBtn("btn_structured_child", [this]() { structuredAdd(true); });
    bindBtn("btn_structured_remove", [this]() { structuredRemove(); });
    bindBtn("btn_structured_up", [this]() { structuredMove(-1); });
    bindBtn("btn_structured_down", [this]() { structuredMove(1); });
    auto toggleRunFlag = [this](int flag) {
        const int index = _structuredModel.selectedIndex();
        if (_structuredModel.kind() != LayoutStructuredKind::RichText ||
            index < 0 || index >= static_cast<int>(
                _structuredModel.entries().size())) return;
        RichRun run = _structuredModel.entries()[static_cast<size_t>(index)].richRun;
        if (flag == 0) run.bold = !run.bold;
        else if (flag == 1) run.italic = !run.italic;
        else run.underline = !run.underline;
        pushUndo(LayoutEditKind::Property, "Toggle rich-text run style");
        if (_structuredModel.setSelectedRichRun(run)) markDirty(true);
        syncStructuredEditor();
    };
    bindBtn("btn_structured_bold", [toggleRunFlag]() mutable { toggleRunFlag(0); });
    bindBtn("btn_structured_italic", [toggleRunFlag]() mutable { toggleRunFlag(1); });
    bindBtn("btn_structured_underline", [toggleRunFlag]() mutable { toggleRunFlag(2); });

    _textureResourceList = dynamic_cast<ListView*>(
        findChromeById("texture_resource_list"));
    if (_textureResourceList != nullptr) {
        _textureResourceList->setSelectionMode(ListView::SelectionMode::Single);
        _textureResourceList->setOnSelectionChanged([this](int index) {
            if (_suppressTextureResources || index < 0) return;
            const LayoutTextureResource* resource =
                _textureCatalog.visibleEntry(static_cast<size_t>(index));
            if (resource == nullptr || dynamic_cast<Image*>(_selected) == nullptr)
                return;
            beginPropertyMutation("texture", "Assign texture resource");
            applyTextureName(resource->key);
            endPropertyMutation();
            syncPropertyStrip();
        });
    }
    _textureSearch = dynamic_cast<TextInput*>(
        findChromeById("texture_resource_search"));
    if (_textureSearch != nullptr) {
        _textureSearch->setOnTextChanged([this](const std::wstring& value) {
            if (_suppressTextureResources) return;
            _textureCatalog.setFilter(value);
            syncTextureBrowser();
        });
    }
    _textureStatus = dynamic_cast<TextLabel*>(
        findChromeById("texture_resource_status"));
    bindBtn("btn_refresh_textures", [this]() { refreshTextureResources(); });

    _validationList = dynamic_cast<ListView*>(
        findChromeById("validation_list"));
    if (_validationList != nullptr) {
        _validationLabels.clear();
        _validationList->setSelectionMode(ListView::SelectionMode::Single);
        _validationList->setOnSelectionChanged([this](int index) {
            if (_suppressValidation || index < 0 ||
                index >= static_cast<int>(_validationModel.diagnostics().size())) {
                return;
            }
            Widget* widget = _validationModel.diagnostics()[
                static_cast<size_t>(index)].widget;
            if (widget != nullptr) select(widget, false);
        });
    }
    _validationStatus = dynamic_cast<TextLabel*>(
        findChromeById("validation_status"));
    bindBtn("btn_validate", [this]() { refreshValidation(); });

    _interactionGraphList = dynamic_cast<ListView*>(
        findChromeById("interaction_graph_list"));
    if (_interactionGraphList != nullptr) {
        _interactionGraphList->setSelectionMode(
            ListView::SelectionMode::Single);
        _interactionGraphList->setOnSelectionChanged([this](int index) {
            if (_suppressInteractionGraph || index < 0 ||
                index >= static_cast<int>(_interactionGraph.edges().size())) {
                return;
            }
            Widget* widget = _interactionGraph.edges()[
                static_cast<size_t>(index)].widget;
            if (widget != nullptr) select(widget, false);
        });
    }
    _interactionGraphStatus = dynamic_cast<TextLabel*>(
        findChromeById("interaction_graph_status"));
    bindBtn("btn_refresh_interactions", [this]() {
        refreshInteractionContracts();
    });

    _reuseList = dynamic_cast<ListView*>(findChromeById("reuse_list"));
    _reuseName = dynamic_cast<TextInput*>(findChromeById("reuse_name"));
    _reuseStatus = dynamic_cast<TextLabel*>(findChromeById("reuse_status"));
    if (_reuseList != nullptr) {
        _reuseList->setSelectionMode(ListView::SelectionMode::Single);
        _reuseList->setOnSelectionChanged([this](int index) {
            if (_suppressReuse || index < 0 ||
                index >= static_cast<int>(_reuseLibrary.blocks().size())) {
                return;
            }
            if (_reuseName != nullptr) {
                _reuseName->setText(utf8ToWide(
                    _reuseLibrary.blocks()[static_cast<size_t>(index)].name));
            }
            syncReuseEditor();
        });
    }
    bindBtn("btn_reuse_define", [this]() {
        const std::string name = _reuseName != nullptr
            ? wideToUtf8(trimWide(_reuseName->getText())) : std::string{};
        defineReusableBlock(name);
    });
    bindBtn("btn_reuse_insert", [this]() {
        const int index = _reuseList != nullptr
            ? _reuseList->getSelectedIndex() : -1;
        if (index >= 0 && index < static_cast<int>(_reuseLibrary.blocks().size())) {
            insertReusableBlock(
                _reuseLibrary.blocks()[static_cast<size_t>(index)].name);
        }
    });
    bindBtn("btn_reuse_remove", [this]() {
        const int index = _reuseList != nullptr
            ? _reuseList->getSelectedIndex() : -1;
        if (index >= 0 && index < static_cast<int>(_reuseLibrary.blocks().size())) {
            removeReusableBlock(
                _reuseLibrary.blocks()[static_cast<size_t>(index)].name);
        }
    });

    _externalComponentList = dynamic_cast<ListView*>(
        findChromeById("component_library_list"));
    _externalComponentSearch = dynamic_cast<TextInput*>(
        findChromeById("component_library_search"));
    _externalComponentCategoryFilter = dynamic_cast<ComboBox*>(
        findChromeById("component_library_filter"));
    _externalComponentId = dynamic_cast<TextInput*>(
        findChromeById("component_library_id"));
    _externalComponentDisplayName = dynamic_cast<TextInput*>(
        findChromeById("component_library_display_name"));
    _externalComponentCategory = dynamic_cast<TextInput*>(
        findChromeById("component_library_category"));
    _externalComponentDescription = dynamic_cast<TextInput*>(
        findChromeById("component_library_description"));
    _externalComponentTags = dynamic_cast<TextInput*>(
        findChromeById("component_library_tags"));
    _externalComponentStatus = dynamic_cast<TextLabel*>(
        findChromeById("component_library_status"));
    if (_externalComponentSearch != nullptr) {
        _externalComponentSearch->setOnTextChanged([this](const std::wstring&) {
            if (!_suppressExternalComponents) syncExternalComponentEditor();
        });
    }
    if (_externalComponentCategoryFilter != nullptr) {
        _externalComponentCategoryFilter->setOnSelectionChanged([this](int) {
            if (!_suppressExternalComponents) syncExternalComponentEditor();
        });
    }
    if (_externalComponentList != nullptr) {
        _externalComponentList->setSelectionMode(ListView::SelectionMode::Single);
        _externalComponentList->setOnSelectionChanged([this](int index) {
            if (_suppressExternalComponents || index < 0
                || index >= static_cast<int>(
                    _externalComponentFilteredIndices.size())) return;
            const LayoutComponentDefinition& definition =
                _externalComponentLibrary.components()[
                    _externalComponentFilteredIndices[static_cast<size_t>(index)]];
            if (_externalComponentId != nullptr) {
                _externalComponentId->setText(utf8ToWide(definition.id));
            }
            if (_externalComponentDisplayName != nullptr) {
                _externalComponentDisplayName->setText(
                    utf8ToWide(definition.displayName));
            }
            if (_externalComponentCategory != nullptr) {
                _externalComponentCategory->setText(
                    utf8ToWide(definition.category));
            }
            if (_externalComponentDescription != nullptr) {
                _externalComponentDescription->setText(
                    utf8ToWide(definition.description));
            }
            if (_externalComponentTags != nullptr) {
                std::vector<std::wstring> tags;
                for (const std::string& tag : definition.tags) {
                    tags.push_back(utf8ToWide(tag));
                }
                _externalComponentTags->setText(joinItems(tags));
            }
            syncExternalComponentEditor();
        });
        _externalComponentList->setOnItemActivated([this](int index) {
            if (index < 0 || index >= static_cast<int>(
                    _externalComponentFilteredIndices.size())) return;
            insertExternalComponent(_externalComponentLibrary.components()[
                _externalComponentFilteredIndices[static_cast<size_t>(index)]].id);
        });
    }
    bindBtn("btn_component_library_save", [this]() {
        const std::string id = _externalComponentId != nullptr
            ? wideToUtf8(trimWide(_externalComponentId->getText()))
            : std::string{};
        const std::string category = _externalComponentCategory != nullptr
            ? wideToUtf8(trimWide(_externalComponentCategory->getText()))
            : std::string{};
        const std::string displayName = _externalComponentDisplayName != nullptr
            ? wideToUtf8(trimWide(_externalComponentDisplayName->getText()))
            : id;
        const std::string description = _externalComponentDescription != nullptr
            ? wideToUtf8(trimWide(_externalComponentDescription->getText()))
            : std::string{};
        std::vector<std::string> tags;
        if (_externalComponentTags != nullptr) {
            for (const std::wstring& tag : splitItems(
                     _externalComponentTags->getText())) {
                tags.push_back(wideToUtf8(tag));
            }
        }
        defineExternalComponent(id, displayName, category, description, tags);
    });
    bindBtn("btn_component_library_insert", [this]() {
        const int index = _externalComponentList != nullptr
            ? _externalComponentList->getSelectedIndex() : -1;
        if (index >= 0 && index < static_cast<int>(
                _externalComponentFilteredIndices.size())) {
            insertExternalComponent(_externalComponentLibrary.components()[
                _externalComponentFilteredIndices[static_cast<size_t>(index)]].id);
        }
    });
    bindBtn("btn_component_library_remove", [this]() {
        const int index = _externalComponentList != nullptr
            ? _externalComponentList->getSelectedIndex() : -1;
        if (index >= 0 && index < static_cast<int>(
                _externalComponentFilteredIndices.size())) {
            removeExternalComponent(_externalComponentLibrary.components()[
                _externalComponentFilteredIndices[static_cast<size_t>(index)]].id);
        }
    });

    _themeTokenList = dynamic_cast<ListView*>(findChromeById("theme_token_list"));
    _themeTokenKey = dynamic_cast<TextInput*>(findChromeById("theme_token_key"));
    _themeTokenValue = dynamic_cast<TextInput*>(findChromeById("theme_token_value"));
    _themeTokenSwatch = dynamic_cast<Button*>(
        findChromeById("btn_theme_token_swatch"));
    _themeStyleList = dynamic_cast<ListView*>(findChromeById("theme_style_list"));
    _themeStyleFragment = dynamic_cast<TextInput*>(
        findChromeById("theme_style_fragment"));
    _themeStyleId = dynamic_cast<TextInput*>(findChromeById("theme_style_id"));
    _themeStyleProperty = dynamic_cast<ComboBox*>(
        findChromeById("theme_style_property"));
    _themeStyleBinding = dynamic_cast<TextInput*>(
        findChromeById("theme_style_binding"));
    _themeEditorStatus = dynamic_cast<TextLabel*>(
        findChromeById("theme_editor_status"));
    if (_themeTokenList != nullptr) {
        _themeTokenList->setSelectionMode(ListView::SelectionMode::Single);
        _themeTokenList->setOnSelectionChanged([this](int) {
            if (!_suppressThemeEditor) syncThemeEditor();
        });
    }
    if (_themeStyleList != nullptr) {
        _themeStyleList->setSelectionMode(ListView::SelectionMode::Single);
        _themeStyleList->setOnSelectionChanged([this](int) {
            if (!_suppressThemeEditor) syncThemeEditor();
        });
    }
    if (_themeStyleProperty != nullptr) {
        _themeStyleProperty->setItems({L"Background", L"Border", L"Text"});
        _themeStyleProperty->setSelectedIndex(0);
        _themeStyleProperty->setOnSelectionChanged([this](int) {
            if (!_suppressThemeEditor) syncThemeEditor();
        });
    }
    bindBtn("btn_theme_open", [this]() {
        if (_themePicker == nullptr) return;
        const std::string path = _themePicker();
        if (!path.empty()) openThemeDocument(path);
    });
    bindBtn("btn_theme_save", [this]() { saveThemeDocument(); });
    bindBtn("btn_theme_token_apply", [this]() { applyThemeToken(); });
    bindBtn("btn_theme_token_swatch", [this]() {
        openThemeTokenColorPicker();
    });
    bindBtn("btn_theme_token_rename", [this]() { renameThemeToken(); });
    bindBtn("btn_theme_token_remove", [this]() { removeThemeToken(); });
    bindBtn("btn_theme_style_apply", [this]() { applyThemeStyleBinding(); });
    bindBtn("btn_theme_style_new", [this]() { createThemeStyle(); });
    bindBtn("btn_theme_style_duplicate", [this]() { duplicateThemeStyle(); });
    bindBtn("btn_theme_style_remove", [this]() { removeThemeStyle(); });

    _projectRefactorKind = dynamic_cast<ComboBox*>(
        findChromeById("project_refactor_kind"));
    _projectRefactorOld = dynamic_cast<TextInput*>(
        findChromeById("project_refactor_old"));
    _projectRefactorNew = dynamic_cast<TextInput*>(
        findChromeById("project_refactor_new"));
    _projectRefactorPreview = dynamic_cast<ListView*>(
        findChromeById("project_refactor_preview"));
    _projectRefactorStatus = dynamic_cast<TextLabel*>(
        findChromeById("project_refactor_status"));
    if (_projectRefactorKind != nullptr) {
        _projectRefactorKind->setOnSelectionChanged([this](int) {
            if (_suppressProjectRefactor) return;
            seedProjectRefactorValue();
            syncProjectRefactorEditor();
        });
    }
    if (_projectRefactorPreview != nullptr) {
        _projectRefactorPreview->setSelectionMode(
            ListView::SelectionMode::Single);
    }
    bindBtn("btn_project_refactor_preview", [this]() {
        runProjectRefactor(false);
    });
    bindBtn("btn_project_refactor_apply", [this]() {
        runProjectRefactor(true);
    });
    syncProjectRefactorEditor();

    _responsiveBreakpoint = dynamic_cast<ComboBox*>(
        findChromeById("responsive_breakpoint"));
    _responsiveVisibility = dynamic_cast<ComboBox*>(
        findChromeById("responsive_visibility"));
    _responsiveStatus = dynamic_cast<TextLabel*>(
        findChromeById("responsive_status"));
    if (_responsiveBreakpoint != nullptr) {
        std::vector<std::wstring> labels;
        for (const LayoutBreakpoint& breakpoint :
             _responsiveModel.breakpoints()) {
            labels.push_back(breakpoint.label);
        }
        _responsiveBreakpoint->setItems(labels);
        _responsiveBreakpoint->setOnSelectionChanged([this](int index) {
            if (_suppressResponsive || index < 0 ||
                index >= static_cast<int>(
                    _responsiveModel.breakpoints().size())) return;
            _responsiveAuthoringIndex = index;
            syncResponsiveEditor();
        });
    }
    if (_responsiveVisibility != nullptr) {
        _responsiveVisibility->setItems({L"Inherit", L"Visible", L"Hidden"});
        _responsiveVisibility->setOnSelectionChanged([this](int index) {
            if (_suppressResponsive || index < 0 || index > 2) return;
            setResponsiveVisibility(
                _responsiveAuthoringIndex,
                static_cast<ResponsiveVisibility>(index));
        });
    }
    bindBtn("btn_responsive_preview", [this]() {
        previewResponsiveBreakpoint(_responsiveAuthoringIndex);
    });
    bindBtn("btn_responsive_capture", [this]() {
        captureResponsiveAnchors(_responsiveAuthoringIndex);
    });
    bindBtn("btn_responsive_clear", [this]() {
        clearResponsiveRule(_responsiveAuthoringIndex);
    });

    _animationClipList = dynamic_cast<ListView*>(
        findChromeById("animation_clip_list"));
    _animationClipName = dynamic_cast<TextInput*>(
        findChromeById("animation_clip_name"));
    _animationTrackList = dynamic_cast<ListView*>(
        findChromeById("animation_track_list"));
    _animationProperty = dynamic_cast<ComboBox*>(
        findChromeById("animation_track_property"));
    _animationKeyList = dynamic_cast<ListView*>(
        findChromeById("animation_key_list"));
    _animationTime = dynamic_cast<TextInput*>(
        findChromeById("animation_time"));
    _animationCurve = dynamic_cast<ComboBox*>(
        findChromeById("animation_curve"));
    _animationBezierX1 = dynamic_cast<TextInput*>(
        findChromeById("animation_bezier_x1"));
    _animationBezierY1 = dynamic_cast<TextInput*>(
        findChromeById("animation_bezier_y1"));
    _animationBezierX2 = dynamic_cast<TextInput*>(
        findChromeById("animation_bezier_x2"));
    _animationBezierY2 = dynamic_cast<TextInput*>(
        findChromeById("animation_bezier_y2"));
    _animationSpringMass = dynamic_cast<TextInput*>(
        findChromeById("animation_spring_mass"));
    _animationSpringStiffness = dynamic_cast<TextInput*>(
        findChromeById("animation_spring_stiffness"));
    _animationSpringDamping = dynamic_cast<TextInput*>(
        findChromeById("animation_spring_damping"));
    _animationSpringVelocity = dynamic_cast<TextInput*>(
        findChromeById("animation_spring_velocity"));
    _animationSpringClamp = dynamic_cast<ComboBox*>(
        findChromeById("animation_spring_clamp"));
    _animationRepeat = dynamic_cast<TextInput*>(
        findChromeById("animation_repeat"));
    _animationYoyo = dynamic_cast<ComboBox*>(
        findChromeById("animation_yoyo"));
    _animationImportance = dynamic_cast<ComboBox*>(
        findChromeById("animation_importance"));
    _animationStatus = dynamic_cast<TextLabel*>(
        findChromeById("animation_status"));

    if (_animationClipList != nullptr) {
        _animationClipList->setSelectionMode(ListView::SelectionMode::Single);
        _animationClipList->setOnSelectionChanged([this](int index) {
            if (_suppressAnimation) return;
            stopAnimationPreview();
            _animationClipIndex = index;
            _animationTrackIndex = -1;
            _animationKeyIndex = -1;
            syncAnimationEditor();
        });
    }
    if (_animationTrackList != nullptr) {
        _animationTrackList->setSelectionMode(ListView::SelectionMode::Single);
        _animationTrackList->setOnSelectionChanged([this](int index) {
            if (_suppressAnimation) return;
            stopAnimationPreview();
            _animationTrackIndex = index;
            _animationKeyIndex = -1;
            syncAnimationEditor();
        });
    }
    if (_animationKeyList != nullptr) {
        _animationKeyList->setSelectionMode(ListView::SelectionMode::Single);
        _animationKeyList->setOnSelectionChanged([this](int index) {
            if (_suppressAnimation) return;
            _animationKeyIndex = index;
            syncAnimationEditor();
        });
    }
    if (_animationProperty != nullptr) {
        _animationProperty->setItems({L"Opacity", L"Position", L"Size"});
        _animationProperty->setSelectedIndex(0);
    }
    if (_animationCurve != nullptr) {
        _animationCurve->setItems(
            {L"Linear", L"Ease In", L"Ease Out", L"Ease In Out", L"Spring",
             L"Cubic Bezier"});
        _animationCurve->setSelectedIndex(0);
        _animationCurve->setOnSelectionChanged([this](int index) {
            if (_suppressAnimation || index < 0 || index > 5) return;
            syncAnimationCurveParameterVisibility(
                static_cast<AnimationCurve>(index),
                _animationKeyIndex >= 0);
        });
    }
    if (_animationSpringClamp != nullptr) {
        _animationSpringClamp->setItems({L"Allow overshoot", L"Clamp 0..1"});
        _animationSpringClamp->setSelectedIndex(0);
    }
    if (_animationYoyo != nullptr) {
        _animationYoyo->setItems({L"No yoyo", L"Yoyo"});
    }
    if (_animationImportance != nullptr) {
        _animationImportance->setItems({L"Decorative", L"Essential"});
    }
    bindBtn("btn_animation_clip_add", [this]() {
        const std::string name = _animationClipName != nullptr
            ? wideToUtf8(trimWide(_animationClipName->getText()))
            : std::string{};
        createAnimationClip(name);
    });
    bindBtn("btn_animation_clip_remove", [this]() {
        removeAnimationClip(_animationClipIndex);
    });
    bindBtn("btn_animation_track_add", [this]() {
        const int property = _animationProperty != nullptr
            ? _animationProperty->getSelectedIndex() : 0;
        if (property >= 0 && property <= 2) {
            addAnimationTrack(_animationClipIndex,
                static_cast<UIAnimationProperty>(property));
        }
    });
    bindBtn("btn_animation_track_remove", [this]() {
        removeAnimationTrack(_animationClipIndex, _animationTrackIndex);
    });
    bindBtn("btn_animation_key_capture", [this]() {
        float timeMs = 0.0f;
        const int curve = _animationCurve != nullptr
            ? _animationCurve->getSelectedIndex() : 0;
        if (_animationTime == nullptr ||
            !parseFloat(_animationTime->getText(), timeMs) || timeMs < 0.0f ||
            curve < 0 || curve > 5) {
            setStatus(L"Animation key time must be a non-negative number");
            return;
        }
        captureAnimationKeyframe(_animationClipIndex, _animationTrackIndex,
                                 timeMs, static_cast<AnimationCurve>(curve));
    });
    bindBtn("btn_animation_key_remove", [this]() {
        removeAnimationKeyframe(_animationClipIndex, _animationTrackIndex,
                                _animationKeyIndex);
    });
    bindBtn("btn_animation_curve_apply", [this]() {
        commitAnimationCurveParameters();
    });
    bindBtn("btn_animation_playback_apply", [this]() {
        int repeat = 0;
        if (_animationRepeat == nullptr ||
            !parseInt(_animationRepeat->getText(), repeat) || repeat < -1) {
            setStatus(L"Repeat must be -1 (forever) or a non-negative integer");
            return;
        }
        const bool yoyo = _animationYoyo != nullptr &&
            _animationYoyo->getSelectedIndex() == 1;
        const AnimationImportance importance =
            _animationImportance != nullptr &&
            _animationImportance->getSelectedIndex() == 1
                ? AnimationImportance::Essential
                : AnimationImportance::Decorative;
        setAnimationPlayback(_animationClipIndex, repeat, yoyo, importance);
    });
    bindBtn("btn_animation_preview", [this]() {
        float timeMs = 0.0f;
        if (_animationTime == nullptr ||
            !parseFloat(_animationTime->getText(), timeMs) || timeMs < 0.0f) {
            setStatus(L"Animation preview time must be a non-negative number");
            return;
        }
        previewAnimationFrame(_animationClipIndex, timeMs);
    });
    bindBtn("btn_animation_reset", [this]() { stopAnimationPreview(); });
    bindBtn("btn_animation_play", [this]() {
        playAnimationPreview(_animationClipIndex);
    });
    bindBtn("btn_animation_pause", [this]() { pauseAnimationPreview(); });
    bindBtn("btn_animation_stop", [this]() { stopAnimationPreview(); });
    bindBtn("btn_animation_loop", [this]() {
        setAnimationPreviewLoop(!_animationPreviewLoop);
    });

    _previewPreset = dynamic_cast<ComboBox*>(
        findChromeById("preview_preset"));
    _previewDpi = dynamic_cast<ComboBox*>(findChromeById("preview_dpi"));
    _previewWidth = dynamic_cast<TextInput*>(findChromeById("preview_width"));
    _previewHeight = dynamic_cast<TextInput*>(findChromeById("preview_height"));
    _previewSafeL = dynamic_cast<TextInput*>(findChromeById("preview_safe_l"));
    _previewSafeT = dynamic_cast<TextInput*>(findChromeById("preview_safe_t"));
    _previewSafeR = dynamic_cast<TextInput*>(findChromeById("preview_safe_r"));
    _previewSafeB = dynamic_cast<TextInput*>(findChromeById("preview_safe_b"));
    if (_previewPreset != nullptr) {
        std::vector<std::wstring> names;
        for (const auto& preset : _previewModel.presets()) names.push_back(preset.name);
        names.push_back(L"Custom");
        _previewPreset->setItems(names);
        _previewPreset->setOnSelectionChanged([this](int index) {
            if (_suppressPreview) return;
            if (index >= 0 && index < static_cast<int>(_previewModel.presets().size()))
                setPreviewPreset(index);
        });
    }
    if (_previewDpi != nullptr) {
        _previewDpi->setItems({L"1.00x", L"1.25x", L"1.50x", L"2.00x", L"3.00x"});
        _previewDpi->setOnSelectionChanged([this](int index) {
            if (_suppressPreview || index < 0) return;
            static constexpr float scales[] = {1.0f, 1.25f, 1.5f, 2.0f, 3.0f};
            if (index >= static_cast<int>(sizeof(scales) / sizeof(scales[0]))) return;
            LayoutPreviewSettings settings = _previewModel.settings();
            settings.dpiScale = scales[index];
            setPreviewSettings(settings);
        });
    }
    for (TextInput* field : {_previewWidth, _previewHeight, _previewSafeL,
                             _previewSafeT, _previewSafeR, _previewSafeB}) {
        if (field == nullptr) continue;
        field->setOnSubmit([this](const std::wstring&) { commitPreviewFields(); });
        field->setOnFocusLostNotify([this]() {
            if (!_suppressPreview) commitPreviewFields();
        });
    }
    bindBtn("btn_preview_safe", [this]() {
        setSafeAreaVisible(!_previewModel.settings().showSafeArea);
    });
    bindBtn("btn_preview_mode", [this]() {
        setMode(_mode == Mode::Edit ? Mode::Interact : Mode::Edit);
    });

    _status = dynamic_cast<TextLabel*>(findChromeById("lbl_status"));
    ensureAnimationTimelineView();
}

void LayoutEditorSession::clearDocument() {
    if (_canvasHost == nullptr) {
        return;
    }
    cancelToolDrag();
    _dragMode = DragMode::None;
    _dragTarget = nullptr;
    _animationRuntimePreview.reset();
    _animationPreviewBaseline.reset();
    _animationPreviewClipIndex = -1;
    // Tear down selection chrome first so destroyWidgetTree doesn't
    // double-free panels we still hold pointers to.
    destroySelectionChrome();
    if (_docRoot != nullptr) {
        if (Widget* parent = _docRoot->getParent()) {
            parent->removeChild(_docRoot);
        }
        destroyWidgetTree(_docRoot);
    }
    _documentModel.setRoot(nullptr);
    _interactionStates.clear();
    _selected = nullptr;
    _selection.clear();
}

void LayoutEditorSession::setDocumentRoot(Widget* root) {
    if (_canvasHost == nullptr || root == nullptr) {
        return;
    }
    ensureCanvasViewport();
    if (_canvasViewport == nullptr) {
        return;
    }
    clearDocument();
    _canvasViewport->addChild(root);
    _documentModel.setRoot(root);
    _interactionStates.clear();
    _authoredRootSize = root->getSize();
    _docRoot->setLayoutPositionManaged(false);
    _docRoot->setLayoutSizeManaged(false);
    if (_mode == Mode::Interact) restoreDocumentInteraction(_docRoot);
    else freezeDocumentInteraction(_docRoot);
    rehydrateRuntimePresentation(_docRoot);
    applyPreviewToDocument();
    ensureSelectionChrome();
}

void LayoutEditorSession::ensureCanvasViewport() {
    if (_canvasHost == nullptr) {
        _canvasViewport = nullptr;
        return;
    }
    if (_canvasViewport != nullptr &&
        _canvasViewport->getParent() == _canvasHost) {
        syncCanvasViewportGeometry();
        return;
    }

    LayoutCanvasViewport* viewport = nullptr;
    for (Widget* child : _canvasHost->getChildren()) {
        if (child != nullptr && child->getId() == "__le_canvas_viewport") {
            viewport = dynamic_cast<LayoutCanvasViewport*>(child);
            if (viewport != nullptr) break;
        }
    }
    if (viewport == nullptr) {
        viewport = new LayoutCanvasViewport();

        // Migrate a document that chrome JSON or an earlier session mounted
        // directly under canvas_host. Editor overlays stay above the new
        // viewport and never become authored children.
        std::vector<Widget*> authoredRoots;
        for (Widget* child : _canvasHost->getChildren()) {
            if (child != nullptr && !isEditorOverlay(child)) {
                authoredRoots.push_back(child);
            }
        }
        _canvasHost->addChild(viewport);
        _canvasHost->moveChildToIndex(viewport, 0u);
        for (Widget* child : authoredRoots) {
            _canvasHost->removeChild(child);
            viewport->addChild(child);
        }
    }
    _canvasViewport = viewport;
    syncCanvasViewportGeometry();
    applyViewportTransform();
}

void LayoutEditorSession::syncCanvasViewportGeometry() {
    auto* viewport = dynamic_cast<LayoutCanvasViewport*>(_canvasViewport);
    if (viewport == nullptr || _canvasHost == nullptr) {
        return;
    }
    const math::FVector2 hostSize = _canvasHost->getSize();
    if (viewport->getPosition().x != 0.0f ||
        viewport->getPosition().y != 0.0f) {
        viewport->setPosition(math::FVector2(0.0f, 0.0f));
    }
    if (std::fabs(viewport->getSize().x - hostSize.x) > 0.01f ||
        std::fabs(viewport->getSize().y - hostSize.y) > 0.01f) {
        viewport->setSize(hostSize);
    }
}

void LayoutEditorSession::applyViewportTransform() {
    if (auto* viewport = dynamic_cast<LayoutCanvasViewport*>(_canvasViewport)) {
        viewport->setView(_viewZoom, _viewPan);
    }
    if (_ui != nullptr) {
        _ui->invalidateLayout();
    }
}

math::FVector2 LayoutEditorSession::documentToScreen(
    const math::FVector2& point) const {
    if (auto* viewport = dynamic_cast<LayoutCanvasViewport*>(_canvasViewport)) {
        return viewport->documentToScreen(point);
    }
    return point;
}

math::FVector2 LayoutEditorSession::screenToDocument(
    const math::FVector2& point) const {
    if (auto* viewport = dynamic_cast<LayoutCanvasViewport*>(_canvasViewport)) {
        return viewport->screenToDocument(point);
    }
    return point;
}

math::FRectangle LayoutEditorSession::documentToScreen(
    const math::FRectangle& bounds) const {
    if (auto* viewport = dynamic_cast<LayoutCanvasViewport*>(_canvasViewport)) {
        return viewport->documentToScreen(bounds);
    }
    return bounds;
}

void LayoutEditorSession::freezeDocumentInteraction(Widget* root) {
    if (root == nullptr) {
        return;
    }
    std::function<void(Widget*)> walk = [&](Widget* n) {
        auto [stateIt, inserted] = _interactionStates.try_emplace(n);
        InteractionState& saved = stateIt->second;
        if (auto* iw = dynamic_cast<InteractiveWidget*>(n)) {
            if (inserted) saved.enabled = iw->isEnabled();
            iw->setEnabled(false);
        }
        if (auto* ti = dynamic_cast<TextInput*>(n)) {
            if (inserted || !saved.hasReadOnly) {
                saved.readOnly = ti->isReadOnly();
                saved.hasReadOnly = true;
            }
            ti->setReadOnly(true);
        }
        // Children of layout containers stay container-managed. Letting a
        // Grid child become free-positioned disconnects it from its cell and
        // makes all newly-authored controls appear stacked at (0,0).
        const bool underLayout = n->getParent() != nullptr &&
            (dynamic_cast<BoxBase*>(n->getParent()) != nullptr ||
             dynamic_cast<GridPanel*>(n->getParent()) != nullptr);
        n->setLayoutPositionManaged(underLayout);
        if (dynamic_cast<GridPanel*>(n->getParent()) != nullptr) {
            n->setLayoutSizeManaged(true);
        }
        std::vector<Widget*> children;
        collectAuthoredChildren(n, children);
        for (Widget* c : children) {
            walk(c);
        }
    };
    walk(root);
}

void LayoutEditorSession::restoreDocumentInteraction(Widget* root) {
    if (root == nullptr) return;
    std::function<void(Widget*)> walk = [&](Widget* node) {
        const auto found = _interactionStates.find(node);
        if (auto* interactive = dynamic_cast<InteractiveWidget*>(node)) {
            interactive->setEnabled(found == _interactionStates.end()
                ? true : found->second.enabled);
        }
        if (auto* input = dynamic_cast<TextInput*>(node)) {
            input->setReadOnly(found != _interactionStates.end() &&
                found->second.hasReadOnly ? found->second.readOnly : false);
        }
        std::vector<Widget*> children;
        collectAuthoredChildren(node, children);
        for (Widget* child : children) walk(child);
    };
    walk(root);
}

void LayoutEditorSession::rehydrateRuntimePresentation(Widget* root) {
    if (root == nullptr || !_texturePreviewLoader) {
        return;
    }
    std::function<void(Widget*)> walk = [&](Widget* node) {
        if (node == nullptr || isEditorOverlay(node)) return;
        if (auto* image = dynamic_cast<Image*>(node)) {
            const std::string textureName = image->getTextureName();
            if (!textureName.empty() && !image->hasTexture()) {
                ImageTextureHandle preview = _texturePreviewLoader(
                    _textureCatalog.resolvePreviewPath(textureName));
                if (preview.isValid()) {
                    // The decoder path is host-local; retain the stable key
                    // in Image so serialization never leaks machine paths.
                    preview.name = textureName;
                    image->setTexture(preview);
                }
            }
        }
        std::vector<Widget*> children;
        collectAuthoredChildren(node, children);
        for (Widget* child : children) walk(child);
    };
    walk(root);
}

void LayoutEditorSession::ensureEmptyDocument() {
    Widget* panel = WidgetFactory::get().create("Panel");
    if (panel == nullptr) {
        return;
    }
    panel->setId("document_root");
    panel->setPosition(math::FVector2(16.0f, 16.0f));
    panel->setSize(math::FVector2(640.0f, 480.0f));
    panel->setLayoutPositionManaged(false);
    panel->setLayoutSizeManaged(false);
    setDocumentRoot(panel);
    _documentPath.clear();
    _reuseLibrary.clear();
    _animationLibrary.clear();
    _animationClipIndex = -1;
    _animationTrackIndex = -1;
    _animationKeyIndex = -1;
    markDirty(false);
    syncProjectRefactorEditor();
    seedProjectRefactorValue();
}

bool LayoutEditorSession::open(const std::string& path) {
    if (_ui == nullptr || path.empty()) {
        return false;
    }
    if (_mode == Mode::Interact) setMode(Mode::Edit);
    const std::string json = readFileToString(path);
    if (json.empty()) {
        setStatus(utf8ToWide("Open failed: " + path));
        return false;
    }
    std::string rootJson;
    std::string decodeError;
    LayoutReuseLibrary decodedLibrary;
    UIAnimationLibrary decodedAnimations;
    if (!decodedLibrary.decodeDocument(
            json, rootJson, decodedAnimations, &decodeError)) {
        setStatus(utf8ToWide("Open failed: " + decodeError));
        return false;
    }
    Widget* loaded = _docLoader.loadFromString(rootJson);
    if (loaded == nullptr) {
        setStatus(utf8ToWide("Open failed: " + path));
        return false;
    }
    _commandStack.clear();
    _reuseLibrary = std::move(decodedLibrary);
    _animationLibrary = std::move(decodedAnimations);
    _animationClipIndex = _animationLibrary.empty() ? -1 : 0;
    _animationTrackIndex = -1;
    _animationKeyIndex = -1;
    setDocumentRoot(loaded);
    _documentPath = path;
    markDirty(false);
    if (_ui != nullptr) {
        _ui->invalidateLayout();
        _ui->layout();
    }
    refreshHierarchy();
    syncReuseEditor();
    syncProjectRefactorEditor();
    select(_docRoot, false);
    setStatus(utf8ToWide("Opened " + path));
    return true;
}

bool LayoutEditorSession::save() {
    if (_documentPath.empty()) {
        if (!_savePicker) {
            setStatus(L"No path — use Save As");
            return false;
        }
        const std::string path = _savePicker();
        if (path.empty()) {
            return false;
        }
        return saveAs(path);
    }
    return saveAs(_documentPath);
}

bool LayoutEditorSession::saveAs(const std::string& path) {
    if (_docRoot == nullptr || path.empty()) {
        return false;
    }
    if (_mode == Mode::Interact) setMode(Mode::Edit);
    const math::FVector2 previewSize = _docRoot->getSize();
    const bool restorePreview = previewOverridesDocumentSize();
    try {
        if (restorePreview) {
            _docRoot->setSize(_authoredRootSize);
            _docRoot->performLayout();
        }
        UILayoutLoader saver;
        const std::string document = _reuseLibrary.encodeDocument(
            _docRoot, _animationLibrary, true);
        if (!saver.saveJsonDocument(path, document)) {
            if (restorePreview) applyPreviewToDocument();
            setStatus(utf8ToWide("Save failed: " + path));
            return false;
        }
    } catch (const std::exception& ex) {
        if (restorePreview) {
            _docRoot->setSize(previewSize);
            _docRoot->performLayout();
        }
        setStatus(utf8ToWide(std::string("Save aborted: ") + ex.what()));
        return false;
    } catch (...) {
        if (restorePreview) {
            _docRoot->setSize(previewSize);
            _docRoot->performLayout();
        }
        setStatus(L"Save aborted");
        return false;
    }
    if (restorePreview) applyPreviewToDocument();
    _documentPath = path;
    markDirty(false);
    syncProjectRefactorEditor();
    setStatus(utf8ToWide("Saved " + path));
    return true;
}

bool LayoutEditorSession::writeRecoveryCopy(const std::string& path) const {
    if (_docRoot == nullptr || path.empty()) return false;
    try {
        UILayoutLoader saver;
        return saver.saveJsonDocument(
            path, _reuseLibrary.encodeDocument(
                _docRoot, _animationLibrary, true));
    } catch (...) {
        return false;
    }
}

bool LayoutEditorSession::selectionContains(Widget* widget) const {
    return _selectionModel.contains(widget);
}

void LayoutEditorSession::pruneSelection() {
    _selectionModel.prune([this](Widget* widget) {
        return isUnderCanvas(widget) && widget != _canvasHost;
    });
    normalizeSelectionNesting();
    if (_selected != nullptr && !selectionContains(_selected)) {
        _selected = _selection.empty() ? nullptr : _selection.back();
    }
}

void LayoutEditorSession::normalizeSelectionNesting() {
    // Multi-select must not mix an ancestor with its descendants — chrome,
    // align, and move all assume a flat peer set. Prefer the deepest
    // (leaf-most) widgets when both appear.
    if (_selection.size() < 2) {
        return;
    }
    _selection.erase(
        std::remove_if(_selection.begin(), _selection.end(),
                       [this](Widget* w) {
                           if (w == nullptr) {
                               return true;
                           }
                           for (Widget* other : _selection) {
                               if (other != nullptr && other != w &&
                                   isAncestorOf(w, other)) {
                                   return true;
                               }
                           }
                           return false;
                       }),
        _selection.end());
}

void LayoutEditorSession::select(Widget* widget, bool additive) {
    if (_insideSelect) {
        return;
    }
    struct SelectGuard {
        bool& flag;
        explicit SelectGuard(bool& f) : flag(f) { flag = true; }
        ~SelectGuard() { flag = false; }
    } guard(_insideSelect);

    // Finish any in-progress property edit against the CURRENT selection
    // before retargeting / hiding prop fields. Hiding a focused TextInput
    // without clearing focus leaves UIManager holding a stale focus target;
    // the next setFocus then runs onFocusLost → commitPropField mid-sync.
    if (_ui != nullptr && _ui->getFocusedWidget() != nullptr) {
        _ui->setFocus(nullptr);
    }
    // Selection changes are a hard transaction boundary even if a host
    // backend did not emit focus-lost for the previous inspector field.
    endPropertyMutation();

    // Inactive TabControl pages are part of the authored model but are not
    // mounted under the body panel. Selecting a page (or one of its authored
    // descendants) from the Outline must first make that page active so its
    // bounds, canvas chrome, and Inspector all refer to the live tree.
    if (widget != nullptr && !isUnderCanvas(widget)) {
        for (Widget* candidate = widget; candidate != nullptr;
             candidate = candidate->getParent()) {
            auto* tabs = dynamic_cast<TabControl*>(
                structuredContentOwner(candidate));
            if (tabs == nullptr) continue;
            for (int i = 0; i < static_cast<int>(tabs->getTabCount()); ++i) {
                if (tabs->getTabContent(i) == candidate) {
                    tabs->setSelectedIndex(i);
                    tabs->performLayout();
                    if (_ui != nullptr) {
                        _ui->invalidateLayout();
                        _ui->layout();
                    }
                    break;
                }
            }
            break;
        }
    }

    if (widget == _canvasHost) {
        widget = nullptr;
    }
    if (widget != nullptr && !isUnderCanvas(widget)) {
        widget = nullptr;
    }
    // Document root is never part of a multi-selection — additive pick of
    // root replaces the selection (avoids ancestor/descendant chrome bugs).
    if (widget != nullptr && widget == _docRoot) {
        additive = false;
    }

    if (!additive || widget == nullptr) {
        _selectionModel.setSingle(widget);
    } else {
        if (selectionContains(widget)) {
            _selectionModel.remove(widget);
        } else {
            // Drop any ancestor/descendant of the new pick first, then add.
            _selection.erase(
                std::remove_if(_selection.begin(), _selection.end(),
                               [this, widget](Widget* w) {
                                   return w != nullptr &&
                                          (isAncestorOf(widget, w) ||
                                           isAncestorOf(w, widget));
                               }),
                _selection.end());
            _selectionModel.add(widget);
        }
    }

    normalizeSelectionNesting();
    syncPropertyStrip();
    syncHierarchySelection();
    syncSelectionChrome();
    updateContainerHint();
    seedProjectRefactorValue();
    if (_selection.size() >= 2) {
        std::wostringstream oss;
        oss << L"Selected " << _selection.size()
            << L" — Align/Dist ready";
        setStatus(oss.str());
    }
}

void LayoutEditorSession::syncSelectionChrome() {
    ensureSelectionChrome();
    if (_selBox == nullptr || _canvasHost == nullptr) {
        return;
    }
    syncPreviewChrome();
    if (_mode == Mode::Interact) {
        _selBox->setVisible(false);
        for (Panel* handle : _handles) if (handle != nullptr) handle->setVisible(false);
        for (Panel* outline : _selOutlines) if (outline != nullptr) outline->setVisible(false);
        if (_anchorBox != nullptr) _anchorBox->setVisible(false);
        for (Panel* point : _anchorPoints) if (point != nullptr) point->setVisible(false);
        return;
    }
    pruneSelection();
    if (_selection.empty()) {
        _selBox->setVisible(false);
        for (Panel* h : _handles) {
            if (h != nullptr) {
                h->setVisible(false);
            }
        }
        for (Panel* o : _selOutlines) {
            if (o != nullptr) {
                o->setVisible(false);
            }
        }
        if (_anchorBox != nullptr) _anchorBox->setVisible(false);
        for (Panel* point : _anchorPoints) {
            if (point != nullptr) point->setVisible(false);
        }
        return;
    }

    const math::FRectangle host = _canvasHost->getWorldBounds();
    constexpr float kOutset = 2.0f;

    // Per-widget outlines for multi-select; primary uses handles.
    ensureSelOutlines(_selection.size());
    for (size_t i = 0; i < _selOutlines.size(); ++i) {
        Panel* outline = _selOutlines[i];
        if (outline == nullptr) {
            continue;
        }
        if (i >= _selection.size() || _selection[i] == nullptr) {
            outline->setVisible(false);
            continue;
        }
        if (_selection[i] == _selected) {
            // The primary already has the stronger selection ring. Drawing
            // both rings at slightly different offsets produced a fuzzy,
            // stair-stepped edge in the GDI-hosted Designer.
            outline->setVisible(false);
            continue;
        }
        const math::FRectangle wb = documentToScreen(
            _selection[i]->getWorldBounds());
        const float minX = std::round(wb.minX - host.minX);
        const float minY = std::round(wb.minY - host.minY);
        const float maxX = std::round(wb.maxX - host.minX);
        const float maxY = std::round(wb.maxY - host.minY);
        outline->setPosition(math::FVector2(
            minX - kOutset, minY - kOutset));
        outline->setSize(math::FVector2(
            maxX - minX + kOutset * 2.0f,
            maxY - minY + kOutset * 2.0f));
        outline->setVisible(true);
    }

    // Keep legacy selBox as primary highlight (slightly thicker).
    if (_selected != nullptr && isUnderCanvas(_selected)) {
        const math::FRectangle wb = documentToScreen(
            _selected->getWorldBounds());
        const float px = std::round(wb.minX - host.minX);
        const float py = std::round(wb.minY - host.minY);
        const float right = std::round(wb.maxX - host.minX);
        const float bottom = std::round(wb.maxY - host.minY);
        const float pw = right - px;
        const float ph = bottom - py;
        _selBox->setPosition(math::FVector2(px - kOutset, py - kOutset));
        _selBox->setSize(math::FVector2(pw + kOutset * 2.0f,
                                        ph + kOutset * 2.0f));
        _selBox->setVisible(true);
        // The document origin is immutable, but its preview size must remain
        // authorable so responsive anchors can be tested. Root exposes only
        // right/bottom handles; left/top handles would rewrite the origin.
        if (_selected != _docRoot) {
            placeHandle(_handles[0], px, py);
            placeHandle(_handles[1], px + pw * 0.5f, py);
            placeHandle(_handles[2], px + pw, py);
            placeHandle(_handles[3], px, py + ph * 0.5f);
            placeHandle(_handles[4], px + pw, py + ph * 0.5f);
            placeHandle(_handles[5], px, py + ph);
            placeHandle(_handles[6], px + pw * 0.5f, py + ph);
            placeHandle(_handles[7], px + pw, py + ph);
        } else if (!previewOverridesDocumentSize()) {
            for (Panel* h : _handles) {
                if (h != nullptr) {
                    h->setVisible(false);
                }
            }
            placeHandle(_handles[4], px + pw, py + ph * 0.5f);
            placeHandle(_handles[6], px + pw * 0.5f, py + ph);
            placeHandle(_handles[7], px + pw, py + ph);
        } else {
            for (Panel* h : _handles) {
                if (h != nullptr) h->setVisible(false);
            }
        }
    } else {
        _selBox->setVisible(false);
        for (Panel* h : _handles) {
            if (h != nullptr) {
                h->setVisible(false);
            }
        }
    }

    const ResponsiveLayoutRule* activeResponsive = _selected != nullptr
        ? _selected->getActiveResponsiveLayoutRule() : nullptr;
    const bool hasVisibleAnchor = _selected != nullptr &&
        (_selected->hasAnchorLayout() ||
         (activeResponsive != nullptr && activeResponsive->overrideAnchors));
    const bool showAnchors = _selected != nullptr &&
        isUnderCanvas(_selected) && canUseAnchorLayout(_selected) &&
        hasVisibleAnchor;
    if (!showAnchors) {
        if (_anchorBox != nullptr) _anchorBox->setVisible(false);
        for (Panel* point : _anchorPoints) {
            if (point != nullptr) point->setVisible(false);
        }
        return;
    }

    const AnchorLayout& anchor = activeResponsive != nullptr &&
        activeResponsive->overrideAnchors
        ? activeResponsive->anchors : _selected->getAnchorLayout();
    Widget* parent = _selected->getParent();
    const math::FRectangle parentWorld = parent->getWorldBounds();
    const math::FVector2 parentSize = parent->getSize();
    const math::FVector2 minScreen = documentToScreen(math::FVector2(
        parentWorld.minX + parentSize.x * anchor.anchorMin.x,
        parentWorld.minY + parentSize.y * anchor.anchorMin.y));
    const math::FVector2 maxScreen = documentToScreen(math::FVector2(
        parentWorld.minX + parentSize.x * anchor.anchorMax.x,
        parentWorld.minY + parentSize.y * anchor.anchorMax.y));
    const float minX = minScreen.x - host.minX;
    const float minY = minScreen.y - host.minY;
    const float maxX = maxScreen.x - host.minX;
    const float maxY = maxScreen.y - host.minY;
    if (_anchorBox != nullptr) {
        const bool hasExtent = std::fabs(maxX - minX) > 0.5f ||
                               std::fabs(maxY - minY) > 0.5f;
        _anchorBox->setPosition(math::FVector2(std::round(minX),
                                               std::round(minY)));
        _anchorBox->setSize(math::FVector2(
            (std::max)(1.0f, std::round(maxX - minX)),
            (std::max)(1.0f, std::round(maxY - minY))));
        _anchorBox->setVisible(hasExtent);
    }
    const math::FVector2 positions[4] = {
        {minX, minY}, {maxX, minY}, {minX, maxY}, {maxX, maxY}
    };
    constexpr float kAnchorPointSize = 6.0f;
    for (int i = 0; i < 4; ++i) {
        Panel* point = _anchorPoints[i];
        if (point == nullptr) continue;
        bool duplicate = false;
        for (int prior = 0; prior < i; ++prior) {
            if (std::fabs(positions[i].x - positions[prior].x) < 0.5f &&
                std::fabs(positions[i].y - positions[prior].y) < 0.5f) {
                duplicate = true;
                break;
            }
        }
        point->setVisible(!duplicate);
        if (!duplicate) {
            point->setPosition(math::FVector2(
                std::round(positions[i].x - kAnchorPointSize * 0.5f),
                std::round(positions[i].y - kAnchorPointSize * 0.5f)));
            point->setSize(math::FVector2(kAnchorPointSize,
                                          kAnchorPointSize));
        }
    }
}

void LayoutEditorSession::selectById(const std::string& id) {
    select(findInDocument(id), false);
}

void LayoutEditorSession::syncHierarchySelection() {
    if (_hierarchy == nullptr || _suppressHierarchy) {
        return;
    }
    int index = -1;
    for (size_t i = 0; i < _hierarchyIndex.size(); ++i) {
        if (_hierarchyIndex[i] == _selected) {
            index = static_cast<int>(i);
            break;
        }
    }
    _suppressHierarchy = true;
    _hierarchy->setSelectedIndex(index);
    _suppressHierarchy = false;
    syncHierarchySummary();
}

bool LayoutEditorSession::hierarchyFilterActive() const {
    return _hierarchySearch != nullptr
        && !trimWide(_hierarchySearch->getText()).empty();
}

void LayoutEditorSession::syncHierarchySummary() {
    if (_hierarchySummary == nullptr) return;
    std::wstring summary;
    if (_selection.size() > 1u) {
        summary = std::to_wstring(_selection.size()) + L" widgets selected";
    } else if (_selected != nullptr) {
        std::vector<std::wstring> path;
        for (Widget* node = _selected; node != nullptr; node = node->getParent()) {
            if (!node->getId().empty()) path.push_back(utf8ToWide(node->getId()));
            if (node == _docRoot) break;
        }
        std::reverse(path.begin(), path.end());
        for (std::size_t i = 0; i < path.size(); ++i) {
            if (i != 0u) summary += L" / ";
            summary += path[i];
        }
        if (summary.empty()) summary = L"Anonymous widget selected";
    } else {
        summary = L"No selection";
    }
    if (hierarchyFilterActive()) {
        summary = std::to_wstring(_hierarchyMatchCount)
            + L" match(es) · " + summary;
    }
    _hierarchySummary->setText(summary);
}

void LayoutEditorSession::refreshHierarchy() {
    std::vector<Widget*> fullIndex;
    std::vector<std::wstring> fullLabels;
    if (_docRoot != nullptr) {
        collectHierarchy(_docRoot, 0, fullLabels, fullIndex);
    }

    std::vector<Widget*> authoredWidgets;
    collectDocumentWidgets(_docRoot, authoredWidgets);
    _documentModel.rebuildIndex(authoredWidgets);

    if (_hierarchy == nullptr) {
        setStatus(L"DBG: refreshHierarchy skipped — no ListView");
        return;
    }

    _hierarchyIndex.clear();
    std::vector<std::wstring> labels;
    const std::wstring query = _hierarchySearch != nullptr
        ? lowerWide(trimWide(_hierarchySearch->getText())) : L"";
    _hierarchyMatchCount = 0u;
    if (query.empty()) {
        labels = std::move(fullLabels);
        _hierarchyIndex = std::move(fullIndex);
    } else {
        std::unordered_set<Widget*> context;
        for (Widget* widget : fullIndex) {
            std::wstring searchable = widget != nullptr
                ? utf8ToWide(widget->getId()) : L"";
            if (const WidgetAuthoringDescriptor* descriptor =
                    WidgetAuthoringRegistry::get().findForWidget(widget)) {
                searchable += L" ";
                searchable += utf8ToWide(descriptor->typeName);
                searchable += L" ";
                searchable += utf8ToWide(descriptor->displayName);
            }
            if (lowerWide(std::move(searchable)).find(query)
                == std::wstring::npos) continue;
            ++_hierarchyMatchCount;
            for (Widget* ancestor = widget; ancestor != nullptr;
                 ancestor = ancestor->getParent()) {
                context.insert(ancestor);
                if (ancestor == _docRoot) break;
            }
        }
        for (std::size_t i = 0; i < fullIndex.size(); ++i) {
            if (context.find(fullIndex[i]) == context.end()) continue;
            _hierarchyIndex.push_back(fullIndex[i]);
            labels.push_back(fullLabels[i]);
        }
    }

    _suppressHierarchy = true;
    _hierarchy->setItems(labels);
    _hierarchy->performLayout();
    _suppressHierarchy = false;

    pruneSelection();
    syncHierarchySelection();
    syncHierarchySummary();
    syncSelectionChrome();
    updateContainerHint();
}

void LayoutEditorSession::updateContainerHint() {
    if (_selected == nullptr) {
        return;
    }
    if (isContainerWidget(_selected)) {
        const std::string& id = _selected->getId();
        std::wstring msg = L"Container selected";
        if (!id.empty()) {
            msg += L": ";
            msg += utf8ToWide(id);
        }
        msg += L" — use palette (click or drag) to add a child";
        setStatus(msg);
    }
}
void LayoutEditorSession::addWidget(const std::string& typeName) {
    Widget* parent = pickParentForAdd();
    if (parent == nullptr) {
        return;
    }
    Widget* created = createWidgetInstance(typeName);
    if (created == nullptr) {
        setStatus(utf8ToWide("Unknown type: " + typeName));
        return;
    }

    pushUndo(LayoutEditKind::Insert, "Add widget");
    if (!placeNewWidget(created, parent, -1, nullptr)) {
        _commandStack.discardLastUndo();
        destroyWidgetTree(created);
        return;
    }
    freezeDocumentInteraction(created);
    markDirty(true);
    refreshHierarchy();
    select(created, false);
    {
        std::wstring msg = utf8ToWide("Added " + typeName + " under ");
        const std::string& pid = parent->getId();
        msg += pid.empty() ? L"(parent)" : utf8ToWide(pid);
        setStatus(msg);
    }
    if (_ui != nullptr) {
        _ui->invalidateLayout();
        _ui->layout();
    }
}

void LayoutEditorSession::deleteSelected() {
    if (_selection.empty() || _docRoot == nullptr) {
        return;
    }
    std::vector<Widget*> doomed;
    bool keptStructuredRoot = false;
    for (Widget* w : _selection) {
        if (w != nullptr && w != _docRoot &&
            structuredContentOwner(w) == nullptr) {
            doomed.push_back(w);
        } else if (w != nullptr && structuredContentOwner(w) != nullptr) {
            keptStructuredRoot = true;
        }
    }
    if (doomed.empty()) {
        setStatus(keptStructuredRoot
            ? L"Structured content roots use their owner's model"
            : L"Cannot delete document root");
        return;
    }

    std::sort(doomed.begin(), doomed.end(), [](Widget* a, Widget* b) {
        int da = 0, db = 0;
        for (Widget* p = a; p; p = p->getParent()) ++da;
        for (Widget* p = b; p; p = p->getParent()) ++db;
        return da > db;
    });

    pushUndo(LayoutEditKind::Delete, "Delete selection");
    _dragMode = DragMode::None;
    _dragTarget = nullptr;
    _selected = nullptr;
    _selection.clear();

    for (Widget* w : doomed) {
        Widget* parent = w->getParent();
        if (parent == nullptr) {
            continue;
        }
        if (auto* box = dynamic_cast<BoxBase*>(parent)) {
            box->removeWidget(w);
        } else {
            parent->removeChild(w);
        }
        destroyWidgetTree(w);
    }

    markDirty(true);
    refreshHierarchy();
    syncPropertyStrip();
    setStatus(keptStructuredRoot
        ? L"Deleted selection; kept structured content root"
        : L"Deleted");
    if (_ui != nullptr) {
        _ui->invalidateLayout();
        _ui->layout();
    }
}
void LayoutEditorSession::applyProperty(const std::string& field,
                                        const std::wstring& value) {
    if (_selected == nullptr) {
        return;
    }

    if (field == "id") {
        const std::string newId = wideToUtf8(trimWide(value));
        std::wstring reason;
        if (!validateWidgetId(newId, _selected, &reason)) {
            setStatus(L"ID rejected — " + reason);
            return;
        }
        if (newId == _selected->getId()) return;
        const std::string oldId = _selected->getId();
        UIAnimationLibrary retargeted = _animationLibrary;
        std::string animationError;
        if (!retargeted.retargetWidget(oldId, newId, &animationError)) {
            setStatus(L"ID rejected — " + utf8ToWide(animationError));
            return;
        }
        _selected->setId(newId);
        _animationLibrary = std::move(retargeted);
        markDirty(true);
        refreshHierarchy();
        syncAnimationEditor();
        select(_selected, false);
        return;
    }

    // Apply every shared Inspector field to the whole selection. Geometry
    // and style already have native group implementations below because
    // they need parent-layout validation and one shared refresh.
    if (_selection.size() > 1u && field != "style" && field != "x" &&
        field != "y" && field != "w" && field != "h") {
        const std::vector<Widget*> targets = _selection;
        Widget* const savedPrimary = _selected;
        for (Widget* target : targets) {
            if (target == nullptr) continue;
            _selected = target;
            _selection.assign(1u, target);
            applyProperty(field, value);
        }
        _selection = targets;
        _selected = savedPrimary;
        syncPropertyStrip();
        return;
    }

    if (field == "style") {
        const std::string styleId = wideToUtf8(value);
        const std::vector<Widget*> targets = _selection.size() > 1u
            ? _selection : std::vector<Widget*>{_selected};
        for (Widget* widget : targets) {
            if (widget != nullptr) widget->setStyleId(styleId);
        }
        markDirty(true);
        syncStyleInspector();
        return;
    }

    if (field == "controller") {
        _selected->setControllerId(wideToUtf8(trimWide(value)));
        markDirty(true);
        return;
    }

    if (field.rfind("event:", 0) == 0) {
        _selected->setEventBinding(field.substr(6),
                                   wideToUtf8(trimWide(value)));
        markDirty(true);
        return;
    }

    if (field == "texture") {
        if (dynamic_cast<Image*>(_selected) == nullptr) {
            setStatus(L"Texture: only Image");
            return;
        }
        applyTextureName(wideToUtf8(trimWide(value)));
        return;
    }

    if (field == "items") {
        const std::vector<std::wstring> items = splitItems(value);
        if (auto* combo = dynamic_cast<ComboBox*>(_selected)) {
            combo->setItems(items);
        } else if (auto* list = dynamic_cast<ListView*>(_selected)) {
            list->setItems(items);
        } else if (auto* tiles = dynamic_cast<TileView*>(_selected)) {
            tiles->setItems(items);
        } else if (auto* tree = dynamic_cast<TreeView*>(_selected)) {
            std::vector<TreeNodeData> nodes;
            nodes.reserve(items.size());
            for (const std::wstring& item : items) {
                nodes.push_back({item, {}, false, false, -1});
            }
            tree->setTree(nodes);
        } else if (auto* strip = dynamic_cast<TabStrip*>(_selected)) {
            strip->clearTabs();
            for (const std::wstring& item : items) strip->addTab(item);
        } else if (auto* tabs = dynamic_cast<TabControl*>(_selected)) {
            const int oldCount = static_cast<int>(tabs->getTabCount());
            const int shared = (std::min)(oldCount, static_cast<int>(items.size()));
            for (int i = 0; i < shared; ++i) tabs->setTabLabel(i, items[i]);
            for (int i = oldCount - 1; i >= static_cast<int>(items.size()); --i) {
                tabs->removeTab(i);
            }
            for (int i = oldCount; i < static_cast<int>(items.size()); ++i) {
                auto* page = new Panel();
                page->setId(makeUniqueId("tab_page", tabs));
                page->setBorderEnabled(false);
                tabs->addTabOwned(items[static_cast<size_t>(i)], page);
            }
        } else {
            setStatus(L"Items: only collection and tab controls");
            return;
        }
        markDirty(true);
        refreshHierarchy();
        if (_ui != nullptr) _ui->invalidateLayout();
        return;
    }

    if (field == "text") {
        if (setTextPayload(_selected, value)) {
            markDirty(true);
        }
        return;
    }

    if (field == "hAlign") {
        const std::string a = wideToUtf8(value);
        if (auto* label = dynamic_cast<TextLabel*>(_selected)) {
            if (a == "Center") {
                label->setHorizontalAlignment(TextLabel::HAlignment::Center);
            } else if (a == "Right") {
                label->setHorizontalAlignment(TextLabel::HAlignment::Right);
            } else {
                label->setHorizontalAlignment(TextLabel::HAlignment::Left);
            }
            markDirty(true);
            return;
        }
        if (auto* ti = dynamic_cast<TextInput*>(_selected)) {
            if (a == "Center") {
                ti->setHAlign(TextInput::HAlign::Center);
            } else if (a == "Right") {
                ti->setHAlign(TextInput::HAlign::Right);
            } else {
                ti->setHAlign(TextInput::HAlign::Left);
            }
            markDirty(true);
            return;
        }
        setStatus(L"text hAlign: only TextLabel / TextInput");
        return;
    }

    if (field == "vAlign") {
        const std::string a = wideToUtf8(value);
        if (auto* label = dynamic_cast<TextLabel*>(_selected)) {
            if (a == "Center" || a == "Middle") {
                label->setVerticalAlignment(TextLabel::VAlignment::Center);
            } else if (a == "Bottom") {
                label->setVerticalAlignment(TextLabel::VAlignment::Bottom);
            } else {
                label->setVerticalAlignment(TextLabel::VAlignment::Top);
            }
            markDirty(true);
            return;
        }
        setStatus(L"text vAlign: only TextLabel");
        return;
    }

    if (field == "checked") {
        if (auto* cb = dynamic_cast<CheckBox*>(_selected)) {
            cb->setChecked(value == L"true" || value == L"1" || value == L"True");
            markDirty(true);
            return;
        }
        if (auto* radio = dynamic_cast<RadioButton*>(_selected)) {
            radio->setChecked(value == L"true" || value == L"1" || value == L"True");
            markDirty(true);
            return;
        }
        setStatus(L"checked: only CheckBox or RadioButton");
        return;
    }
    if (field == "password") {
        if (auto* ti = dynamic_cast<TextInput*>(_selected)) {
            ti->setPasswordMode(value == L"true" || value == L"1" || value == L"True");
            markDirty(true);
            return;
        }
        setStatus(L"password: only TextInput");
        return;
    }
    if (field == "readOnly") {
        if (auto* ti = dynamic_cast<TextInput*>(_selected)) {
            ti->setReadOnly(value == L"true" || value == L"1" || value == L"True");
            markDirty(true);
            return;
        }
        if (auto* area = dynamic_cast<TextArea*>(_selected)) {
            area->setReadOnly(value == L"true" || value == L"1" || value == L"True");
            markDirty(true);
            return;
        }
        setStatus(L"readOnly: only TextInput or TextArea");
        return;
    }
    if (field == "gravity") {
        if (auto* box = dynamic_cast<BoxBase*>(_selected)) {
            const std::string g = wideToUtf8(value);
            auto parseG = [](const std::string& s) -> BoxBase::Gravity {
                if (s == "TopCenter") return BoxBase::Gravity::TopCenter;
                if (s == "TopRight") return BoxBase::Gravity::TopRight;
                if (s == "CenterLeft") return BoxBase::Gravity::CenterLeft;
                if (s == "Center") return BoxBase::Gravity::Center;
                if (s == "CenterRight") return BoxBase::Gravity::CenterRight;
                if (s == "BottomLeft") return BoxBase::Gravity::BottomLeft;
                if (s == "BottomCenter") return BoxBase::Gravity::BottomCenter;
                if (s == "BottomRight") return BoxBase::Gravity::BottomRight;
                return BoxBase::Gravity::TopLeft;
            };
            box->setGravity(parseG(g));
            markDirty(true);
            if (_ui != nullptr) {
                _ui->invalidateLayout();
            }
            return;
        }
        setStatus(L"gravity: only VBox/HBox");
        return;
    }

    const PropertyFieldSchema* fieldSchema = findPropertyFieldSchema(field);
    auto parseSchemaNumber = [&](float& parsed) {
        if (!parseFloat(value, parsed) || !std::isfinite(parsed)) {
            setStatus(L"Invalid numeric value");
            return false;
        }
        if (fieldSchema != nullptr && fieldSchema->hasRange) {
            parsed = std::clamp(parsed, fieldSchema->minimum,
                                fieldSchema->maximum);
        }
        if (fieldSchema != nullptr &&
            fieldSchema->editorKind == PropertyEditorKind::Integer) {
            parsed = std::round(parsed);
        }
        return true;
    };
    auto finishSchemaProperty = [&]() {
        _selected->markDirty();
        markDirty(true);
        const bool previousSuppress = _suppressProp;
        _suppressProp = true;
        syncSchemaPropertyFields();
        _suppressProp = previousSuppress;
        if (_ui != nullptr) _ui->invalidateLayout();
    };

    if (field == "min" || field == "max" || field == "value") {
        float parsed = 0.0f;
        if (!parseSchemaNumber(parsed)) return;
        if (auto* slider = dynamic_cast<Slider*>(_selected)) {
            if (field == "min") slider->setMin(parsed);
            else if (field == "max") slider->setMax(parsed);
            else slider->setValue(parsed);
        } else if (auto* progress = dynamic_cast<ProgressBar*>(_selected)) {
            if (field == "min") progress->setMin(parsed);
            else if (field == "max") progress->setMax(parsed);
            else progress->setValue(parsed);
        } else {
            return;
        }
        finishSchemaProperty();
        return;
    }

    if (field == "imageTint") {
        auto* image = dynamic_cast<Image*>(_selected);
        math::FVector4 color;
        if (image == nullptr || !parseColorHex(value, color)) {
            setStatus(L"Image tint expects #RRGGBB or #RRGGBBAA");
            return;
        }
        image->setColor(color);
        finishSchemaProperty();
        return;
    }

    if (field == "uvMinX" || field == "uvMinY" ||
        field == "uvMaxX" || field == "uvMaxY") {
        auto* image = dynamic_cast<Image*>(_selected);
        float parsed = 0.0f;
        if (image == nullptr || !parseSchemaNumber(parsed)) return;
        math::FRectangle uv = image->getUV();
        if (field == "uvMinX") uv.minX = parsed;
        else if (field == "uvMinY") uv.minY = parsed;
        else if (field == "uvMaxX") uv.maxX = parsed;
        else uv.maxY = parsed;
        image->setUV(uv);
        finishSchemaProperty();
        return;
    }

    if (field == "selectionMode") {
        const bool extended = value == L"Extended";
        if (auto* list = dynamic_cast<ListView*>(_selected)) {
            list->setSelectionMode(extended
                ? ListView::SelectionMode::Extended
                : ListView::SelectionMode::Single);
        } else if (auto* tiles = dynamic_cast<TileView*>(_selected)) {
            tiles->setSelectionMode(extended
                ? TileView::SelectionMode::Extended
                : TileView::SelectionMode::Single);
        } else {
            return;
        }
        finishSchemaProperty();
        return;
    }

    if (field == "itemHeight") {
        float parsed = 0.0f;
        if (!parseSchemaNumber(parsed)) return;
        if (auto* list = dynamic_cast<ListView*>(_selected)) {
            list->setItemHeight(parsed);
        } else if (auto* tree = dynamic_cast<TreeView*>(_selected)) {
            tree->setItemHeight(parsed);
        } else {
            return;
        }
        finishSchemaProperty();
        return;
    }

    if (field == "tileWidth" || field == "tileHeight" ||
        field == "tileSpacing") {
        auto* tiles = dynamic_cast<TileView*>(_selected);
        float parsed = 0.0f;
        if (tiles == nullptr || !parseSchemaNumber(parsed)) return;
        if (field == "tileSpacing") {
            tiles->setTileSpacing(parsed);
        } else {
            math::FVector2 size = tiles->getTileSize();
            if (field == "tileWidth") size.x = parsed;
            else size.y = parsed;
            tiles->setTileSize(size);
        }
        finishSchemaProperty();
        return;
    }

    if (field == "verticalScrollBarVisibility" ||
        field == "horizontalScrollBarVisibility") {
        auto* scroll = dynamic_cast<ScrollView*>(_selected);
        if (scroll == nullptr) return;
        ScrollView::ScrollBarVisibility visibility =
            ScrollView::ScrollBarVisibility::Auto;
        if (value == L"Always") {
            visibility = ScrollView::ScrollBarVisibility::Always;
        } else if (value == L"Hidden") {
            visibility = ScrollView::ScrollBarVisibility::Hidden;
        }
        if (field == "verticalScrollBarVisibility") {
            scroll->setVerticalScrollBarVisibility(visibility);
        } else {
            scroll->setHorizontalScrollBarVisibility(visibility);
        }
        finishSchemaProperty();
        return;
    }

    if (field == "overflowMode" || field == "minTabWidth") {
        auto* strip = dynamic_cast<TabStrip*>(_selected);
        if (strip == nullptr) return;
        if (field == "overflowMode") {
            TabStrip::OverflowMode mode = TabStrip::OverflowMode::Scroll;
            if (value == L"Compress") mode = TabStrip::OverflowMode::Compress;
            else if (value == L"Clip") mode = TabStrip::OverflowMode::Clip;
            strip->setOverflowMode(mode);
        } else {
            float parsed = 0.0f;
            if (!parseSchemaNumber(parsed)) return;
            strip->setMinTabWidth(parsed);
        }
        finishSchemaProperty();
        return;
    }

    if (field == "gridRows" || field == "gridColumns" ||
        field == "gridSpacingX" || field == "gridSpacingY") {
        auto* grid = dynamic_cast<GridPanel*>(_selected);
        float parsed = 0.0f;
        if (grid == nullptr || !parseSchemaNumber(parsed)) return;
        if (field == "gridRows" || field == "gridColumns") {
            const int requested = static_cast<int>(parsed);
            int requiredRows = 1;
            int requiredColumns = 1;
            for (int row = 0; row < grid->getRowCount(); ++row) {
                for (int column = 0; column < grid->getColumnCount(); ++column) {
                    const GridPanel::CellInfo* cell = grid->findCell(row, column);
                    if (cell != nullptr && cell->widget != nullptr) {
                        requiredRows = (std::max)(requiredRows,
                            row + cell->rowSpan);
                        requiredColumns = (std::max)(requiredColumns,
                            column + cell->colSpan);
                    }
                }
            }
            if ((field == "gridRows" && requested < requiredRows) ||
                (field == "gridColumns" && requested < requiredColumns)) {
                setStatus(L"Grid size cannot discard occupied cells");
                const bool previousSuppress = _suppressProp;
                _suppressProp = true;
                syncSchemaPropertyFields();
                _suppressProp = previousSuppress;
                return;
            }
            if (field == "gridRows") grid->setRowCount(requested);
            else grid->setColumnCount(requested);
        } else {
            float horizontal = grid->getHorizontalSpacing();
            float vertical = grid->getVerticalSpacing();
            if (field == "gridSpacingX") horizontal = parsed;
            else vertical = parsed;
            grid->setSpacing(horizontal, vertical);
        }
        finishSchemaProperty();
        return;
    }

    if (field == "richWrapMode" || field == "richOverflow" ||
        field == "lineHeight" || field == "maxLines") {
        auto* rich = dynamic_cast<RichText*>(_selected);
        if (rich == nullptr) return;
        if (field == "richWrapMode") {
            RichTextWrapMode mode = RichTextWrapMode::NoWrap;
            if (value == L"Word") mode = RichTextWrapMode::Word;
            else if (value == L"Character") mode = RichTextWrapMode::Character;
            rich->setWrapMode(mode);
        } else if (field == "richOverflow") {
            rich->setOverflow(value == L"Ellipsis"
                ? RichTextOverflow::Ellipsis : RichTextOverflow::Clip);
        } else {
            float parsed = 0.0f;
            if (!parseSchemaNumber(parsed)) return;
            if (field == "lineHeight") rich->setLineHeight(parsed);
            else rich->setMaxLines(static_cast<size_t>(parsed));
        }
        finishSchemaProperty();
        return;
    }

    const std::vector<Widget*> geometryTargets = _selection.size() > 1u
        ? _selection : std::vector<Widget*>{_selected};
    Widget* blockedPositionTarget = nullptr;
    bool blockedByParentLayout = false;
    for (Widget* widget : geometryTargets) {
        if (widget == nullptr) continue;
        const bool positionOwnedByLayout = widget->getParent() != nullptr &&
            (dynamic_cast<BoxBase*>(widget->getParent()) != nullptr ||
             dynamic_cast<GridPanel*>(widget->getParent()) != nullptr);
        if ((widget == _docRoot || structuredContentOwner(widget) != nullptr ||
             positionOwnedByLayout) && (field == "x" || field == "y")) {
            blockedPositionTarget = widget;
            blockedByParentLayout = positionOwnedByLayout;
            break;
        }
    }
    if (blockedPositionTarget != nullptr) {
        setStatus(blockedPositionTarget == _docRoot
            ? L"Document root position is fixed"
            : (blockedByParentLayout
                ? L"Position is managed by the parent layout"
                : L"Structured content position is fixed by its owner"));
        syncPropertyStrip();
        return;
    }

    const bool anchorField =
        field == "anchorMinX" || field == "anchorMinY" ||
        field == "anchorMaxX" || field == "anchorMaxY" ||
        field == "offsetMinX" || field == "offsetMinY" ||
        field == "offsetMaxX" || field == "offsetMaxY" ||
        field == "pivotX" || field == "pivotY";
    float f = 0.0f;
    if (field == "x" || field == "y" || field == "w" || field == "h" ||
        field == "spacing" || field == "padL" || field == "padT" ||
        field == "padR" || field == "padB" || anchorField) {
        if (!parseFloat(value, f)) {
            return;
        }
    }

    if (anchorField) {
        if (!canUseAnchorLayout(_selected)) {
            setStatus(L"Anchors apply only to free-positioned children");
            syncPropertyStrip();
            return;
        }
        if (!_selected->hasAnchorLayout()) {
            _selected->setAnchorLayoutPreservingRect(
                math::FVector2(0.0f, 0.0f),
                math::FVector2(0.0f, 0.0f));
        }
        AnchorLayout layout = _selected->getAnchorLayout();
        const bool preservesRect =
            field == "anchorMinX" || field == "anchorMinY" ||
            field == "anchorMaxX" || field == "anchorMaxY" ||
            field == "pivotX" || field == "pivotY";
        if (field == "anchorMinX") layout.anchorMin.x = f;
        else if (field == "anchorMinY") layout.anchorMin.y = f;
        else if (field == "anchorMaxX") layout.anchorMax.x = f;
        else if (field == "anchorMaxY") layout.anchorMax.y = f;
        else if (field == "offsetMinX") layout.offsetMin.x = f;
        else if (field == "offsetMinY") layout.offsetMin.y = f;
        else if (field == "offsetMaxX") layout.offsetMax.x = f;
        else if (field == "offsetMaxY") layout.offsetMax.y = f;
        else if (field == "pivotX") layout.pivot.x = f;
        else if (field == "pivotY") layout.pivot.y = f;

        _selected->setLayoutPositionManaged(false);
        _selected->setLayoutSizeManaged(false);
        _selected->setAnchorLayout(layout);
        if (preservesRect) {
            _selected->refreshAnchorOffsetsFromCurrentRect();
        } else if (_selected->getParent() != nullptr) {
            _selected->applyAnchorLayout(_selected->getParent()->getSize());
        }
        markDirty(true);
        syncSelectionChrome();
        syncAnchorPresetStyles();
        if (_ui != nullptr) {
            _ui->invalidateLayout();
        }
        return;
    }

    if (field == "x" || field == "y" || field == "w" || field == "h") {
        for (Widget* widget : geometryTargets) {
            if (widget == nullptr) continue;
            math::FVector2 pos = widget->getPosition();
            math::FVector2 size = widget->getSize();
            if (field == "x") {
                pos.x = f;
                widget->setPosition(pos);
            } else if (field == "y") {
                pos.y = f;
                widget->setPosition(pos);
            } else if (field == "w") {
                size.x = (std::max)(kMinWidgetSize, f);
                if (widget == _docRoot && previewOverridesDocumentSize()) {
                    _authoredRootSize.x = size.x;
                } else {
                    widget->setSize(size);
                }
            } else {
                size.y = (std::max)(kMinWidgetSize, f);
                if (widget == _docRoot && previewOverridesDocumentSize()) {
                    _authoredRootSize.y = size.y;
                } else {
                    widget->setSize(size);
                }
            }
            refreshAnchorOffsets(widget);
        }
        markDirty(true);
        if (_ui != nullptr) {
            _ui->invalidateLayout();
        }
        return;
    }

    if (auto* box = dynamic_cast<BoxBase*>(_selected)) {
        if (field == "spacing") {
            box->setSpacing(f);
            markDirty(true);
            if (_ui != nullptr) {
                _ui->invalidateLayout();
            }
            return;
        }
        if (field == "padL" || field == "padT" || field == "padR" ||
            field == "padB") {
            math::FVector4 pad = box->getPadding();
            if (field == "padL") pad.x = f;
            else if (field == "padT") pad.y = f;
            else if (field == "padR") pad.z = f;
            else pad.w = f;
            box->setPadding(pad.x, pad.y, pad.z, pad.w);
            markDirty(true);
            if (_ui != nullptr) {
                _ui->invalidateLayout();
            }
        }
    }
}

LayoutEditorSession::Snapshot LayoutEditorSession::captureSnapshot() {
    Snapshot snap;
    if (_docRoot != nullptr) {
        const math::FVector2 previewSize = _docRoot->getSize();
        const bool restorePreview = previewOverridesDocumentSize();
        if (restorePreview) {
            _docRoot->setSize(_authoredRootSize);
            _docRoot->performLayout();
        }
        snap.json = _reuseLibrary.encodeDocument(
            _docRoot, _animationLibrary, false);
        if (restorePreview) {
            _docRoot->setSize(previewSize);
            _docRoot->performLayout();
        }
    }
    for (Widget* w : _selection) {
        if (w != nullptr && !w->getId().empty()) {
            snap.selectedIds.push_back(w->getId());
        }
    }
    if (_selected != nullptr) {
        snap.primaryId = _selected->getId();
    }
    snap.dirty = _dirty;
    return snap;
}

void LayoutEditorSession::restoreSnapshot(const Snapshot& snap) {
    if (snap.json.empty() || _ui == nullptr) {
        return;
    }
    std::string rootJson;
    LayoutReuseLibrary restoredLibrary;
    UIAnimationLibrary restoredAnimations;
    if (!restoredLibrary.decodeDocument(
            snap.json, rootJson, restoredAnimations, nullptr)) {
        setStatus(L"Undo/Redo document metadata restore failed");
        return;
    }
    Widget* loaded = _docLoader.loadFromString(rootJson);
    if (loaded == nullptr) {
        setStatus(L"Undo/Redo restore failed");
        return;
    }
    _reuseLibrary = std::move(restoredLibrary);
    _animationLibrary = std::move(restoredAnimations);
    _animationClipIndex = std::min(
        _animationClipIndex, static_cast<int>(_animationLibrary.size()) - 1);
    _animationTrackIndex = -1;
    _animationKeyIndex = -1;
    setDocumentRoot(loaded);
    refreshHierarchy();
    _selection.clear();
    for (const std::string& id : snap.selectedIds) {
        if (Widget* w = findInDocument(id)) {
            _selection.push_back(w);
        }
    }
    _selected = findInDocument(snap.primaryId);
    if (_selected == nullptr && !_selection.empty()) {
        _selected = _selection.back();
    }
    syncPropertyStrip();
    syncHierarchySelection();
    syncReuseEditor();
    markDirty(snap.dirty);
    _ui->invalidateLayout();
    _ui->layout();
}

void LayoutEditorSession::pushUndo(LayoutEditKind kind, const char* label) {
    if (_docRoot == nullptr) {
        return;
    }
    _commandStack.push(captureSnapshot(), kind,
                       label != nullptr ? label : std::string{});
}

void LayoutEditorSession::beginMutation(LayoutEditKind kind,
                                        const char* label) {
    if (_docRoot == nullptr) return;
    _commandStack.begin(captureSnapshot(), kind,
                        label != nullptr ? label : "Edit property");
}

void LayoutEditorSession::endMutation() {
    _commandStack.end();
}

std::wstring LayoutEditorSession::propertyValueForWidget(
    Widget* widget, const std::string& field) const {
    if (widget == nullptr) return {};
    if (field == "x") return formatFloat(widget->getPosition().x);
    if (field == "y") return formatFloat(widget->getPosition().y);
    if (field == "w") {
        return formatFloat(widget == _docRoot
            ? authoredRootSize().x : widget->getSize().x);
    }
    if (field == "h") {
        return formatFloat(widget == _docRoot
            ? authoredRootSize().y : widget->getSize().y);
    }
    if (field == "style") return utf8ToWide(widget->getStyleId());
    if (field == "controller") return utf8ToWide(widget->getControllerId());
    if (field.rfind("event:", 0) == 0) {
        return utf8ToWide(widget->getEventBinding(field.substr(6)));
    }
    if (field == "text") {
        std::wstring text;
        getTextPayload(widget, text);
        return text;
    }
    if (field == "texture") {
        if (auto* image = dynamic_cast<Image*>(widget)) {
            return utf8ToWide(image->getTextureName());
        }
        return {};
    }
    if (field == "items") {
        std::vector<std::wstring> items;
        if (auto* combo = dynamic_cast<ComboBox*>(widget)) {
            items = combo->getItemsRef();
        } else if (auto* list = dynamic_cast<ListView*>(widget)) {
            items = list->getItemsRef();
        } else if (auto* tiles = dynamic_cast<TileView*>(widget)) {
            items = tiles->getItemsRef();
        } else if (auto* tree = dynamic_cast<TreeView*>(widget)) {
            for (size_t i = 0; i < tree->getNodeCount(); ++i) {
                items.push_back(tree->getNodeData(i).label);
            }
        } else if (auto* strip = dynamic_cast<TabStrip*>(widget)) {
            for (int i = 0; i < strip->getTabCount(); ++i) {
                items.push_back(strip->getTabLabel(i));
            }
        } else if (auto* tabs = dynamic_cast<TabControl*>(widget)) {
            for (int i = 0; i < static_cast<int>(tabs->getTabCount()); ++i) {
                items.push_back(tabs->getTabLabel(i));
            }
        }
        return joinItems(items);
    }
    if (field == "checked") {
        if (auto* check = dynamic_cast<CheckBox*>(widget))
            return check->isChecked() ? L"true" : L"false";
        if (auto* radio = dynamic_cast<RadioButton*>(widget))
            return radio->isChecked() ? L"true" : L"false";
    }
    if (field == "password") {
        if (auto* input = dynamic_cast<TextInput*>(widget))
            return input->isPasswordMode() ? L"true" : L"false";
    }
    if (field == "readOnly") {
        if (auto* input = dynamic_cast<TextInput*>(widget))
            return input->isReadOnly() ? L"true" : L"false";
        if (auto* area = dynamic_cast<TextArea*>(widget))
            return area->isReadOnly() ? L"true" : L"false";
    }
    if (field == "hAlign") {
        if (auto* label = dynamic_cast<TextLabel*>(widget)) {
            switch (label->getHorizontalAlignment()) {
            case TextLabel::HAlignment::Center: return L"Center";
            case TextLabel::HAlignment::Right: return L"Right";
            default: return L"Left";
            }
        }
        if (auto* input = dynamic_cast<TextInput*>(widget)) {
            switch (input->getHAlign()) {
            case TextInput::HAlign::Center: return L"Center";
            case TextInput::HAlign::Right: return L"Right";
            default: return L"Left";
            }
        }
    }
    if (field == "vAlign") {
        if (auto* label = dynamic_cast<TextLabel*>(widget)) {
            switch (label->getVerticalAlignment()) {
            case TextLabel::VAlignment::Center: return L"Center";
            case TextLabel::VAlignment::Bottom: return L"Bottom";
            default: return L"Top";
            }
        }
    }
    if (auto* box = dynamic_cast<BoxBase*>(widget)) {
        if (field == "spacing") return formatFloat(box->getSpacing());
        const math::FVector4& padding = box->getPadding();
        if (field == "padL") return formatFloat(padding.x);
        if (field == "padT") return formatFloat(padding.y);
        if (field == "padR") return formatFloat(padding.z);
        if (field == "padB") return formatFloat(padding.w);
        if (field == "gravity") {
            switch (box->getGravity()) {
            case BoxBase::Gravity::TopCenter: return L"TopCenter";
            case BoxBase::Gravity::TopRight: return L"TopRight";
            case BoxBase::Gravity::CenterLeft: return L"CenterLeft";
            case BoxBase::Gravity::Center: return L"Center";
            case BoxBase::Gravity::CenterRight: return L"CenterRight";
            case BoxBase::Gravity::BottomLeft: return L"BottomLeft";
            case BoxBase::Gravity::BottomCenter: return L"BottomCenter";
            case BoxBase::Gravity::BottomRight: return L"BottomRight";
            default: return L"TopLeft";
            }
        }
    }
    if (widget->hasAnchorLayout()) {
        const AnchorLayout& anchor = widget->getAnchorLayout();
        if (field == "anchorMinX") return formatFloat(anchor.anchorMin.x);
        if (field == "anchorMinY") return formatFloat(anchor.anchorMin.y);
        if (field == "anchorMaxX") return formatFloat(anchor.anchorMax.x);
        if (field == "anchorMaxY") return formatFloat(anchor.anchorMax.y);
        if (field == "offsetMinX") return formatFloat(anchor.offsetMin.x);
        if (field == "offsetMinY") return formatFloat(anchor.offsetMin.y);
        if (field == "offsetMaxX") return formatFloat(anchor.offsetMax.x);
        if (field == "offsetMaxY") return formatFloat(anchor.offsetMax.y);
        if (field == "pivotX") return formatFloat(anchor.pivot.x);
        if (field == "pivotY") return formatFloat(anchor.pivot.y);
    }
    return schemaPropertyValueForWidget(widget, field);
}

std::wstring LayoutEditorSession::commonPropertyValue(
    const std::string& field) const {
    if (_selection.empty()) return propertyValueForWidget(_selected, field);
    const std::wstring first = propertyValueForWidget(_selection.front(), field);
    for (size_t i = 1; i < _selection.size(); ++i) {
        if (propertyValueForWidget(_selection[i], field) != first) {
            return L"\u2014";
        }
    }
    return first;
}

void LayoutEditorSession::beginPropertyMutation(
    const std::string& field, const char* label) {
    if (_pendingPropertyMutation.has_value()) {
        if (_pendingPropertyMutation->field == field) return;
        endPropertyMutation();
    }
    if (field == "id" || field == "items" || _selected == nullptr) {
        beginMutation(LayoutEditKind::Property, label);
        return;
    }

    const std::vector<Widget*> targets = _selection.size() > 1u
        ? _selection : std::vector<Widget*>{_selected};
    PendingPropertyMutation pending;
    pending.field = field;
    pending.label = label != nullptr ? label : "Edit property";
    pending.dirtyBefore = _dirty;
    for (Widget* target : targets) {
        if (target == nullptr || target->getId().empty()) {
            beginMutation(LayoutEditKind::Property, label);
            return;
        }
        pending.changes.push_back({target->getId(), field,
            propertyValueForWidget(target, field), {}});
    }
    _pendingPropertyMutation = std::move(pending);
}

void LayoutEditorSession::endPropertyMutation() {
    if (!_pendingPropertyMutation.has_value()) {
        endMutation();
        return;
    }
    PendingPropertyMutation pending =
        std::move(*_pendingPropertyMutation);
    _pendingPropertyMutation.reset();

    bool changed = false;
    for (PropertyValueChange& value : pending.changes) {
        Widget* target = findInDocument(value.widgetId);
        value.afterValue = propertyValueForWidget(target, value.field);
        changed = changed || value.beforeValue != value.afterValue;
    }
    if (!changed) return;

    const bool dirtyAfter = _dirty;
    const std::vector<PropertyValueChange> changes = pending.changes;
    _commandStack.pushTyped(
        [this, changes, dirty = pending.dirtyBefore]() {
            applyTypedPropertyChanges(changes, false, dirty);
        },
        [this, changes, dirtyAfter]() {
            applyTypedPropertyChanges(changes, true, dirtyAfter);
        }, LayoutEditKind::Property, std::move(pending.label));
}

void LayoutEditorSession::applyTypedPropertyChanges(
    const std::vector<PropertyValueChange>& changes,
    bool useAfterValues, bool dirtyState) {
    const std::vector<Widget*> savedSelection = _selection;
    Widget* savedPrimary = _selected;
    for (const PropertyValueChange& change : changes) {
        Widget* target = findInDocument(change.widgetId);
        if (target == nullptr) continue;
        _selected = target;
        _selection.assign(1u, target);
        applyProperty(change.field,
            useAfterValues ? change.afterValue : change.beforeValue);
    }
    _selection = savedSelection;
    _selected = savedPrimary;
    markDirty(dirtyState);
    syncPropertyStrip();
    syncSelectionChrome();
    refreshValidation();
}

void LayoutEditorSession::undo() {
    stopAnimationPreview();
    endPropertyMutation();
    if (!_commandStack.canUndo() || _docRoot == nullptr) {
        setStatus(L"Nothing to undo");
        return;
    }
    if (_commandStack.nextUndoIsTyped()) {
        if (_commandStack.undoTyped()) {
            _dragMode = DragMode::None;
            setStatus(L"Undo");
        }
        return;
    }
    const std::optional<Snapshot> snap =
        _commandStack.undo(captureSnapshot());
    if (!snap) return;
    _dragMode = DragMode::None;
    restoreSnapshot(*snap);
    setStatus(L"Undo");
}

void LayoutEditorSession::redo() {
    stopAnimationPreview();
    endPropertyMutation();
    if (!_commandStack.canRedo() || _docRoot == nullptr) {
        setStatus(L"Nothing to redo");
        return;
    }
    if (_commandStack.nextRedoIsTyped()) {
        if (_commandStack.redoTyped()) {
            _dragMode = DragMode::None;
            setStatus(L"Redo");
        }
        return;
    }
    const std::optional<Snapshot> snap =
        _commandStack.redo(captureSnapshot());
    if (!snap) return;
    _dragMode = DragMode::None;
    restoreSnapshot(*snap);
    setStatus(L"Redo");
}

void LayoutEditorSession::alignSelection(AlignMode mode) {
    if (_selection.size() < 2) {
        setStatus(L"Align needs Ctrl/Shift multi-select (2+ widgets)");
        return;
    }
    Widget* commonParent = _selection.front() != nullptr
        ? _selection.front()->getParent() : nullptr;
    for (Widget* widget : _selection) {
        if (structuredContentOwner(widget) != nullptr ||
            (widget != nullptr && widget->getParent() != nullptr &&
             (dynamic_cast<BoxBase*>(widget->getParent()) != nullptr ||
              dynamic_cast<GridPanel*>(widget->getParent()) != nullptr)) ||
            widget == nullptr || widget->getParent() != commonParent) {
            setStatus(L"Align only applies to free-positioned siblings");
            return;
        }
    }
    pushUndo(LayoutEditKind::Transform, "Align selection");

    float minX = _selection[0]->getPosition().x;
    float minY = _selection[0]->getPosition().y;
    float maxX = minX + _selection[0]->getSize().x;
    float maxY = minY + _selection[0]->getSize().y;
    for (Widget* w : _selection) {
        const auto& p = w->getPosition();
        const auto& s = w->getSize();
        minX = (std::min)(minX, p.x);
        minY = (std::min)(minY, p.y);
        maxX = (std::max)(maxX, p.x + s.x);
        maxY = (std::max)(maxY, p.y + s.y);
    }
    const float midX = (minX + maxX) * 0.5f;
    const float midY = (minY + maxY) * 0.5f;

    auto setX = [](Widget* w, float x) {
        auto p = w->getPosition();
        p.x = x;
        w->setPosition(p);
    };
    auto setY = [](Widget* w, float y) {
        auto p = w->getPosition();
        p.y = y;
        w->setPosition(p);
    };

    const wchar_t* doneMsg = L"Aligned";
    switch (mode) {
    case AlignMode::Left:
        for (Widget* w : _selection) setX(w, minX);
        doneMsg = L"Aligned left";
        break;
    case AlignMode::Right:
        for (Widget* w : _selection) setX(w, maxX - w->getSize().x);
        doneMsg = L"Aligned right";
        break;
    case AlignMode::HCenter:
        for (Widget* w : _selection) setX(w, midX - w->getSize().x * 0.5f);
        doneMsg = L"Aligned horizontal center";
        break;
    case AlignMode::Top:
        for (Widget* w : _selection) setY(w, minY);
        doneMsg = L"Aligned top";
        break;
    case AlignMode::Bottom:
        for (Widget* w : _selection) setY(w, maxY - w->getSize().y);
        doneMsg = L"Aligned bottom";
        break;
    case AlignMode::VCenter:
        for (Widget* w : _selection) setY(w, midY - w->getSize().y * 0.5f);
        doneMsg = L"Aligned vertical center";
        break;
    case AlignMode::DistributeH: {
        if (_selection.size() < 3) {
            setStatus(L"DistH needs 3+ widgets");
            _commandStack.discardLastUndo();
            return;
        }
        std::vector<Widget*> sorted = _selection;
        std::sort(sorted.begin(), sorted.end(), [](Widget* a, Widget* b) {
            return a->getPosition().x < b->getPosition().x;
        });
        float totalW = 0.0f;
        for (Widget* w : sorted) totalW += w->getSize().x;
        const float span = sorted.back()->getPosition().x +
                           sorted.back()->getSize().x -
                           sorted.front()->getPosition().x;
        const float gap = (span - totalW) /
                          static_cast<float>(sorted.size() - 1);
        float x = sorted.front()->getPosition().x;
        for (Widget* w : sorted) {
            setX(w, x);
            x += w->getSize().x + gap;
        }
        doneMsg = L"Distributed horizontally";
        break;
    }
    case AlignMode::DistributeV: {
        if (_selection.size() < 3) {
            setStatus(L"DistV needs 3+ widgets");
            _commandStack.discardLastUndo();
            return;
        }
        std::vector<Widget*> sorted = _selection;
        std::sort(sorted.begin(), sorted.end(), [](Widget* a, Widget* b) {
            return a->getPosition().y < b->getPosition().y;
        });
        float totalH = 0.0f;
        for (Widget* w : sorted) totalH += w->getSize().y;
        const float span = sorted.back()->getPosition().y +
                           sorted.back()->getSize().y -
                           sorted.front()->getPosition().y;
        const float gap = (span - totalH) /
                          static_cast<float>(sorted.size() - 1);
        float y = sorted.front()->getPosition().y;
        for (Widget* w : sorted) {
            setY(w, y);
            y += w->getSize().y + gap;
        }
        doneMsg = L"Distributed vertically";
        break;
    }
    }

    for (Widget* widget : _selection) {
        refreshAnchorOffsets(widget);
    }

    endMutation();
    markDirty(true);
    syncPropertyStrip();
    if (_ui != nullptr) {
        _ui->invalidateLayout();
    }
    setStatus(doneMsg);
}

bool LayoutEditorSession::canUseAnchorLayout(Widget* widget) const {
    if (widget == nullptr || widget == _docRoot || widget->getParent() == nullptr ||
        widget->getParent() == _canvasHost ||
        structuredContentOwner(widget) != nullptr) {
        return false;
    }
    Widget* parent = widget->getParent();
    return dynamic_cast<BoxBase*>(parent) == nullptr &&
           dynamic_cast<GridPanel*>(parent) == nullptr;
}

void LayoutEditorSession::refreshAnchorOffsets(Widget* widget) {
    if (canUseAnchorLayout(widget) && widget->hasAnchorLayout()) {
        widget->refreshAnchorOffsetsFromCurrentRect();
    }
}

void LayoutEditorSession::setAnchorPreset(AnchorAxisMode horizontal,
                                          AnchorAxisMode vertical,
                                          bool snapWidgetToAnchor) {
    auto resolveAxis = [](AnchorAxisMode mode, float& minValue,
                          float& maxValue, float& pivotValue) {
        switch (mode) {
        case AnchorAxisMode::Start:
            minValue = maxValue = 0.0f;
            pivotValue = 0.0f;
            break;
        case AnchorAxisMode::Center:
            minValue = maxValue = 0.5f;
            pivotValue = 0.5f;
            break;
        case AnchorAxisMode::End:
            minValue = maxValue = 1.0f;
            pivotValue = 1.0f;
            break;
        case AnchorAxisMode::Stretch:
            minValue = 0.0f;
            maxValue = 1.0f;
            pivotValue = 0.5f;
            break;
        }
    };

    float minX = 0.0f, maxX = 0.0f, pivotX = 0.5f;
    float minY = 0.0f, maxY = 0.0f, pivotY = 0.5f;
    resolveAxis(horizontal, minX, maxX, pivotX);
    resolveAxis(vertical, minY, maxY, pivotY);

    int changed = 0;
    endMutation();
    pushUndo(LayoutEditKind::Transform, "Set anchor preset");
    for (Widget* widget : _selection) {
        if (!canUseAnchorLayout(widget)) {
            continue;
        }
        widget->setLayoutPositionManaged(false);
        widget->setLayoutSizeManaged(false);
        if (!snapWidgetToAnchor) {
            widget->setAnchorLayoutPreservingRect(
                math::FVector2(minX, minY), math::FVector2(maxX, maxY),
                math::FVector2(pivotX, pivotY));
        } else {
            const math::FVector2 size = widget->getSize();
            AnchorLayout layout;
            layout.anchorMin = math::FVector2(minX, minY);
            layout.anchorMax = math::FVector2(maxX, maxY);
            layout.pivot = math::FVector2(pivotX, pivotY);
            if (minX == maxX) {
                layout.offsetMin.x = -pivotX * size.x;
                layout.offsetMax.x = (1.0f - pivotX) * size.x;
            }
            if (minY == maxY) {
                layout.offsetMin.y = -pivotY * size.y;
                layout.offsetMax.y = (1.0f - pivotY) * size.y;
            }
            widget->setAnchorLayout(layout);
            if (widget->getParent() != nullptr) {
                widget->applyAnchorLayout(widget->getParent()->getSize());
            }
        }
        ++changed;
    }
    if (changed == 0) {
        _commandStack.discardLastUndo();
        setStatus(L"Anchors apply only to free-positioned children");
        return;
    }
    markDirty(true);
    syncPropertyStrip();
    syncSelectionChrome();
    if (_ui != nullptr) {
        _ui->invalidateLayout();
        _ui->layout();
    }
    std::wostringstream status;
    status << (snapWidgetToAnchor
        ? L"Anchor preset snapped " : L"Anchor preset applied to ")
           << changed << L" widget(s)";
    if (!snapWidgetToAnchor) {
        status << L" — Ctrl+click to snap widget";
    }
    setStatus(status.str());
}

void LayoutEditorSession::clearSelectedAnchors() {
    int changed = 0;
    endMutation();
    pushUndo(LayoutEditKind::Transform, "Clear anchors");
    for (Widget* widget : _selection) {
        if (!canUseAnchorLayout(widget) || !widget->hasAnchorLayout()) {
            continue;
        }
        widget->clearAnchorLayout();
        ++changed;
    }
    if (changed == 0) {
        _commandStack.discardLastUndo();
        setStatus(L"Selection has no free-layout anchors");
        return;
    }
    markDirty(true);
    syncPropertyStrip();
    syncSelectionChrome();
    if (_ui != nullptr) {
        _ui->invalidateLayout();
        _ui->layout();
    }
    setStatus(L"Anchors disabled; absolute rectangle preserved");
}

void LayoutEditorSession::syncAnchorPresetStyles() {
    auto setButtonStyle = [this](const std::string& id, bool active) {
        if (auto* button = dynamic_cast<Button*>(findChromeById(id))) {
            button->setStyleId(active ? "__le_primary" : "__le_command");
        }
    };
    for (int vertical = 0; vertical < 4; ++vertical) {
        for (int horizontal = 0; horizontal < 4; ++horizontal) {
            setButtonStyle("btn_anchor_" + std::to_string(vertical) +
                               std::to_string(horizontal), false);
        }
    }

    const bool eligible = canUseAnchorLayout(_selected);
    setButtonStyle("btn_anchor_none",
                   eligible && !_selected->hasAnchorLayout());
    if (!eligible || !_selected->hasAnchorLayout()) {
        return;
    }

    const AnchorLayout& layout = _selected->getAnchorLayout();
    auto axisMode = [](float minValue, float maxValue) {
        constexpr float epsilon = 0.0001f;
        if (std::fabs(minValue) < epsilon &&
            std::fabs(maxValue - 1.0f) < epsilon) return 3;
        if (std::fabs(minValue - maxValue) >= epsilon) return -1;
        if (std::fabs(minValue) < epsilon) return 0;
        if (std::fabs(minValue - 0.5f) < epsilon) return 1;
        if (std::fabs(minValue - 1.0f) < epsilon) return 2;
        return -1;
    };
    const int horizontal = axisMode(layout.anchorMin.x, layout.anchorMax.x);
    const int vertical = axisMode(layout.anchorMin.y, layout.anchorMax.y);
    if (horizontal >= 0 && vertical >= 0) {
        setButtonStyle("btn_anchor_" + std::to_string(vertical) +
                           std::to_string(horizontal), true);
    }
}

void LayoutEditorSession::reorderSelected(int delta) {
    if (_selected == nullptr || _selected == _docRoot || delta == 0 ||
        structuredContentOwner(_selected) != nullptr) {
        return;
    }
    Widget* parent = _selected->getParent();
    if (parent == nullptr || parent == _canvasHost) {
        return;
    }

    pushUndo(LayoutEditKind::Reorder, "Reorder selection");
    bool ok = false;
    if (auto* box = dynamic_cast<BoxBase*>(parent)) {
        const int cur = box->slotIndexOf(_selected);
        const int next = cur + delta;
        if (cur >= 0 && next >= 0 && box->slotAt(next) != nullptr) {
            ok = box->moveSlotToIndex(_selected, static_cast<size_t>(next));
        }
    } else if (auto* grid = dynamic_cast<GridPanel*>(parent)) {
        const int cur = designerGridIndexOf(grid, _selected);
        const int next = cur + delta;
        const int count = static_cast<int>(
            collectDesignerGridEntries(grid).size());
        if (cur >= 0 && next >= 0 && next < count) {
            ok = rebuildDesignerGrid(grid, _selected,
                                     static_cast<size_t>(next));
        }
    } else {
        const auto& kids = parent->getChildren();
        size_t cur = static_cast<size_t>(-1);
        for (size_t i = 0; i < kids.size(); ++i) {
            if (kids[i] == _selected) {
                cur = i;
                break;
            }
        }
        if (cur != static_cast<size_t>(-1)) {
            const int next = static_cast<int>(cur) + delta;
            if (next >= 0 && next < static_cast<int>(kids.size())) {
                ok = parent->moveChildToIndex(
                    _selected, static_cast<size_t>(next));
            }
        }
    }

    endMutation();
    if (!ok) {
        _commandStack.discardLastUndo();
        setStatus(L"Cannot reorder further");
        return;
    }
    markDirty(true);
    refreshHierarchy();
    select(_selected, false);
    if (_ui != nullptr) {
        _ui->invalidateLayout();
        _ui->layout();
    }
    setStatus(delta < 0 ? L"Moved up" : L"Moved down");
}

LayoutEditorSession::DragMode LayoutEditorSession::hitTestResizeHandle(
    Widget* widget, const math::FVector2& worldPos) const {
    if (widget == nullptr) {
        return DragMode::None;
    }
    const math::FRectangle b = documentToScreen(widget->getWorldBounds());
    const float x = worldPos.x;
    const float y = worldPos.y;
    const bool nearL = std::abs(x - b.minX) <= kHandleHit;
    const bool nearR = std::abs(x - b.maxX) <= kHandleHit;
    const bool nearT = std::abs(y - b.minY) <= kHandleHit;
    const bool nearB = std::abs(y - b.maxY) <= kHandleHit;
    const bool inX = x >= b.minX - kHandleHit && x <= b.maxX + kHandleHit;
    const bool inY = y >= b.minY - kHandleHit && y <= b.maxY + kHandleHit;
    if (!inX || !inY) {
        return DragMode::None;
    }
    if (widget == _docRoot) {
        if (previewOverridesDocumentSize()) return DragMode::None;
        if (nearB && nearR) return DragMode::ResizeSE;
        if (nearB) return DragMode::ResizeS;
        if (nearR) return DragMode::ResizeE;
        return DragMode::None;
    }
    if (nearT && nearL) return DragMode::ResizeNW;
    if (nearT && nearR) return DragMode::ResizeNE;
    if (nearB && nearL) return DragMode::ResizeSW;
    if (nearB && nearR) return DragMode::ResizeSE;
    if (nearT) return DragMode::ResizeN;
    if (nearB) return DragMode::ResizeS;
    if (nearL) return DragMode::ResizeW;
    if (nearR) return DragMode::ResizeE;
    if (b.contains(worldPos)) return DragMode::Move;
    return DragMode::None;
}

void LayoutEditorSession::applyResizeDelta(Widget* widget, DragMode mode,
                                           const math::FVector2& delta) {
    if (widget == nullptr || (delta.x == 0.0f && delta.y == 0.0f)) {
        return;
    }
    math::FVector2 pos = widget->getPosition();
    math::FVector2 size = widget->getSize();

    auto clampSize = [](float& s) {
        if (s < kMinWidgetSize) s = kMinWidgetSize;
    };

    switch (mode) {
    case DragMode::ResizeE:
        size.x += delta.x;
        clampSize(size.x);
        break;
    case DragMode::ResizeW:
        pos.x += delta.x;
        size.x -= delta.x;
        if (size.x < kMinWidgetSize) {
            pos.x -= (kMinWidgetSize - size.x);
            size.x = kMinWidgetSize;
        }
        break;
    case DragMode::ResizeS:
        size.y += delta.y;
        clampSize(size.y);
        break;
    case DragMode::ResizeN:
        pos.y += delta.y;
        size.y -= delta.y;
        if (size.y < kMinWidgetSize) {
            pos.y -= (kMinWidgetSize - size.y);
            size.y = kMinWidgetSize;
        }
        break;
    case DragMode::ResizeSE:
        size.x += delta.x;
        size.y += delta.y;
        clampSize(size.x);
        clampSize(size.y);
        break;
    case DragMode::ResizeSW:
        pos.x += delta.x;
        size.x -= delta.x;
        size.y += delta.y;
        if (size.x < kMinWidgetSize) {
            pos.x -= (kMinWidgetSize - size.x);
            size.x = kMinWidgetSize;
        }
        clampSize(size.y);
        break;
    case DragMode::ResizeNE:
        size.x += delta.x;
        pos.y += delta.y;
        size.y -= delta.y;
        clampSize(size.x);
        if (size.y < kMinWidgetSize) {
            pos.y -= (kMinWidgetSize - size.y);
            size.y = kMinWidgetSize;
        }
        break;
    case DragMode::ResizeNW:
        pos.x += delta.x;
        size.x -= delta.x;
        pos.y += delta.y;
        size.y -= delta.y;
        if (size.x < kMinWidgetSize) {
            pos.x -= (kMinWidgetSize - size.x);
            size.x = kMinWidgetSize;
        }
        if (size.y < kMinWidgetSize) {
            pos.y -= (kMinWidgetSize - size.y);
            size.y = kMinWidgetSize;
        }
        break;
    default:
        return;
    }
    widget->setPosition(pos);
    widget->setSize(size);
    refreshAnchorOffsets(widget);
}

Widget* LayoutEditorSession::pickDocumentWidget(
    const math::FVector2& worldPos) const {
    if (_docRoot == nullptr || !isCanvasHit(worldPos)) {
        return nullptr;
    }
    const math::FVector2 documentPos = screenToDocument(worldPos);
    // Deepest-first walk of the authored document only. Composite runtime
    // controls own implementation children (virtual rows, scrollbars, tab
    // buttons, dialog chrome); those must never become Designer selections.
    std::function<Widget*(Widget*)> walk = [&](Widget* n) -> Widget* {
        if (n == nullptr || !n->isVisible() ||
            !n->getWorldBounds().contains(documentPos)) {
            return nullptr;
        }
        std::vector<Widget*> kids;
        collectAuthoredChildren(n, kids);
        for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
            // An inactive TabControl page is authored but intentionally not
            // mounted. It can be selected from the Outline, not by stale
            // canvas geometry left from its previous activation.
            if (!isUnderCanvas(*it)) continue;
            if (Widget* hit = walk(*it)) {
                return hit;
            }
        }
        return n;
    };
    return walk(_docRoot);
}

bool LayoutEditorSession::isCanvasHit(const math::FVector2& worldPos) const {
    return _canvasHost != nullptr &&
           _canvasHost->getWorldBounds().contains(worldPos);
}

bool LayoutEditorSession::isEditorOverlay(Widget* widget) const {
    if (widget == nullptr) {
        return false;
    }
    if (widget == _selBox) {
        return true;
    }
    for (Panel* h : _handles) {
        if (h == widget) {
            return true;
        }
    }
    const std::string& id = widget->getId();
    return id.size() >= 4 && id.compare(0, 4, "__le") == 0;
}

void LayoutEditorSession::placeHandle(Widget* handle, float x, float y) {
    if (handle == nullptr) {
        return;
    }
    handle->setPosition(math::FVector2(x - kHandleSize * 0.5f,
                                       y - kHandleSize * 0.5f));
    handle->setSize(math::FVector2(kHandleSize, kHandleSize));
    handle->setVisible(true);
}

void LayoutEditorSession::destroySelectionChrome() {
    if (_canvasHost == nullptr) {
        _selBox = nullptr;
        for (Panel*& h : _handles) {
            h = nullptr;
        }
        _anchorBox = nullptr;
        for (Panel*& point : _anchorPoints) point = nullptr;
        _selOutlines.clear();
        _marqueeBox = nullptr;
        _safeAreaBox = nullptr;
        return;
    }
    auto destroyOne = [this](Panel*& p) {
        if (p == nullptr) {
            return;
        }
        if (p->getParent() == _canvasHost) {
            _canvasHost->removeChild(p);
        }
        destroyWidgetTree(p);
        p = nullptr;
    };
    destroyOne(_selBox);
    for (Panel*& h : _handles) {
        destroyOne(h);
    }
    destroyOne(_anchorBox);
    for (Panel*& point : _anchorPoints) {
        destroyOne(point);
    }
    for (Panel*& o : _selOutlines) {
        destroyOne(o);
    }
    _selOutlines.clear();
    destroyOne(_marqueeBox);
    destroyOne(_safeAreaBox);
}

void LayoutEditorSession::ensureSelectionChrome() {
    if (_canvasHost == nullptr) {
        return;
    }
    if (_selBox != nullptr) {
        return;
    }

    if (auto* sheet = StyleManager::get().getStyleSheet()) {
        WidgetStyle box{};
        box.backgroundColor = math::FVector4(0.20f, 0.55f, 0.95f, 0.0f);
        box.borderColor = math::FVector4(0.35f, 0.75f, 1.0f, 1.0f);
        box.border.color = box.borderColor;
        box.border.width = 2.0f;
        sheet->setStyle("__le_sel_box", box);

        WidgetStyle handle{};
        handle.backgroundColor = math::FVector4(0.95f, 0.95f, 1.0f, 1.0f);
        handle.borderColor = math::FVector4(0.20f, 0.55f, 0.95f, 1.0f);
        handle.border.color = handle.borderColor;
        handle.border.width = 1.0f;
        sheet->setStyle("__le_sel_handle", handle);

        WidgetStyle anchorBox{};
        anchorBox.backgroundColor = math::FVector4(1.0f, 0.67f, 0.20f, 0.0f);
        anchorBox.borderColor = math::FVector4(1.0f, 0.67f, 0.20f, 0.85f);
        anchorBox.border.color = anchorBox.borderColor;
        anchorBox.border.width = 1.0f;
        sheet->setStyle("__le_anchor_box", anchorBox);

        WidgetStyle anchorPoint{};
        anchorPoint.backgroundColor = math::FVector4(1.0f, 0.72f, 0.25f, 1.0f);
        anchorPoint.borderColor = math::FVector4(0.18f, 0.12f, 0.04f, 1.0f);
        anchorPoint.border.color = anchorPoint.borderColor;
        anchorPoint.border.width = 1.0f;
        sheet->setStyle("__le_anchor_point", anchorPoint);

        WidgetStyle safeArea{};
        safeArea.backgroundColor = math::FVector4(0.95f, 0.66f, 0.18f, 0.0f);
        safeArea.borderColor = math::FVector4(0.95f, 0.66f, 0.18f, 0.9f);
        safeArea.border.color = safeArea.borderColor;
        safeArea.border.width = 1.0f;
        sheet->setStyle("__le_safe_area", safeArea);
    }

    _safeAreaBox = new Panel();
    _safeAreaBox->setId("__le_safe_area");
    _safeAreaBox->setStyleId("__le_safe_area");
    _safeAreaBox->setBorderEnabled(true);
    _safeAreaBox->setBackgroundEnabled(true);
    _safeAreaBox->setVisible(false);
    _safeAreaBox->setLayoutPositionManaged(false);
    _canvasHost->addChild(_safeAreaBox);

    _anchorBox = new Panel();
    _anchorBox->setId("__le_anchor_box");
    _anchorBox->setStyleId("__le_anchor_box");
    _anchorBox->setBorderEnabled(true);
    _anchorBox->setBackgroundEnabled(true);
    _anchorBox->setVisible(false);
    _anchorBox->setLayoutPositionManaged(false);
    _canvasHost->addChild(_anchorBox);
    for (int i = 0; i < 4; ++i) {
        _anchorPoints[i] = new Panel();
        _anchorPoints[i]->setId(
            std::string("__le_anchor_point_") + std::to_string(i));
        _anchorPoints[i]->setStyleId("__le_anchor_point");
        _anchorPoints[i]->setBorderEnabled(true);
        _anchorPoints[i]->setBackgroundEnabled(true);
        _anchorPoints[i]->setVisible(false);
        _anchorPoints[i]->setLayoutPositionManaged(false);
        _canvasHost->addChild(_anchorPoints[i]);
    }

    _selBox = new Panel();
    _selBox->setId("__le_sel_box");
    _selBox->setStyleId("__le_sel_box");
    _selBox->setBorderEnabled(true);
    _selBox->setBackgroundEnabled(true);
    _selBox->setVisible(false);
    _selBox->setLayoutPositionManaged(false);
    _canvasHost->addChild(_selBox);

    for (int i = 0; i < 8; ++i) {
        _handles[i] = new Panel();
        _handles[i]->setId(std::string("__le_handle_") + std::to_string(i));
        _handles[i]->setStyleId("__le_sel_handle");
        _handles[i]->setBorderEnabled(true);
        _handles[i]->setVisible(false);
        _handles[i]->setLayoutPositionManaged(false);
        _canvasHost->addChild(_handles[i]);
    }
}

void LayoutEditorSession::syncPreviewChrome() {
    if (_safeAreaBox == nullptr || _canvasHost == nullptr || _docRoot == nullptr) {
        return;
    }
    const LayoutPreviewSettings& settings = _previewModel.settings();
    if (!settings.showSafeArea) {
        _safeAreaBox->setVisible(false);
        return;
    }
    const math::FVector4 inset = _previewModel.logicalSafeArea();
    const math::FRectangle root = _docRoot->getWorldBounds();
    const float minX = std::min(root.maxX, root.minX + inset.x);
    const float minY = std::min(root.maxY, root.minY + inset.y);
    const float maxX = std::max(minX, root.maxX - inset.z);
    const float maxY = std::max(minY, root.maxY - inset.w);
    const math::FRectangle screen = documentToScreen(
        math::FRectangle(minX, minY, maxX, maxY));
    const math::FRectangle host = _canvasHost->getWorldBounds();
    _safeAreaBox->setPosition({std::round(screen.minX - host.minX),
                               std::round(screen.minY - host.minY)});
    _safeAreaBox->setSize({std::max(1.0f, std::round(screen.maxX - screen.minX)),
                           std::max(1.0f, std::round(screen.maxY - screen.minY))});
    _safeAreaBox->setVisible(true);
}

UiCursorHint LayoutEditorSession::canvasCursorHint(
    const math::FVector2& worldPos) const {
    if (_mode == Mode::Interact) return UiCursorHint::Default;
    if (!isCanvasHit(worldPos)) {
        return UiCursorHint::Default;
    }
    if (_dragMode != DragMode::None) {
        switch (_dragMode) {
        case DragMode::ResizeE:
        case DragMode::ResizeW:
            return UiCursorHint::SizeWe;
        case DragMode::ResizeN:
        case DragMode::ResizeS:
            return UiCursorHint::SizeNs;
        case DragMode::ResizeNW:
        case DragMode::ResizeSE:
            return UiCursorHint::SizeNwse;
        case DragMode::ResizeNE:
        case DragMode::ResizeSW:
            return UiCursorHint::SizeNesw;
        case DragMode::Move:
            return structuredContentOwner(_selected) == nullptr
                ? UiCursorHint::Move : UiCursorHint::Default;
        default:
            break;
        }
    }
    if (_selected != nullptr) {
        switch (hitTestResizeHandle(_selected, worldPos)) {
        case DragMode::ResizeE:
        case DragMode::ResizeW:
            return UiCursorHint::SizeWe;
        case DragMode::ResizeN:
        case DragMode::ResizeS:
            return UiCursorHint::SizeNs;
        case DragMode::ResizeNW:
        case DragMode::ResizeSE:
            return UiCursorHint::SizeNwse;
        case DragMode::ResizeNE:
        case DragMode::ResizeSW:
            return UiCursorHint::SizeNesw;
        case DragMode::Move:
            return UiCursorHint::Move;
        default:
            break;
        }
    }
    Widget* picked = pickDocumentWidget(worldPos);
    if (picked != nullptr && picked != _docRoot &&
        structuredContentOwner(picked) == nullptr) {
        return UiCursorHint::Move;
    }
    return UiCursorHint::Default;
}

bool LayoutEditorSession::onPointerDown(const math::FVector2& worldPos,
                                        int button) {
    if (_mode == Mode::Interact) {
        return false;
    }
    if (_toolDrag != ToolDrag::None || _dragMode != DragMode::None) {
        if (_dragMode == DragMode::Marquee) {
            clearMarqueeChrome();
        }
        cancelToolDrag();
        _dragMode = DragMode::None;
        _dragTarget = nullptr;
    }

    // Middle mouse or Space+LMB pans the editor viewport. It must never
    // rewrite the authored root position or participate in undo/dirty state.
    if ((button == 2 && isCanvasHit(worldPos)) ||
        (button == 0 && modifiersSpace() && isCanvasHit(worldPos))) {
        if (_docRoot != nullptr) {
            _dragMode = DragMode::Pan;
            _dragLastMouse = worldPos;
            setStatus(L"Panning canvas");
            return true;
        }
    }

    if (button != 0) {
        return false;
    }

    std::string palType;
    if (hitPaletteType(worldPos, palType)) {
        _toolDrag = ToolDrag::PalettePress;
        _paletteType = std::move(palType);
        _toolPressPos = worldPos;
        _dragLastMouse = worldPos;
        return true;
    }

    if (hitHierarchyList(worldPos) && !hierarchyScrollbarHit(worldPos)) {
        float frac = 0.0f;
        const int idx = hierarchyIndexAt(worldPos, &frac);
        if (idx >= 0 && idx < static_cast<int>(_hierarchyIndex.size())) {
            Widget* node = _hierarchyIndex[static_cast<size_t>(idx)];
            const bool additive = modifiersAdditive();
            select(node, additive);
            _consumeNextPointerUp = true;
            if (!hierarchyFilterActive() && !additive && node != nullptr
                && node != _docRoot &&
                structuredContentOwner(node) == nullptr) {
                _toolDrag = ToolDrag::HierPress;
                _hierDragWidget = node;
                _toolPressPos = worldPos;
                _dragLastMouse = worldPos;
            }
            return true;
        }
        _consumeNextPointerUp = true;
        return true;
    }

    if (!isCanvasHit(worldPos)) {
        return false;
    }

    auto isLayoutChild = [](Widget* w) -> bool {
        return w != nullptr && w->getParent() != nullptr &&
               (dynamic_cast<BoxBase*>(w->getParent()) != nullptr ||
                dynamic_cast<GridPanel*>(w->getParent()) != nullptr);
    };

    if (_selected != nullptr &&
        !isLayoutChild(_selected) && !modifiersAdditive()) {
        const DragMode handle = hitTestResizeHandle(_selected, worldPos);
        if (handle != DragMode::None && handle != DragMode::Move) {
            beginMutation(LayoutEditKind::Transform, "Resize widget");
            _dragMode = handle;
            _dragTarget = _selected;
            _dragLastMouse = worldPos;
            return true;
        }
    }

    Widget* hit = pickDocumentWidget(worldPos);
    if (hit == nullptr) {
        if (!modifiersAdditive()) {
            select(nullptr, false);
        }
        _dragMode = DragMode::Marquee;
        _marqueeStart = worldPos;
        _dragLastMouse = worldPos;
        updateMarqueeChrome(_marqueeStart, worldPos);
        setStatus(L"Marquee select — drag to box-select");
        return true;
    }

    const bool additive = modifiersAdditive();
    const bool hitInSelection = selectionContains(hit);

    if (additive) {
        select(hit, true);
        _dragMode = DragMode::None;
        _dragTarget = nullptr;
        _consumeNextPointerUp = true;
        return true;
    }

    // Clicking an already-selected widget keeps the multi-selection so the
    // whole set can be dragged together (Photoshop-style group move).
    if (hitInSelection && _selection.size() >= 2) {
        _selected = hit;
        syncPropertyStrip();
        syncHierarchySelection();
        syncSelectionChrome();
    } else {
        select(hit, false);
    }

    if (hit == _docRoot || (_selected == _docRoot && _selection.size() == 1) ||
        structuredContentOwner(hit) != nullptr) {
        setStatus(hit == _docRoot
            ? L"Document root selected — drag right/bottom handles to resize"
            : L"Structured content selected — position is owned by its container");
        _dragMode = DragMode::None;
        _dragTarget = nullptr;
        _consumeNextPointerUp = true;
        return true;
    }

    auto canFreeMove = [this, &isLayoutChild](Widget* w) -> bool {
        return w != nullptr && !isLayoutChild(w) &&
               structuredContentOwner(w) == nullptr;
    };

    int movable = 0;
    for (Widget* w : _selection) {
        if (w != nullptr && w != _docRoot && canFreeMove(w)) {
            ++movable;
        }
    }
    if (movable == 0) {
        if (isLayoutChild(hit)) {
            setStatus(L"Layout child — drag in Hierarchy to reorder (canvas drag disabled)");
        } else {
            setStatus(L"Selection has no free-position widgets to drag");
        }
        _dragMode = DragMode::None;
        _dragTarget = nullptr;
        _consumeNextPointerUp = true;
        return true;
    }

    beginMutation(LayoutEditKind::Transform, "Move selection");
    _dragMode = DragMode::Move;
    _dragTarget = hit;
    _dragLastMouse = worldPos;
    for (Widget* w : _selection) {
        if (w != nullptr && w != _docRoot && canFreeMove(w)) {
            w->setLayoutPositionManaged(false);
        }
    }
    if (movable >= 2) {
        std::wostringstream oss;
        oss << L"Moving " << movable << L" widgets";
        setStatus(oss.str());
    }
    return true;
}

bool LayoutEditorSession::onPointerMove(const math::FVector2& worldPos) {
    if (_mode == Mode::Interact) return false;
    if (_toolDrag == ToolDrag::PalettePress ||
        _toolDrag == ToolDrag::HierPress) {
        const float dx = worldPos.x - _toolPressPos.x;
        const float dy = worldPos.y - _toolPressPos.y;
        const bool movedEnough =
            (dx * dx + dy * dy) >= kDragThreshold * kDragThreshold;
        if (_toolDrag == ToolDrag::PalettePress) {
            if (movedEnough || !stillOnPaletteSource(worldPos)) {
                _toolDrag = ToolDrag::PaletteDrag;
                showPaletteGhost(_paletteType);
                setStatus(utf8ToWide("Drag " + _paletteType +
                    " — release on canvas to place"));
            }
        } else if (movedEnough) {
            _toolDrag = ToolDrag::HierDrag;
            ensureHierDropChrome();
            setStatus(L"Reorder — line=before/after, band=into");
        }
    }

    if (_toolDrag == ToolDrag::PaletteDrag) {
        _dragLastMouse = worldPos;
        const bool overCanvas = isCanvasHit(worldPos);
        updatePaletteGhost(worldPos, overCanvas);
        clearHierDropChrome();
        return true;
    }

    if (_toolDrag == ToolDrag::HierDrag) {
        _dragLastMouse = worldPos;
        HierDropTarget drop = resolveHierDrop(worldPos, _hierDragWidget);
        updateHierDropChrome(drop);
        return true;
    }

    if (_toolDrag == ToolDrag::PalettePress ||
        _toolDrag == ToolDrag::HierPress) {
        return true;
    }

    if (_dragMode == DragMode::None) {
        return false;
    }
    const math::FVector2 delta = worldPos - _dragLastMouse;
    _dragLastMouse = worldPos;

    if (_dragMode == DragMode::Marquee) {
        updateMarqueeChrome(_marqueeStart, worldPos);
        return true;
    }

    if (_dragMode == DragMode::Pan) {
        if (delta.x != 0.0f || delta.y != 0.0f) {
            _viewPan.x += delta.x;
            _viewPan.y += delta.y;
            applyViewportTransform();
            syncSelectionChrome();
        }
        return true;
    }

    if (delta.x == 0.0f && delta.y == 0.0f) {
        return true;
    }

    const math::FVector2 documentDelta(
        delta.x / _viewZoom, delta.y / _viewZoom);

    if (_dragMode == DragMode::Move) {
        if (!_selection.empty()) {
            for (Widget* w : _selection) {
                if (w == nullptr || w == _docRoot ||
                    structuredContentOwner(w) != nullptr) {
                    continue;
                }
                if (w->getParent() != nullptr &&
                    (dynamic_cast<BoxBase*>(w->getParent()) != nullptr ||
                     dynamic_cast<GridPanel*>(w->getParent()) != nullptr)) {
                    continue;
                }
                math::FVector2 pos = w->getPosition();
                pos.x += documentDelta.x;
                pos.y += documentDelta.y;
                w->setPosition(pos);
                refreshAnchorOffsets(w);
            }
        } else if (_dragTarget != nullptr && _dragTarget != _docRoot) {
            math::FVector2 pos = _dragTarget->getPosition();
            pos.x += documentDelta.x;
            pos.y += documentDelta.y;
            _dragTarget->setPosition(pos);
            refreshAnchorOffsets(_dragTarget);
        }
    } else if (_dragTarget != nullptr) {
        applyResizeDelta(_dragTarget, _dragMode, documentDelta);
    }

    markDirty(true);
    syncPropertyStrip();
    syncSelectionChrome();
    return true;
}

bool LayoutEditorSession::onPointerUp(const math::FVector2& worldPos,
                                      int /*button*/) {
    if (_mode == Mode::Interact) return false;
    if (_consumeNextPointerUp) {
        _consumeNextPointerUp = false;
        // Still allow hierarchy drag-commit paths below if a drag started.
        if (_toolDrag == ToolDrag::None && _dragMode == DragMode::None) {
            return true;
        }
    }

    if (_toolDrag == ToolDrag::PalettePress) {
        const std::string type = _paletteType;
        cancelToolDrag();
        if (!type.empty()) {
            addWidget(type);
        }
        return true;
    }
    if (_toolDrag == ToolDrag::PaletteDrag) {
        commitPaletteDrop(worldPos);
        cancelToolDrag();
        return true;
    }
    if (_toolDrag == ToolDrag::HierPress) {
        cancelToolDrag();
        return true;
    }
    if (_toolDrag == ToolDrag::HierDrag) {
        commitHierarchyDrop(worldPos);
        cancelToolDrag();
        return true;
    }

    if (_dragMode == DragMode::None) {
        return false;
    }

    if (_dragMode == DragMode::Marquee) {
        commitMarquee(_marqueeStart, worldPos, modifiersAdditive());
        clearMarqueeChrome();
        _dragMode = DragMode::None;
        return true;
    }

    if (_dragMode == DragMode::Pan) {
        _dragMode = DragMode::None;
        setStatus(L"Pan done");
        syncSelectionChrome();
        return true;
    }

    if (_dragMode == DragMode::Move) {
        snapSelectionPositions();
    }

    _dragMode = DragMode::None;
    _dragTarget = nullptr;
    endMutation();
    if (_docRoot != nullptr) {
        freezeDocumentInteraction(_docRoot);
    }
    syncSelectionChrome();
    syncPropertyStrip();
    if (_ui != nullptr) {
        _ui->invalidateLayout();
        _ui->layout();
    }
    return true;
}

bool LayoutEditorSession::onWheel(const math::FVector2& worldPos,
                                  float deltaY) {
    if (!isCanvasHit(worldPos) || !modifiersCtrl()) {
        return false;
    }
    // deltaY is pixels; notch ~40 → zoom step
    const float steps = -deltaY / 40.0f;
    const float factor = std::pow(1.1f, steps);
    setViewZoom(_viewZoom * factor, worldPos);
    return true;
}

bool LayoutEditorSession::chromeEditingText() const {
    if (_ui == nullptr) {
        return false;
    }
    Widget* focused = _ui->getFocusedWidget();
    return dynamic_cast<TextInput*>(focused) != nullptr;
}

bool LayoutEditorSession::modifiersCtrl() const {
    if (_ui == nullptr) return false;
    const uint32_t mods = _ui->getModifiers();
    return (mods & (1u << (UIKey_Control - UIKey_Shift))) != 0u;
}

bool LayoutEditorSession::modifiersShift() const {
    if (_ui == nullptr) return false;
    const uint32_t mods = _ui->getModifiers();
    return (mods & (1u << (UIKey_Shift - UIKey_Shift))) != 0u;
}

bool LayoutEditorSession::modifiersAdditive() const {
    return modifiersCtrl() || modifiersShift();
}

bool LayoutEditorSession::modifiersSpace() const {
    return _spaceDown;
}

bool LayoutEditorSession::onKeyDown(int uiKeyCode) {
    if (_ui == nullptr) {
        return false;
    }

    if (uiKeyCode == UIKey_Space) {
        _spaceDown = true;
    }

    if (uiKeyCode == UIKey_F6) {
        setMode(_mode == Mode::Edit ? Mode::Interact : Mode::Edit);
        return true;
    }
    if (_mode == Mode::Interact) {
        if (uiKeyCode == UIKey_Escape) {
            setMode(Mode::Edit);
            return true;
        }
        return false;
    }

    if (modifiersCtrl() && uiKeyCode == UIKey_Z) {
        if (modifiersShift()) {
            redo();
        } else {
            undo();
        }
        return true;
    }
    if (modifiersCtrl() && uiKeyCode == UIKey_Y) {
        redo();
        return true;
    }
    if (modifiersCtrl() && uiKeyCode == UIKey_S) {
        _deferred = DeferredAction::Save;
        return true;
    }

    if (chromeEditingText()) {
        return false;
    }

    if (modifiersCtrl() && uiKeyCode == UIKey_A) {
        selectAll();
        return true;
    }
    if (modifiersCtrl() && uiKeyCode == UIKey_C) {
        copySelection();
        return true;
    }
    if (modifiersCtrl() && uiKeyCode == UIKey_V) {
        pasteClipboard();
        return true;
    }
    if (modifiersCtrl() && uiKeyCode == UIKey_D) {
        duplicateSelection();
        return true;
    }

    if (uiKeyCode == UIKey_G) {
        toggleSnap();
        return true;
    }
    if (uiKeyCode == UIKey_Escape) {
        select(nullptr, false);
        setStatus(L"Selection cleared");
        return true;
    }

    if (uiKeyCode == UIKey_Delete || uiKeyCode == UIKey_Backspace) {
        if (!_selection.empty()) {
            deleteSelected();
            return true;
        }
    }

    float step = modifiersShift() ? _gridSize : 1.0f;
    if (uiKeyCode == UIKey_Left) {
        nudgeSelection(-step, 0.0f);
        return true;
    }
    if (uiKeyCode == UIKey_Right) {
        nudgeSelection(step, 0.0f);
        return true;
    }
    if (uiKeyCode == UIKey_Up) {
        nudgeSelection(0.0f, -step);
        return true;
    }
    if (uiKeyCode == UIKey_Down) {
        nudgeSelection(0.0f, step);
        return true;
    }
    return false;
}

void LayoutEditorSession::onKeyUp(int uiKeyCode) {
    if (uiKeyCode == UIKey_Space) {
        _spaceDown = false;
    }
}

void LayoutEditorSession::setStatus(const std::wstring& text) {
    if (_status != nullptr) {
        _status->setText(text);
    }
}

void LayoutEditorSession::setChromeVisible(const char* id, bool visible) {
    if (_ui == nullptr || id == nullptr) {
        return;
    }
    if (Widget* w = findChromeById(id)) {
        w->setVisible(visible);
    }
}

void LayoutEditorSession::setChromeEnabled(const char* id, bool enabled) {
    if (_ui == nullptr || id == nullptr) {
        return;
    }
    if (auto* interactive = dynamic_cast<InteractiveWidget*>(
            findChromeById(id))) {
        interactive->setEnabled(enabled);
    }
}

void LayoutEditorSession::updatePropPanelVisibility() {
    const bool hasSel = _selected != nullptr;
    const bool isDocumentRoot = hasSel && _selected == _docRoot;
    const bool isStructuredRoot = hasSel &&
        structuredContentOwner(_selected) != nullptr;
    const WidgetAuthoringDescriptor* authoring =
        WidgetAuthoringRegistry::get().findForWidget(_selected);
    static const PropertySchema fallbackProperties{
        AuthoringProperty::Id, AuthoringProperty::X, AuthoringProperty::Y,
        AuthoringProperty::Width, AuthoringProperty::Height,
        AuthoringProperty::Style};
    const auto propertiesFor = [&](Widget* widget) -> const PropertySchema& {
        const WidgetAuthoringDescriptor* descriptor =
            WidgetAuthoringRegistry::get().findForWidget(widget);
        return descriptor != nullptr
            ? descriptor->properties : fallbackProperties;
    };
    const auto hasProperty = [&](AuthoringProperty property) {
        if (!hasSel) return false;
        const std::vector<Widget*> targets = _selection.empty()
            ? std::vector<Widget*>{_selected} : _selection;
        return std::all_of(targets.begin(), targets.end(),
            [&](Widget* widget) {
                return widget != nullptr &&
                    propertiesFor(widget).contains(property);
            });
    };
    const auto hasSection = [&](PropertySection section) {
        if (!hasSel) return false;
        const std::vector<Widget*> targets = _selection.empty()
            ? std::vector<Widget*>{_selected} : _selection;
        return std::all_of(targets.begin(), targets.end(),
            [&](Widget* widget) {
                return widget != nullptr &&
                    propertiesFor(widget).hasSection(section);
            });
    };
    _structuredModel.bind(_selected);
    const LayoutStructuredKind structuredKind = _structuredModel.kind();

    // Every widget type exposes a different subset of rows. Retaining a
    // large scroll offset from the previous selection can move all rows of
    // a shorter Inspector (especially document_root) above the clip and
    // make the panel look empty. Start each new selection at its first row;
    // edits on the same selection preserve the user's current scroll.
    if (_inspectorSelection != _selected) {
        if (auto* scroll = dynamic_cast<ScrollView*>(
                findChromeById("props_scroll"))) {
            scroll->setScrollOffset(math::FVector2(0.0f, 0.0f));
        }
        _inspectorSelection = _selected;
    }
    // Free position: hide x/y for box-managed children (layout owns them).
    bool showPos = hasSel && !isDocumentRoot && !isStructuredRoot;
    if (hasSel && _selected->getParent() != nullptr &&
        (dynamic_cast<BoxBase*>(_selected->getParent()) != nullptr ||
         dynamic_cast<GridPanel*>(_selected->getParent()) != nullptr)) {
        showPos = false;
    }

    // Never leave focus on a prop control we are about to hide.
    if (_ui != nullptr) {
        if (Widget* focused = _ui->getFocusedWidget()) {
            Widget* propsCol = findChromeById("props_col");
            if (propsCol != nullptr) {
                for (Widget* w = focused; w != nullptr; w = w->getParent()) {
                    if (w == propsCol) {
                        _ui->setFocus(nullptr);
                        break;
                    }
                }
            }
        }
    }

    setChromeVisible("lbl_prop_empty", !hasSel);

    auto row = [this](const char* container, const char* lbl,
                      const char* ctrl, bool on) {
        // Modern chrome groups each label/control pair into one compact row.
        // Keep the child fallback so older standalone chrome resources remain
        // compatible with the shared editor session.
        if (findChromeById(container) != nullptr) {
            setChromeVisible(container, on);
        } else {
            setChromeVisible(lbl, on);
            setChromeVisible(ctrl, on);
        }
    };

    Widget* arrangeParent = !_selection.empty() && _selection.front() != nullptr
        ? _selection.front()->getParent() : nullptr;
    bool freeSiblingSelection = arrangeParent != nullptr;
    for (Widget* widget : _selection) {
        if (!canUseAnchorLayout(widget) || widget->getParent() != arrangeParent) {
            freeSiblingSelection = false;
            break;
        }
    }
    const bool canArrange = hasSel && !isDocumentRoot && !isStructuredRoot &&
                            canUseAnchorLayout(_selected);
    const bool canAlign = freeSiblingSelection && _selection.size() >= 2;
    const bool canDistribute = freeSiblingSelection && _selection.size() >= 3;
    const bool canAnchor = canUseAnchorLayout(_selected);
    const bool hasAnchors = canAnchor && _selected->hasAnchorLayout();
    setChromeVisible("section_arrange", canArrange);
    setChromeVisible("row_arrange_horizontal", canArrange);
    setChromeVisible("row_arrange_vertical", canArrange);
    setChromeVisible("row_arrange_distribute", canArrange);
    for (const char* id : {"btn_align_left", "btn_align_hcenter",
                           "btn_align_right", "btn_align_top",
                           "btn_align_vcenter", "btn_align_bottom"}) {
        setChromeEnabled(id, canAlign);
    }
    setChromeEnabled("btn_dist_h", canDistribute);
    setChromeEnabled("btn_dist_v", canDistribute);
    setChromeEnabled("btn_snap", canArrange);

    setChromeVisible("section_anchor", canAnchor);
    setChromeVisible("row_anchor_presets_0", canAnchor);
    setChromeVisible("row_anchor_presets_1", canAnchor);
    setChromeVisible("row_anchor_presets_2", canAnchor);
    setChromeVisible("row_anchor_presets_3", canAnchor);
    setChromeVisible("row_anchor_none", canAnchor);
    setChromeVisible("row_prop_anchor_min", hasAnchors);
    setChromeVisible("row_prop_anchor_max", hasAnchors);
    setChromeVisible("row_prop_offset_min", hasAnchors);
    setChromeVisible("row_prop_offset_max", hasAnchors);
    setChromeVisible("row_prop_pivot", hasAnchors);
    const bool canResponsive = hasSel && _selection.size() == 1u &&
                               !isDocumentRoot && !isStructuredRoot;
    setChromeVisible("section_responsive", canResponsive);
    setChromeVisible("row_responsive_breakpoint", canResponsive);
    setChromeVisible("row_responsive_visibility", canResponsive);
    setChromeVisible("row_responsive_actions", canResponsive);
    setChromeVisible("responsive_status", canResponsive);
    const bool hasDocument = _docRoot != nullptr;
    setChromeVisible("section_reuse", hasDocument);
    setChromeVisible("reuse_name", hasDocument);
    setChromeVisible("reuse_list", hasDocument);
    setChromeVisible("row_reuse_actions", hasDocument);
    setChromeVisible("reuse_status", hasDocument);
    for (const char* id : {
             "section_component_library", "component_library_search",
             "component_library_filter", "component_library_list",
             "component_library_id", "component_library_display_name",
             "component_library_category", "component_library_tags",
             "component_library_description",
             "row_component_library_actions", "component_library_status"}) {
        setChromeVisible(id, hasDocument);
    }
    for (const char* id : {
             "section_theme_editor", "row_theme_file", "theme_token_list",
             "theme_token_key", "theme_token_value", "row_theme_token_actions",
             "theme_style_list", "theme_style_property",
             "theme_style_binding", "btn_theme_style_apply",
             "theme_editor_status"}) {
        setChromeVisible(id, hasDocument);
    }
    for (const char* id : {
             "section_animation", "animation_clip_name",
             "animation_clip_list", "row_animation_clip_actions",
             "lbl_animation_playback", "row_animation_playback",
             "btn_animation_playback_apply", "animation_track_property",
             "animation_track_list", "row_animation_track_actions",
             "row_animation_key_settings", "animation_key_list",
             "row_animation_key_actions", "row_animation_preview",
             "animation_status"}) {
        setChromeVisible(id, hasDocument);
    }
    setChromeVisible("section_identity", hasSection(PropertySection::Identity));
    setChromeVisible("section_transform", hasSection(PropertySection::Transform));
    setChromeVisible("section_content", hasSection(PropertySection::Content));
    setChromeVisible("section_appearance", hasSection(PropertySection::Appearance));
    setChromeVisible("section_layout", hasSection(PropertySection::Layout));
    setChromeVisible("section_interaction", hasSection(PropertySection::Interaction));

    for (const PropertyFieldSchema& field : allPropertyFieldSchemas()) {
        bool visible = hasProperty(field.property);
        if (field.property == AuthoringProperty::Items &&
            structuredKind != LayoutStructuredKind::None) {
            visible = false;
        }
        if (field.property == AuthoringProperty::Text &&
            structuredKind == LayoutStructuredKind::RichText) {
            visible = false;
        }
        if (field.property == AuthoringProperty::X ||
            field.property == AuthoringProperty::Y) {
            visible = visible && showPos;
        }
        row(field.rowId, field.labelId, field.controlId, visible);
    }
    setChromeVisible("section_structured",
                     structuredKind != LayoutStructuredKind::None);
    if (auto* title = dynamic_cast<TextLabel*>(_ui != nullptr
            ? findChromeById("lbl_props") : nullptr)) {
        if (!hasSel) {
            title->setText(localizedText(
                "ui.editor.ui_designer.properties", L"Properties"));
        } else if (_selection.size() > 1u) {
            title->setText(localizedText(
                "ui.editor.ui_designer.properties", L"Properties") + L" — " +
                std::to_wstring(_selection.size()) + L" widgets");
        } else {
            std::wstring t = localizedText(
                "ui.editor.ui_designer.properties", L"Properties") + L" — ";
            t += authoring != nullptr
                ? utf8ToWide(authoring->displayName) : L"Widget";
            title->setText(t);
        }
    }
    if (_selection.size() > 1u) {
        setChromeVisible("row_prop_id", false);
    }

    if (_ui != nullptr) {
        _ui->invalidateLayout();
    }
}

void LayoutEditorSession::syncPropertyStrip() {
    updatePropPanelVisibility();
    _suppressProp = true;
    auto setField = [](TextInput* ti, const std::wstring& v) {
        if (ti != nullptr) {
            ti->setText(v);
        }
    };
    if (_selected == nullptr) {
        setField(_propId, L"");
        setField(_propX, L"");
        setField(_propY, L"");
        setField(_propW, L"");
        setField(_propH, L"");
        setField(_propAnchorMinX, L"");
        setField(_propAnchorMinY, L"");
        setField(_propAnchorMaxX, L"");
        setField(_propAnchorMaxY, L"");
        setField(_propOffsetMinX, L"");
        setField(_propOffsetMinY, L"");
        setField(_propOffsetMaxX, L"");
        setField(_propOffsetMaxY, L"");
        setField(_propPivotX, L"");
        setField(_propPivotY, L"");
        setField(_propText, L"");
        setField(_propTexture, L"");
        setField(_propItems, L"");
        setField(_propController, L"");
        setField(_propOnClick, L"");
        setField(_propOnToggled, L"");
        setField(_propOnValueChanged, L"");
        setField(_propOnTextChanged, L"");
        setField(_propOnSubmit, L"");
        setField(_propOnSelectionChanged, L"");
        setField(_propOnItemActivated, L"");
        setField(_propOnClose, L"");
        if (_propStyleCombo != nullptr) {
            _suppressStyleCombo = true;
            _propStyleCombo->setSelectedIndex(0);
            _suppressStyleCombo = false;
        }
        if (_propTextHAlign != nullptr) {
            _propTextHAlign->setSelectedIndex(0);
        }
        if (_propTextVAlign != nullptr) {
            _propTextVAlign->setSelectedIndex(0);
        }
        syncEnumCombos();
        syncSchemaPropertyFields();
        setField(_propSpacing, L"");
        setField(_propPadL, L"");
        setField(_propPadT, L"");
        setField(_propPadR, L"");
        setField(_propPadB, L"");
        syncAnchorPresetStyles();
        _suppressProp = false;
        syncStructuredEditor();
        syncTextureBrowser();
        syncStyleInspector();
        syncReuseEditor();
        syncExternalComponentEditor();
        syncThemeEditor();
        syncResponsiveEditor();
        syncAnimationEditor();
        if (_ui != nullptr) {
            _ui->invalidateLayout();
            _ui->layout();
        }
        return;
    }
    setField(_propId, _selection.size() > 1u
        ? L"" : utf8ToWide(_selected->getId()));
    setField(_propX, commonPropertyValue("x"));
    setField(_propY, commonPropertyValue("y"));
    setField(_propW, commonPropertyValue("w"));
    setField(_propH, commonPropertyValue("h"));
    const bool allAnchored = std::all_of(
        _selection.begin(), _selection.end(),
        [](Widget* widget) { return widget != nullptr && widget->hasAnchorLayout(); });
    if (allAnchored) {
        setField(_propAnchorMinX, commonPropertyValue("anchorMinX"));
        setField(_propAnchorMinY, commonPropertyValue("anchorMinY"));
        setField(_propAnchorMaxX, commonPropertyValue("anchorMaxX"));
        setField(_propAnchorMaxY, commonPropertyValue("anchorMaxY"));
        setField(_propOffsetMinX, commonPropertyValue("offsetMinX"));
        setField(_propOffsetMinY, commonPropertyValue("offsetMinY"));
        setField(_propOffsetMaxX, commonPropertyValue("offsetMaxX"));
        setField(_propOffsetMaxY, commonPropertyValue("offsetMaxY"));
        setField(_propPivotX, commonPropertyValue("pivotX"));
        setField(_propPivotY, commonPropertyValue("pivotY"));
    } else {
        setField(_propAnchorMinX, L"");
        setField(_propAnchorMinY, L"");
        setField(_propAnchorMaxX, L"");
        setField(_propAnchorMaxY, L"");
        setField(_propOffsetMinX, L"");
        setField(_propOffsetMinY, L"");
        setField(_propOffsetMaxX, L"");
        setField(_propOffsetMaxY, L"");
        setField(_propPivotX, L"");
        setField(_propPivotY, L"");
    }
    setField(_propText, commonPropertyValue("text"));
    setField(_propTexture, commonPropertyValue("texture"));
    setField(_propItems, commonPropertyValue("items"));
    setField(_propController, commonPropertyValue("controller"));
    auto syncEvent = [this, &setField](TextInput* field, const char* eventName) {
        setField(field, commonPropertyValue(
            std::string("event:") + eventName));
    };
    syncEvent(_propOnClick, "onClick");
    syncEvent(_propOnToggled, "onToggled");
    syncEvent(_propOnValueChanged, "onValueChanged");
    syncEvent(_propOnTextChanged, "onTextChanged");
    syncEvent(_propOnSubmit, "onSubmit");
    syncEvent(_propOnSelectionChanged, "onSelectionChanged");
    syncEvent(_propOnItemActivated, "onItemActivated");
    syncEvent(_propOnClose, "onClose");
    if (_propStyleCombo != nullptr) {
        int idx = 0;
        const std::string& sid = _selected->getStyleId();
        bool mixedStyle = false;
        for (Widget* widget : _selection) {
            if (widget != nullptr && widget->getStyleId() != sid) {
                mixedStyle = true;
                break;
            }
        }
        for (size_t i = 0; i < _styleIds.size(); ++i) {
            if (_styleIds[i] == sid) {
                idx = static_cast<int>(i);
                break;
            }
        }
        _suppressStyleCombo = true;
        _propStyleCombo->setSelectedIndex(mixedStyle ? -1 : idx);
        _suppressStyleCombo = false;
    }
    syncTextAlignCombos();
    syncEnumCombos();
    syncSchemaPropertyFields();
    const bool allBoxes = std::all_of(_selection.begin(), _selection.end(),
        [](Widget* widget) { return dynamic_cast<BoxBase*>(widget) != nullptr; });
    if (allBoxes) {
        setField(_propSpacing, commonPropertyValue("spacing"));
        setField(_propPadL, commonPropertyValue("padL"));
        setField(_propPadT, commonPropertyValue("padT"));
        setField(_propPadR, commonPropertyValue("padR"));
        setField(_propPadB, commonPropertyValue("padB"));
    } else {
        setField(_propSpacing, L"");
        setField(_propPadL, L"");
        setField(_propPadT, L"");
        setField(_propPadR, L"");
        setField(_propPadB, L"");
    }
    syncAnchorPresetStyles();
    _suppressProp = false;
    syncStructuredEditor();
    syncTextureBrowser();
    syncStyleInspector();
    syncReuseEditor();
    syncExternalComponentEditor();
    syncThemeEditor();
    syncResponsiveEditor();
    syncAnimationEditor();
    if (_ui != nullptr) {
        // Visibility and field values are one Inspector transaction. Flush
        // once before returning so rapid canvas/outline selection cannot
        // expose a frame with old row positions and new field contents.
        _ui->invalidateLayout();
        _ui->layout();
    }
}

void LayoutEditorSession::markDirty(bool dirty) {
    _dirty = dirty;
    refreshWindowTitle();
    if (_documentStateUpdater) {
        _documentStateUpdater(_documentPath, _dirty);
    }
    if (_dirty && !_documentPath.empty()) {
        setStatus(utf8ToWide("Modified — " + _documentPath));
    } else if (_dirty) {
        setStatus(L"Modified — unsaved");
    }
    if (_docRoot != nullptr) _validationDirty = true;
}

bool LayoutEditorSession::isUnderCanvas(Widget* widget) const {
    for (Widget* w = widget; w != nullptr; w = w->getParent()) {
        if (w == _canvasHost) {
            return true;
        }
    }
    return false;
}

Widget* LayoutEditorSession::findInDocument(const std::string& id) const {
    return id.empty() ? nullptr : _documentModel.findById(id);
}

Widget* LayoutEditorSession::pickParentForAdd() const {
    if (_docRoot == nullptr) {
        return nullptr;
    }
    if (_selected == nullptr) {
        return _docRoot;
    }
    if (isContainerWidget(_selected)) {
        return _selected;
    }
    Widget* parent = _selected->getParent();
    if (parent != nullptr && isUnderCanvas(parent) && parent != _canvasHost &&
        isContainerWidget(parent)) {
        return parent;
    }
    return _docRoot;
}

void LayoutEditorSession::collectAuthoredChildren(
    Widget* node, std::vector<Widget*>& out) const {
    out.clear();
    if (node == nullptr) return;

    // Virtual collections and self-contained composites are authoring leaves.
    if (dynamic_cast<ListView*>(node) != nullptr ||
        dynamic_cast<TileView*>(node) != nullptr ||
        dynamic_cast<TreeView*>(node) != nullptr ||
        dynamic_cast<ComboBox*>(node) != nullptr ||
        dynamic_cast<TabStrip*>(node) != nullptr ||
        dynamic_cast<TextArea*>(node) != nullptr ||
        dynamic_cast<Tooltip*>(node) != nullptr) {
        return;
    }
    if (auto* scroll = dynamic_cast<ScrollView*>(node)) {
        if (Widget* content = scroll->getContent()) out.push_back(content);
        return;
    }
    if (auto* tabs = dynamic_cast<TabControl*>(node)) {
        out.reserve(tabs->getTabCount());
        for (int i = 0; i < static_cast<int>(tabs->getTabCount()); ++i) {
            if (Widget* page = tabs->getTabContent(i)) out.push_back(page);
        }
        return;
    }
    if (auto* dialog = dynamic_cast<ModalDialog*>(node)) {
        if (Widget* body = dialog->getBodyContent()) out.push_back(body);
        return;
    }
    if (auto* modal = dynamic_cast<Modal*>(node)) {
        if (Widget* content = modal->getContent()) out.push_back(content);
        return;
    }

    for (Widget* child : node->getChildren()) {
        if (child == nullptr || isEditorOverlay(child)) continue;
        if (dynamic_cast<Window*>(node) != nullptr &&
            dynamic_cast<ScrollBar*>(child) != nullptr) continue;
        out.push_back(child);
    }
}

void LayoutEditorSession::collectHierarchy(
    Widget* node, int depth,
    std::vector<std::wstring>& labels,
    std::vector<Widget*>& widgets) const {
    if (node == nullptr) {
        return;
    }
    std::wstring indent(static_cast<size_t>(depth) * 2, L' ');
    std::wstring label = indent;
    const std::string& id = node->getId();
    if (!id.empty()) {
        label += utf8ToWide(id);
    } else {
        label += L"(anon)";
    }
    if (dynamic_cast<BoxBase*>(node) != nullptr) {
        label += L"  [box]";
    } else if (dynamic_cast<CompoundWidget*>(node) != nullptr) {
        label += L"  [container]";
    }
    labels.push_back(label);
    widgets.push_back(node);

    std::vector<Widget*> children;
    collectAuthoredChildren(node, children);
    for (Widget* child : children) {
        collectHierarchy(child, depth + 1, labels, widgets);
    }
}

std::string LayoutEditorSession::makeUniqueId(
    const std::string& prefix, Widget* detachedRoot) const {
    // IDs must also see physical implementation/legacy children. The
    // semantic Outline deliberately hides ListView rows and other private
    // descendants; using only that view let an accidentally-attached child
    // reserve no ID and produced an unbounded run of duplicate `w_3` nodes.
    const auto treeContainsId = [this](Widget* root,
                                       const std::string& id) -> bool {
        std::unordered_set<Widget*> visited;
        std::function<bool(Widget*)> walk = [&](Widget* node) -> bool {
            if (node == nullptr || !visited.insert(node).second) return false;
            if (node->getId() == id) return true;
            for (Widget* child : node->getChildren()) {
                if (walk(child)) return true;
            }
            // Include authored aliases that may not currently be mounted,
            // notably inactive TabControl pages.
            std::vector<Widget*> authored;
            collectAuthoredChildren(node, authored);
            for (Widget* child : authored) {
                if (walk(child)) return true;
            }
            return false;
        };
        return walk(root);
    };
    for (int i = 1; i < 10000; ++i) {
        const std::string candidate = prefix + "_" + std::to_string(i);
        if (!treeContainsId(_docRoot, candidate) &&
            !treeContainsId(detachedRoot, candidate)) {
            return candidate;
        }
    }
    return prefix + "_x";
}

bool LayoutEditorSession::getTextPayload(Widget* widget,
                                         std::wstring& out) const {
    if (auto* b = dynamic_cast<Button*>(widget)) {
        out = b->getText();
        return true;
    }
    if (auto* l = dynamic_cast<TextLabel*>(widget)) {
        out = l->getText();
        return true;
    }
    if (auto* t = dynamic_cast<TextInput*>(widget)) {
        out = t->getText();
        return true;
    }
    if (auto* c = dynamic_cast<CheckBox*>(widget)) {
        out = c->getText();
        return true;
    }
    if (auto* radio = dynamic_cast<RadioButton*>(widget)) {
        out = radio->getText();
        return true;
    }
    if (auto* area = dynamic_cast<TextArea*>(widget)) {
        out = area->getText();
        return true;
    }
    if (auto* tip = dynamic_cast<Tooltip*>(widget)) {
        out = tip->getText();
        return true;
    }
    if (auto* window = dynamic_cast<Window*>(widget)) {
        out = window->getTitle();
        return true;
    }
    if (auto* rich = dynamic_cast<RichText*>(widget)) {
        out = rich->getPlainText();
        return true;
    }
    out.clear();
    return false;
}

bool LayoutEditorSession::setTextPayload(Widget* widget,
                                         const std::wstring& text) {
    if (auto* b = dynamic_cast<Button*>(widget)) {
        b->setText(text);
        return true;
    }
    if (auto* l = dynamic_cast<TextLabel*>(widget)) {
        l->setText(text);
        return true;
    }
    if (auto* t = dynamic_cast<TextInput*>(widget)) {
        t->setText(text);
        return true;
    }
    if (auto* c = dynamic_cast<CheckBox*>(widget)) {
        c->setText(text);
        return true;
    }
    if (auto* radio = dynamic_cast<RadioButton*>(widget)) {
        radio->setText(text);
        return true;
    }
    if (auto* area = dynamic_cast<TextArea*>(widget)) {
        area->setText(text);
        return true;
    }
    if (auto* tip = dynamic_cast<Tooltip*>(widget)) {
        tip->setText(text);
        return true;
    }
    if (auto* window = dynamic_cast<Window*>(widget)) {
        window->setTitle(text);
        return true;
    }
    if (auto* rich = dynamic_cast<RichText*>(widget)) {
        rich->clearRuns();
        if (!text.empty()) rich->addRun(text, rich->getDefaultColor(),
                                       rich->getDefaultFontSize());
        return true;
    }
    return false;
}

void LayoutEditorSession::applyTextureName(const std::string& textureName) {
    auto* image = dynamic_cast<Image*>(_selected);
    if (image == nullptr) return;
    if (textureName.empty()) {
        image->setTexture(std::string());
        markDirty(true);
        return;
    }

    ImageTextureHandle preview;
    if (_texturePreviewLoader) {
        preview = _texturePreviewLoader(
            _textureCatalog.resolvePreviewPath(textureName));
    }
    if (preview.handle != nullptr) {
        preview.name = textureName;
        image->setTexture(preview);
        setStatus(L"Texture preview loaded");
    } else {
        image->setTexture(textureName);
        setStatus(L"Texture assigned; preview is unavailable in this host");
    }
    markDirty(true);
}

void LayoutEditorSession::chooseTexture() {
    if (dynamic_cast<Image*>(_selected) == nullptr) return;
    if (!_texturePicker) {
        setStatus(L"No texture picker is configured");
        return;
    }
    const std::string path = _texturePicker();
    if (path.empty()) return;
    const LayoutTextureResource* catalogEntry =
        _textureCatalog.findByPreviewPath(path);
    beginPropertyMutation("texture", "Assign texture resource");
    applyTextureName(catalogEntry != nullptr ? catalogEntry->key : path);
    endPropertyMutation();
    syncPropertyStrip();
}

void LayoutEditorSession::clearTexture() {
    if (dynamic_cast<Image*>(_selected) == nullptr) return;
    beginPropertyMutation("texture", "Clear texture resource");
    applyTextureName({});
    endPropertyMutation();
    syncPropertyStrip();
}

void LayoutEditorSession::syncTextureBrowser() {
    const auto* image = dynamic_cast<Image*>(_selected);
    const bool visible = image != nullptr;
    setChromeVisible("section_texture_resources", visible);
    setChromeVisible("row_texture_resource_search", visible);
    setChromeVisible("texture_resource_list", visible);
    setChromeVisible("row_texture_resource_actions", visible);
    setChromeVisible("texture_resource_status", visible);
    if (!visible) return;

    _suppressTextureResources = true;
    if (_textureResourceList != nullptr) {
        _textureResourceList->setItems(_textureCatalog.visibleLabels());
        int selected = -1;
        const std::string& key = image->getTextureName();
        for (size_t i = 0; i < _textureCatalog.visibleIndices().size(); ++i) {
            const LayoutTextureResource* entry = _textureCatalog.visibleEntry(i);
            if (entry != nullptr && entry->key == key) {
                selected = static_cast<int>(i);
                break;
            }
        }
        _textureResourceList->setSelectedIndex(selected);
    }
    _suppressTextureResources = false;

    if (_textureStatus != nullptr) {
        const std::string& key = image->getTextureName();
        if (key.empty()) {
            _textureStatus->setText(localizedText(
                "ui.editor.ui_designer.status.no_texture", L"No texture assigned"));
            _textureStatus->setStyleId("__le_muted");
        } else if (_textureCatalog.contains(key) || image->hasTexture()) {
            const ImageTextureHandle& texture = image->getTexture();
            std::wstring label = L"Ready";
            if (const LayoutTextureResource* entry = _textureCatalog.find(key)) {
                if (!entry->detail.empty()) label += L"  \u00b7  " + entry->detail;
            }
            if (texture.width > 0 && texture.height > 0) {
                label += L"  \u00b7  " + std::to_wstring(texture.width) + L" x "
                    + std::to_wstring(texture.height);
            }
            _textureStatus->setText(label);
            _textureStatus->setStyleId("__le_success");
        } else if (_textureResourceProvider) {
            _textureStatus->setText(L"Missing resource — key is not in the current catalog");
            _textureStatus->setStyleId("__le_warning");
        } else {
            _textureStatus->setText(L"Preview unavailable — host has no resource catalog");
            _textureStatus->setStyleId("__le_warning");
        }
    }
}

void LayoutEditorSession::syncStructuredEditor() {
    _structuredModel.bind(_selected);
    const LayoutStructuredKind kind = _structuredModel.kind();
    const bool visible = kind != LayoutStructuredKind::None;
    setChromeVisible("section_structured", visible);
    setChromeVisible("structured_items", visible);
    setChromeVisible("row_structured_text", visible);
    setChromeVisible("row_structured_actions", visible);
    setChromeVisible("row_structured_order", visible);
    setChromeVisible("row_structured_rich_style",
                     kind == LayoutStructuredKind::RichText);
    setChromeVisible("btn_structured_child", kind == LayoutStructuredKind::Tree);
    if (!visible) return;

    _suppressStructured = true;
    if (_structuredList != nullptr) {
        _structuredList->setItems(_structuredModel.displayLabels());
        if (_structuredModel.selectedIndex() < 0 &&
            !_structuredModel.entries().empty()) {
            _structuredModel.setSelectedIndex(0);
        }
        _structuredList->setSelectedIndex(_structuredModel.selectedIndex());
    }
    const int index = _structuredModel.selectedIndex();
    const bool hasEntry = index >= 0 && index < static_cast<int>(
        _structuredModel.entries().size());
    if (_structuredText != nullptr) {
        _structuredText->setText(hasEntry
            ? _structuredModel.entries()[static_cast<size_t>(index)].label
            : std::wstring{});
    }
    if (_structuredFontSize != nullptr) {
        _structuredFontSize->setText(hasEntry &&
            kind == LayoutStructuredKind::RichText
            ? std::to_wstring(_structuredModel.entries()[
                  static_cast<size_t>(index)].richRun.fontSize)
            : std::wstring{});
    }
    if (_structuredColor != nullptr) {
        _structuredColor->setText(hasEntry &&
            kind == LayoutStructuredKind::RichText
            ? formatColorHex(_structuredModel.entries()[
                  static_cast<size_t>(index)].richRun.color)
            : std::wstring{});
    }
    _suppressStructured = false;
    setChromeEnabled("btn_structured_remove", hasEntry);
    setChromeEnabled("btn_structured_up", hasEntry && index > 0);
    setChromeEnabled("btn_structured_down", hasEntry &&
        index + 1 < static_cast<int>(_structuredModel.entries().size()));
    setChromeEnabled("btn_structured_bold", hasEntry);
    setChromeEnabled("btn_structured_italic", hasEntry);
    setChromeEnabled("btn_structured_underline", hasEntry);
}

void LayoutEditorSession::structuredAdd(bool asChild) {
    if (_structuredModel.kind() == LayoutStructuredKind::None) return;
    pushUndo(LayoutEditKind::Insert, "Add structured item");
    const bool added = _structuredModel.add({}, asChild, [this]() -> Widget* {
        Widget* page = WidgetFactory::get().create("Panel");
        if (page == nullptr) return nullptr;
        page->setId(makeUniqueId("tab_page", page));
        page->setSize(math::FVector2(240.0f, 140.0f));
        page->setLayoutPositionManaged(true);
        page->setLayoutSizeManaged(true);
        return page;
    });
    if (!added) {
        _commandStack.discardLastUndo();
        return;
    }
    markDirty(true);
    freezeDocumentInteraction(_docRoot);
    refreshHierarchy();
    syncStructuredEditor();
    syncSelectionChrome();
    if (_ui != nullptr) _ui->invalidateLayout();
}

void LayoutEditorSession::structuredRemove() {
    pushUndo(LayoutEditKind::Delete, "Remove structured item");
    if (!_structuredModel.removeSelected()) {
        _commandStack.discardLastUndo();
        return;
    }
    markDirty(true);
    refreshHierarchy();
    syncStructuredEditor();
    syncSelectionChrome();
    if (_ui != nullptr) _ui->invalidateLayout();
}

void LayoutEditorSession::structuredMove(int delta) {
    pushUndo(LayoutEditKind::Reorder, "Reorder structured item");
    if (!_structuredModel.moveSelected(delta)) {
        _commandStack.discardLastUndo();
        return;
    }
    markDirty(true);
    refreshHierarchy();
    syncStructuredEditor();
    syncSelectionChrome();
    if (_ui != nullptr) _ui->invalidateLayout();
}

void LayoutEditorSession::commitStructuredText() {
    if (_suppressStructured || _structuredText == nullptr) return;
    const int index = _structuredModel.selectedIndex();
    if (index < 0 || index >= static_cast<int>(
        _structuredModel.entries().size())) return;
    const std::wstring value = _structuredText->getText();
    if (_structuredModel.entries()[static_cast<size_t>(index)].label == value)
        return;
    pushUndo(LayoutEditKind::Property, "Rename structured item");
    if (!_structuredModel.setSelectedLabel(value)) {
        _commandStack.discardLastUndo();
        return;
    }
    markDirty(true);
    refreshHierarchy();
    syncStructuredEditor();
    if (_ui != nullptr) _ui->invalidateLayout();
}

void LayoutEditorSession::commitStructuredRichStyle() {
    if (_suppressStructured ||
        _structuredModel.kind() != LayoutStructuredKind::RichText) return;
    const int index = _structuredModel.selectedIndex();
    if (index < 0 || index >= static_cast<int>(
        _structuredModel.entries().size())) return;
    RichRun run = _structuredModel.entries()[static_cast<size_t>(index)].richRun;
    if (_structuredText != nullptr) run.text = _structuredText->getText();
    if (_structuredFontSize != nullptr) {
        float parsed = 0.0f;
        if (!parseFloat(_structuredFontSize->getText(), parsed) || parsed < 1.0f) {
            setStatus(L"Rich text font size must be positive");
            syncStructuredEditor();
            return;
        }
        run.fontSize = static_cast<int>(std::lround(parsed));
    }
    if (_structuredColor != nullptr &&
        !parseColorHex(_structuredColor->getText(), run.color)) {
        setStatus(L"Rich text color must be #RRGGBB or #RRGGBBAA");
        syncStructuredEditor();
        return;
    }
    const RichRun& old = _structuredModel.entries()[static_cast<size_t>(index)].richRun;
    if (old.text == run.text && old.fontSize == run.fontSize &&
        formatColorHex(old.color) == formatColorHex(run.color)) return;
    pushUndo(LayoutEditKind::Property, "Edit rich-text run");
    if (!_structuredModel.setSelectedRichRun(run)) {
        _commandStack.discardLastUndo();
        return;
    }
    markDirty(true);
    syncStructuredEditor();
    if (_ui != nullptr) _ui->invalidateLayout();
}

void LayoutEditorSession::syncPreviewControls() {
    _suppressPreview = true;
    const LayoutPreviewSettings& settings = _previewModel.settings();
    const int preset = _previewModel.presetIndex() >= 0
        ? _previewModel.presetIndex()
        : static_cast<int>(_previewModel.presets().size());
    if (_previewPreset != nullptr) _previewPreset->setSelectedIndex(preset);
    if (_previewDpi != nullptr) {
        static constexpr float scales[] = {1.0f, 1.25f, 1.5f, 2.0f, 3.0f};
        int best = 0;
        for (int i = 1; i < static_cast<int>(sizeof(scales) / sizeof(scales[0])); ++i) {
            if (std::fabs(scales[i] - settings.dpiScale) <
                std::fabs(scales[best] - settings.dpiScale)) best = i;
        }
        _previewDpi->setSelectedIndex(best);
    }
    const math::FVector2 documentPixels = authoredRootSize();
    const float width = settings.followDocumentSize
        ? documentPixels.x * settings.dpiScale : settings.pixelWidth;
    const float height = settings.followDocumentSize
        ? documentPixels.y * settings.dpiScale : settings.pixelHeight;
    auto set = [](TextInput* field, float value) {
        if (field != nullptr) field->setText(formatFloat(value));
    };
    set(_previewWidth, width);
    set(_previewHeight, height);
    set(_previewSafeL, settings.safeAreaPixels.x);
    set(_previewSafeT, settings.safeAreaPixels.y);
    set(_previewSafeR, settings.safeAreaPixels.z);
    set(_previewSafeB, settings.safeAreaPixels.w);
    if (auto* button = dynamic_cast<Button*>(findChromeById("btn_preview_safe"))) {
        button->setText(settings.showSafeArea
            ? localizedText("ui.editor.ui_designer.safe_area_on",
                            L"Safe Area: On")
            : localizedText("ui.editor.ui_designer.safe_area_off",
                            L"Safe Area: Off"));
        button->setStyleId(settings.showSafeArea ? "__le_primary" : "__le_command");
    }
    if (auto* button = dynamic_cast<Button*>(findChromeById("btn_preview_mode"))) {
        button->setText(_mode == Mode::Interact
            ? localizedText("ui.editor.ui_designer.stop_preview",
                            L"Stop Preview")
            : localizedText("ui.editor.ui_designer.interact", L"Interact"));
        button->setStyleId(_mode == Mode::Interact ? "__le_primary" : "__le_command");
    }
    if (auto* hint = dynamic_cast<TextLabel*>(findChromeById("lbl_canvas_hint"))) {
        const math::FVector2 logical = _previewModel.logicalSize(authoredRootSize());
        std::wostringstream label;
        label << static_cast<int>(std::lround(width)) << L" x "
              << static_cast<int>(std::lround(height)) << L" px  \u00b7  "
              << logical.x << L" x " << logical.y << L" DIP  \u00b7  "
              << static_cast<int>(std::lround(_viewZoom * 100.0f)) << L"%";
        hint->setText(label.str());
    }
    _suppressPreview = false;
}

void LayoutEditorSession::syncReuseEditor() {
    if (_reuseList == nullptr && _reuseStatus == nullptr) return;
    std::string selectedName;
    if (_reuseList != nullptr) {
        const int oldIndex = _reuseList->getSelectedIndex();
        if (oldIndex >= 0 &&
            oldIndex < static_cast<int>(_reuseLibrary.blocks().size())) {
            selectedName = _reuseLibrary.blocks()[
                static_cast<size_t>(oldIndex)].name;
        }
    }
    std::vector<std::wstring> labels;
    labels.reserve(_reuseLibrary.blocks().size());
    for (const LayoutReusableBlock& block : _reuseLibrary.blocks()) {
        labels.push_back(utf8ToWide(block.name));
    }
    _suppressReuse = true;
    int selectedIndex = -1;
    if (_reuseList != nullptr) {
        _reuseList->setItems(labels);
        for (size_t index = 0; index < _reuseLibrary.blocks().size(); ++index) {
            if (_reuseLibrary.blocks()[index].name == selectedName) {
                selectedIndex = static_cast<int>(index);
                break;
            }
        }
        if (selectedIndex < 0 && !_reuseLibrary.empty()) selectedIndex = 0;
        _reuseList->setSelectedIndex(selectedIndex);
    }
    if (_reuseName != nullptr && selectedIndex >= 0 &&
        _reuseName->getText().empty()) {
        _reuseName->setText(labels[static_cast<size_t>(selectedIndex)]);
    }
    _suppressReuse = false;

    const bool oneSelected = _selected != nullptr && _selection.size() == 1u;
    setChromeEnabled("btn_reuse_define", oneSelected);
    setChromeEnabled("btn_reuse_insert", selectedIndex >= 0);
    setChromeEnabled("btn_reuse_remove", selectedIndex >= 0);
    if (_reuseStatus != nullptr) {
        _reuseStatus->setText(_reuseLibrary.empty()
            ? L"No blocks — save the selected subtree for reuse"
            : std::to_wstring(_reuseLibrary.size()) +
              L" block(s) — inserted copies are independent");
    }
}

void LayoutEditorSession::syncExternalComponentEditor() {
    if (_externalComponentList == nullptr
        && _externalComponentStatus == nullptr) return;
    std::string selectedId;
    if (_externalComponentList != nullptr) {
        const int oldIndex = _externalComponentList->getSelectedIndex();
        if (oldIndex >= 0 && oldIndex < static_cast<int>(
                _externalComponentFilteredIndices.size())) {
            selectedId = _externalComponentLibrary.components()[
                _externalComponentFilteredIndices[static_cast<size_t>(oldIndex)]].id;
        }
    }

    std::wstring selectedCategory;
    if (_externalComponentCategoryFilter != nullptr
        && _externalComponentCategoryFilter->getSelectedIndex() > 0) {
        selectedCategory = _externalComponentCategoryFilter->getSelectedItem();
    }
    std::vector<std::string> categories;
    for (const LayoutComponentDefinition& definition :
         _externalComponentLibrary.components()) {
        if (!definition.category.empty()) categories.push_back(definition.category);
    }
    std::sort(categories.begin(), categories.end());
    categories.erase(std::unique(categories.begin(), categories.end()),
                     categories.end());

    _suppressExternalComponents = true;
    if (_externalComponentCategoryFilter != nullptr) {
        std::vector<std::wstring> items{L"All Categories"};
        for (const std::string& category : categories) {
            items.push_back(utf8ToWide(category));
        }
        _externalComponentCategoryFilter->setItems(items);
        int categoryIndex = 0;
        for (size_t index = 1; index < items.size(); ++index) {
            if (items[index] == selectedCategory) {
                categoryIndex = static_cast<int>(index);
                break;
            }
        }
        _externalComponentCategoryFilter->setSelectedIndex(categoryIndex);
        selectedCategory = categoryIndex > 0 ? items[categoryIndex] : L"";
    }

    const std::wstring query = _externalComponentSearch != nullptr
        ? lowerWide(trimWide(_externalComponentSearch->getText())) : L"";
    std::vector<std::wstring> labels;
    _externalComponentFilteredIndices.clear();
    const auto& components = _externalComponentLibrary.components();
    labels.reserve(components.size());
    for (size_t componentIndex = 0; componentIndex < components.size();
         ++componentIndex) {
        const LayoutComponentDefinition& definition = components[componentIndex];
        if (!selectedCategory.empty()
            && utf8ToWide(definition.category) != selectedCategory) continue;
        std::wstring searchable = utf8ToWide(
            definition.id + " " + definition.displayName + " "
            + definition.category + " " + definition.description);
        for (const std::string& tag : definition.tags) {
            searchable += L" " + utf8ToWide(tag);
        }
        if (!query.empty()
            && lowerWide(std::move(searchable)).find(query)
                == std::wstring::npos) continue;
        std::wstring label;
        if (!definition.category.empty()) {
            label = utf8ToWide(definition.category) + L" / ";
        }
        label += utf8ToWide(definition.displayName);
        if (!definition.tags.empty()) {
            label += L"  #" + utf8ToWide(definition.tags.front());
        }
        _externalComponentFilteredIndices.push_back(componentIndex);
        labels.push_back(std::move(label));
    }
    int selectedIndex = -1;
    if (_externalComponentList != nullptr) {
        _externalComponentList->setItems(labels);
        for (size_t index = 0; index < _externalComponentFilteredIndices.size();
             ++index) {
            if (components[_externalComponentFilteredIndices[index]].id
                == selectedId) {
                selectedIndex = static_cast<int>(index);
                break;
            }
        }
        if (selectedIndex < 0 && !_externalComponentFilteredIndices.empty()) {
            selectedIndex = 0;
        }
        _externalComponentList->setSelectedIndex(selectedIndex);
    }
    if (selectedIndex >= 0) {
        const LayoutComponentDefinition& definition =
            components[_externalComponentFilteredIndices[
                static_cast<size_t>(selectedIndex)]];
        if (_externalComponentId != nullptr
            && _externalComponentId->getText().empty()) {
            _externalComponentId->setText(utf8ToWide(definition.id));
        }
        if (_externalComponentDisplayName != nullptr
            && _externalComponentDisplayName->getText().empty()) {
            _externalComponentDisplayName->setText(
                utf8ToWide(definition.displayName));
        }
        if (_externalComponentCategory != nullptr
            && _externalComponentCategory->getText().empty()) {
            _externalComponentCategory->setText(
                utf8ToWide(definition.category));
        }
        if (_externalComponentDescription != nullptr
            && _externalComponentDescription->getText().empty()) {
            _externalComponentDescription->setText(
                utf8ToWide(definition.description));
        }
        if (_externalComponentTags != nullptr
            && _externalComponentTags->getText().empty()) {
            std::vector<std::wstring> tags;
            for (const std::string& tag : definition.tags) {
                tags.push_back(utf8ToWide(tag));
            }
            _externalComponentTags->setText(joinItems(tags));
        }
    }
    _suppressExternalComponents = false;
    const bool oneSelected = _selected != nullptr && _selection.size() == 1u;
    setChromeEnabled("btn_component_library_save",
                     oneSelected && !_externalComponentLibraryPath.empty());
    setChromeEnabled("btn_component_library_insert", selectedIndex >= 0);
    setChromeEnabled("btn_component_library_remove",
                     selectedIndex >= 0 && !_externalComponentLibraryPath.empty());
    if (_externalComponentStatus != nullptr) {
        if (_externalComponentLibraryPath.empty()) {
            _externalComponentStatus->setText(localizedText(
                "ui.editor.ui_designer.status.component_library_unconfigured",
                L"Project component library is not configured"));
        } else if (_externalComponentLibrary.empty()) {
            _externalComponentStatus->setText(L"No project components");
        } else {
            _externalComponentStatus->setText(
                std::to_wstring(_externalComponentFilteredIndices.size())
                + L" shown / "
                + std::to_wstring(_externalComponentLibrary.size())
                + L" reusable project component(s); Enter inserts");
        }
    }
}

void LayoutEditorSession::syncThemeEditor(
    const std::string& preferredToken, const std::string& preferredStyle) {
    if (_themeTokenList == nullptr && _themeStyleList == nullptr
        && _themeEditorStatus == nullptr) return;
    const std::vector<LayoutThemeTokenEntry> tokens = _themeEditorModel.tokens();
    const std::vector<LayoutThemeStyleEntry> styles = _themeEditorModel.styles();
    int oldTokenIndex = _themeTokenList != nullptr
        ? _themeTokenList->getSelectedIndex() : -1;
    int oldStyleIndex = _themeStyleList != nullptr
        ? _themeStyleList->getSelectedIndex() : -1;
    std::string selectedToken = preferredToken;
    if (selectedToken.empty() && oldTokenIndex >= 0
        && oldTokenIndex < static_cast<int>(tokens.size())) {
        selectedToken = tokens[static_cast<size_t>(oldTokenIndex)].key;
    }
    std::string selectedStyle = preferredStyle;
    if (selectedStyle.empty() && oldStyleIndex >= 0
        && oldStyleIndex < static_cast<int>(styles.size())) {
        const auto& entry = styles[static_cast<size_t>(oldStyleIndex)];
        selectedStyle = entry.fragment + "/" + entry.styleId;
    }

    _suppressThemeEditor = true;
    int tokenIndex = -1;
    if (_themeTokenList != nullptr) {
        std::vector<std::wstring> labels;
        labels.reserve(tokens.size());
        for (const auto& entry : tokens) {
            const std::size_t uses = _themeEditorModel.referenceCount(entry.key);
            labels.push_back(utf8ToWide(entry.key) + L"  ·  "
                             + std::to_wstring(uses) + L" use(s)");
        }
        _themeTokenList->setItems(labels);
        for (size_t index = 0; index < tokens.size(); ++index) {
            if (tokens[index].key == selectedToken) tokenIndex = static_cast<int>(index);
        }
        if (tokenIndex < 0 && !tokens.empty()) tokenIndex = 0;
        _themeTokenList->setSelectedIndex(tokenIndex);
    }
    if (tokenIndex >= 0) {
        const LayoutThemeTokenEntry& token = tokens[static_cast<size_t>(tokenIndex)];
        if (_themeTokenKey != nullptr) {
            _themeTokenKey->setText(utf8ToWide(token.key));
        }
        if (_themeTokenValue != nullptr) {
            if (token.kind == LayoutThemeTokenKind::Color) {
                const Theme preview = _themeEditorModel.buildPreviewTheme();
                _themeTokenValue->setText(
                    formatColorHex(preview.getColorToken(token.key)));
            } else {
                _themeTokenValue->setText(utf8ToWide(token.value));
            }
        }
        if (_themeTokenSwatch != nullptr) {
            const bool isColor = token.kind == LayoutThemeTokenKind::Color;
            _themeTokenSwatch->setEnabled(isColor);
            if (isColor) {
                _themeTokenSwatch->setColor(
                    _themeEditorModel.buildPreviewTheme().getColorToken(token.key));
            }
        }
    } else if (_themeTokenSwatch != nullptr) {
        _themeTokenSwatch->setEnabled(false);
    }

    int styleIndex = -1;
    if (_themeStyleList != nullptr) {
        std::vector<std::wstring> labels;
        labels.reserve(styles.size());
        for (const auto& entry : styles) {
            labels.push_back(utf8ToWide(entry.fragment + " / " + entry.styleId));
        }
        _themeStyleList->setItems(labels);
        for (size_t index = 0; index < styles.size(); ++index) {
            if (styles[index].fragment + "/" + styles[index].styleId
                == selectedStyle) styleIndex = static_cast<int>(index);
        }
        if (styleIndex < 0 && !styles.empty()) styleIndex = 0;
        _themeStyleList->setSelectedIndex(styleIndex);
    }
    if (styleIndex >= 0) {
        const auto& style = styles[static_cast<size_t>(styleIndex)];
        if (_themeStyleFragment != nullptr) {
            _themeStyleFragment->setText(utf8ToWide(style.fragment));
        }
        if (_themeStyleId != nullptr) {
            _themeStyleId->setText(utf8ToWide(style.styleId));
        }
        if (_themeStyleBinding != nullptr && _themeStyleProperty != nullptr) {
            static const char* properties[] = {
                "backgroundColor", "borderColor", "textColor"
            };
            const int property = _themeStyleProperty->getSelectedIndex();
            if (property >= 0 && property < 3) {
                _themeStyleBinding->setText(utf8ToWide(
                    _themeEditorModel.styleProperty(
                        style.fragment, style.styleId, properties[property])));
            }
        }
    }
    _suppressThemeEditor = false;

    const bool loaded = !_themeDocumentPath.empty();
    setChromeEnabled("btn_theme_save", loaded && _themeDocumentDirty);
    setChromeEnabled("btn_theme_token_apply", loaded);
    setChromeEnabled("btn_theme_token_rename", loaded && tokenIndex >= 0);
    setChromeEnabled("btn_theme_token_remove", loaded && tokenIndex >= 0);
    setChromeEnabled("btn_theme_style_apply", loaded && styleIndex >= 0);
    setChromeEnabled("btn_theme_style_new", loaded);
    setChromeEnabled("btn_theme_style_duplicate", loaded && styleIndex >= 0);
    setChromeEnabled("btn_theme_style_remove", loaded && styleIndex >= 0);
    if (_themeEditorStatus != nullptr) {
        if (!loaded) {
            _themeEditorStatus->setText(localizedText(
                "ui.editor.ui_designer.status.open_theme_hint",
                L"Open a Theme JSON to edit tokens and styles"));
        } else {
            _themeEditorStatus->setText(
                std::to_wstring(tokens.size()) + L" token(s), "
                + std::to_wstring(styles.size()) + L" style(s)"
                + (_themeDocumentDirty ? L" — unsaved" : L""));
        }
    }
}

void LayoutEditorSession::syncProjectRefactorEditor() {
    if (_projectRefactorKind == nullptr && _projectRefactorStatus == nullptr) {
        return;
    }
    int selectedIndex = _projectRefactorKind != nullptr
        ? _projectRefactorKind->getSelectedIndex() : -1;
    _suppressProjectRefactor = true;
    if (_projectRefactorKind != nullptr) {
        std::vector<std::wstring> labels;
        labels.reserve(_projectRefactorKinds.size());
        for (const auto& kind : _projectRefactorKinds) {
            labels.push_back(kind.displayName);
        }
        _projectRefactorKind->setItems(labels);
        if (selectedIndex < 0
            || selectedIndex >= static_cast<int>(_projectRefactorKinds.size())) {
            selectedIndex = _projectRefactorKinds.empty() ? -1 : 0;
        }
        _projectRefactorKind->setSelectedIndex(selectedIndex);
    }
    _suppressProjectRefactor = false;

    const bool available = _projectRefactorAction != nullptr
        && !_projectRefactorKinds.empty() && !_documentPath.empty();
    setChromeEnabled("btn_project_refactor_preview", available);
    setChromeEnabled("btn_project_refactor_apply", available);
    if (_projectRefactorStatus != nullptr
        && _projectRefactorStatus->getText().empty()) {
        _projectRefactorStatus->setText(available
            ? L"Preview all affected files before applying"
            : L"Project refactoring requires an AYEditor project host");
    }
}

void LayoutEditorSession::seedProjectRefactorValue() {
    if (_projectRefactorOld == nullptr || _projectRefactorKind == nullptr) {
        return;
    }
    const int index = _projectRefactorKind->getSelectedIndex();
    if (index < 0 || index >= static_cast<int>(_projectRefactorKinds.size())) {
        return;
    }
    std::string value;
    switch (_projectRefactorKinds[static_cast<size_t>(index)].seed) {
    case LayoutProjectRefactorKind::Seed::SelectedWidgetId:
        if (_selected != nullptr) value = _selected->getId();
        break;
    case LayoutProjectRefactorKind::Seed::DocumentPath:
        value = _documentPath;
        break;
    case LayoutProjectRefactorKind::Seed::None:
        break;
    }
    _projectRefactorOld->setText(utf8ToWide(value));
    if (_projectRefactorNew != nullptr) _projectRefactorNew->setText(L"");
    if (_projectRefactorPreview != nullptr) _projectRefactorPreview->setItems({});
}

void LayoutEditorSession::runProjectRefactor(bool apply) {
    if (_projectRefactorAction == nullptr || _projectRefactorKind == nullptr
        || _projectRefactorOld == nullptr || _projectRefactorNew == nullptr) {
        setStatus(L"Project refactoring requires an AYEditor project host");
        return;
    }
    const int index = _projectRefactorKind->getSelectedIndex();
    if (index < 0 || index >= static_cast<int>(_projectRefactorKinds.size())) {
        setStatus(L"Choose a project reference kind");
        return;
    }
    const std::string oldValue = wideToUtf8(trimWide(
        _projectRefactorOld->getText()));
    const std::string newValue = wideToUtf8(trimWide(
        _projectRefactorNew->getText()));
    if (oldValue.empty() || newValue.empty() || oldValue == newValue) {
        setStatus(L"Safe Rename requires two different non-empty values");
        return;
    }

    LayoutProjectRefactorResult result = _projectRefactorAction(
        _documentPath, _projectRefactorKinds[static_cast<size_t>(index)].id,
        oldValue, newValue, apply);
    if (_projectRefactorPreview != nullptr) {
        _projectRefactorPreview->setItems(result.details);
        _projectRefactorPreview->setSelectedIndex(-1);
    }
    const std::wstring message = utf8ToWide(result.message.empty()
        ? (result.succeeded ? std::string("Project refactor completed")
                            : std::string("Project refactor failed"))
        : result.message);
    if (_projectRefactorStatus != nullptr) {
        _projectRefactorStatus->setText(message);
        _projectRefactorStatus->setStyleId(
            result.succeeded && result.safe ? "__le_success" : "__le_warning");
    }
    setStatus(message);
}

void LayoutEditorSession::revealProjectRefactorEditor() {
    syncProjectRefactorEditor();
    seedProjectRefactorValue();
    if (_ui != nullptr) {
        _ui->layout();
        auto* scroll = dynamic_cast<ScrollView*>(findChromeById("props_scroll"));
        Widget* section = findChromeById("section_project_refactor");
        if (scroll != nullptr && section != nullptr) {
            const math::FRectangle client = scroll->getClientRect();
            const math::FRectangle bounds = section->getWorldBounds();
            scroll->setScrollOffset(math::FVector2(
                scroll->getScrollOffset().x,
                scroll->getScrollOffset().y + bounds.minY - client.minY - 8.0f));
        }
        if (_projectRefactorOld != nullptr) _ui->setFocus(_projectRefactorOld);
    }
}

void LayoutEditorSession::syncResponsiveEditor() {
    const bool editable = _selected != nullptr && _selected != _docRoot;
    if (_responsiveAuthoringIndex < 0 ||
        _responsiveAuthoringIndex >= static_cast<int>(
            _responsiveModel.breakpoints().size())) {
        _responsiveAuthoringIndex = 0;
    }
    _suppressResponsive = true;
    if (_responsiveBreakpoint != nullptr) {
        _responsiveBreakpoint->setSelectedIndex(_responsiveAuthoringIndex);
    }
    const ResponsiveLayoutRule* authoredRule = editable
        ? _responsiveModel.rule(*_selected, _responsiveAuthoringIndex)
        : nullptr;
    if (_responsiveVisibility != nullptr) {
        _responsiveVisibility->setSelectedIndex(static_cast<int>(
            authoredRule != nullptr ? authoredRule->visibility
                                    : ResponsiveVisibility::Inherit));
    }
    _suppressResponsive = false;

    setChromeEnabled("btn_responsive_preview", editable);
    setChromeEnabled("btn_responsive_capture",
        editable && canUseAnchorLayout(_selected) &&
        _selected->hasAnchorLayout());
    setChromeEnabled("btn_responsive_clear", authoredRule != nullptr);
    if (_responsiveStatus != nullptr) {
        if (!editable) {
            _responsiveStatus->setText(localizedText(
                "ui.editor.ui_designer.status.select_non_root",
                L"Select a non-root widget"));
        } else {
            const float parentWidth = _selected->getParent() != nullptr
                ? _selected->getParent()->getSize().x : 0.0f;
            const int activeIndex =
                _responsiveModel.breakpointForWidth(parentWidth);
            std::wstring text = activeIndex >= 0
                ? L"Active: " + _responsiveModel.breakpoints()[
                    static_cast<size_t>(activeIndex)].label
                : L"No active width band";
            if (authoredRule != nullptr) {
                text += authoredRule->overrideAnchors
                    ? L" · visibility + anchors" : L" · visibility rule";
            } else {
                text += L" · inherits authored layout";
            }
            _responsiveStatus->setText(text);
        }
    }
}

float LayoutEditorSession::animationDisplayDurationMs(
    const UIAnimationClip* clip) const {
    float duration = 1000.0f;
    if (clip != nullptr) {
        for (const UIAnimationTrack& track : clip->tracks) {
            if (!track.keyframes.empty()) {
                duration = std::max(duration,
                    track.keyframes.back().timeMs * 1.1f + 50.0f);
            }
        }
    }
    return duration;
}

void LayoutEditorSession::ensureAnimationTimelineView() {
    _animationTimelineHost = findChromeById("animation_timeline_host");
    _animationTransportStatus = dynamic_cast<TextLabel*>(
        findChromeById("animation_transport_status"));
    if (_animationTimelineHost == nullptr) {
        _animationTimelineView = nullptr;
        return;
    }
    for (Widget* child : _animationTimelineHost->getChildren()) {
        if (child != nullptr &&
            child->getId() == "__le_animation_timeline_view") {
            _animationTimelineView =
                dynamic_cast<LayoutAnimationTimelineView*>(child);
            break;
        }
    }
    if (_animationTimelineView == nullptr) {
        _animationTimelineView = new LayoutAnimationTimelineView();
        _animationTimelineHost->addChild(_animationTimelineView);
    }
    _animationTimelineView->setOnSeek([this](float timeMs) {
        previewAnimationFrame(_animationClipIndex, timeMs);
    });
    _animationTimelineView->setOnSelectionChanged(
        [this](int trackIndex, int keyIndex) {
            _animationTrackIndex = trackIndex;
            _animationKeyIndex = keyIndex;
            syncAnimationEditor();
        });
    _animationTimelineView->setOnKeyDragged(
        [this](int trackIndex, int keyIndex, float timeMs,
               LayoutAnimationKeyDragPhase phase) -> int {
            if (phase == LayoutAnimationKeyDragPhase::Begin) {
                return beginAnimationKeyframeDrag(
                    _animationClipIndex, trackIndex, keyIndex)
                    ? keyIndex : -1;
            }
            if (phase == LayoutAnimationKeyDragPhase::Update) {
                return updateAnimationKeyframeDrag(timeMs);
            }
            if (phase == LayoutAnimationKeyDragPhase::Cancel) {
                cancelAnimationKeyframeDrag();
                return _animationKeyIndex;
            }
            endAnimationKeyframeDrag();
            return _animationKeyIndex;
        });
    syncAnimationTimelineGeometry();
}

void LayoutEditorSession::syncAnimationTimelineGeometry() {
    if (_animationTimelineHost == nullptr ||
        _animationTimelineView == nullptr) return;
    const math::FVector2 hostSize = _animationTimelineHost->getSize();
    if (_animationTimelineView->getPosition().x != 0.0f ||
        _animationTimelineView->getPosition().y != 0.0f) {
        _animationTimelineView->setPosition({0.0f, 0.0f});
    }
    if (std::fabs(_animationTimelineView->getSize().x - hostSize.x) > 0.01f ||
        std::fabs(_animationTimelineView->getSize().y - hostSize.y) > 0.01f) {
        _animationTimelineView->setSize(hostSize);
    }
}

void LayoutEditorSession::syncAnimationTimelineView() {
    if (_animationTimelineView == nullptr) return;

    const UIAnimationClip* clip = _animationClipIndex >= 0 &&
        _animationClipIndex < static_cast<int>(_animationLibrary.size())
            ? &_animationLibrary.clips()[static_cast<size_t>(
                  _animationClipIndex)]
            : nullptr;
    std::vector<LayoutAnimationTimelineTrackView> tracks;
    if (clip != nullptr) {
        tracks.reserve(clip->tracks.size());
        for (const UIAnimationTrack& item : clip->tracks) {
            LayoutAnimationTimelineTrackView row;
            row.label = utf8ToWide(item.targetId) + L"  ·  " +
                utf8ToWide(UIAnimationLibrary::propertyName(item.property));
            row.keyTimesMs.reserve(item.keyframes.size());
            for (const UIAnimationKeyframe& key : item.keyframes) {
                row.keyTimesMs.push_back(key.timeMs);
            }
            tracks.push_back(std::move(row));
        }
    }
    _animationTimelineView->setDurationMs(animationDisplayDurationMs(clip));
    _animationTimelineView->setTracks(std::move(tracks));
    _animationTimelineView->setSelection(
        _animationTrackIndex, _animationKeyIndex);
    _animationTimelineView->setCurrentTimeMs(_animationPreviewTimeMs);

    const UIAnimationTrack* selectedTrack = clip != nullptr &&
        _animationTrackIndex >= 0 &&
        _animationTrackIndex < static_cast<int>(clip->tracks.size())
            ? &clip->tracks[static_cast<size_t>(_animationTrackIndex)]
            : nullptr;
    if (selectedTrack != nullptr && _animationKeyIndex >= 0 &&
        _animationKeyIndex < static_cast<int>(
            selectedTrack->keyframes.size())) {
        const UIAnimationKeyframe& key = selectedTrack->keyframes[
            static_cast<size_t>(_animationKeyIndex)];
        const float segmentDuration = _animationKeyIndex > 0
            ? key.timeMs - selectedTrack->keyframes[
                static_cast<size_t>(_animationKeyIndex - 1)].timeMs
            : 1000.0f;
        _animationTimelineView->setSelectedCurve(
            key.curve, key.bezier, key.spring, segmentDuration);
    } else {
        _animationTimelineView->setSelectedCurve(AnimationCurve::Linear);
    }
}

void LayoutEditorSession::syncAnimationTimelineTrack(int trackIndex) {
    if (_animationTimelineView == nullptr || _animationClipIndex < 0 ||
        _animationClipIndex >= static_cast<int>(_animationLibrary.size())) {
        return;
    }
    const UIAnimationClip& clip = _animationLibrary.clips()[
        static_cast<size_t>(_animationClipIndex)];
    if (trackIndex < 0 || trackIndex >= static_cast<int>(clip.tracks.size()) ||
        _animationTimelineView->tracks().size() != clip.tracks.size()) {
        syncAnimationTimelineView();
        return;
    }

    std::vector<float> keyTimes;
    const UIAnimationTrack& track = clip.tracks[
        static_cast<size_t>(trackIndex)];
    keyTimes.reserve(track.keyframes.size());
    for (const UIAnimationKeyframe& key : track.keyframes) {
        keyTimes.push_back(key.timeMs);
    }
    if (!_animationTimelineView->setTrackKeyTimes(
            trackIndex, std::move(keyTimes))) {
        syncAnimationTimelineView();
        return;
    }
    _animationTimelineView->setDurationMs(animationDisplayDurationMs(&clip));
    _animationTimelineView->setSelection(
        _animationTrackIndex, _animationKeyIndex);
    _animationTimelineView->setCurrentTimeMs(_animationPreviewTimeMs);
}

void LayoutEditorSession::syncAnimationTransportStatus() {
    if (_animationTransportStatus == nullptr) return;
    std::wstring transport = std::to_wstring(static_cast<int>(
        std::lround(_animationPreviewTimeMs))) + L" ms";
    if (isAnimationPreviewPlaying()) transport += L"  ·  Playing";
    else if (isAnimationPreviewPaused()) transport += L"  ·  Paused";
    else if (isAnimationPreviewing()) transport += L"  ·  Scrub";
    _animationTransportStatus->setText(transport);
}

void LayoutEditorSession::syncAnimationCurveParameterVisibility(
    AnimationCurve curve, bool hasKeyframe) {
    const bool bezier = hasKeyframe &&
        curve == AnimationCurve::CubicBezier;
    const bool spring = hasKeyframe && curve == AnimationCurve::Spring;
    setChromeVisible("animation_curve_parameter_label", bezier || spring);
    setChromeVisible("row_animation_bezier", bezier);
    setChromeVisible("row_animation_spring_a", spring);
    setChromeVisible("row_animation_spring_b", spring);
    setChromeVisible("row_animation_curve_apply", hasKeyframe);
    setChromeVisible("animation_spring_clamp", spring);
    setChromeEnabled("btn_animation_curve_apply", hasKeyframe);
    if (auto* label = dynamic_cast<TextLabel*>(
            findChromeById("animation_curve_parameter_label"))) {
        label->setText(bezier
            ? L"Bezier control points  x1 · y1 · x2 · y2"
            : L"Spring parameters  mass · stiffness · damping · velocity");
    }
}

void LayoutEditorSession::commitAnimationCurveParameters() {
    if (_animationCurve == nullptr || _animationKeyIndex < 0) return;
    const int curveIndex = _animationCurve->getSelectedIndex();
    if (curveIndex < 0 || curveIndex > 5) return;
    const AnimationCurve curve = static_cast<AnimationCurve>(curveIndex);
    CubicBezierParameters bezier;
    SpringParameters spring;
    if (curve == AnimationCurve::CubicBezier) {
        if (_animationBezierX1 == nullptr || _animationBezierY1 == nullptr ||
            _animationBezierX2 == nullptr || _animationBezierY2 == nullptr ||
            !parseFloat(_animationBezierX1->getText(), bezier.x1) ||
            !parseFloat(_animationBezierY1->getText(), bezier.y1) ||
            !parseFloat(_animationBezierX2->getText(), bezier.x2) ||
            !parseFloat(_animationBezierY2->getText(), bezier.y2) ||
            bezier.x1 < 0.0f || bezier.x1 > 1.0f ||
            bezier.x2 < 0.0f || bezier.x2 > 1.0f) {
            setStatus(L"Bezier x1/x2 must be in 0..1; y1/y2 must be numbers");
            return;
        }
    } else if (curve == AnimationCurve::Spring) {
        if (_animationSpringMass == nullptr ||
            _animationSpringStiffness == nullptr ||
            _animationSpringDamping == nullptr ||
            _animationSpringVelocity == nullptr ||
            !parseFloat(_animationSpringMass->getText(), spring.mass) ||
            !parseFloat(_animationSpringStiffness->getText(),
                        spring.stiffness) ||
            !parseFloat(_animationSpringDamping->getText(), spring.damping) ||
            !parseFloat(_animationSpringVelocity->getText(),
                        spring.initialVelocity) ||
            spring.mass <= 0.0f || spring.stiffness <= 0.0f ||
            spring.damping < 0.0f) {
            setStatus(L"Spring mass/stiffness must be positive; damping >= 0");
            return;
        }
        spring.clampOvershoot = _animationSpringClamp != nullptr &&
            _animationSpringClamp->getSelectedIndex() == 1;
    }
    setAnimationKeyframeCurve(
        _animationClipIndex, _animationTrackIndex, _animationKeyIndex,
        curve, bezier, spring);
}

void LayoutEditorSession::syncAnimationEditor() {
    if (_animationClipList == nullptr && _animationStatus == nullptr) return;

    const int clipCount = static_cast<int>(_animationLibrary.size());
    if (clipCount == 0) {
        _animationClipIndex = -1;
    } else if (_animationClipIndex < 0 || _animationClipIndex >= clipCount) {
        _animationClipIndex = 0;
    }

    _suppressAnimation = true;
    std::vector<std::wstring> clipLabels;
    clipLabels.reserve(_animationLibrary.size());
    for (const UIAnimationClip& clip : _animationLibrary.clips()) {
        clipLabels.push_back(utf8ToWide(clip.name));
    }
    if (_animationClipList != nullptr) {
        _animationClipList->setItems(clipLabels);
        _animationClipList->setSelectedIndex(_animationClipIndex);
    }

    const UIAnimationClip* clip = _animationClipIndex >= 0
        ? &_animationLibrary.clips()[static_cast<size_t>(_animationClipIndex)]
        : nullptr;
    if (_animationClipName != nullptr && clip != nullptr)
        _animationClipName->setText(utf8ToWide(clip->name));
    if (_animationRepeat != nullptr)
        _animationRepeat->setText(clip != nullptr
            ? std::to_wstring(clip->repeatCount) : L"0");
    if (_animationYoyo != nullptr)
        _animationYoyo->setSelectedIndex(clip != nullptr && clip->yoyo ? 1 : 0);
    if (_animationImportance != nullptr) {
        _animationImportance->setSelectedIndex(
            clip != nullptr &&
            clip->importance == AnimationImportance::Essential ? 1 : 0);
    }

    const int trackCount = clip != nullptr
        ? static_cast<int>(clip->tracks.size()) : 0;
    if (trackCount == 0) {
        _animationTrackIndex = -1;
    } else if (_animationTrackIndex < 0 || _animationTrackIndex >= trackCount) {
        _animationTrackIndex = 0;
    }
    std::vector<std::wstring> trackLabels;
    if (clip != nullptr) {
        trackLabels.reserve(clip->tracks.size());
        for (const UIAnimationTrack& track : clip->tracks) {
            trackLabels.push_back(
                utf8ToWide(track.targetId) + L"  ·  " +
                utf8ToWide(UIAnimationLibrary::propertyName(track.property)));
        }
    }
    if (_animationTrackList != nullptr) {
        _animationTrackList->setItems(trackLabels);
        _animationTrackList->setSelectedIndex(_animationTrackIndex);
    }

    const UIAnimationTrack* track = clip != nullptr &&
        _animationTrackIndex >= 0
            ? &clip->tracks[static_cast<size_t>(_animationTrackIndex)]
            : nullptr;
    const int keyCount = track != nullptr
        ? static_cast<int>(track->keyframes.size()) : 0;
    if (keyCount == 0) {
        _animationKeyIndex = -1;
    } else if (_animationKeyIndex < 0 || _animationKeyIndex >= keyCount) {
        _animationKeyIndex = 0;
    }
    std::vector<std::wstring> keyLabels;
    if (track != nullptr) {
        keyLabels.reserve(track->keyframes.size());
        for (const UIAnimationKeyframe& key : track->keyframes) {
            std::wostringstream label;
            label << key.timeMs << L" ms  ·  " << key.value.x;
            if (track->property != UIAnimationProperty::Opacity)
                label << L", " << key.value.y;
            label << L"  ·  " << utf8ToWide(
                UIAnimationLibrary::curveName(key.curve));
            keyLabels.push_back(label.str());
        }
    }
    if (_animationKeyList != nullptr) {
        _animationKeyList->setItems(keyLabels);
        _animationKeyList->setSelectedIndex(_animationKeyIndex);
    }
    if (_animationKeyIndex >= 0 && track != nullptr) {
        const UIAnimationKeyframe& key = track->keyframes[
            static_cast<size_t>(_animationKeyIndex)];
        if (_animationTime != nullptr)
            _animationTime->setText(formatFloat(key.timeMs));
        if (_animationCurve != nullptr)
            _animationCurve->setSelectedIndex(static_cast<int>(key.curve));
        if (_animationBezierX1 != nullptr)
            _animationBezierX1->setText(formatFloat(key.bezier.x1));
        if (_animationBezierY1 != nullptr)
            _animationBezierY1->setText(formatFloat(key.bezier.y1));
        if (_animationBezierX2 != nullptr)
            _animationBezierX2->setText(formatFloat(key.bezier.x2));
        if (_animationBezierY2 != nullptr)
            _animationBezierY2->setText(formatFloat(key.bezier.y2));
        if (_animationSpringMass != nullptr)
            _animationSpringMass->setText(formatFloat(key.spring.mass));
        if (_animationSpringStiffness != nullptr)
            _animationSpringStiffness->setText(
                formatFloat(key.spring.stiffness));
        if (_animationSpringDamping != nullptr)
            _animationSpringDamping->setText(formatFloat(key.spring.damping));
        if (_animationSpringVelocity != nullptr)
            _animationSpringVelocity->setText(
                formatFloat(key.spring.initialVelocity));
        if (_animationSpringClamp != nullptr)
            _animationSpringClamp->setSelectedIndex(
                key.spring.clampOvershoot ? 1 : 0);
    } else if (_animationTime != nullptr && _animationTime->getText().empty()) {
        _animationTime->setText(L"0");
    }
    _suppressAnimation = false;

    const bool hasSelectedKey = _animationKeyIndex >= 0 && track != nullptr;
    const AnimationCurve selectedCurve = hasSelectedKey
        ? track->keyframes[static_cast<size_t>(_animationKeyIndex)].curve
        : AnimationCurve::Linear;
    syncAnimationCurveParameterVisibility(selectedCurve, hasSelectedKey);

    syncAnimationTimelineView();

    const bool hasClip = clip != nullptr;
    const bool hasTrack = track != nullptr;
    const bool canBind = hasClip && _selected != nullptr &&
        _selection.size() == 1u && !_selected->getId().empty();
    setChromeEnabled("btn_animation_clip_remove", hasClip);
    setChromeEnabled("btn_animation_playback_apply", hasClip);
    setChromeEnabled("btn_animation_track_add", canBind);
    setChromeEnabled("btn_animation_track_remove", hasTrack);
    setChromeEnabled("btn_animation_key_capture", hasTrack);
    setChromeEnabled("btn_animation_key_remove", _animationKeyIndex >= 0);
    setChromeEnabled("btn_animation_preview", hasTrack && keyCount > 0);
    setChromeEnabled("btn_animation_reset", isAnimationPreviewing());
    setChromeEnabled("btn_animation_play", hasTrack && keyCount > 0 &&
                     !isAnimationPreviewPlaying());
    setChromeEnabled("btn_animation_pause", isAnimationPreviewPlaying());
    setChromeEnabled("btn_animation_stop", isAnimationPreviewing());
    setChromeEnabled("btn_animation_loop", hasClip);
    setChromeVisible("animation_timeline_separator", hasClip);
    setChromeVisible("animation_timeline_workspace", hasClip);
    if (auto* loop = dynamic_cast<Button*>(findChromeById(
            "btn_animation_loop"))) {
        loop->setText(_animationPreviewLoop
            ? localizedText("ui.editor.ui_designer.loop_on", L"Loop: On")
            : localizedText("ui.editor.ui_designer.loop_off", L"Loop: Off"));
    }
    syncAnimationTransportStatus();
    if (_animationStatus != nullptr) {
        if (!hasClip) {
            _animationStatus->setText(localizedText(
                "ui.editor.ui_designer.status.animation_hint",
                L"Create a clip, bind a Widget, then capture keys"));
        } else {
            size_t totalKeys = 0;
            for (const UIAnimationTrack& item : clip->tracks)
                totalKeys += item.keyframes.size();
            _animationStatus->setText(
                std::to_wstring(clip->tracks.size()) + L" track(s)  ·  " +
                std::to_wstring(totalKeys) + L" key(s)" +
                (isAnimationPreviewPlaying() ? L"  ·  playing" :
                 isAnimationPreviewPaused() ? L"  ·  paused" :
                 isAnimationPreviewing() ? L"  ·  scrub preview" : L""));
        }
    }
}

void LayoutEditorSession::commitPreviewFields() {
    if (_suppressPreview) return;
    float width = 0.0f, height = 0.0f, left = 0.0f, top = 0.0f,
          right = 0.0f, bottom = 0.0f;
    if (_previewWidth == nullptr || _previewHeight == nullptr ||
        !parseFloat(_previewWidth->getText(), width) ||
        !parseFloat(_previewHeight->getText(), height) || width < 1.0f ||
        height < 1.0f) {
        setStatus(L"Preview resolution must be positive");
        syncPreviewControls();
        return;
    }
    auto readInset = [](TextInput* field, float& value) {
        if (field == nullptr || field->getText().empty()) {
            value = 0.0f;
            return true;
        }
        return parseFloat(field->getText(), value) && value >= 0.0f;
    };
    if (!readInset(_previewSafeL, left) || !readInset(_previewSafeT, top) ||
        !readInset(_previewSafeR, right) || !readInset(_previewSafeB, bottom)) {
        setStatus(L"Safe Area insets must be non-negative");
        syncPreviewControls();
        return;
    }
    LayoutPreviewSettings settings = _previewModel.settings();
    settings.pixelWidth = width;
    settings.pixelHeight = height;
    settings.safeAreaPixels = {left, top, right, bottom};
    setPreviewSettings(settings);
}

bool LayoutEditorSession::hitPaletteType(const math::FVector2& worldPos,
                                         std::string& outType) const {
    if (_ui == nullptr) {
        return false;
    }
    // The palette content is intentionally much taller than its viewport.
    // Directly testing each button's world bounds bypassed ScrollView's
    // clipped hit-test contract, so off-screen entries remained clickable
    // over Document Outline. Gate this editor-level shortcut by the exact
    // same client rectangle used by ScrollView rendering and hit testing.
    auto* paletteScroll = dynamic_cast<ScrollView*>(
        findChromeById("palette_scroll"));
    if (paletteScroll == nullptr ||
        !paletteScroll->getClientRect().contains(worldPos)) {
        return false;
    }
    for (const WidgetAuthoringDescriptor& entry :
         WidgetAuthoringRegistry::get().descriptors()) {
        if (entry.paletteButtonId.empty()) continue;
        Widget* w = findChromeById(entry.paletteButtonId);
        if (w != nullptr && w->isVisible() &&
            w->getWorldBounds().contains(worldPos)) {
            outType = entry.typeName;
            return true;
        }
    }
    return false;
}

bool LayoutEditorSession::hitHierarchyList(const math::FVector2& worldPos) const {
    return _hierarchy != nullptr &&
           _hierarchy->getWorldBounds().contains(worldPos);
}

bool LayoutEditorSession::hierarchyScrollbarHit(
    const math::FVector2& worldPos) const {
    if (_hierarchy == nullptr) {
        return false;
    }
    if (ScrollBar* bar = _hierarchy->getVerticalScrollBar()) {
        if (bar->isVisible() && bar->getWorldBounds().contains(worldPos)) {
            return true;
        }
    }
    return false;
}

int LayoutEditorSession::hierarchyIndexAt(const math::FVector2& worldPos,
                                          float* fracInRow) const {
    if (fracInRow != nullptr) {
        *fracInRow = 0.0f;
    }
    if (_hierarchy == nullptr || _hierarchyIndex.empty()) {
        return -1;
    }
    const math::FRectangle client = _hierarchy->getClientRect();
    if (!client.contains(worldPos) &&
        !_hierarchy->getWorldBounds().contains(worldPos)) {
        return -1;
    }
    float itemH = _hierarchy->getItemHeight();
    if (itemH < 1.0f) {
        itemH = 24.0f;
    }
    const float scrollY = _hierarchy->getScrollOffset().y;
    const float localY = worldPos.y - client.minY + scrollY;
    if (localY < 0.0f) {
        return 0;
    }
    const int idx = static_cast<int>(localY / itemH);
    if (fracInRow != nullptr) {
        *fracInRow = (localY / itemH) - static_cast<float>(idx);
    }
    if (idx < 0) {
        return 0;
    }
    if (idx >= static_cast<int>(_hierarchyIndex.size())) {
        return static_cast<int>(_hierarchyIndex.size()) - 1;
    }
    return idx;
}

bool LayoutEditorSession::isContainerWidget(Widget* widget) const {
    return dynamic_cast<Panel*>(widget) != nullptr ||
        dynamic_cast<BoxBase*>(widget) != nullptr ||
        dynamic_cast<GridPanel*>(widget) != nullptr ||
        dynamic_cast<ScrollView*>(widget) != nullptr ||
        dynamic_cast<TabControl*>(widget) != nullptr ||
        dynamic_cast<Modal*>(widget) != nullptr ||
        dynamic_cast<Window*>(widget) != nullptr;
}

Widget* LayoutEditorSession::structuredContentOwner(Widget* content) const {
    if (content == nullptr || _docRoot == nullptr) {
        return nullptr;
    }

    // The outline exposes authored content slots instead of implementation
    // children. Some of those slots (inactive TabControl pages in particular)
    // are not mounted in the physical Widget tree, so walking getParent()
    // cannot identify their logical owner. Follow the same semantic tree as
    // collectHierarchy() and return the control that owns the slot.
    std::function<Widget*(Widget*)> visit = [&](Widget* node) -> Widget* {
        if (node == nullptr) return nullptr;

        if (auto* tabs = dynamic_cast<TabControl*>(node)) {
            for (int i = 0; i < static_cast<int>(tabs->getTabCount()); ++i) {
                Widget* page = tabs->getTabContent(i);
                if (page == content) return tabs;
                if (Widget* owner = visit(page)) return owner;
            }
            return nullptr;
        }
        if (auto* dialog = dynamic_cast<ModalDialog*>(node)) {
            Widget* body = dialog->getBodyContent();
            if (body == content) return dialog;
            return visit(body);
        }
        if (auto* scroll = dynamic_cast<ScrollView*>(node)) {
            Widget* scrollContent = scroll->getContent();
            if (scrollContent == content) return scroll;
            return visit(scrollContent);
        }
        if (auto* modal = dynamic_cast<Modal*>(node)) {
            Widget* modalContent = modal->getContent();
            if (modalContent == content) return modal;
            return visit(modalContent);
        }

        std::vector<Widget*> children;
        collectAuthoredChildren(node, children);
        for (Widget* child : children) {
            if (Widget* owner = visit(child)) return owner;
        }
        return nullptr;
    };
    return visit(_docRoot);
}

bool LayoutEditorSession::isAncestorOf(Widget* ancestor, Widget* node) const {
    if (ancestor == nullptr || node == nullptr || ancestor == node) {
        return false;
    }
    std::function<bool(Widget*)> contains = [&](Widget* current) {
        std::vector<Widget*> children;
        collectAuthoredChildren(current, children);
        for (Widget* child : children) {
            if (child == node || contains(child)) {
                return true;
            }
        }
        return false;
    };
    return contains(ancestor);
}

int LayoutEditorSession::siblingIndexOf(Widget* child) const {
    if (child == nullptr) {
        return -1;
    }
    Widget* parent = child->getParent();
    if (parent == nullptr) {
        return -1;
    }
    if (auto* box = dynamic_cast<BoxBase*>(parent)) {
        return box->slotIndexOf(child);
    }
    if (auto* grid = dynamic_cast<GridPanel*>(parent)) {
        return designerGridIndexOf(grid, child);
    }
    const auto& kids = parent->getChildren();
    for (size_t i = 0; i < kids.size(); ++i) {
        if (kids[i] == child) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

LayoutEditorSession::HierDropTarget LayoutEditorSession::resolveHierDrop(
    const math::FVector2& worldPos, Widget* dragged) const {
    HierDropTarget out;
    if (hierarchyFilterActive()) return out;
    if (!hitHierarchyList(worldPos) || hierarchyScrollbarHit(worldPos)) {
        return out;
    }
    float frac = 0.5f;
    const int idx = hierarchyIndexAt(worldPos, &frac);
    if (idx < 0 || idx >= static_cast<int>(_hierarchyIndex.size())) {
        return out;
    }
    Widget* target = _hierarchyIndex[static_cast<size_t>(idx)];
    if (target == nullptr) {
        return out;
    }
    if (dragged != nullptr) {
        if (target == dragged || isAncestorOf(dragged, target)) {
            return out;
        }
    }

    out.target = target;
    out.listIndex = idx;
    out.valid = true;

    // Structured content roots represent a fixed slot (a tab page, modal
    // body, or scroll content), not an ordinary reorderable child. Dropping
    // on their outline row means inserting into that root regardless of the
    // pointer's vertical fraction. Moving the root itself is rejected by the
    // commit path below.
    if (structuredContentOwner(target) != nullptr) {
        if (isContainerWidget(target)) {
            out.place = DropPlace::Into;
        } else {
            out.valid = false;
        }
        return out;
    }

    const bool canInto = isContainerWidget(target) && target != dragged &&
                         (dragged == nullptr || !isAncestorOf(dragged, target));
    if (canInto && frac > 0.28f && frac < 0.72f) {
        out.place = DropPlace::Into;
    } else if (frac < 0.5f) {
        out.place = DropPlace::Before;
    } else {
        out.place = DropPlace::After;
    }

    if (out.place != DropPlace::Into && target == _docRoot) {
        // Root has no sibling slot — treat as into root.
        if (isContainerWidget(target)) {
            out.place = DropPlace::Into;
        } else {
            out.valid = false;
        }
    }
    return out;
}

bool LayoutEditorSession::detachFromTree(Widget* child) {
    if (child == nullptr) {
        return false;
    }
    // A structured root cannot be detached without changing the control's
    // authored model (for example, removing a TabControl page). Reordering
    // such roots is intentionally handled as a separate future operation.
    if (structuredContentOwner(child) != nullptr) {
        return false;
    }
    Widget* parent = child->getParent();
    if (parent == nullptr) {
        return false;
    }
    if (auto* box = dynamic_cast<BoxBase*>(parent)) {
        box->removeWidget(child);
    } else if (auto* grid = dynamic_cast<GridPanel*>(parent)) {
        const int index = designerGridIndexOf(grid, child);
        if (index < 0 || grid->getColumnCount() <= 0) return false;
        grid->clearCell(index / grid->getColumnCount(),
                        index % grid->getColumnCount());
    } else {
        parent->removeChild(child);
    }
    return child->getParent() == nullptr;
}

bool LayoutEditorSession::attachAt(Widget* parent, Widget* child, size_t index) {
    if (parent == nullptr || child == nullptr) {
        return false;
    }

    // Redirect drops on structural controls through their semantic content
    // slots. This keeps private aliases such as ScrollView::_content and
    // Modal::_content synchronized with the visible Widget tree.
    if (auto* scroll = dynamic_cast<ScrollView*>(parent)) {
        if (Widget* content = scroll->getContent()) {
            if (!isContainerWidget(content)) return false;
            return attachAt(content, child, index);
        }
        scroll->setContentOwned(child);
        child->setPosition({0.0f, 0.0f});
        return scroll->getContent() == child && child->getParent() == scroll;
    }
    if (auto* tabs = dynamic_cast<TabControl*>(parent)) {
        Widget* page = tabs->getTabContent(tabs->getSelectedIndex());
        if (page == nullptr || !isContainerWidget(page)) return false;
        return attachAt(page, child, index);
    }
    if (auto* dialog = dynamic_cast<ModalDialog*>(parent)) {
        if (Widget* body = dialog->getBodyContent()) {
            if (!isContainerWidget(body)) return false;
            return attachAt(body, child, index);
        }
        dialog->setBodyContentOwned(child);
        child->setPosition({0.0f, 0.0f});
        return dialog->getBodyContent() == child;
    }
    if (auto* modal = dynamic_cast<Modal*>(parent)) {
        if (Widget* content = modal->getContent()) {
            if (!isContainerWidget(content)) return false;
            return attachAt(content, child, index);
        }
        modal->setContentOwned(child);
        child->setPosition({0.0f, 0.0f});
        return modal->getContent() == child && child->getParent() == modal;
    }
    if (!isContainerWidget(parent)) {
        return false;
    }
    if (auto* box = dynamic_cast<BoxBase*>(parent)) {
        child->clearAnchorLayout();
        int slotCount = 0;
        while (box->slotAt(slotCount) != nullptr) {
            ++slotCount;
        }
        int insertAt = static_cast<int>(index);
        if (insertAt < 0) {
            insertAt = slotCount;
        }
        if (insertAt > slotCount) {
            insertAt = slotCount;
        }
        box->insertWidget(insertAt, child, 0.0f);
        return child->getParent() == parent;
    }
    if (auto* grid = dynamic_cast<GridPanel*>(parent)) {
        child->clearAnchorLayout();
        return rebuildDesignerGrid(grid, child, index);
    }

    parent->addChild(child);
    if (child->getParent() != parent) {
        return false;
    }
    const size_t n = parent->getChildren().size();
    size_t insertAt = index;
    if (n == 0) {
        return true;
    }
    if (insertAt >= n) {
        insertAt = n - 1;
    }
    const bool moved = parent->moveChildToIndex(child, insertAt);
    if (moved && child->hasAnchorLayout()) {
        child->refreshAnchorOffsetsFromCurrentRect();
    }
    return moved;
}

Widget* LayoutEditorSession::createWidgetInstance(const std::string& typeName) {
    return WidgetAuthoringRegistry::get().create(
        typeName, [this](const std::string& prefix, Widget* detachedRoot) {
            return makeUniqueId(prefix, detachedRoot);
        });
}

bool LayoutEditorSession::placeNewWidget(Widget* created, Widget* parent,
                                         int insertIndex,
                                         const math::FVector2* worldPos) {
    if (created == nullptr || parent == nullptr) {
        return false;
    }
    if (auto* scroll = dynamic_cast<ScrollView*>(parent)) {
        if (Widget* content = scroll->getContent()) {
            if (!isContainerWidget(content)) {
                setStatus(L"ScrollView content is not a container");
                return false;
            }
            parent = content;
        } else {
            scroll->setContentOwned(created);
            created->setPosition({0.0f, 0.0f});
            return true;
        }
    }
    if (auto* tabs = dynamic_cast<TabControl*>(parent)) {
        Widget* page = tabs->getTabContent(tabs->getSelectedIndex());
        if (page == nullptr || !isContainerWidget(page)) {
            setStatus(L"TabControl has no editable content page");
            return false;
        }
        parent = page;
    }
    if (auto* dialog = dynamic_cast<ModalDialog*>(parent)) {
        if (Widget* body = dialog->getBodyContent()) {
            if (!isContainerWidget(body)) {
                setStatus(L"ModalDialog body is not a container");
                return false;
            }
            parent = body;
        } else {
            dialog->setBodyContentOwned(created);
            created->setPosition({0.0f, 0.0f});
            return true;
        }
    } else if (auto* modal = dynamic_cast<Modal*>(parent)) {
        if (Widget* content = modal->getContent()) {
            if (!isContainerWidget(content)) {
                setStatus(L"Modal content is not a container");
                return false;
            }
            parent = content;
        } else {
            modal->setContentOwned(created);
            created->setPosition({0.0f, 0.0f});
            return true;
        }
    }
    if (!isContainerWidget(parent)) {
        setStatus(L"Selected control cannot contain authored children");
        return false;
    }
    if (auto* box = dynamic_cast<BoxBase*>(parent)) {
        created->clearAnchorLayout();
        if (insertIndex < 0) {
            box->addWidget(created, 0.0f);
        } else {
            box->insertWidget(insertIndex, created, 0.0f);
        }
    } else if (auto* grid = dynamic_cast<GridPanel*>(parent)) {
        created->clearAnchorLayout();
        const size_t index = insertIndex < 0
            ? collectDesignerGridEntries(grid).size()
            : static_cast<size_t>(insertIndex);
        if (!rebuildDesignerGrid(grid, created, index)) return false;
    } else {
        parent->addChild(created);
        if (insertIndex >= 0) {
            const size_t n = parent->getChildren().size();
            size_t idx = static_cast<size_t>(insertIndex);
            if (idx >= n) {
                idx = n > 0 ? n - 1 : 0;
            }
            parent->moveChildToIndex(created, idx);
        }
        if (worldPos != nullptr) {
            const math::FRectangle pb = parent->getWorldBounds();
            created->setPosition(math::FVector2(
                worldPos->x - pb.minX, worldPos->y - pb.minY));
            created->setLayoutPositionManaged(false);
        } else {
            created->setPosition(nextDesignerFreePosition(parent, created));
            created->setLayoutPositionManaged(false);
        }
        snapWidgetPosition(created);
        created->setLayoutSizeManaged(false);
        if (created->hasAnchorLayout()) {
            created->refreshAnchorOffsetsFromCurrentRect();
        } else {
            created->setAnchorLayoutPreservingRect(
                math::FVector2(0.0f, 0.0f),
                math::FVector2(0.0f, 0.0f),
                math::FVector2(0.0f, 0.0f));
        }
    }
    return created->getParent() == parent;
}

void LayoutEditorSession::commitPaletteDrop(const math::FVector2& worldPos) {
    if (_paletteType.empty()) {
        return;
    }
    // Only place when releasing over the canvas (document area).
    if (!isCanvasHit(worldPos)) {
        setStatus(L"已取消放置（需在画布区域松开）");
        return;
    }

    Widget* parent = nullptr;
    int insertIndex = -1;
    math::FVector2 dropPos = screenToDocument(worldPos);
    const math::FVector2* posPtr = &dropPos;

    Widget* hit = pickDocumentWidget(worldPos);
    if (hit != nullptr && isContainerWidget(hit)) {
        parent = hit;
    } else if (hit != nullptr) {
        Widget* p = hit->getParent();
            if (p != nullptr && isUnderCanvas(p) && p != _canvasHost &&
                p != _canvasViewport &&
                isContainerWidget(p)) {
            parent = p;
            insertIndex = siblingIndexOf(hit) + 1;
        } else {
            parent = _docRoot;
        }
    } else {
        parent = _docRoot;
    }

    if (parent == nullptr) {
        setStatus(L"已取消放置 — 无父节点");
        return;
    }

    Widget* created = createWidgetInstance(_paletteType);
    if (created == nullptr) {
        setStatus(utf8ToWide("Unknown type: " + _paletteType));
        return;
    }
    pushUndo(LayoutEditKind::Insert, "Drop widget");
    if (!placeNewWidget(created, parent, insertIndex, posPtr)) {
        _commandStack.discardLastUndo();
        destroyWidgetTree(created);
        return;
    }
    freezeDocumentInteraction(created);
    markDirty(true);
    refreshHierarchy();
    select(created, false);
    if (_ui != nullptr) {
        _ui->invalidateLayout();
        _ui->layout();
    }
    setStatus(utf8ToWide("已放置 " + _paletteType));
}

void LayoutEditorSession::commitHierarchyDrop(const math::FVector2& worldPos) {
    Widget* dragged = _hierDragWidget;
    if (dragged == nullptr || dragged == _docRoot) {
        setStatus(L"Reorder cancelled");
        return;
    }
    if (structuredContentOwner(dragged) != nullptr) {
        setStatus(L"Structured content roots cannot be reordered");
        return;
    }
    HierDropTarget drop = resolveHierDrop(worldPos, dragged);
    if (!drop.valid || drop.target == nullptr) {
        setStatus(L"Reorder cancelled");
        return;
    }

    Widget* newParent = nullptr;
    int insertIndex = 0;
    if (drop.place == DropPlace::Into) {
        newParent = drop.target;
        insertIndex = -1;
    } else {
        newParent = drop.target->getParent();
        if (newParent == nullptr || newParent == _canvasHost) {
            setStatus(L"Cannot reorder here");
            return;
        }
        insertIndex = siblingIndexOf(drop.target);
        if (drop.place == DropPlace::After) {
            ++insertIndex;
        }
    }

    if (newParent == nullptr || isAncestorOf(dragged, newParent)) {
        setStatus(L"Invalid drop target");
        return;
    }

    Widget* oldParent = dragged->getParent();
    const int oldIndex = siblingIndexOf(dragged);
    if (oldParent == newParent && drop.place != DropPlace::Into) {
        int dest = insertIndex;
        if (oldIndex >= 0 && oldIndex < dest) {
            --dest;
        }
        if (dest == oldIndex) {
            setStatus(L"No change");
            return;
        }
        pushUndo(LayoutEditKind::Reorder, "Reorder hierarchy");
        bool ok = false;
        if (auto* box = dynamic_cast<BoxBase*>(newParent)) {
            ok = box->moveSlotToIndex(dragged, static_cast<size_t>(dest));
        } else if (auto* grid = dynamic_cast<GridPanel*>(newParent)) {
            ok = rebuildDesignerGrid(grid, dragged,
                                     static_cast<size_t>(dest));
        } else {
            ok = newParent->moveChildToIndex(dragged,
                                             static_cast<size_t>(dest));
        }
        if (!ok) {
            _commandStack.discardLastUndo();
            setStatus(L"Reorder failed");
            return;
        }
    } else {
        pushUndo(LayoutEditKind::Reorder, "Reparent hierarchy");
        if (!detachFromTree(dragged)) {
            _commandStack.discardLastUndo();
            setStatus(L"Reorder failed");
            return;
        }
        size_t dest = 0;
        if (insertIndex < 0) {
            if (auto* box = dynamic_cast<BoxBase*>(newParent)) {
                int n = 0;
                while (box->slotAt(n) != nullptr) {
                    ++n;
                }
                dest = static_cast<size_t>(n);
            } else {
                dest = newParent->getChildren().size();
            }
        } else {
            dest = static_cast<size_t>(insertIndex);
            if (oldParent == newParent && oldIndex >= 0 &&
                oldIndex < insertIndex) {
                // already detached — children shrunk; insertIndex was
                // computed before detach, so adjust.
                if (dest > 0) {
                    --dest;
                }
            }
        }
        if (!attachAt(newParent, dragged, dest)) {
            // Best-effort: try put back under old parent.
            if (oldParent != nullptr) {
                attachAt(oldParent, dragged,
                         oldIndex >= 0 ? static_cast<size_t>(oldIndex) : 0);
            }
            _commandStack.discardLastUndo();
            setStatus(L"Reorder failed");
            return;
        }
    }

    if (_docRoot != nullptr) {
        freezeDocumentInteraction(_docRoot);
    }
    markDirty(true);
    refreshHierarchy();
    select(dragged, false);
    if (_ui != nullptr) {
        _ui->invalidateLayout();
        _ui->layout();
    }
    setStatus(drop.place == DropPlace::Into ? L"Moved into container"
                                            : L"Reordered");
}

void LayoutEditorSession::ensureHierDropChrome() {
    if (_hierarchyCol == nullptr) {
        _hierarchyCol = findChromeById("hierarchy_col");
    }
    if (_hierarchyCol == nullptr) {
        return;
    }
    if (_hierDropLine != nullptr) {
        return;
    }

    if (auto* sheet = StyleManager::get().getStyleSheet()) {
        WidgetStyle line{};
        line.backgroundColor = math::FVector4(0.35f, 0.75f, 1.0f, 0.95f);
        line.borderColor = line.backgroundColor;
        line.border.color = line.borderColor;
        line.border.width = 0.0f;
        sheet->setStyle("__le_hier_drop_line", line);

        WidgetStyle into{};
        into.backgroundColor = math::FVector4(0.25f, 0.55f, 0.95f, 0.28f);
        into.borderColor = math::FVector4(0.35f, 0.75f, 1.0f, 0.9f);
        into.border.color = into.borderColor;
        into.border.width = 1.0f;
        sheet->setStyle("__le_hier_drop_into", into);
    }

    _hierDropLine = new Panel();
    _hierDropLine->setId("__le_hier_drop_line");
    _hierDropLine->setStyleId("__le_hier_drop_line");
    _hierDropLine->setBorderEnabled(false);
    _hierDropLine->setBackgroundEnabled(true);
    _hierDropLine->setVisible(false);
    _hierDropLine->setLayoutPositionManaged(false);
    _hierDropLine->setLayoutSizeManaged(false);
    _hierarchyCol->addChild(_hierDropLine);

    _hierDropInto = new Panel();
    _hierDropInto->setId("__le_hier_drop_into");
    _hierDropInto->setStyleId("__le_hier_drop_into");
    _hierDropInto->setBorderEnabled(true);
    _hierDropInto->setBackgroundEnabled(true);
    _hierDropInto->setVisible(false);
    _hierDropInto->setLayoutPositionManaged(false);
    _hierDropInto->setLayoutSizeManaged(false);
    _hierarchyCol->addChild(_hierDropInto);
}

void LayoutEditorSession::destroyHierDropChrome() {
    auto destroyOne = [this](Panel*& p) {
        if (p == nullptr) {
            return;
        }
        if (_hierarchyCol != nullptr && p->getParent() == _hierarchyCol) {
            _hierarchyCol->removeChild(p);
        }
        destroyWidgetTree(p);
        p = nullptr;
    };
    destroyOne(_hierDropLine);
    destroyOne(_hierDropInto);
}

void LayoutEditorSession::clearHierDropChrome() {
    if (_hierDropLine != nullptr) {
        _hierDropLine->setVisible(false);
    }
    if (_hierDropInto != nullptr) {
        _hierDropInto->setVisible(false);
    }
}

void LayoutEditorSession::updateHierDropChrome(const HierDropTarget& drop) {
    ensureHierDropChrome();
    if (_hierarchyCol == nullptr || _hierarchy == nullptr) {
        clearHierDropChrome();
        return;
    }
    if (!drop.valid || drop.target == nullptr || drop.listIndex < 0) {
        clearHierDropChrome();
        return;
    }

    float itemH = _hierarchy->getItemHeight();
    if (itemH < 1.0f) {
        itemH = 24.0f;
    }
    const math::FRectangle client = _hierarchy->getClientRect();
    const math::FRectangle col = _hierarchyCol->getWorldBounds();
    const float scrollY = _hierarchy->getScrollOffset().y;
    const float rowWorldY =
        client.minY - scrollY + static_cast<float>(drop.listIndex) * itemH;
    const float localX = client.minX - col.minX;
    const float width = std::max(8.0f, client.width());

    if (drop.place == DropPlace::Into) {
        if (_hierDropLine != nullptr) {
            _hierDropLine->setVisible(false);
        }
        if (_hierDropInto != nullptr) {
            _hierDropInto->setPosition(math::FVector2(
                localX, rowWorldY - col.minY));
            _hierDropInto->setSize(math::FVector2(width, itemH));
            _hierDropInto->setVisible(true);
        }
    } else {
        if (_hierDropInto != nullptr) {
            _hierDropInto->setVisible(false);
        }
        float lineY = rowWorldY;
        if (drop.place == DropPlace::After) {
            lineY += itemH;
        }
        if (_hierDropLine != nullptr) {
            _hierDropLine->setPosition(math::FVector2(
                localX, lineY - col.minY - 1.0f));
            _hierDropLine->setSize(math::FVector2(width, 3.0f));
            _hierDropLine->setVisible(true);
        }
    }
}

void LayoutEditorSession::cancelToolDrag() {
    _toolDrag = ToolDrag::None;
    _paletteType.clear();
    _hierDragWidget = nullptr;
    clearHierDropChrome();
    hidePaletteGhost();
}

bool LayoutEditorSession::stillOnPaletteSource(
    const math::FVector2& worldPos) const {
    std::string type;
    if (!hitPaletteType(worldPos, type)) {
        return false;
    }
    return type == _paletteType;
}

void LayoutEditorSession::ensurePaletteGhost() {
    if (_chromeRoot == nullptr && _ui != nullptr) {
        _chromeRoot = findChromeById("layout_editor_root");
    }
    if (_chromeRoot == nullptr || _paletteGhost != nullptr) {
        return;
    }

    if (auto* sheet = StyleManager::get().getStyleSheet()) {
        WidgetStyle ghost{};
        ghost.backgroundColor = math::FVector4(0.30f, 0.55f, 0.90f, 0.35f);
        ghost.borderColor = math::FVector4(0.45f, 0.80f, 1.0f, 0.95f);
        ghost.border.color = ghost.borderColor;
        ghost.border.width = 2.0f;
        sheet->setStyle("__le_palette_ghost", ghost);

        WidgetStyle ghostBad{};
        ghostBad.backgroundColor = math::FVector4(0.35f, 0.35f, 0.38f, 0.28f);
        ghostBad.borderColor = math::FVector4(0.65f, 0.65f, 0.70f, 0.7f);
        ghostBad.border.color = ghostBad.borderColor;
        ghostBad.border.width = 2.0f;
        sheet->setStyle("__le_palette_ghost_bad", ghostBad);

        WidgetStyle ghostLbl{};
        ghostLbl.textColor = math::FVector4(0.95f, 0.97f, 1.0f, 0.95f);
        sheet->setStyle("__le_palette_ghost_lbl", ghostLbl);
    }

    _paletteGhost = new Panel();
    _paletteGhost->setId("__le_palette_ghost");
    _paletteGhost->setStyleId("__le_palette_ghost");
    _paletteGhost->setBorderEnabled(true);
    _paletteGhost->setBackgroundEnabled(true);
    _paletteGhost->setVisible(false);
    _paletteGhost->setLayoutPositionManaged(false);
    _paletteGhost->setLayoutSizeManaged(false);
    _chromeRoot->addChild(_paletteGhost);

    _paletteGhostLabel = new TextLabel();
    _paletteGhostLabel->setId("__le_palette_ghost_lbl");
    _paletteGhostLabel->setStyleId("__le_palette_ghost_lbl");
    _paletteGhostLabel->setLayoutPositionManaged(false);
    _paletteGhostLabel->setLayoutSizeManaged(false);
    _paletteGhostLabel->setPosition(math::FVector2(6.0f, 4.0f));
    _paletteGhost->addChild(_paletteGhostLabel);
}

void LayoutEditorSession::destroyPaletteGhost() {
    if (_paletteGhost == nullptr) {
        _paletteGhostLabel = nullptr;
        return;
    }
    if (_chromeRoot != nullptr && _paletteGhost->getParent() == _chromeRoot) {
        _chromeRoot->removeChild(_paletteGhost);
    }
    destroyWidgetTree(_paletteGhost);
    _paletteGhost = nullptr;
    _paletteGhostLabel = nullptr;
}

void LayoutEditorSession::showPaletteGhost(const std::string& typeName) {
    ensurePaletteGhost();
    if (_paletteGhost == nullptr) {
        return;
    }
    const WidgetAuthoringDescriptor* descriptor =
        WidgetAuthoringRegistry::get().find(typeName);
    const math::FVector2 sz = descriptor != nullptr
        ? descriptor->defaultSize : math::FVector2(120.0f, 28.0f);
    _paletteGhost->setSize(sz);
    if (_paletteGhostLabel != nullptr) {
        _paletteGhostLabel->setText(utf8ToWide(typeName));
        _paletteGhostLabel->setSize(math::FVector2(
            (std::max)(40.0f, sz.x - 12.0f), 20.0f));
    }
    _paletteGhost->setVisible(true);
    updatePaletteGhost(_dragLastMouse, isCanvasHit(_dragLastMouse));
}

void LayoutEditorSession::updatePaletteGhost(const math::FVector2& worldPos,
                                             bool overCanvas) {
    if (_paletteGhost == nullptr || !_paletteGhost->isVisible()) {
        return;
    }
    if (_chromeRoot == nullptr) {
        return;
    }
    _paletteGhost->setStyleId(overCanvas ? "__le_palette_ghost"
                                         : "__le_palette_ghost_bad");
    const math::FRectangle root = _chromeRoot->getWorldBounds();
    const math::FVector2 sz = _paletteGhost->getSize();
    _paletteGhost->setPosition(math::FVector2(
        worldPos.x - root.minX - sz.x * 0.5f,
        worldPos.y - root.minY - sz.y * 0.5f));
}

void LayoutEditorSession::hidePaletteGhost() {
    if (_paletteGhost != nullptr) {
        _paletteGhost->setVisible(false);
    }
}

void LayoutEditorSession::selectAll() {
    _selection.clear();
    std::vector<Widget*> authored;
    collectDocumentWidgets(_docRoot, authored);
    for (Widget* widget : authored) {
        if (widget != nullptr && widget != _docRoot &&
            isUnderCanvas(widget)) {
            _selection.push_back(widget);
        }
    }
    normalizeSelectionNesting();
    if (_selection.empty() && _docRoot != nullptr) {
        _selection.push_back(_docRoot);
    }
    _selected = _selection.empty() ? nullptr : _selection.back();
    syncPropertyStrip();
    syncHierarchySelection();
    syncSelectionChrome();
    std::wostringstream oss;
    oss << L"Selected all (" << _selection.size() << L")";
    setStatus(oss.str());
}

std::vector<std::string> LayoutEditorSession::serializeSelectionItems() const {
    std::vector<std::string> items;
    UILayoutLoader saver;
    for (Widget* w : _selection) {
        if (w == nullptr || w == _docRoot || isEditorOverlay(w)) {
            continue;
        }
        std::string json;
        if (saver.saveLayoutToString(w, json, false) && !json.empty()) {
            items.push_back(std::move(json));
        }
    }
    return items;
}

void LayoutEditorSession::copySelection() {
    _clipboardItems = serializeSelectionItems();
    if (_clipboardItems.empty()) {
        setStatus(L"Copy: nothing to copy");
        return;
    }
    std::string joined;
    for (size_t i = 0; i < _clipboardItems.size(); ++i) {
        if (i > 0) {
            joined += "\n/*AYUI_CLIP*/\n";
        }
        joined += _clipboardItems[i];
    }
    getClipboard().setText(utf8ToWide(joined));
    std::wostringstream oss;
    oss << L"Copied " << _clipboardItems.size() << L" widget(s)";
    setStatus(oss.str());
}

void LayoutEditorSession::pasteClipboard() {
    std::vector<std::string> source = _clipboardItems;
    std::wstring clip;
    if (getClipboard().getText(clip)) {
        source.clear();
        const std::string utf8 = wideToUtf8(clip);
        const std::string delim = "\n/*AYUI_CLIP*/\n";
        size_t start = 0;
        while (start < utf8.size()) {
            const size_t pos = utf8.find(delim, start);
            const std::string item = pos == std::string::npos
                ? utf8.substr(start) : utf8.substr(start, pos - start);
            if (!item.empty()) source.push_back(item);
            if (pos == std::string::npos) break;
            start = pos + delim.size();
        }
    }
    if (source.empty()) {
        setStatus(L"Paste: clipboard empty");
        return;
    }
    _clipboardItems = source;
    pasteSerializedItems(source);
}

void LayoutEditorSession::pasteSerializedItems(
    const std::vector<std::string>& items) {
    Widget* parent = pickParentForAdd();
    if (parent == nullptr) {
        setStatus(L"Paste: no parent");
        return;
    }

    pushUndo(LayoutEditKind::Clipboard, "Paste");
    std::vector<Widget*> pasted;
    float offset = 16.0f;
    for (const std::string& json : items) {
        Widget* created = _docLoader.loadFromString(json);
        if (created == nullptr) {
            continue;
        }
        remintTreeIds(created);
        math::FVector2 pos = created->getPosition();
        pos.x += offset;
        pos.y += offset;
        created->setPosition(pos);
        if (!placeNewWidget(created, parent, -1, nullptr)) {
            destroyWidgetTree(created);
            continue;
        }
        freezeDocumentInteraction(created);
        rehydrateRuntimePresentation(created);
        pasted.push_back(created);
        offset += 8.0f;
    }
    if (pasted.empty()) {
        _commandStack.discardLastUndo();
        setStatus(L"Paste failed");
        return;
    }
    markDirty(true);
    refreshHierarchy();
    _selection = pasted;
    _selected = pasted.back();
    syncPropertyStrip();
    syncHierarchySelection();
    syncSelectionChrome();
    if (_ui != nullptr) {
        _ui->invalidateLayout();
        _ui->layout();
    }
    std::wostringstream oss;
    oss << L"Pasted " << pasted.size() << L" widget(s)";
    setStatus(oss.str());
}

void LayoutEditorSession::duplicateSelection() {
    const std::vector<std::string> items = serializeSelectionItems();
    if (items.empty()) {
        setStatus(L"Duplicate: nothing to duplicate");
        return;
    }
    pasteSerializedItems(items);
    setStatus(L"Duplicated");
}

void LayoutEditorSession::nudgeSelection(float dx, float dy) {
    if (_selection.empty()) {
        return;
    }
    const std::size_t undoDepthBefore = _commandStack.undoDepth();
    beginMutation(LayoutEditKind::Transform, "Nudge selection");
    bool moved = false;
    for (Widget* w : _selection) {
        if (w == nullptr || w == _docRoot ||
            structuredContentOwner(w) != nullptr) {
            continue;
        }
        if (w->getParent() != nullptr &&
            (dynamic_cast<BoxBase*>(w->getParent()) != nullptr ||
             dynamic_cast<GridPanel*>(w->getParent()) != nullptr)) {
            continue;
        }
        math::FVector2 pos = w->getPosition();
        pos.x += dx;
        pos.y += dy;
        w->setPosition(pos);
        refreshAnchorOffsets(w);
        moved = true;
    }
    endMutation();
    if (!moved) {
        if (_commandStack.undoDepth() > undoDepthBefore) {
            _commandStack.discardLastUndo();
        }
        if (_selected == _docRoot && _selection.size() == 1u) {
            setStatus(L"Document root position is fixed");
        } else if (_selected != nullptr &&
                   structuredContentOwner(_selected) != nullptr) {
            setStatus(L"Structured content position is fixed by its owner");
        } else {
            setStatus(L"Nudge: no free-position widgets in selection");
        }
        return;
    }
    markDirty(true);
    syncPropertyStrip();
    syncSelectionChrome();
    if (_ui != nullptr) {
        _ui->invalidateLayout();
    }
}

void LayoutEditorSession::toggleSnap() {
    _snapEnabled = !_snapEnabled;
    std::wostringstream oss;
    oss << L"Snap " << (_snapEnabled ? L"ON" : L"OFF")
        << L" (grid " << static_cast<int>(_gridSize) << L"px)";
    setStatus(oss.str());
}

void LayoutEditorSession::setViewZoom(float zoom,
                                      const math::FVector2& pivotWorld) {
    zoom = (std::max)(0.25f, (std::min)(4.0f, zoom));
    if (std::fabs(zoom - _viewZoom) < 0.0001f) {
        _viewZoom = zoom;
        return;
    }
    const math::FVector2 documentPivot = screenToDocument(pivotWorld);
    _viewZoom = zoom;
    if (_canvasViewport != nullptr) {
        const math::FRectangle viewportBounds =
            _canvasViewport->getWorldBounds();
        _viewPan.x = pivotWorld.x - viewportBounds.minX -
            (documentPivot.x - viewportBounds.minX) * _viewZoom;
        _viewPan.y = pivotWorld.y - viewportBounds.minY -
            (documentPivot.y - viewportBounds.minY) * _viewZoom;
    }
    applyViewportTransform();
    syncSelectionChrome();
    syncPreviewControls();
    std::wostringstream oss;
    oss << L"Zoom " << static_cast<int>(_viewZoom * 100.0f + 0.5f) << L"%";
    setStatus(oss.str());
}

void LayoutEditorSession::syncStyleCombo() {
    _styleIds.clear();
    std::vector<std::wstring> labels;
    labels.emplace_back(L"(none)");
    _styleIds.emplace_back("");
    if (auto* sheet = StyleManager::get().getStyleSheet()) {
        for (const auto& kv : sheet->getAllStylesForCompose()) {
            if (kv.first.size() >= 4 && kv.first.compare(0, 4, "__le") == 0) {
                continue;
            }
            _styleIds.push_back(kv.first);
            labels.push_back(utf8ToWide(kv.first));
        }
    }
    if (_propStyleCombo != nullptr) {
        _suppressStyleCombo = true;
        _propStyleCombo->setItems(labels);
        _suppressStyleCombo = false;
    }
}

void LayoutEditorSession::syncStyleInspector() {
    const bool visible = _selected != nullptr;
    setChromeVisible("section_style_quality", visible);
    setChromeVisible("row_style_preview_state", visible);
    setChromeVisible("row_style_preview", visible);
    setChromeVisible("style_source_status", visible);
    setChromeVisible("style_color_status", visible);
    setChromeVisible("btn_reset_style", visible);
    if (!visible) return;

    StyleSheet* sheet = StyleManager::get().getStyleSheet();
    const LayoutStyleInspection inspection =
        _styleInspectorModel.inspect(_selected, sheet);
    bool mixedStyle = false;
    for (Widget* widget : _selection) {
        if (widget != nullptr &&
            widget->getStyleId() != inspection.styleId) {
            mixedStyle = true;
            break;
        }
    }

    if (_stylePreviewStateCombo != nullptr) {
        const bool previous = _suppressProp;
        _suppressProp = true;
        _stylePreviewStateCombo->setSelectedIndex(
            static_cast<int>(_stylePreviewState));
        _suppressProp = previous;
    }
    if (_styleSourceStatus != nullptr) {
        _styleSourceStatus->setText(mixedStyle
            ? L"Mixed style IDs — choosing a style applies to all selected widgets"
            : _styleInspectorModel.sourceLabel(inspection));
        _styleSourceStatus->setStyleId(
            inspection.source == LayoutStyleSource::Missing
                ? "__le_warning" : "__le_muted");
    }

    const math::FVector4 color =
        _styleInspectorModel.backgroundForState(inspection,
                                                _stylePreviewState);
    if (_styleColorStatus != nullptr) {
        _styleColorStatus->setText(localizedText(
            "ui.editor.ui_designer.property.background", L"Background")
            + L"  " + formatColorHex(color));
    }
    if (_stylePreviewSwatch != nullptr && sheet != nullptr) {
        WidgetStyle previewStyle = StyleBuilder::makePanel();
        previewStyle.backgroundColor = color;
        previewStyle.borderColor = math::FVector4(
            (std::min)(1.0f, color.x + 0.18f),
            (std::min)(1.0f, color.y + 0.18f),
            (std::min)(1.0f, color.z + 0.18f), 1.0f);
        previewStyle.border.color = previewStyle.borderColor;
        previewStyle.border.width = 1.0f;
        sheet->setStyle("__le_style_preview_dynamic", previewStyle);
        _stylePreviewSwatch->setStyleId("__le_style_preview_dynamic");
        _stylePreviewSwatch->setBackgroundEnabled(true);
        _stylePreviewSwatch->setBorderEnabled(true);
    }
}

void LayoutEditorSession::refreshValidation() {
    std::vector<Widget*> authored;
    collectDocumentWidgets(_docRoot, authored);
    LayoutValidationContext context;
    context.textureCatalog = _textureResourceProvider
        ? &_textureCatalog : nullptr;
    context.styleSheet = StyleManager::get().getStyleSheet();
    context.animations = &_animationLibrary;
    context.interactionRegistry = _interactionContractsConfigured
        ? &_interactionRegistry : nullptr;
    _validationModel.run(authored, context);
    _interactionGraph.rebuild(authored, context.interactionRegistry);
    _validationDirty = false;

    std::vector<std::wstring> interactionLabels =
        _interactionGraph.displayLabels();
    if (interactionLabels != _interactionGraphLabels) {
        _interactionGraphLabels = std::move(interactionLabels);
        _suppressInteractionGraph = true;
        if (_interactionGraphList != nullptr) {
            _interactionGraphList->setItems(_interactionGraphLabels);
            _interactionGraphList->setSelectedIndex(-1);
        }
        _suppressInteractionGraph = false;
    }
    if (_interactionGraphStatus != nullptr) {
        const size_t edges = _interactionGraph.edges().size();
        if (edges == 0u) {
            _interactionGraphStatus->setText(localizedText(
                "ui.editor.ui_designer.status.no_event_bindings",
                L"No event bindings"));
            _interactionGraphStatus->setStyleId("__le_muted");
        } else if (!_interactionContractsConfigured) {
            _interactionGraphStatus->setText(
                std::to_wstring(edges) +
                L" bindings  \u00b7  host registry not connected");
            _interactionGraphStatus->setStyleId("__le_muted");
        } else {
            const size_t unresolved = _interactionGraph.unresolvedCount();
            _interactionGraphStatus->setText(
                std::to_wstring(_interactionGraph.resolvedCount()) +
                L" resolved  \u00b7  " + std::to_wstring(unresolved) +
                L" unresolved");
            _interactionGraphStatus->setStyleId(unresolved == 0u
                ? "__le_success" : "__le_warning");
        }
    }

    std::vector<std::wstring> labels = _validationModel.displayLabels();
    if (labels != _validationLabels) {
        _validationLabels = std::move(labels);
        _suppressValidation = true;
        if (_validationList != nullptr) {
            _validationList->setItems(_validationLabels);
            _validationList->setSelectedIndex(-1);
        }
        _suppressValidation = false;
    }

    if (_validationStatus != nullptr) {
        const size_t errors = _validationModel.errorCount();
        const size_t warnings = _validationModel.warningCount();
        if (errors == 0u && warnings == 0u) {
            _validationStatus->setText(localizedText(
                "ui.editor.ui_designer.status.no_issues",
                L"No authoring issues"));
            _validationStatus->setStyleId("__le_success");
        } else {
            _validationStatus->setText(
                std::to_wstring(errors) + L" errors  \u00b7  " +
                std::to_wstring(warnings) + L" warnings");
            _validationStatus->setStyleId(errors > 0u
                ? "__le_warning" : "__le_muted");
        }
    }
}

void LayoutEditorSession::syncTextAlignCombos() {
    if (_selected == nullptr) {
        return;
    }
    const auto alignIndex = [](const std::wstring& value,
                               const wchar_t* center,
                               const wchar_t* end) {
        if (value == L"\u2014") return -1;
        if (value == center) return 1;
        if (value == end) return 2;
        return 0;
    };
    const int hIdx = alignIndex(commonPropertyValue("hAlign"),
                                L"Center", L"Right");
    const int vIdx = alignIndex(commonPropertyValue("vAlign"),
                                L"Center", L"Bottom");
    const bool prev = _suppressProp;
    _suppressProp = true;
    if (_propTextHAlign != nullptr) {
        _propTextHAlign->setSelectedIndex(hIdx);
    }
    if (_propTextVAlign != nullptr) {
        _propTextVAlign->setSelectedIndex(vIdx);
    }
    _suppressProp = prev;
}

void LayoutEditorSession::ensureSchemaPropertyChrome() {
    auto* column = dynamic_cast<VBox*>(findChromeById("props_col"));
    if (column == nullptr) return;

    auto sectionAnchor = [this](PropertySection section) -> Widget* {
        switch (section) {
        case PropertySection::Identity:
            return findChromeById("section_transform");
        case PropertySection::Transform:
            return findChromeById("section_content");
        case PropertySection::Content:
            return findChromeById("section_texture_resources");
        case PropertySection::Appearance:
            return findChromeById("section_style_quality");
        case PropertySection::Interaction:
            return findChromeById("section_interaction_graph");
        case PropertySection::Layout:
            return findChromeById("section_validation");
        }
        return nullptr;
    };

    std::vector<const PropertyFieldSchema*> missing;
    for (const PropertyFieldSchema& schema : allPropertyFieldSchemas()) {
        if (schema.displayName != nullptr &&
            findChromeById(schema.rowId) == nullptr) {
            missing.push_back(&schema);
        }
    }
    if (missing.empty()) return;

    for (const PropertyFieldSchema* schema : missing) {
        Widget* insertBefore = sectionAnchor(schema->section);
        const int insertIndex = insertBefore != nullptr
            ? column->slotIndexOf(insertBefore) : -1;
        if (insertIndex < 0) continue;

        BoxBase* row = nullptr;
        if (schema->rowKind == PropertyRowKind::Vector4) {
            row = new VBox();
        } else {
            row = new HBox();
        }
        row->setId(schema->rowId);
        row->setSize({272.0f, schema->rowHeight});
        row->setSpacing(schema->rowKind == PropertyRowKind::Vector4
            ? 4.0f : 8.0f);
        row->setVisible(false);

        auto* label = new TextLabel();
        label->setId(schema->labelId);
        label->setStyleId("__le_label");
        label->setText(localizedText(
            std::string("ui.") + "editor.ui_designer.property_field."
                + schema->key,
            utf8ToWide(schema->displayName)));
        if (schema->rowKind == PropertyRowKind::Vector4) {
            label->setSize({272.0f, 24.0f});
            row->addWidget(label, 24.0f);

            auto* components = new LayoutVectorPropertyEditor(
                schema->componentControlIds, schema->componentLabels);
            components->setId(std::string(schema->rowId) + "_inputs");
            components->setSize({272.0f, 32.0f});
            row->addWidget(components, 32.0f);
        } else {
            label->setSize({86.0f, 30.0f});
            row->addWidget(label, 86.0f);

            Widget* control = nullptr;
            if (schema->editorKind == PropertyEditorKind::Color) {
                auto* editor = new LayoutColorPropertyEditor(
                    schema->controlId);
                editor->setId(std::string(schema->controlId) + "_editor");
                editor->setManager(_ui);
                control = editor;
            } else if (schema->editorKind == PropertyEditorKind::Resource) {
                auto* editor = new LayoutResourcePropertyEditor(
                    schema->controlId);
                editor->setId(std::string(schema->controlId) + "_editor");
                control = editor;
            } else if (schema->editorKind == PropertyEditorKind::Enum ||
                schema->editorKind == PropertyEditorKind::Boolean) {
                control = new ComboBox();
            } else {
                control = new TextInput();
            }
            if (schema->editorKind != PropertyEditorKind::Color &&
                schema->editorKind != PropertyEditorKind::Resource) {
                control->setId(schema->controlId);
                control->setStyleId("__le_input");
            }
            control->setSize({0.0f, 30.0f});
            row->addWidget(control, 0.0f);
        }
        column->insertWidget(insertIndex, row, schema->rowHeight);
    }
}

void LayoutEditorSession::bindSchemaPropertyFields() {
    _schemaPropertyInputs.clear();
    _schemaPropertyCombos.clear();
    for (const PropertyFieldSchema& schema : allPropertyFieldSchemas()) {
        if (!usesGenericSchemaBinding(schema.property)) {
            continue;
        }
        if (schema.editorKind == PropertyEditorKind::Enum ||
            schema.editorKind == PropertyEditorKind::Boolean) {
            auto* combo = dynamic_cast<ComboBox*>(
                findChromeById(schema.controlId));
            if (combo == nullptr) continue;
            std::vector<std::wstring> items;
            items.reserve(schema.enumOptions.size());
            for (const std::string& option : schema.enumOptions) {
                items.push_back(utf8ToWide(option));
            }
            combo->setItems(items);
            const std::string key = schema.key;
            combo->setOnSelectionChanged([this, key](int index) {
                if (_suppressProp || index < 0) return;
                const PropertyFieldSchema* current =
                    findPropertyFieldSchema(key);
                if (current == nullptr ||
                    index >= static_cast<int>(current->enumOptions.size())) {
                    return;
                }
                beginPropertyMutation(key);
                applyProperty(key, utf8ToWide(
                    current->enumOptions[static_cast<size_t>(index)]));
                endPropertyMutation();
            });
            _schemaPropertyCombos.emplace(key, combo);
            continue;
        }

        TextInput* input = nullptr;
        const bool numeric = schema.editorKind == PropertyEditorKind::Number ||
                             schema.editorKind == PropertyEditorKind::Integer;
        bindPropField(schema.controlId, schema.key, input, numeric);
        if (input != nullptr) {
            _schemaPropertyInputs.emplace(schema.key, input);
        }
        if (schema.editorKind == PropertyEditorKind::Color) {
            auto* editor = dynamic_cast<LayoutColorPropertyEditor*>(
                findChromeById(std::string(schema.controlId) + "_editor"));
            if (editor == nullptr) continue;
            const std::string key = schema.key;
            editor->setOnInteractionStarted([this, key]() {
                beginPropertyMutation(key, "Edit color");
            });
            editor->setOnColorChanged([this, key](const math::FVector4& color) {
                if (!propertyMutationOpen()) {
                    beginPropertyMutation(key, "Edit color");
                }
                applyProperty(key, ColorPicker::formatHexCode(color));
            });
            editor->setOnColorCommitted([this, key](const math::FVector4& color) {
                if (!propertyMutationOpen()) {
                    beginPropertyMutation(key, "Edit color");
                }
                applyProperty(key, ColorPicker::formatHexCode(color));
                endPropertyMutation();
            });
        }
    }
}

std::wstring LayoutEditorSession::schemaPropertyValue(
    const std::string& field) const {
    return schemaPropertyValueForWidget(_selected, field);
}

std::wstring LayoutEditorSession::schemaPropertyValueForWidget(
    Widget* widget, const std::string& field) const {
    if (widget == nullptr) return {};
    if (auto* slider = dynamic_cast<Slider*>(widget)) {
        if (field == "min") return formatFloat(slider->getMin());
        if (field == "max") return formatFloat(slider->getMax());
        if (field == "value") return formatFloat(slider->getValue());
    }
    if (auto* progress = dynamic_cast<ProgressBar*>(widget)) {
        if (field == "min") return formatFloat(progress->getMin());
        if (field == "max") return formatFloat(progress->getMax());
        if (field == "value") return formatFloat(progress->getValue());
    }
    if (auto* image = dynamic_cast<Image*>(widget)) {
        if (field == "imageTint") return formatColorHex(image->getColor());
        const math::FRectangle& uv = image->getUV();
        if (field == "uvMinX") return formatFloat(uv.minX);
        if (field == "uvMinY") return formatFloat(uv.minY);
        if (field == "uvMaxX") return formatFloat(uv.maxX);
        if (field == "uvMaxY") return formatFloat(uv.maxY);
    }
    if (auto* list = dynamic_cast<ListView*>(widget)) {
        if (field == "selectionMode") {
            return list->getSelectionMode() == ListView::SelectionMode::Extended
                ? L"Extended" : L"Single";
        }
        if (field == "itemHeight") return formatFloat(list->getItemHeight());
    }
    if (auto* tiles = dynamic_cast<TileView*>(widget)) {
        if (field == "selectionMode") {
            return tiles->getSelectionMode() == TileView::SelectionMode::Extended
                ? L"Extended" : L"Single";
        }
        if (field == "tileWidth") return formatFloat(tiles->getTileSize().x);
        if (field == "tileHeight") return formatFloat(tiles->getTileSize().y);
        if (field == "tileSpacing") return formatFloat(tiles->getTileSpacing());
    }
    if (auto* tree = dynamic_cast<TreeView*>(widget)) {
        if (field == "itemHeight") return formatFloat(tree->getItemHeight());
    }
    if (auto* scroll = dynamic_cast<ScrollView*>(widget)) {
        auto visibilityName = [](ScrollView::ScrollBarVisibility visibility) {
            switch (visibility) {
            case ScrollView::ScrollBarVisibility::Always: return L"Always";
            case ScrollView::ScrollBarVisibility::Hidden: return L"Hidden";
            case ScrollView::ScrollBarVisibility::Auto:
            default: return L"Auto";
            }
        };
        if (field == "verticalScrollBarVisibility") {
            return visibilityName(scroll->getVerticalScrollBarVisibility());
        }
        if (field == "horizontalScrollBarVisibility") {
            return visibilityName(scroll->getHorizontalScrollBarVisibility());
        }
    }
    if (auto* strip = dynamic_cast<TabStrip*>(widget)) {
        if (field == "overflowMode") {
            switch (strip->getOverflowMode()) {
            case TabStrip::OverflowMode::Compress: return L"Compress";
            case TabStrip::OverflowMode::Clip: return L"Clip";
            case TabStrip::OverflowMode::Scroll:
            default: return L"Scroll";
            }
        }
        if (field == "minTabWidth") return formatFloat(strip->getMinTabWidth());
    }
    if (auto* grid = dynamic_cast<GridPanel*>(widget)) {
        if (field == "gridRows") return std::to_wstring(grid->getRowCount());
        if (field == "gridColumns") {
            return std::to_wstring(grid->getColumnCount());
        }
        if (field == "gridSpacingX") {
            return formatFloat(grid->getHorizontalSpacing());
        }
        if (field == "gridSpacingY") {
            return formatFloat(grid->getVerticalSpacing());
        }
    }
    if (auto* rich = dynamic_cast<RichText*>(widget)) {
        if (field == "richWrapMode") {
            switch (rich->getWrapMode()) {
            case RichTextWrapMode::Word: return L"Word";
            case RichTextWrapMode::Character: return L"Character";
            case RichTextWrapMode::NoWrap:
            default: return L"NoWrap";
            }
        }
        if (field == "richOverflow") {
            return rich->getOverflow() == RichTextOverflow::Ellipsis
                ? L"Ellipsis" : L"Clip";
        }
        if (field == "lineHeight") return formatFloat(rich->getLineHeight());
        if (field == "maxLines") return std::to_wstring(rich->getMaxLines());
    }
    return {};
}

void LayoutEditorSession::syncSchemaPropertyFields() {
    for (const auto& [field, input] : _schemaPropertyInputs) {
        if (input != nullptr) input->setText(commonPropertyValue(field));
    }
    for (const auto& [field, combo] : _schemaPropertyCombos) {
        if (combo == nullptr) continue;
        const PropertyFieldSchema* schema = findPropertyFieldSchema(field);
        const std::wstring value = commonPropertyValue(field);
        int selected = -1;
        if (schema != nullptr) {
            for (size_t i = 0; i < schema->enumOptions.size(); ++i) {
                if (value == utf8ToWide(schema->enumOptions[i])) {
                    selected = static_cast<int>(i);
                    break;
                }
            }
        }
        combo->setSelectedIndex(selected);
    }
    for (const PropertyFieldSchema& schema : allPropertyFieldSchemas()) {
        if (schema.editorKind != PropertyEditorKind::Color) continue;
        auto* editor = dynamic_cast<LayoutColorPropertyEditor*>(
            findChromeById(std::string(schema.controlId) + "_editor"));
        if (editor == nullptr) continue;
        math::FVector4 color;
        if (ColorPicker::parseHexCode(commonPropertyValue(schema.key), color)) {
            editor->setColor(color);
        }
    }
}

void LayoutEditorSession::bindBoolCombo(ComboBox*& slot, const char* id,
                                        const char* field) {
    if (_ui == nullptr) {
        return;
    }
    slot = dynamic_cast<ComboBox*>(findChromeById(id));
    if (slot == nullptr) {
        return;
    }
    const AuthoringProperty property = field == std::string("checked")
        ? AuthoringProperty::Checked
        : (field == std::string("password")
            ? AuthoringProperty::Password : AuthoringProperty::ReadOnly);
    slot->setItems(propertyOptionLabels(property));
    const std::string fieldName = field;
    slot->setOnSelectionChanged([this, fieldName, property](int index) {
        if (_suppressProp || index < 0 || index > 1) {
            return;
        }
        const PropertyFieldSchema& schema = propertyFieldSchema(property);
        beginPropertyMutation(fieldName);
        applyProperty(fieldName, utf8ToWide(
            schema.enumOptions[static_cast<size_t>(index)]));
        endPropertyMutation();
    });
}

void LayoutEditorSession::bindGravityCombo() {
    if (_ui == nullptr) {
        return;
    }
    _propGravity = dynamic_cast<ComboBox*>(findChromeById("prop_gravity"));
    if (_propGravity == nullptr) {
        return;
    }
    _propGravity->setItems(
        propertyOptionLabels(AuthoringProperty::Gravity));
    _propGravity->setOnSelectionChanged([this](int index) {
        if (_suppressProp || index < 0 || index > 8) {
            return;
        }
        const PropertyFieldSchema& schema =
            propertyFieldSchema(AuthoringProperty::Gravity);
        beginPropertyMutation("gravity");
        applyProperty("gravity", utf8ToWide(
            schema.enumOptions[static_cast<size_t>(index)]));
        endPropertyMutation();
    });
}

void LayoutEditorSession::syncEnumCombos() {
    const auto booleanIndex = [this](const char* field) {
        const std::wstring value = commonPropertyValue(field);
        if (value == L"\u2014") return -1;
        return value == L"true" ? 1 : 0;
    };
    const int checkedIdx = booleanIndex("checked");
    const int passwordIdx = booleanIndex("password");
    const int readOnlyIdx = booleanIndex("readOnly");
    int gravityIdx = -1;
    const std::wstring gravity = commonPropertyValue("gravity");
    static const std::array<const wchar_t*, 9> gravityValues{{
        L"TopLeft", L"TopCenter", L"TopRight", L"CenterLeft", L"Center",
        L"CenterRight", L"BottomLeft", L"BottomCenter", L"BottomRight"}};
    for (size_t i = 0; i < gravityValues.size(); ++i) {
        if (gravity == gravityValues[i]) {
            gravityIdx = static_cast<int>(i);
            break;
        }
    }

    const bool prev = _suppressProp;
    _suppressProp = true;
    if (_propChecked != nullptr) {
        _propChecked->setSelectedIndex(checkedIdx);
    }
    if (_propPassword != nullptr) {
        _propPassword->setSelectedIndex(passwordIdx);
    }
    if (_propReadOnly != nullptr) {
        _propReadOnly->setSelectedIndex(readOnlyIdx);
    }
    if (_propGravity != nullptr) {
        _propGravity->setSelectedIndex(gravityIdx);
    }
    _suppressProp = prev;
}

void LayoutEditorSession::refreshWindowTitle() {
    if (!_titleUpdater) {
        return;
    }
    std::wstring title = L"AYUI Layout Editor";
    if (!_documentPath.empty()) {
        title += L" — ";
        title += utf8ToWide(_documentPath);
    }
    if (_dirty) {
        title += L" *";
    }
    _titleUpdater(title);
}

float LayoutEditorSession::snapValue(float v) const {
    if (!_snapEnabled || _gridSize < 1.0f) {
        return v;
    }
    return std::round(v / _gridSize) * _gridSize;
}

void LayoutEditorSession::snapWidgetPosition(Widget* w) {
    if (w == nullptr || !_snapEnabled ||
        structuredContentOwner(w) != nullptr) {
        return;
    }
    if (w->getParent() != nullptr &&
        (dynamic_cast<BoxBase*>(w->getParent()) != nullptr ||
         dynamic_cast<GridPanel*>(w->getParent()) != nullptr)) {
        return;
    }
    math::FVector2 pos = w->getPosition();
    pos.x = snapValue(pos.x);
    pos.y = snapValue(pos.y);
    w->setPosition(pos);
    refreshAnchorOffsets(w);
}

void LayoutEditorSession::snapSelectionPositions() {
    for (Widget* w : _selection) {
        snapWidgetPosition(w);
    }
}

void LayoutEditorSession::remintTreeIds(Widget* root) {
    if (root == nullptr) {
        return;
    }
    std::function<void(Widget*)> walk = [&](Widget* n) {
        if (n == nullptr || isEditorOverlay(n)) {
            return;
        }
        const std::string& id = n->getId();
        std::string prefix = "w";
        if (!id.empty()) {
            const size_t us = id.find_last_of('_');
            prefix = (us == std::string::npos) ? id : id.substr(0, us);
            if (prefix.empty()) {
                prefix = "w";
            }
        }
        n->setId(makeUniqueId(prefix));
        std::vector<Widget*> children;
        collectAuthoredChildren(n, children);
        for (Widget* c : children) {
            walk(c);
        }
    };
    walk(root);
}

void LayoutEditorSession::collectDocumentWidgets(
    Widget* node, std::vector<Widget*>& out) const {
    if (node == nullptr || isEditorOverlay(node)) {
        return;
    }
    out.push_back(node);
    std::vector<Widget*> children;
    collectAuthoredChildren(node, children);
    for (Widget* c : children) {
        collectDocumentWidgets(c, out);
    }
}

void LayoutEditorSession::ensureSelOutlines(size_t count) {
    if (_canvasHost == nullptr) {
        return;
    }
    if (auto* sheet = StyleManager::get().getStyleSheet()) {
        WidgetStyle outline{};
        outline.backgroundColor = math::FVector4(0.20f, 0.55f, 0.95f, 0.0f);
        outline.borderColor = math::FVector4(0.45f, 0.80f, 1.0f, 0.85f);
        outline.border.color = outline.borderColor;
        outline.border.width = 1.0f;
        sheet->setStyle("__le_sel_outline", outline);
    }
    while (_selOutlines.size() < count) {
        Panel* p = new Panel();
        p->setId(std::string("__le_sel_outline_") +
                 std::to_string(_selOutlines.size()));
        p->setStyleId("__le_sel_outline");
        p->setBorderEnabled(true);
        p->setBackgroundEnabled(true);
        p->setVisible(false);
        p->setLayoutPositionManaged(false);
        p->setLayoutSizeManaged(false);
        _canvasHost->addChild(p);
        _selOutlines.push_back(p);
    }
}

void LayoutEditorSession::updateMarqueeChrome(const math::FVector2& a,
                                              const math::FVector2& b) {
    ensureSelectionChrome();
    if (_canvasHost == nullptr) {
        return;
    }
    if (_marqueeBox == nullptr) {
        if (auto* sheet = StyleManager::get().getStyleSheet()) {
            WidgetStyle m{};
            m.backgroundColor = math::FVector4(0.25f, 0.55f, 0.95f, 0.12f);
            m.borderColor = math::FVector4(0.40f, 0.75f, 1.0f, 0.95f);
            m.border.color = m.borderColor;
            m.border.width = 1.0f;
            sheet->setStyle("__le_marquee", m);
        }
        _marqueeBox = new Panel();
        _marqueeBox->setId("__le_marquee");
        _marqueeBox->setStyleId("__le_marquee");
        _marqueeBox->setBorderEnabled(true);
        _marqueeBox->setBackgroundEnabled(true);
        _marqueeBox->setLayoutPositionManaged(false);
        _marqueeBox->setLayoutSizeManaged(false);
        _canvasHost->addChild(_marqueeBox);
    }
    const math::FRectangle host = _canvasHost->getWorldBounds();
    const float minX = (std::min)(a.x, b.x) - host.minX;
    const float minY = (std::min)(a.y, b.y) - host.minY;
    const float maxX = (std::max)(a.x, b.x) - host.minX;
    const float maxY = (std::max)(a.y, b.y) - host.minY;
    _marqueeBox->setPosition(math::FVector2(minX, minY));
    _marqueeBox->setSize(math::FVector2(maxX - minX, maxY - minY));
    _marqueeBox->setVisible(true);
}

void LayoutEditorSession::clearMarqueeChrome() {
    if (_marqueeBox != nullptr) {
        _marqueeBox->setVisible(false);
    }
}

void LayoutEditorSession::commitMarquee(const math::FVector2& a,
                                        const math::FVector2& b,
                                        bool additive) {
    if (std::fabs(a.x - b.x) < 3.0f && std::fabs(a.y - b.y) < 3.0f) {
        return;
    }
    const math::FVector2 documentA = screenToDocument(a);
    const math::FVector2 documentB = screenToDocument(b);
    math::FRectangle box(
        (std::min)(documentA.x, documentB.x),
        (std::min)(documentA.y, documentB.y),
        (std::max)(documentA.x, documentB.x),
        (std::max)(documentA.y, documentB.y));
    std::vector<Widget*> hits;
    std::vector<Widget*> all;
    collectDocumentWidgets(_docRoot, all);
    for (Widget* w : all) {
        if (w == nullptr || w == _docRoot || isEditorOverlay(w) ||
            !isUnderCanvas(w)) {
            continue;
        }
        const math::FRectangle wb = w->getWorldBounds();
        const bool overlap =
            wb.minX <= box.maxX && wb.maxX >= box.minX &&
            wb.minY <= box.maxY && wb.maxY >= box.minY;
        if (overlap) {
            hits.push_back(w);
        }
    }
    if (!additive) {
        _selection.clear();
    }
    for (Widget* w : hits) {
        if (!selectionContains(w)) {
            _selection.push_back(w);
        }
    }
    normalizeSelectionNesting();
    _selected = _selection.empty() ? nullptr : _selection.back();
    syncPropertyStrip();
    syncHierarchySelection();
    syncSelectionChrome();
    std::wostringstream oss;
    oss << L"Marquee selected " << _selection.size()
        << L" — drag any selected widget to move together; Align/Dist on toolbar";
    setStatus(oss.str());
}

} // namespace ayt::ui
