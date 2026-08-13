#pragma once

#include "AYUIManager.h"

#include <functional>
#include <string>
#include <vector>

namespace ayt::ui {

class Button;
class ListView;
class TextInput;
class TextLabel;

// Shared core for AYUI_LayoutEditor.exe and AYEditor ChildWindow host.
// One UIManager: chrome (layout_editor.ui.json) + document under canvas_host.
class LayoutEditorSession {
public:
    using PathPicker = std::function<std::string()>;

    LayoutEditorSession() = default;
    ~LayoutEditorSession();

    LayoutEditorSession(const LayoutEditorSession&) = delete;
    LayoutEditorSession& operator=(const LayoutEditorSession&) = delete;

    // Wire chrome controls on an already-loaded layout_editor.ui.json tree.
    // Creates an empty document_root Panel under canvas_host when empty.
    bool attach(UIManager& ui);
    void detach();

    bool open(const std::string& path);
    bool save();
    bool saveAs(const std::string& path);

    void select(Widget* widget);
    void selectById(const std::string& id);
    void addWidget(const std::string& typeName);
    void deleteSelected();
    void refreshHierarchy();
    void applyProperty(const std::string& field, const std::wstring& value);

    // Hit-test: only accepts widgets under canvas_host (ignores chrome).
    void onCanvasClick(const math::FVector2& worldPos);

    bool isDirty() const { return _dirty; }
    const std::string& documentPath() const { return _documentPath; }
    Widget* selected() const { return _selected; }
    Widget* documentRoot() const { return _docRoot; }

    void setOpenPathPicker(PathPicker picker) { _openPicker = std::move(picker); }
    void setSavePathPicker(PathPicker picker) { _savePicker = std::move(picker); }

private:
    void wireChrome();
    void clearDocument();
    void setDocumentRoot(Widget* root);
    void ensureEmptyDocument();
    void setStatus(const std::wstring& text);
    void syncPropertyStrip();
    void markDirty(bool dirty = true);
    bool isUnderCanvas(Widget* widget) const;
    Widget* findInDocument(const std::string& id) const;
    Widget* pickParentForAdd() const;
    void collectHierarchy(Widget* node, int depth,
                          std::vector<std::wstring>& labels,
                          std::vector<Widget*>& widgets) const;
    std::string makeUniqueId(const std::string& prefix) const;
    bool setTextPayload(Widget* widget, const std::wstring& text);
    bool getTextPayload(Widget* widget, std::wstring& out) const;

    UIManager* _ui = nullptr;
    Widget* _canvasHost = nullptr;
    Widget* _docRoot = nullptr;
    Widget* _selected = nullptr;
    std::string _documentPath;
    bool _dirty = false;
    bool _suppressProp = false;
    bool _suppressHierarchy = false;

    ListView* _hierarchy = nullptr;
    TextInput* _propId = nullptr;
    TextInput* _propX = nullptr;
    TextInput* _propY = nullptr;
    TextInput* _propW = nullptr;
    TextInput* _propH = nullptr;
    TextInput* _propText = nullptr;
    TextLabel* _status = nullptr;
    std::vector<Widget*> _hierarchyIndex;

    PathPicker _openPicker;
    PathPicker _savePicker;
    UILayoutLoader _docLoader;
};

} // namespace ayt::ui
