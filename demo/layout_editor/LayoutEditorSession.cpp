#include "LayoutEditorSession.h"

#include "AYButton.h"
#include "AYLayoutLoader.h"
#include "AYListView.h"
#include "AYPanel.h"
#include "AYTextInput.h"
#include "AYTextLabel.h"
#include "AYWidget.h"
#include "AYWidgetFactory.h"

#include <cstdio>
#include <functional>
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
    return std::wstring(s.begin(), s.end());
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
    return std::string(s.begin(), s.end());
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

} // namespace

LayoutEditorSession::~LayoutEditorSession() {
    detach();
}

bool LayoutEditorSession::attach(UIManager& ui) {
    detach();
    _ui = &ui;
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
        _docRoot = _canvasHost->getChildren().front();
    }

    refreshHierarchy();
    syncPropertyStrip();
    setStatus(L"Ready");
    return true;
}

void LayoutEditorSession::detach() {
    _ui = nullptr;
    _canvasHost = nullptr;
    _docRoot = nullptr;
    _selected = nullptr;
    _hierarchy = nullptr;
    _propId = _propX = _propY = _propW = _propH = _propText = nullptr;
    _status = nullptr;
    _hierarchyIndex.clear();
    _documentPath.clear();
    _dirty = false;
}

void LayoutEditorSession::wireChrome() {
    if (_ui == nullptr) {
        return;
    }

    if (auto* b = dynamic_cast<Button*>(_ui->findById("btn_open"))) {
        b->setOnClicked([this]() {
            if (_openPicker) {
                const std::string path = _openPicker();
                if (!path.empty()) {
                    open(path);
                }
            }
        });
    }
    if (auto* b = dynamic_cast<Button*>(_ui->findById("btn_save"))) {
        b->setOnClicked([this]() {
            if (!save()) {
                setStatus(L"Save failed (use Save As?)");
            }
        });
    }
    if (auto* b = dynamic_cast<Button*>(_ui->findById("btn_save_as"))) {
        b->setOnClicked([this]() {
            if (!_savePicker) {
                setStatus(L"No Save As picker");
                return;
            }
            const std::string path = _savePicker();
            if (!path.empty()) {
                saveAs(path);
            }
        });
    }
    if (auto* b = dynamic_cast<Button*>(_ui->findById("btn_add_button"))) {
        b->setOnClicked([this]() { addWidget("Button"); });
    }
    if (auto* b = dynamic_cast<Button*>(_ui->findById("btn_add_label"))) {
        b->setOnClicked([this]() { addWidget("TextLabel"); });
    }
    if (auto* b = dynamic_cast<Button*>(_ui->findById("btn_add_panel"))) {
        b->setOnClicked([this]() { addWidget("Panel"); });
    }
    if (auto* b = dynamic_cast<Button*>(_ui->findById("btn_delete"))) {
        b->setOnClicked([this]() { deleteSelected(); });
    }

    _hierarchy = dynamic_cast<ListView*>(_ui->findById("list_hierarchy"));
    if (_hierarchy != nullptr) {
        _hierarchy->setOnSelectionChanged([this](int index) {
            if (_suppressHierarchy || index < 0 ||
                index >= static_cast<int>(_hierarchyIndex.size())) {
                return;
            }
            select(_hierarchyIndex[static_cast<size_t>(index)]);
        });
    }

    auto bindProp = [this](const char* id, const char* field, TextInput*& slot) {
        slot = dynamic_cast<TextInput*>(_ui->findById(id));
        if (slot == nullptr) {
            return;
        }
        const std::string fieldName(field);
        slot->setOnSubmit([this, fieldName](const std::wstring& value) {
            if (!_suppressProp) {
                applyProperty(fieldName, value);
            }
        });
        slot->setOnTextChanged([this, fieldName](const std::wstring& value) {
            if (!_suppressProp) {
                applyProperty(fieldName, value);
            }
        });
    };
    bindProp("prop_id", "id", _propId);
    bindProp("prop_x", "x", _propX);
    bindProp("prop_y", "y", _propY);
    bindProp("prop_w", "w", _propW);
    bindProp("prop_h", "h", _propH);
    bindProp("prop_text", "text", _propText);

    _status = dynamic_cast<TextLabel*>(_ui->findById("lbl_status"));
}

void LayoutEditorSession::clearDocument() {
    if (_canvasHost == nullptr) {
        return;
    }
    std::vector<Widget*> kids = _canvasHost->getChildren();
    for (Widget* child : kids) {
        _canvasHost->removeChild(child);
        destroyWidgetTree(child);
    }
    _docRoot = nullptr;
    _selected = nullptr;
}

void LayoutEditorSession::setDocumentRoot(Widget* root) {
    if (_canvasHost == nullptr || root == nullptr) {
        return;
    }
    clearDocument();
    _canvasHost->addChild(root);
    _docRoot = root;
}

void LayoutEditorSession::ensureEmptyDocument() {
    Widget* panel = WidgetFactory::get().create("Panel");
    if (panel == nullptr) {
        return;
    }
    panel->setId("document_root");
    panel->setPosition(math::FVector2(16.0f, 16.0f));
    panel->setSize(math::FVector2(640.0f, 480.0f));
    setDocumentRoot(panel);
    _documentPath.clear();
    markDirty(false);
}

bool LayoutEditorSession::open(const std::string& path) {
    if (_ui == nullptr || path.empty()) {
        return false;
    }
    Widget* loaded = _docLoader.loadFromFile(path);
    if (loaded == nullptr) {
        setStatus(utf8ToWide("Open failed: " + path));
        return false;
    }
    setDocumentRoot(loaded);
    _documentPath = path;
    markDirty(false);
    refreshHierarchy();
    select(_docRoot);
    setStatus(utf8ToWide("Opened " + path));
    if (_ui != nullptr) {
        _ui->invalidateLayout();
        _ui->layout();
    }
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
    UILayoutLoader saver;
    if (!saver.saveLayout(path, _docRoot, true)) {
        setStatus(utf8ToWide("Save failed: " + path));
        return false;
    }
    _documentPath = path;
    markDirty(false);
    setStatus(utf8ToWide("Saved " + path));
    return true;
}

void LayoutEditorSession::select(Widget* widget) {
    if (widget == _canvasHost) {
        widget = nullptr;
    }
    if (widget != nullptr && !isUnderCanvas(widget)) {
        widget = nullptr;
    }
    _selected = widget;
    syncPropertyStrip();

    if (_hierarchy != nullptr && !_suppressHierarchy) {
        _suppressHierarchy = true;
        int index = -1;
        for (size_t i = 0; i < _hierarchyIndex.size(); ++i) {
            if (_hierarchyIndex[i] == widget) {
                index = static_cast<int>(i);
                break;
            }
        }
        _hierarchy->setSelectedIndex(index);
        _suppressHierarchy = false;
    }
}

void LayoutEditorSession::selectById(const std::string& id) {
    select(findInDocument(id));
}

void LayoutEditorSession::addWidget(const std::string& typeName) {
    Widget* parent = pickParentForAdd();
    if (parent == nullptr) {
        return;
    }
    Widget* created = WidgetFactory::get().create(typeName);
    if (created == nullptr) {
        setStatus(utf8ToWide("Unknown type: " + typeName));
        return;
    }

    const std::string prefix =
        typeName == "TextLabel" ? "lbl" :
        typeName == "Button" ? "btn" :
        typeName == "Panel" ? "panel" : "w";
    created->setId(makeUniqueId(prefix));
    created->setPosition(math::FVector2(24.0f, 24.0f));
    created->setSize(math::FVector2(
        typeName == "Panel" ? 200.0f : 120.0f,
        typeName == "Panel" ? 120.0f : 28.0f));
    if (typeName == "Button") {
        setTextPayload(created, L"Button");
    } else if (typeName == "TextLabel") {
        setTextPayload(created, L"Label");
    }

    parent->addChild(created);
    markDirty(true);
    refreshHierarchy();
    select(created);
    setStatus(utf8ToWide("Added " + typeName));
    if (_ui != nullptr) {
        _ui->invalidateLayout();
        _ui->layout();
    }
}

void LayoutEditorSession::deleteSelected() {
    if (_selected == nullptr || _docRoot == nullptr) {
        return;
    }
    if (_selected == _docRoot) {
        setStatus(L"Cannot delete document root");
        return;
    }
    Widget* parent = _selected->getParent();
    if (parent == nullptr) {
        return;
    }
    Widget* doomed = _selected;
    _selected = nullptr;
    parent->removeChild(doomed);
    destroyWidgetTree(doomed);
    markDirty(true);
    refreshHierarchy();
    syncPropertyStrip();
    setStatus(L"Deleted");
    if (_ui != nullptr) {
        _ui->invalidateLayout();
        _ui->layout();
    }
}

void LayoutEditorSession::refreshHierarchy() {
    _hierarchyIndex.clear();
    std::vector<std::wstring> labels;
    if (_docRoot != nullptr) {
        collectHierarchy(_docRoot, 0, labels, _hierarchyIndex);
    }
    if (_hierarchy != nullptr) {
        _suppressHierarchy = true;
        _hierarchy->setItems(labels);
        _suppressHierarchy = false;
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
        select(_selected);
        return;
    }

    float f = 0.0f;
    if (field == "x" || field == "y" || field == "w" || field == "h") {
        if (!parseFloat(value, f)) {
            return;
        }
        math::FVector2 pos = _selected->getPosition();
        math::FVector2 size = _selected->getSize();
        if (field == "x") {
            pos.x = f;
            _selected->setPosition(pos);
        } else if (field == "y") {
            pos.y = f;
            _selected->setPosition(pos);
        } else if (field == "w") {
            size.x = f;
            _selected->setSize(size);
        } else if (field == "h") {
            size.y = f;
            _selected->setSize(size);
        }
        markDirty(true);
        if (_ui != nullptr) {
            _ui->invalidateLayout();
        }
        return;
    }

    if (field == "text") {
        if (setTextPayload(_selected, value)) {
            markDirty(true);
        }
    }
}

void LayoutEditorSession::onCanvasClick(const math::FVector2& worldPos) {
    if (_ui == nullptr || _canvasHost == nullptr) {
        return;
    }
    Widget* hit = _ui->pickTopmostWidget(worldPos);
    while (hit != nullptr && !isUnderCanvas(hit)) {
        hit = hit->getParent();
    }
    if (hit == nullptr || hit == _canvasHost || !isUnderCanvas(hit)) {
        return;
    }
    // Prefer deepest document widget (already topmost under canvas).
    select(hit);
}

void LayoutEditorSession::setStatus(const std::wstring& text) {
    if (_status != nullptr) {
        _status->setText(text);
    }
}

void LayoutEditorSession::syncPropertyStrip() {
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
    _suppressProp = false;
}

void LayoutEditorSession::markDirty(bool dirty) {
    _dirty = dirty;
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

bool LayoutEditorSession::getTextPayload(Widget* widget, std::wstring& out) const {
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
    out.clear();
    return false;
}

bool LayoutEditorSession::setTextPayload(Widget* widget, const std::wstring& text) {
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
    return false;
}

} // namespace ayt::ui
