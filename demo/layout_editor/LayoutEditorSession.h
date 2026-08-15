#pragma once

#include "AYUIManager.h"

#include <functional>
#include <string>
#include <vector>

namespace ayt::ui {

class Button;
class ComboBox;
class ListView;
class Panel;
class TextInput;
class TextLabel;

// Shared core for AYUI_LayoutEditor.exe and AYEditor ChildWindow host.
// One UIManager: chrome (layout_editor.ui.json) + document under canvas_host.
class LayoutEditorSession {
public:
    using PathPicker = std::function<std::string()>;
    using TitleUpdater = std::function<void(const std::wstring& title)>;

    enum class AlignMode {
        Left, HCenter, Right,
        Top, VCenter, Bottom,
        DistributeH, DistributeV
    };

    LayoutEditorSession() = default;
    ~LayoutEditorSession();

    LayoutEditorSession(const LayoutEditorSession&) = delete;
    LayoutEditorSession& operator=(const LayoutEditorSession&) = delete;

    bool attach(UIManager& ui);
    void detach();
    void pumpDeferred();

    bool open(const std::string& path);
    bool save();
    bool saveAs(const std::string& path);

    void select(Widget* widget, bool additive = false);
    void selectById(const std::string& id);
    void selectAll();
    void addWidget(const std::string& typeName);
    void deleteSelected();
    void refreshHierarchy();
    void applyProperty(const std::string& field, const std::wstring& value);

    void undo();
    void redo();
    void alignSelection(AlignMode mode);
    void reorderSelected(int delta);

    void copySelection();
    void pasteClipboard();
    void duplicateSelection();
    void nudgeSelection(float dx, float dy);
    void toggleSnap();
    void setViewZoom(float zoom, const math::FVector2& pivotWorld);

    // button: 0=LMB, 1=RMB, 2=MMB
    bool onPointerDown(const math::FVector2& worldPos, int button = 0);
    bool onPointerMove(const math::FVector2& worldPos);
    bool onPointerUp(const math::FVector2& worldPos, int button = 0);
    bool onWheel(const math::FVector2& worldPos, float deltaY);

    bool onKeyDown(int uiKeyCode);
    UiCursorHint canvasCursorHint(const math::FVector2& worldPos) const;
    void syncSelectionChrome();

    void onCanvasClick(const math::FVector2& worldPos) { onPointerDown(worldPos); }

    bool isDirty() const { return _dirty; }
    bool isDraggingDocument() const {
        return _dragMode != DragMode::None || _toolDrag != ToolDrag::None;
    }
    const std::string& documentPath() const { return _documentPath; }
    Widget* selected() const { return _selected; }
    const std::vector<Widget*>& selection() const { return _selection; }
    Widget* documentRoot() const { return _docRoot; }
    float viewZoom() const { return _viewZoom; }
    bool snapEnabled() const { return _snapEnabled; }
    float gridSize() const { return _gridSize; }

    void setOpenPathPicker(PathPicker picker) { _openPicker = std::move(picker); }
    void setSavePathPicker(PathPicker picker) { _savePicker = std::move(picker); }
    void setTitleUpdater(TitleUpdater updater) { _titleUpdater = std::move(updater); }

private:
    enum class DeferredAction { None, Open, Save, SaveAs };
    enum class DragMode {
        None, Move, Marquee, Pan,
        ResizeN, ResizeS, ResizeE, ResizeW,
        ResizeNW, ResizeNE, ResizeSW, ResizeSE
    };
    enum class ToolDrag {
        None,
        PalettePress,
        PaletteDrag,
        HierPress,
        HierDrag
    };
    enum class DropPlace { Before, After, Into };

    struct Snapshot {
        std::string json;
        std::vector<std::string> selectedIds;
        std::string primaryId;
    };

    struct HierDropTarget {
        Widget* target = nullptr;
        DropPlace place = DropPlace::Before;
        int listIndex = -1;
        bool valid = false;
    };

    void wireChrome();
    void clearDocument();
    void setDocumentRoot(Widget* root);
    void ensureEmptyDocument();
    void freezeDocumentInteraction(Widget* root);
    void setStatus(const std::wstring& text);
    void syncPropertyStrip();
    void updatePropPanelVisibility();
    void setChromeVisible(const char* id, bool visible);
    void syncHierarchySelection();
    void syncStyleCombo();
    void syncTextAlignCombos();
    void syncEnumCombos();
    void refreshWindowTitle();
    void bindBoolCombo(ComboBox*& slot, const char* id, const char* field);
    void bindGravityCombo();
    void markDirty(bool dirty = true);
    bool isUnderCanvas(Widget* widget) const;
    Widget* pickDocumentWidget(const math::FVector2& worldPos) const;
    Widget* findInDocument(const std::string& id) const;
    Widget* pickParentForAdd() const;
    void collectHierarchy(Widget* node, int depth,
                          std::vector<std::wstring>& labels,
                          std::vector<Widget*>& widgets) const;
    void updateContainerHint();
    std::string makeUniqueId(const std::string& prefix) const;
    bool setTextPayload(Widget* widget, const std::wstring& text);
    bool getTextPayload(Widget* widget, std::wstring& out) const;
    void bindPropField(const char* id, const char* field, TextInput*& slot,
                       bool numericScrub);
    void commitPropField(const std::string& field, TextInput* slot);

    void beginMutation();
    void endMutation();
    void pushUndo();
    Snapshot captureSnapshot() const;
    void restoreSnapshot(const Snapshot& snap);
    bool chromeEditingText() const;
    bool modifiersCtrl() const;
    bool modifiersShift() const;
    bool modifiersAdditive() const;
    bool modifiersSpace() const;
    DragMode hitTestResizeHandle(Widget* widget,
                                 const math::FVector2& worldPos) const;
    void applyResizeDelta(Widget* widget, DragMode mode,
                          const math::FVector2& delta);
    void pruneSelection();
    void normalizeSelectionNesting();
    bool selectionContains(Widget* widget) const;
    bool isCanvasHit(const math::FVector2& worldPos) const;
    bool isEditorOverlay(Widget* widget) const;
    void ensureSelectionChrome();
    void destroySelectionChrome();
    void placeHandle(Widget* handle, float x, float y);
    void ensureSelOutlines(size_t count);
    void updateMarqueeChrome(const math::FVector2& a, const math::FVector2& b);
    void clearMarqueeChrome();
    void commitMarquee(const math::FVector2& a, const math::FVector2& b,
                       bool additive);

    bool hitPaletteType(const math::FVector2& worldPos, std::string& outType) const;
    bool hitHierarchyList(const math::FVector2& worldPos) const;
    bool hierarchyScrollbarHit(const math::FVector2& worldPos) const;
    int hierarchyIndexAt(const math::FVector2& worldPos, float* fracInRow) const;
    HierDropTarget resolveHierDrop(const math::FVector2& worldPos,
                                   Widget* dragged) const;
    bool isContainerWidget(Widget* widget) const;
    bool isAncestorOf(Widget* ancestor, Widget* node) const;
    int siblingIndexOf(Widget* child) const;
    void detachFromTree(Widget* child);
    bool attachAt(Widget* parent, Widget* child, size_t index);
    Widget* createWidgetInstance(const std::string& typeName);
    void placeNewWidget(Widget* created, Widget* parent, int insertIndex,
                        const math::FVector2* worldPos);
    void commitPaletteDrop(const math::FVector2& worldPos);
    void commitHierarchyDrop(const math::FVector2& worldPos);
    void ensureHierDropChrome();
    void destroyHierDropChrome();
    void updateHierDropChrome(const HierDropTarget& drop);
    void clearHierDropChrome();
    void ensurePaletteGhost();
    void destroyPaletteGhost();
    void showPaletteGhost(const std::string& typeName);
    void updatePaletteGhost(const math::FVector2& worldPos, bool overCanvas);
    void hidePaletteGhost();
    void cancelToolDrag();
    bool stillOnPaletteSource(const math::FVector2& worldPos) const;

    float snapValue(float v) const;
    void snapWidgetPosition(Widget* w);
    void snapSelectionPositions();
    void remintTreeIds(Widget* root);
    void collectDocumentWidgets(Widget* node, std::vector<Widget*>& out) const;
    void scaleDocumentTree(Widget* node, float factor);

    UIManager* _ui = nullptr;
    Widget* _canvasHost = nullptr;
    Widget* _docRoot = nullptr;
    Widget* _selected = nullptr;
    std::vector<Widget*> _selection;
    std::string _documentPath;
    bool _dirty = false;
    bool _suppressProp = false;
    bool _suppressHierarchy = false;
    bool _suppressStyleCombo = false;
    bool _insideSelect = false;
    // Hierarchy click is handled on pointer-down; consume the matching up
    // so ListView does not re-fire selection (esp. with Shift held).
    bool _consumeNextPointerUp = false;

    DragMode _dragMode = DragMode::None;
    Widget* _dragTarget = nullptr;
    math::FVector2 _dragLastMouse{0.0f, 0.0f};
    math::FVector2 _marqueeStart{0.0f, 0.0f};
    bool _mutationOpen = false;

    ToolDrag _toolDrag = ToolDrag::None;
    std::string _paletteType;
    Widget* _hierDragWidget = nullptr;
    math::FVector2 _toolPressPos{0.0f, 0.0f};
    static constexpr float kDragThreshold = 6.0f;

    Panel* _selBox = nullptr;
    Panel* _handles[8] = {};
    std::vector<Panel*> _selOutlines;
    Panel* _marqueeBox = nullptr;
    static constexpr float kHandleSize = 8.0f;

    Widget* _hierarchyCol = nullptr;
    Panel* _hierDropLine = nullptr;
    Panel* _hierDropInto = nullptr;

    Widget* _chromeRoot = nullptr;
    Panel* _paletteGhost = nullptr;
    TextLabel* _paletteGhostLabel = nullptr;

    float _viewZoom = 1.0f;
    bool _snapEnabled = true;
    float _gridSize = 8.0f;
    std::vector<std::string> _clipboardItems;

    std::vector<Snapshot> _undoStack;
    std::vector<Snapshot> _redoStack;
    static constexpr size_t kMaxUndo = 64;

    DeferredAction _deferred = DeferredAction::None;

    ListView* _hierarchy = nullptr;
    TextInput* _propId = nullptr;
    TextInput* _propX = nullptr;
    TextInput* _propY = nullptr;
    TextInput* _propW = nullptr;
    TextInput* _propH = nullptr;
    TextInput* _propText = nullptr;
    ComboBox* _propStyleCombo = nullptr;
    ComboBox* _propTextHAlign = nullptr;
    ComboBox* _propTextVAlign = nullptr;
    ComboBox* _propChecked = nullptr;
    ComboBox* _propPassword = nullptr;
    ComboBox* _propReadOnly = nullptr;
    ComboBox* _propGravity = nullptr;
    TextInput* _propSpacing = nullptr;
    TextInput* _propPadL = nullptr;
    TextInput* _propPadT = nullptr;
    TextInput* _propPadR = nullptr;
    TextInput* _propPadB = nullptr;
    TextLabel* _status = nullptr;
    std::vector<Widget*> _hierarchyIndex;
    std::vector<std::string> _styleIds;

    PathPicker _openPicker;
    PathPicker _savePicker;
    TitleUpdater _titleUpdater;
    UILayoutLoader _docLoader;
};

} // namespace ayt::ui
