#include "LayoutEditorSession.h"

#include "AYUI/Box.h"
#include "AYUI/Button.h"
#include "AYUI/CheckBox.h"
#include "AYUI/Clipboard.h"
#include "AYUI/ComboBox.h"
#include "AYUI/InteractiveWidget.h"
#include "AYUI/LayoutLoader.h"
#include "AYUI/Panel.h"
#include "AYUI/Style.h"
#include "AYUI/TextInput.h"
#include "AYUI/TextLabel.h"
#include "AYUI/ListView.h"
#include "AYUI/ScrollBar.h"
#include "AYUI/Widget.h"
#include "AYUI/WidgetFactory.h"
#include "AYUI/UIKeyCode.h"

#include <algorithm>
#include <cmath>
#include <codecvt>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <locale>
#include <sstream>

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

std::string readFileToString(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return {};
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

std::string typePrefix(const std::string& typeName) {
    if (typeName == "TextLabel") return "lbl";
    if (typeName == "Button") return "btn";
    if (typeName == "Panel") return "panel";
    if (typeName == "TextInput") return "input";
    if (typeName == "CheckBox") return "chk";
    if (typeName == "Slider") return "slider";
    if (typeName == "ScrollView") return "scroll";
    if (typeName == "VBox") return "vbox";
    if (typeName == "HBox") return "hbox";
    if (typeName == "ProgressBar") return "prog";
    return "w";
}

math::FVector2 defaultSizeForType(const std::string& typeName) {
    if (typeName == "Panel" || typeName == "ScrollView") {
        return math::FVector2(200.0f, 120.0f);
    }
    if (typeName == "VBox" || typeName == "HBox") {
        return math::FVector2(220.0f, 160.0f);
    }
    if (typeName == "TextArea") {
        return math::FVector2(200.0f, 80.0f);
    }
    if (typeName == "Slider") {
        return math::FVector2(160.0f, 24.0f);
    }
    return math::FVector2(120.0f, 28.0f);
}

} // namespace

LayoutEditorSession::~LayoutEditorSession() {
    detach();
}

bool LayoutEditorSession::attach(UIManager& ui) {
    detach();
    _ui = &ui;
    _ui->disableLayoutHotReload();

    _canvasHost = ui.findById("canvas_host");
    if (_canvasHost == nullptr) {
        std::fprintf(stderr,
            "[LayoutEditorSession] canvas_host not found — load layout_editor.ui.json first\n");
        _ui = nullptr;
        return false;
    }

    _docLoader.setWidgetFactory(&WidgetFactory::get());
    wireChrome();

    if (_canvasHost->getChildren().empty()) {
        ensureEmptyDocument();
    } else {
        // First child is the document; selection chrome is appended after.
        _docRoot = nullptr;
        for (Widget* c : _canvasHost->getChildren()) {
            if (!isEditorOverlay(c)) {
                _docRoot = c;
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
    if (_ui != nullptr) {
        _ui->invalidateLayout();
        _ui->layout();
    }
    refreshHierarchy();
    syncPropertyStrip();
    syncSelectionChrome();
    setStatus(L"Ready — Ctrl multi-select | C/V/D | arrows | G snap | Ctrl+wheel zoom | MMB pan | empty-drag marquee");
    refreshWindowTitle();
    return true;
}

void LayoutEditorSession::detach() {
    destroySelectionChrome();
    destroyHierDropChrome();
    destroyPaletteGhost();
    _dragMode = DragMode::None;
    _dragTarget = nullptr;
    _toolDrag = ToolDrag::None;
    _spaceDown = false;
    _paletteType.clear();
    _hierDragWidget = nullptr;
    _mutationOpen = false;
    _deferred = DeferredAction::None;
    _consumeNextPointerUp = false;
    _ui = nullptr;
    _canvasHost = nullptr;
    _docRoot = nullptr;
    _selected = nullptr;
    _selection.clear();
    _hierarchy = nullptr;
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
    _clipboardItems.clear();
    _styleIds.clear();
    _viewZoom = 1.0f;
    _snapEnabled = true;
    _gridSize = 8.0f;
    _propId = _propX = _propY = _propW = _propH = _propText = nullptr;
    _propSpacing = nullptr;
    _propPadL = _propPadT = _propPadR = _propPadB = nullptr;
    _status = nullptr;
    _hierarchyIndex.clear();
    _undoStack.clear();
    _redoStack.clear();
    _documentPath.clear();
    _dirty = false;
}

void LayoutEditorSession::pumpDeferred() {
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

void LayoutEditorSession::bindPropField(const char* id, const char* field,
                                        TextInput*& slot, bool numericScrub) {
    slot = dynamic_cast<TextInput*>(_ui->findById(id));
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
        endMutation();
    });
    ti->setOnTextChanged({});

    if (numericScrub) {
        ti->setNumericScrubEnabled(true);
        ti->setOnNumericScrub([this, fieldName, ti](float v) {
            if (_suppressProp) {
                return;
            }
            beginMutation();
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
    beginMutation();
    applyProperty(field, slot->getText());
}

void LayoutEditorSession::wireChrome() {
    if (_ui == nullptr) {
        return;
    }

    auto bindBtn = [this](const char* id, std::function<void()> fn) {
        if (auto* b = dynamic_cast<Button*>(_ui->findById(id))) {
            b->setOnClicked(std::move(fn));
        }
    };

    bindBtn("btn_open", [this]() { _deferred = DeferredAction::Open; });
    bindBtn("btn_save", [this]() { _deferred = DeferredAction::Save; });
    bindBtn("btn_save_as", [this]() { _deferred = DeferredAction::SaveAs; });
    bindBtn("btn_undo", [this]() { undo(); });
    bindBtn("btn_redo", [this]() { redo(); });
    bindBtn("btn_delete", [this]() { deleteSelected(); });

    // Palette buttons are driven by onPointer* (click + drag-create).
    // Keep click handlers as a fallback if pointer routing misses them.
    bindBtn("btn_add_button", [this]() { addWidget("Button"); });
    bindBtn("btn_add_label", [this]() { addWidget("TextLabel"); });
    bindBtn("btn_add_panel", [this]() { addWidget("Panel"); });
    bindBtn("btn_add_input", [this]() { addWidget("TextInput"); });
    bindBtn("btn_add_checkbox", [this]() { addWidget("CheckBox"); });
    bindBtn("btn_add_slider", [this]() { addWidget("Slider"); });
    bindBtn("btn_add_scroll", [this]() { addWidget("ScrollView"); });
    bindBtn("btn_add_vbox", [this]() { addWidget("VBox"); });
    bindBtn("btn_add_hbox", [this]() { addWidget("HBox"); });

    bindBtn("btn_align_left", [this]() { alignSelection(AlignMode::Left); });
    bindBtn("btn_align_hcenter", [this]() { alignSelection(AlignMode::HCenter); });
    bindBtn("btn_align_right", [this]() { alignSelection(AlignMode::Right); });
    bindBtn("btn_align_top", [this]() { alignSelection(AlignMode::Top); });
    bindBtn("btn_align_vcenter", [this]() { alignSelection(AlignMode::VCenter); });
    bindBtn("btn_align_bottom", [this]() { alignSelection(AlignMode::Bottom); });
    bindBtn("btn_dist_h", [this]() { alignSelection(AlignMode::DistributeH); });
    bindBtn("btn_dist_v", [this]() { alignSelection(AlignMode::DistributeV); });
    bindBtn("btn_move_up", [this]() { reorderSelected(-1); });
    bindBtn("btn_move_down", [this]() { reorderSelected(1); });
    bindBtn("btn_snap", [this]() { toggleSnap(); });

    _hierarchyCol = _ui->findById("hierarchy_col");
    _chromeRoot = _ui->findById("layout_editor_root");
    _hierarchy = dynamic_cast<ListView*>(_ui->findById("list_hierarchy"));
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
    bindPropField("prop_text", "text", _propText, false);
    bindPropField("prop_spacing", "spacing", _propSpacing, true);
    bindPropField("prop_pad_l", "padL", _propPadL, true);
    bindPropField("prop_pad_t", "padT", _propPadT, true);
    bindPropField("prop_pad_r", "padR", _propPadR, true);
    bindPropField("prop_pad_b", "padB", _propPadB, true);

    _propStyleCombo = dynamic_cast<ComboBox*>(_ui->findById("prop_style_combo"));
    if (_propStyleCombo != nullptr) {
        _propStyleCombo->setOnSelectionChanged([this](int index) {
            if (_suppressStyleCombo || _suppressProp || index < 0 ||
                index >= static_cast<int>(_styleIds.size())) {
                return;
            }
            beginMutation();
            applyProperty("style", utf8ToWide(_styleIds[static_cast<size_t>(index)]));
        });
        syncStyleCombo();
    }

    _propTextHAlign = dynamic_cast<ComboBox*>(_ui->findById("prop_text_halign"));
    _propTextVAlign = dynamic_cast<ComboBox*>(_ui->findById("prop_text_valign"));
    if (_propTextHAlign != nullptr) {
        _propTextHAlign->setItems({L"Left", L"Center", L"Right"});
        _propTextHAlign->setOnSelectionChanged([this](int index) {
            if (_suppressProp || index < 0 || index > 2) {
                return;
            }
            static const char* kH[] = {"Left", "Center", "Right"};
            beginMutation();
            applyProperty("hAlign", utf8ToWide(kH[index]));
        });
    }
    if (_propTextVAlign != nullptr) {
        _propTextVAlign->setItems({L"Top", L"Center", L"Bottom"});
        _propTextVAlign->setOnSelectionChanged([this](int index) {
            if (_suppressProp || index < 0 || index > 2) {
                return;
            }
            static const char* kV[] = {"Top", "Center", "Bottom"};
            beginMutation();
            applyProperty("vAlign", utf8ToWide(kV[index]));
        });
    }

    bindBoolCombo(_propChecked, "prop_checked", "checked");
    bindBoolCombo(_propPassword, "prop_password", "password");
    bindBoolCombo(_propReadOnly, "prop_readonly", "readOnly");
    bindGravityCombo();

    _status = dynamic_cast<TextLabel*>(_ui->findById("lbl_status"));
}

void LayoutEditorSession::clearDocument() {
    if (_canvasHost == nullptr) {
        return;
    }
    cancelToolDrag();
    _dragMode = DragMode::None;
    _dragTarget = nullptr;
    // Tear down selection chrome first so destroyWidgetTree doesn't
    // double-free panels we still hold pointers to.
    destroySelectionChrome();
    std::vector<Widget*> kids = _canvasHost->getChildren();
    for (Widget* child : kids) {
        _canvasHost->removeChild(child);
        destroyWidgetTree(child);
    }
    _docRoot = nullptr;
    _selected = nullptr;
    _selection.clear();
}

void LayoutEditorSession::setDocumentRoot(Widget* root) {
    if (_canvasHost == nullptr || root == nullptr) {
        return;
    }
    clearDocument();
    _canvasHost->addChild(root);
    _docRoot = root;
    freezeDocumentInteraction(_docRoot);
    ensureSelectionChrome();
}

void LayoutEditorSession::freezeDocumentInteraction(Widget* root) {
    if (root == nullptr) {
        return;
    }
    std::function<void(Widget*)> walk = [&](Widget* n) {
        if (auto* iw = dynamic_cast<InteractiveWidget*>(n)) {
            iw->setEnabled(false);
            iw->setOnClicked({});
        }
        if (auto* ti = dynamic_cast<TextInput*>(n)) {
            ti->setReadOnly(true);
        }
        // Children of VBox/HBox must stay layout-managed or they stack
        // at (0,0) and can be dragged out of the box. Free-move only
        // when the parent is not a box layout.
        const bool underBox =
            n->getParent() != nullptr &&
            dynamic_cast<BoxBase*>(n->getParent()) != nullptr;
        n->setLayoutPositionManaged(underBox);
        for (Widget* c : n->getChildren()) {
            walk(c);
        }
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
    setDocumentRoot(panel);
    _documentPath.clear();
    markDirty(false);
}

bool LayoutEditorSession::open(const std::string& path) {
    if (_ui == nullptr || path.empty()) {
        return false;
    }
    const std::string json = readFileToString(path);
    if (json.empty()) {
        setStatus(utf8ToWide("Open failed: " + path));
        return false;
    }
    Widget* loaded = _docLoader.loadFromString(json);
    if (loaded == nullptr) {
        setStatus(utf8ToWide("Open failed: " + path));
        return false;
    }
    _undoStack.clear();
    _redoStack.clear();
    setDocumentRoot(loaded);
    _documentPath = path;
    markDirty(false);
    if (_ui != nullptr) {
        _ui->invalidateLayout();
        _ui->layout();
    }
    refreshHierarchy();
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
    try {
        UILayoutLoader saver;
        if (!saver.saveLayout(path, _docRoot, true)) {
            setStatus(utf8ToWide("Save failed: " + path));
            return false;
        }
    } catch (const std::exception& ex) {
        setStatus(utf8ToWide(std::string("Save aborted: ") + ex.what()));
        return false;
    } catch (...) {
        setStatus(L"Save aborted");
        return false;
    }
    _documentPath = path;
    markDirty(false);
    setStatus(utf8ToWide("Saved " + path));
    return true;
}

bool LayoutEditorSession::selectionContains(Widget* widget) const {
    return std::find(_selection.begin(), _selection.end(), widget) !=
           _selection.end();
}

void LayoutEditorSession::pruneSelection() {
    _selection.erase(
        std::remove_if(_selection.begin(), _selection.end(),
                       [this](Widget* w) {
                           return w == nullptr || !isUnderCanvas(w) ||
                                  w == _canvasHost;
                       }),
        _selection.end());
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
        _selection.clear();
        if (widget != nullptr) {
            _selection.push_back(widget);
        }
        _selected = widget;
    } else {
        if (selectionContains(widget)) {
            _selection.erase(
                std::remove(_selection.begin(), _selection.end(), widget),
                _selection.end());
            _selected = _selection.empty() ? nullptr : _selection.back();
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
            _selection.push_back(widget);
            _selected = widget;
        }
    }

    normalizeSelectionNesting();
    syncPropertyStrip();
    syncHierarchySelection();
    syncSelectionChrome();
    updateContainerHint();
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
        return;
    }

    const math::FRectangle host = _canvasHost->getWorldBounds();
    constexpr float kOutset = 3.0f;

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
        const math::FRectangle wb = _selection[i]->getWorldBounds();
        outline->setPosition(math::FVector2(
            wb.minX - host.minX - kOutset,
            wb.minY - host.minY - kOutset));
        outline->setSize(math::FVector2(
            wb.width() + kOutset * 2.0f,
            wb.height() + kOutset * 2.0f));
        outline->setVisible(true);
    }

    // Keep legacy selBox as primary highlight (slightly thicker).
    if (_selected != nullptr && isUnderCanvas(_selected)) {
        const math::FRectangle wb = _selected->getWorldBounds();
        const float px = wb.minX - host.minX;
        const float py = wb.minY - host.minY;
        const float pw = wb.width();
        const float ph = wb.height();
        _selBox->setPosition(math::FVector2(px - kOutset - 1.0f,
                                            py - kOutset - 1.0f));
        _selBox->setSize(math::FVector2(pw + (kOutset + 1.0f) * 2.0f,
                                        ph + (kOutset + 1.0f) * 2.0f));
        _selBox->setVisible(true);
        // No resize handles on document root — full-canvas handles fight
        // selection chrome and are easy to mis-hit after Shift-select.
        if (_selected != _docRoot) {
            placeHandle(_handles[0], px, py);
            placeHandle(_handles[1], px + pw * 0.5f, py);
            placeHandle(_handles[2], px + pw, py);
            placeHandle(_handles[3], px, py + ph * 0.5f);
            placeHandle(_handles[4], px + pw, py + ph * 0.5f);
            placeHandle(_handles[5], px, py + ph);
            placeHandle(_handles[6], px + pw * 0.5f, py + ph);
            placeHandle(_handles[7], px + pw, py + ph);
        } else {
            for (Panel* h : _handles) {
                if (h != nullptr) {
                    h->setVisible(false);
                }
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
}

void LayoutEditorSession::refreshHierarchy() {
    _hierarchyIndex.clear();
    std::vector<std::wstring> labels;
    if (_docRoot != nullptr) {
        std::function<void(Widget*, int)> walk = [&](Widget* node, int depth) {
            if (node == nullptr || isEditorOverlay(node)) {
                return;
            }
            std::wstring label(static_cast<size_t>(depth) * 2, L' ');
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
            _hierarchyIndex.push_back(node);
            for (Widget* c : node->getChildren()) {
                walk(c, depth + 1);
            }
        };
        walk(_docRoot, 0);
    }

    if (_hierarchy == nullptr) {
        setStatus(L"DBG: refreshHierarchy skipped — no ListView");
        return;
    }

    _suppressHierarchy = true;
    _hierarchy->setItems(labels);
    _hierarchy->performLayout();
    _suppressHierarchy = false;

    pruneSelection();
    syncHierarchySelection();
    syncSelectionChrome();
    updateContainerHint();

    {
        std::wostringstream oss;
        oss << L"Hierarchy: " << labels.size() << L" nodes, listH="
            << _hierarchy->getSize().y << L" listW=" << _hierarchy->getSize().x;
        setStatus(oss.str());
        std::fprintf(stderr, "[LayoutEditorSession] hierarchy nodes=%zu size=%.0fx%.0f\n",
            labels.size(), _hierarchy->getSize().x, _hierarchy->getSize().y);
    }
}

void LayoutEditorSession::updateContainerHint() {
    if (_selected == nullptr) {
        return;
    }
    if (dynamic_cast<CompoundWidget*>(_selected) != nullptr) {
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

    pushUndo();
    placeNewWidget(created, parent, -1, nullptr);
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
    for (Widget* w : _selection) {
        if (w != nullptr && w != _docRoot) {
            doomed.push_back(w);
        }
    }
    if (doomed.empty()) {
        setStatus(L"Cannot delete document root");
        return;
    }

    std::sort(doomed.begin(), doomed.end(), [](Widget* a, Widget* b) {
        int da = 0, db = 0;
        for (Widget* p = a; p; p = p->getParent()) ++da;
        for (Widget* p = b; p; p = p->getParent()) ++db;
        return da > db;
    });

    pushUndo();
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
    setStatus(L"Deleted");
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
        const std::string newId = wideToUtf8(value);
        if (newId.empty()) {
            return;
        }
        _selected->setId(newId);
        markDirty(true);
        refreshHierarchy();
        select(_selected, false);
        return;
    }

    if (field == "style") {
        _selected->setStyleId(wideToUtf8(value));
        markDirty(true);
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
        setStatus(L"checked: only CheckBox");
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
        setStatus(L"readOnly: only TextInput");
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

    float f = 0.0f;
    if (field == "x" || field == "y" || field == "w" || field == "h" ||
        field == "spacing" || field == "padL" || field == "padT" ||
        field == "padR" || field == "padB") {
        if (!parseFloat(value, f)) {
            return;
        }
    }

    if (field == "x" || field == "y" || field == "w" || field == "h") {
        math::FVector2 pos = _selected->getPosition();
        math::FVector2 size = _selected->getSize();
        if (field == "x") {
            pos.x = f;
            _selected->setPosition(pos);
        } else if (field == "y") {
            pos.y = f;
            _selected->setPosition(pos);
        } else if (field == "w") {
            size.x = (std::max)(kMinWidgetSize, f);
            _selected->setSize(size);
        } else if (field == "h") {
            size.y = (std::max)(kMinWidgetSize, f);
            _selected->setSize(size);
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

LayoutEditorSession::Snapshot LayoutEditorSession::captureSnapshot() const {
    Snapshot snap;
    if (_docRoot != nullptr) {
        UILayoutLoader saver;
        saver.saveLayoutToString(_docRoot, snap.json, false);
    }
    for (Widget* w : _selection) {
        if (w != nullptr && !w->getId().empty()) {
            snap.selectedIds.push_back(w->getId());
        }
    }
    if (_selected != nullptr) {
        snap.primaryId = _selected->getId();
    }
    return snap;
}

void LayoutEditorSession::restoreSnapshot(const Snapshot& snap) {
    if (snap.json.empty() || _ui == nullptr) {
        return;
    }
    Widget* loaded = _docLoader.loadFromString(snap.json);
    if (loaded == nullptr) {
        setStatus(L"Undo/Redo restore failed");
        return;
    }
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
    markDirty(true);
    _ui->invalidateLayout();
    _ui->layout();
}

void LayoutEditorSession::pushUndo() {
    if (_docRoot == nullptr) {
        return;
    }
    _undoStack.push_back(captureSnapshot());
    if (_undoStack.size() > kMaxUndo) {
        _undoStack.erase(_undoStack.begin());
    }
    _redoStack.clear();
}

void LayoutEditorSession::beginMutation() {
    if (!_mutationOpen) {
        pushUndo();
        _mutationOpen = true;
    }
}

void LayoutEditorSession::endMutation() {
    _mutationOpen = false;
}

void LayoutEditorSession::undo() {
    if (_undoStack.empty() || _docRoot == nullptr) {
        setStatus(L"Nothing to undo");
        return;
    }
    _redoStack.push_back(captureSnapshot());
    Snapshot snap = _undoStack.back();
    _undoStack.pop_back();
    _mutationOpen = false;
    _dragMode = DragMode::None;
    restoreSnapshot(snap);
    setStatus(L"Undo");
}

void LayoutEditorSession::redo() {
    if (_redoStack.empty() || _docRoot == nullptr) {
        setStatus(L"Nothing to redo");
        return;
    }
    _undoStack.push_back(captureSnapshot());
    Snapshot snap = _redoStack.back();
    _redoStack.pop_back();
    _mutationOpen = false;
    _dragMode = DragMode::None;
    restoreSnapshot(snap);
    setStatus(L"Redo");
}

void LayoutEditorSession::alignSelection(AlignMode mode) {
    if (_selection.size() < 2) {
        setStatus(L"Align needs Ctrl/Shift multi-select (2+ widgets)");
        return;
    }
    pushUndo();

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
            _undoStack.pop_back();
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
            _undoStack.pop_back();
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

    endMutation();
    markDirty(true);
    syncPropertyStrip();
    if (_ui != nullptr) {
        _ui->invalidateLayout();
    }
    setStatus(doneMsg);
}

void LayoutEditorSession::reorderSelected(int delta) {
    if (_selected == nullptr || _selected == _docRoot || delta == 0) {
        return;
    }
    Widget* parent = _selected->getParent();
    if (parent == nullptr || parent == _canvasHost) {
        return;
    }

    pushUndo();
    bool ok = false;
    if (auto* box = dynamic_cast<BoxBase*>(parent)) {
        const int cur = box->slotIndexOf(_selected);
        const int next = cur + delta;
        if (cur >= 0 && next >= 0 && box->slotAt(next) != nullptr) {
            ok = box->moveSlotToIndex(_selected, static_cast<size_t>(next));
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
        if (!_undoStack.empty()) {
            _undoStack.pop_back();
        }
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
    const math::FRectangle b = widget->getWorldBounds();
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
}

Widget* LayoutEditorSession::pickDocumentWidget(
    const math::FVector2& worldPos) const {
    if (_docRoot == nullptr || !isCanvasHit(worldPos)) {
        return nullptr;
    }
    // Deepest-first walk of the document only (ignores selection chrome).
    std::function<Widget*(Widget*)> walk = [&](Widget* n) -> Widget* {
        if (n == nullptr || !n->isVisible()) {
            return nullptr;
        }
        const auto& kids = n->getChildren();
        for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
            if (Widget* hit = walk(*it)) {
                return hit;
            }
        }
        if (n->getWorldBounds().contains(worldPos)) {
            return n;
        }
        return nullptr;
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
        _selOutlines.clear();
        _marqueeBox = nullptr;
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
    for (Panel*& o : _selOutlines) {
        destroyOne(o);
    }
    _selOutlines.clear();
    destroyOne(_marqueeBox);
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
        box.backgroundColor = math::FVector4(0.20f, 0.55f, 0.95f, 0.12f);
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

UiCursorHint LayoutEditorSession::canvasCursorHint(
    const math::FVector2& worldPos) const {
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
            return UiCursorHint::Move;
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
    if (pickDocumentWidget(worldPos) != nullptr) {
        return UiCursorHint::Move;
    }
    return UiCursorHint::Default;
}

bool LayoutEditorSession::onPointerDown(const math::FVector2& worldPos,
                                        int button) {
    if (_toolDrag != ToolDrag::None || _dragMode != DragMode::None) {
        if (_dragMode == DragMode::Marquee) {
            clearMarqueeChrome();
        }
        cancelToolDrag();
        _dragMode = DragMode::None;
        _dragTarget = nullptr;
    }

    // Middle mouse or Space+LMB → pan document root.
    if (button == 2 || (button == 0 && modifiersSpace() && isCanvasHit(worldPos))) {
        if (_docRoot != nullptr) {
            _dragMode = DragMode::Pan;
            _dragLastMouse = worldPos;
            _docRoot->setLayoutPositionManaged(false);
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
            if (!additive && node != nullptr && node != _docRoot) {
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

    auto isBoxChild = [](Widget* w) -> bool {
        return w != nullptr && w->getParent() != nullptr &&
               dynamic_cast<BoxBase*>(w->getParent()) != nullptr;
    };

    if (_selected != nullptr && _selected != _docRoot &&
        !isBoxChild(_selected) && !modifiersAdditive()) {
        const DragMode handle = hitTestResizeHandle(_selected, worldPos);
        if (handle != DragMode::None && handle != DragMode::Move) {
            beginMutation();
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

    if (hit == _docRoot || (_selected == _docRoot && _selection.size() == 1)) {
        setStatus(L"Document root selected");
        _dragMode = DragMode::None;
        _dragTarget = nullptr;
        _consumeNextPointerUp = true;
        return true;
    }

    auto canFreeMove = [&isBoxChild](Widget* w) -> bool {
        return w != nullptr && !isBoxChild(w);
    };

    int movable = 0;
    for (Widget* w : _selection) {
        if (w != nullptr && w != _docRoot && canFreeMove(w)) {
            ++movable;
        }
    }
    if (movable == 0) {
        if (isBoxChild(hit)) {
            setStatus(L"Box child — drag in Hierarchy to reorder (canvas drag disabled)");
        } else {
            setStatus(L"Selection has no free-position widgets to drag");
        }
        _dragMode = DragMode::None;
        _dragTarget = nullptr;
        _consumeNextPointerUp = true;
        return true;
    }

    beginMutation();
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
        if (_docRoot != nullptr && (delta.x != 0.0f || delta.y != 0.0f)) {
            math::FVector2 pos = _docRoot->getPosition();
            pos.x += delta.x;
            pos.y += delta.y;
            _docRoot->setPosition(pos);
            syncSelectionChrome();
        }
        return true;
    }

    if (delta.x == 0.0f && delta.y == 0.0f) {
        return true;
    }

    if (_dragMode == DragMode::Move) {
        if (!_selection.empty()) {
            for (Widget* w : _selection) {
                if (w == nullptr || w == _docRoot) {
                    continue;
                }
                if (w->getParent() != nullptr &&
                    dynamic_cast<BoxBase*>(w->getParent()) != nullptr) {
                    continue;
                }
                math::FVector2 pos = w->getPosition();
                pos.x += delta.x;
                pos.y += delta.y;
                w->setPosition(pos);
            }
        } else if (_dragTarget != nullptr && _dragTarget != _docRoot) {
            math::FVector2 pos = _dragTarget->getPosition();
            pos.x += delta.x;
            pos.y += delta.y;
            _dragTarget->setPosition(pos);
        }
    } else if (_dragTarget != nullptr) {
        applyResizeDelta(_dragTarget, _dragMode, delta);
    }

    markDirty(true);
    syncPropertyStrip();
    syncSelectionChrome();
    return true;
}

bool LayoutEditorSession::onPointerUp(const math::FVector2& worldPos,
                                      int /*button*/) {
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
    if (Widget* w = _ui->findById(id)) {
        w->setVisible(visible);
    }
}

void LayoutEditorSession::updatePropPanelVisibility() {
    const bool hasSel = _selected != nullptr;
    const bool isLabel = dynamic_cast<TextLabel*>(_selected) != nullptr;
    const bool isButton = dynamic_cast<Button*>(_selected) != nullptr;
    const bool isInput = dynamic_cast<TextInput*>(_selected) != nullptr;
    const bool isCheck = dynamic_cast<CheckBox*>(_selected) != nullptr;
    const bool isBox = dynamic_cast<BoxBase*>(_selected) != nullptr;
    const bool hasText = isLabel || isButton || isInput || isCheck;
    // Free position: hide x/y for box-managed children (layout owns them).
    bool showPos = hasSel;
    if (hasSel && _selected->getParent() != nullptr &&
        dynamic_cast<BoxBase*>(_selected->getParent()) != nullptr) {
        showPos = false;
    }

    // Never leave focus on a prop control we are about to hide.
    if (_ui != nullptr) {
        if (Widget* focused = _ui->getFocusedWidget()) {
            Widget* propsCol = _ui->findById("props_col");
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

    auto row = [this](const char* lbl, const char* ctrl, bool on) {
        setChromeVisible(lbl, on);
        setChromeVisible(ctrl, on);
    };

    row("lbl_prop_id", "prop_id", hasSel);
    row("lbl_prop_x", "prop_x", showPos);
    row("lbl_prop_y", "prop_y", showPos);
    row("lbl_prop_w", "prop_w", hasSel);
    row("lbl_prop_h", "prop_h", hasSel);
    row("lbl_prop_text", "prop_text", hasText);
    row("lbl_prop_text_halign", "prop_text_halign", isLabel || isInput);
    row("lbl_prop_text_valign", "prop_text_valign", isLabel);
    row("lbl_prop_style", "prop_style_combo", hasSel);
    row("lbl_prop_checked", "prop_checked", isCheck);
    row("lbl_prop_password", "prop_password", isInput);
    row("lbl_prop_readonly", "prop_readonly", isInput);
    row("lbl_prop_gravity", "prop_gravity", isBox);
    row("lbl_prop_spacing", "prop_spacing", isBox);
    setChromeVisible("lbl_prop_pad", isBox);
    setChromeVisible("prop_pad_l", isBox);
    setChromeVisible("prop_pad_t", isBox);
    setChromeVisible("prop_pad_r", isBox);
    setChromeVisible("prop_pad_b", isBox);

    if (auto* title = dynamic_cast<TextLabel*>(_ui != nullptr
            ? _ui->findById("lbl_props") : nullptr)) {
        if (!hasSel) {
            title->setText(L"Properties");
        } else {
            std::wstring t = L"Properties — ";
            if (isLabel) t += L"TextLabel";
            else if (isButton) t += L"Button";
            else if (isInput) t += L"TextInput";
            else if (isCheck) t += L"CheckBox";
            else if (dynamic_cast<VBox*>(_selected) != nullptr) t += L"VBox";
            else if (dynamic_cast<HBox*>(_selected) != nullptr) t += L"HBox";
            else if (dynamic_cast<Panel*>(_selected) != nullptr) t += L"Panel";
            else t += L"Widget";
            title->setText(t);
        }
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
        setField(_propText, L"");
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
        setField(_propSpacing, L"");
        setField(_propPadL, L"");
        setField(_propPadT, L"");
        setField(_propPadR, L"");
        setField(_propPadB, L"");
        _suppressProp = false;
        return;
    }
    setField(_propId, utf8ToWide(_selected->getId()));
    setField(_propX, formatFloat(_selected->getPosition().x));
    setField(_propY, formatFloat(_selected->getPosition().y));
    setField(_propW, formatFloat(_selected->getSize().x));
    setField(_propH, formatFloat(_selected->getSize().y));
    std::wstring text;
    getTextPayload(_selected, text);
    setField(_propText, text);
    if (_propStyleCombo != nullptr) {
        int idx = 0;
        const std::string& sid = _selected->getStyleId();
        for (size_t i = 0; i < _styleIds.size(); ++i) {
            if (_styleIds[i] == sid) {
                idx = static_cast<int>(i);
                break;
            }
        }
        _suppressStyleCombo = true;
        _propStyleCombo->setSelectedIndex(idx);
        _suppressStyleCombo = false;
    }
    syncTextAlignCombos();
    syncEnumCombos();
    if (auto* box = dynamic_cast<BoxBase*>(_selected)) {
        setField(_propSpacing, formatFloat(box->getSpacing()));
        const math::FVector4& pad = box->getPadding();
        setField(_propPadL, formatFloat(pad.x));
        setField(_propPadT, formatFloat(pad.y));
        setField(_propPadR, formatFloat(pad.z));
        setField(_propPadB, formatFloat(pad.w));
    } else {
        setField(_propSpacing, L"");
        setField(_propPadL, L"");
        setField(_propPadT, L"");
        setField(_propPadR, L"");
        setField(_propPadB, L"");
    }
    _suppressProp = false;
}

void LayoutEditorSession::markDirty(bool dirty) {
    _dirty = dirty;
    refreshWindowTitle();
    if (_dirty && !_documentPath.empty()) {
        setStatus(utf8ToWide("Modified — " + _documentPath));
    } else if (_dirty) {
        setStatus(L"Modified — unsaved");
    }
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
    if (_docRoot == nullptr || id.empty()) {
        return nullptr;
    }
    std::function<Widget*(Widget*)> walk = [&](Widget* n) -> Widget* {
        if (n->getId() == id) {
            return n;
        }
        for (Widget* c : n->getChildren()) {
            if (Widget* found = walk(c)) {
                return found;
            }
        }
        return nullptr;
    };
    return walk(_docRoot);
}

Widget* LayoutEditorSession::pickParentForAdd() const {
    if (_docRoot == nullptr) {
        return nullptr;
    }
    if (_selected == nullptr) {
        return _docRoot;
    }
    if (dynamic_cast<CompoundWidget*>(_selected) != nullptr) {
        return _selected;
    }
    Widget* parent = _selected->getParent();
    if (parent != nullptr && isUnderCanvas(parent) && parent != _canvasHost) {
        return parent;
    }
    return _docRoot;
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
    labels.push_back(label);
    widgets.push_back(node);
    for (Widget* child : node->getChildren()) {
        collectHierarchy(child, depth + 1, labels, widgets);
    }
}

std::string LayoutEditorSession::makeUniqueId(const std::string& prefix) const {
    for (int i = 1; i < 10000; ++i) {
        const std::string candidate = prefix + "_" + std::to_string(i);
        if (findInDocument(candidate) == nullptr) {
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
    return false;
}

bool LayoutEditorSession::hitPaletteType(const math::FVector2& worldPos,
                                         std::string& outType) const {
    if (_ui == nullptr) {
        return false;
    }
    static const struct {
        const char* id;
        const char* type;
    } kEntries[] = {
        {"btn_add_button", "Button"},
        {"btn_add_label", "TextLabel"},
        {"btn_add_panel", "Panel"},
        {"btn_add_input", "TextInput"},
        {"btn_add_checkbox", "CheckBox"},
        {"btn_add_slider", "Slider"},
        {"btn_add_scroll", "ScrollView"},
        {"btn_add_vbox", "VBox"},
        {"btn_add_hbox", "HBox"},
    };
    for (const auto& e : kEntries) {
        Widget* w = _ui->findById(e.id);
        if (w != nullptr && w->isVisible() &&
            w->getWorldBounds().contains(worldPos)) {
            outType = e.type;
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
    return dynamic_cast<CompoundWidget*>(widget) != nullptr;
}

bool LayoutEditorSession::isAncestorOf(Widget* ancestor, Widget* node) const {
    if (ancestor == nullptr || node == nullptr || ancestor == node) {
        return false;
    }
    for (Widget* p = node->getParent(); p != nullptr; p = p->getParent()) {
        if (p == ancestor) {
            return true;
        }
    }
    return false;
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

void LayoutEditorSession::detachFromTree(Widget* child) {
    if (child == nullptr) {
        return;
    }
    Widget* parent = child->getParent();
    if (parent == nullptr) {
        return;
    }
    if (auto* box = dynamic_cast<BoxBase*>(parent)) {
        box->removeWidget(child);
    } else {
        parent->removeChild(child);
    }
}

bool LayoutEditorSession::attachAt(Widget* parent, Widget* child, size_t index) {
    if (parent == nullptr || child == nullptr) {
        return false;
    }
    if (auto* box = dynamic_cast<BoxBase*>(parent)) {
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
    return parent->moveChildToIndex(child, insertAt);
}

Widget* LayoutEditorSession::createWidgetInstance(const std::string& typeName) {
    Widget* created = WidgetFactory::get().create(typeName);
    if (created == nullptr) {
        return nullptr;
    }
    created->setId(makeUniqueId(typePrefix(typeName)));
    created->setPosition(math::FVector2(24.0f, 24.0f));
    created->setSize(defaultSizeForType(typeName));
    if (typeName == "Button") {
        setTextPayload(created, L"Button");
    } else if (typeName == "TextLabel") {
        setTextPayload(created, L"Label");
    } else if (typeName == "TextInput") {
        setTextPayload(created, L"");
    } else if (typeName == "CheckBox") {
        setTextPayload(created, L"Check");
    }
    return created;
}

void LayoutEditorSession::placeNewWidget(Widget* created, Widget* parent,
                                         int insertIndex,
                                         const math::FVector2* worldPos) {
    if (created == nullptr || parent == nullptr) {
        return;
    }
    if (auto* box = dynamic_cast<BoxBase*>(parent)) {
        if (insertIndex < 0) {
            box->addWidget(created, 0.0f);
        } else {
            box->insertWidget(insertIndex, created, 0.0f);
        }
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
        if (worldPos != nullptr &&
            dynamic_cast<BoxBase*>(parent) == nullptr) {
            const math::FRectangle pb = parent->getWorldBounds();
            created->setPosition(math::FVector2(
                worldPos->x - pb.minX, worldPos->y - pb.minY));
            created->setLayoutPositionManaged(false);
        }
        snapWidgetPosition(created);
    }
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
    math::FVector2 dropPos = worldPos;
    const math::FVector2* posPtr = &dropPos;

    Widget* hit = pickDocumentWidget(worldPos);
    if (hit != nullptr && isContainerWidget(hit)) {
        parent = hit;
    } else if (hit != nullptr) {
        Widget* p = hit->getParent();
        if (p != nullptr && isUnderCanvas(p) && p != _canvasHost) {
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
    pushUndo();
    placeNewWidget(created, parent, insertIndex, posPtr);
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
        pushUndo();
        bool ok = false;
        if (auto* box = dynamic_cast<BoxBase*>(newParent)) {
            ok = box->moveSlotToIndex(dragged, static_cast<size_t>(dest));
        } else {
            ok = newParent->moveChildToIndex(dragged,
                                             static_cast<size_t>(dest));
        }
        if (!ok) {
            if (!_undoStack.empty()) {
                _undoStack.pop_back();
            }
            setStatus(L"Reorder failed");
            return;
        }
    } else {
        pushUndo();
        detachFromTree(dragged);
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
            if (!_undoStack.empty()) {
                _undoStack.pop_back();
            }
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
        _hierarchyCol = _ui != nullptr ? _ui->findById("hierarchy_col") : nullptr;
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
        _chromeRoot = _ui->findById("layout_editor_root");
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
    const math::FVector2 sz = defaultSizeForType(typeName);
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
    collectDocumentWidgets(_docRoot, _selection);
    // Prefer not selecting the document root alone as primary if children exist.
    _selected = nullptr;
    for (Widget* w : _selection) {
        if (w != nullptr && w != _docRoot) {
            _selected = w;
            break;
        }
    }
    if (_selected == nullptr && !_selection.empty()) {
        _selected = _selection.front();
    }
    normalizeSelectionNesting();
    if (_selected != nullptr && !selectionContains(_selected)) {
        _selected = _selection.empty() ? nullptr : _selection.back();
    }
    syncPropertyStrip();
    syncHierarchySelection();
    syncSelectionChrome();
    std::wostringstream oss;
    oss << L"Selected all (" << _selection.size() << L")";
    setStatus(oss.str());
}

void LayoutEditorSession::copySelection() {
    _clipboardItems.clear();
    for (Widget* w : _selection) {
        if (w == nullptr || w == _docRoot || isEditorOverlay(w)) {
            continue;
        }
        std::string json;
        if (_docLoader.saveLayoutToString(w, json, false) && !json.empty()) {
            _clipboardItems.push_back(std::move(json));
        }
    }
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
    if (_clipboardItems.empty()) {
        std::wstring clip;
        if (getClipboard().getText(clip) && !clip.empty()) {
            const std::string utf8 = wideToUtf8(clip);
            _clipboardItems.clear();
            const std::string delim = "\n/*AYUI_CLIP*/\n";
            size_t start = 0;
            while (start < utf8.size()) {
                size_t pos = utf8.find(delim, start);
                if (pos == std::string::npos) {
                    _clipboardItems.push_back(utf8.substr(start));
                    break;
                }
                _clipboardItems.push_back(utf8.substr(start, pos - start));
                start = pos + delim.size();
            }
        }
    }
    if (_clipboardItems.empty()) {
        setStatus(L"Paste: clipboard empty");
        return;
    }

    Widget* parent = pickParentForAdd();
    if (parent == nullptr) {
        setStatus(L"Paste: no parent");
        return;
    }

    pushUndo();
    std::vector<Widget*> pasted;
    float offset = 16.0f;
    for (const std::string& json : _clipboardItems) {
        Widget* created = _docLoader.loadFromString(json);
        if (created == nullptr) {
            continue;
        }
        remintTreeIds(created);
        math::FVector2 pos = created->getPosition();
        pos.x += offset;
        pos.y += offset;
        created->setPosition(pos);
        placeNewWidget(created, parent, -1, nullptr);
        freezeDocumentInteraction(created);
        pasted.push_back(created);
        offset += 8.0f;
    }
    if (pasted.empty()) {
        if (!_undoStack.empty()) {
            _undoStack.pop_back();
        }
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
    copySelection();
    if (_clipboardItems.empty()) {
        return;
    }
    pasteClipboard();
    setStatus(L"Duplicated");
}

void LayoutEditorSession::nudgeSelection(float dx, float dy) {
    if (_selection.empty()) {
        return;
    }
    beginMutation();
    bool moved = false;
    for (Widget* w : _selection) {
        if (w == nullptr || w == _docRoot) {
            continue;
        }
        if (w->getParent() != nullptr &&
            dynamic_cast<BoxBase*>(w->getParent()) != nullptr) {
            continue;
        }
        math::FVector2 pos = w->getPosition();
        pos.x += dx;
        pos.y += dy;
        w->setPosition(pos);
        snapWidgetPosition(w);
        moved = true;
    }
    endMutation();
    if (!moved) {
        setStatus(L"Nudge: no free-position widgets in selection");
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
    if (_docRoot == nullptr || std::fabs(zoom - _viewZoom) < 0.0001f) {
        _viewZoom = zoom;
        return;
    }
    const float factor = zoom / _viewZoom;
    _viewZoom = zoom;

    // Keep pivot stable: scale free positions around canvas-local pivot.
    const math::FRectangle host = _canvasHost != nullptr
                                      ? _canvasHost->getWorldBounds()
                                      : math::FRectangle();
    const math::FVector2 pivotLocal(pivotWorld.x - host.minX,
                                    pivotWorld.y - host.minY);

    scaleDocumentTree(_docRoot, factor);
    if (_docRoot != nullptr && !_docRoot->isLayoutPositionManaged()) {
        math::FVector2 p = _docRoot->getPosition();
        p.x = pivotLocal.x + (p.x - pivotLocal.x) * factor;
        p.y = pivotLocal.y + (p.y - pivotLocal.y) * factor;
        _docRoot->setPosition(p);
    }

    markDirty(true);
    syncSelectionChrome();
    if (_ui != nullptr) {
        _ui->invalidateLayout();
        _ui->layout();
    }
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

void LayoutEditorSession::syncTextAlignCombos() {
    if (_selected == nullptr) {
        return;
    }
    int hIdx = 0;
    int vIdx = 0;
    if (auto* label = dynamic_cast<TextLabel*>(_selected)) {
        switch (label->getHorizontalAlignment()) {
        case TextLabel::HAlignment::Center: hIdx = 1; break;
        case TextLabel::HAlignment::Right:  hIdx = 2; break;
        default: hIdx = 0; break;
        }
        switch (label->getVerticalAlignment()) {
        case TextLabel::VAlignment::Center: vIdx = 1; break;
        case TextLabel::VAlignment::Bottom: vIdx = 2; break;
        default: vIdx = 0; break;
        }
    } else if (auto* ti = dynamic_cast<TextInput*>(_selected)) {
        switch (ti->getHAlign()) {
        case TextInput::HAlign::Center: hIdx = 1; break;
        case TextInput::HAlign::Right:  hIdx = 2; break;
        default: hIdx = 0; break;
        }
        vIdx = 0;
    }
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

void LayoutEditorSession::bindBoolCombo(ComboBox*& slot, const char* id,
                                        const char* field) {
    if (_ui == nullptr) {
        return;
    }
    slot = dynamic_cast<ComboBox*>(_ui->findById(id));
    if (slot == nullptr) {
        return;
    }
    slot->setItems({L"false", L"true"});
    const std::string fieldName = field;
    slot->setOnSelectionChanged([this, fieldName](int index) {
        if (_suppressProp || index < 0 || index > 1) {
            return;
        }
        beginMutation();
        applyProperty(fieldName, index == 1 ? L"true" : L"false");
    });
}

void LayoutEditorSession::bindGravityCombo() {
    if (_ui == nullptr) {
        return;
    }
    _propGravity = dynamic_cast<ComboBox*>(_ui->findById("prop_gravity"));
    if (_propGravity == nullptr) {
        return;
    }
    static const wchar_t* kGravity[] = {
        L"TopLeft", L"TopCenter", L"TopRight",
        L"CenterLeft", L"Center", L"CenterRight",
        L"BottomLeft", L"BottomCenter", L"BottomRight"
    };
    std::vector<std::wstring> items;
    for (const wchar_t* g : kGravity) {
        items.emplace_back(g);
    }
    _propGravity->setItems(items);
    _propGravity->setOnSelectionChanged([this](int index) {
        if (_suppressProp || index < 0 || index > 8) {
            return;
        }
        static const char* kG[] = {
            "TopLeft", "TopCenter", "TopRight",
            "CenterLeft", "Center", "CenterRight",
            "BottomLeft", "BottomCenter", "BottomRight"
        };
        beginMutation();
        applyProperty("gravity", utf8ToWide(kG[index]));
    });
}

void LayoutEditorSession::syncEnumCombos() {
    int checkedIdx = 0;
    int passwordIdx = 0;
    int readOnlyIdx = 0;
    int gravityIdx = 0;

    if (auto* cb = dynamic_cast<CheckBox*>(_selected)) {
        checkedIdx = cb->isChecked() ? 1 : 0;
    }
    if (auto* ti = dynamic_cast<TextInput*>(_selected)) {
        passwordIdx = ti->isPasswordMode() ? 1 : 0;
        readOnlyIdx = ti->isReadOnly() ? 1 : 0;
    }
    if (auto* box = dynamic_cast<BoxBase*>(_selected)) {
        switch (box->getGravity()) {
        case BoxBase::Gravity::TopCenter: gravityIdx = 1; break;
        case BoxBase::Gravity::TopRight: gravityIdx = 2; break;
        case BoxBase::Gravity::CenterLeft: gravityIdx = 3; break;
        case BoxBase::Gravity::Center: gravityIdx = 4; break;
        case BoxBase::Gravity::CenterRight: gravityIdx = 5; break;
        case BoxBase::Gravity::BottomLeft: gravityIdx = 6; break;
        case BoxBase::Gravity::BottomCenter: gravityIdx = 7; break;
        case BoxBase::Gravity::BottomRight: gravityIdx = 8; break;
        case BoxBase::Gravity::TopLeft:
        default: gravityIdx = 0; break;
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
    if (w == nullptr || !_snapEnabled) {
        return;
    }
    if (w->getParent() != nullptr &&
        dynamic_cast<BoxBase*>(w->getParent()) != nullptr) {
        return;
    }
    math::FVector2 pos = w->getPosition();
    pos.x = snapValue(pos.x);
    pos.y = snapValue(pos.y);
    w->setPosition(pos);
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
        for (Widget* c : n->getChildren()) {
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
    for (Widget* c : node->getChildren()) {
        collectDocumentWidgets(c, out);
    }
}

void LayoutEditorSession::scaleDocumentTree(Widget* node, float factor) {
    if (node == nullptr || isEditorOverlay(node)) {
        return;
    }
    for (Widget* c : node->getChildren()) {
        if (c == nullptr || isEditorOverlay(c)) {
            continue;
        }
        if (!c->isLayoutPositionManaged()) {
            math::FVector2 p = c->getPosition();
            p.x *= factor;
            p.y *= factor;
            c->setPosition(p);
        }
        if (!c->isLayoutSizeManaged()) {
            math::FVector2 s = c->getSize();
            s.x *= factor;
            s.y *= factor;
            c->setSize(s);
        }
        scaleDocumentTree(c, factor);
    }
    if (!node->isLayoutSizeManaged() && node == _docRoot) {
        math::FVector2 s = node->getSize();
        s.x *= factor;
        s.y *= factor;
        node->setSize(s);
    }
}

void LayoutEditorSession::ensureSelOutlines(size_t count) {
    if (_canvasHost == nullptr) {
        return;
    }
    if (auto* sheet = StyleManager::get().getStyleSheet()) {
        WidgetStyle outline{};
        outline.backgroundColor = math::FVector4(0.20f, 0.55f, 0.95f, 0.06f);
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
    math::FRectangle box(
        (std::min)(a.x, b.x), (std::min)(a.y, b.y),
        (std::max)(a.x, b.x), (std::max)(a.y, b.y));
    if (box.width() < 3.0f && box.height() < 3.0f) {
        return;
    }
    std::vector<Widget*> hits;
    std::vector<Widget*> all;
    collectDocumentWidgets(_docRoot, all);
    for (Widget* w : all) {
        if (w == nullptr || w == _docRoot || isEditorOverlay(w)) {
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
