#pragma once

#include "AYUI/Version.h"
#include "AYUI/ImageTexture.h"
#include "AYUI/LayoutEditor/LayoutAnimationTimelineView.h"
#include "AYEditorCommand/EditorCommandHistory.h"
#include "AYUI/LayoutEditor/LayoutComponentLibrary.h"
#include "AYUI/LayoutEditor/LayoutDocumentModel.h"
#include "AYUI/LayoutEditor/LayoutInteractionModel.h"
#include "AYUI/LayoutEditor/LayoutPreviewModel.h"
#include "AYUI/LayoutEditor/LayoutResponsiveModel.h"
#include "AYUI/LayoutEditor/LayoutResourceCatalog.h"
#include "AYUI/LayoutEditor/LayoutReuseLibrary.h"
#include "AYUI/LayoutEditor/LayoutSelectionModel.h"
#include "AYUI/LayoutEditor/LayoutStyleInspectorModel.h"
#include "AYUI/LayoutEditor/LayoutThemeEditorModel.h"
#include "AYUI/LayoutEditor/LayoutStructuredContentModel.h"
#include "AYUI/LayoutEditor/LayoutValidationModel.h"
#include "AYUI/UIAnimation.h"
#include "AYUI/UIManager.h"

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ayt::ui {

enum class LayoutEditKind {
    SnapshotFallback,
    Property,
    Insert,
    Delete,
    Reorder,
    Transform,
    Clipboard,
    Reusable,
    Responsive,
    Animation
};

struct LayoutEditorSnapshot {
    std::string json;
    std::vector<std::string> selectedIds;
    std::string primaryId;
    bool dirty = false;
};

class Button;
class ComboBox;
class ListView;
class Panel;
class TextInput;
class TextLabel;

struct LayoutProjectRefactorKind {
    enum class Seed {
        None,
        SelectedWidgetId,
        DocumentPath,
    };

    std::string id;
    std::wstring displayName;
    Seed seed = Seed::None;
};

struct LayoutProjectRefactorResult {
    bool succeeded = false;
    bool safe = false;
    std::size_t changedFiles = 0u;
    std::vector<std::wstring> details;
    std::string message;
};

// Product authoring core shared by AYUI_LayoutEditor and AYEditor hosts.
// One UIManager: chrome (layout_editor.ui.json) + document under canvas_host.
class LayoutEditorSession {
public:
    using PathPicker = std::function<std::string()>;
    using TexturePreviewLoader =
        std::function<ImageTextureHandle(const std::string& texturePath)>;
    using TextureResourceProvider =
        std::function<std::vector<LayoutTextureResource>()>;
    using TitleUpdater = std::function<void(const std::wstring& title)>;
    using DocumentStateUpdater =
        std::function<void(const std::string& path, bool dirty)>;
    using InteractionContractProvider =
        std::function<std::vector<LayoutControllerContract>()>;
    using ThemePathPicker = std::function<std::string()>;
    using ProjectWorkflowAction = std::function<bool(
        const std::string& layoutPath, std::string& message)>;
    using ProjectRefactorAction = std::function<LayoutProjectRefactorResult(
        const std::string& layoutPath, const std::string& kind,
        const std::string& oldValue, const std::string& newValue,
        bool apply)>;

    enum class AlignMode {
        Left, HCenter, Right,
        Top, VCenter, Bottom,
        DistributeH, DistributeV
    };

    enum class AnchorAxisMode {
        Start,
        Center,
        End,
        Stretch
    };

    enum class Mode { Edit, Interact };

    LayoutEditorSession();
    ~LayoutEditorSession();

    LayoutEditorSession(const LayoutEditorSession&) = delete;
    LayoutEditorSession& operator=(const LayoutEditorSession&) = delete;

    bool attach(UIManager& ui);
    bool attach(UIManager& ui, Widget* chromeRoot);
    void detach();
    void pumpDeferred(float deltaSeconds = 0.0f);
    // Re-resolve editor chrome after the host switches language. Authored
    // document content is intentionally excluded.
    void retranslateChrome();

    bool open(const std::string& path);
    bool save();
    bool saveAs(const std::string& path);
    // Writes the current in-memory layout without changing its document path,
    // dirty state, command history, or active preview mode.
    bool writeRecoveryCopy(const std::string& path) const;

    void select(Widget* widget, bool additive = false);
    void selectById(const std::string& id);
    void selectAll();
    void addWidget(const std::string& typeName);
    void deleteSelected();
    void refreshHierarchy();
    void applyProperty(const std::string& field, const std::wstring& value);

    void undo();
    void redo();
    bool canUndo() const { return _history.canUndo(); }
    bool canRedo() const { return _history.canRedo(); }
    void alignSelection(AlignMode mode);
    void setAnchorPreset(AnchorAxisMode horizontal,
                         AnchorAxisMode vertical,
                         bool snapWidgetToAnchor = false);
    void clearSelectedAnchors();
    void reorderSelected(int delta);

    void copySelection();
    void pasteClipboard();
    void duplicateSelection();
    void nudgeSelection(float dx, float dy);
    void toggleSnap();
    void setViewZoom(float zoom, const math::FVector2& pivotWorld);
    void setMode(Mode mode);
    Mode mode() const { return _mode; }
    bool isInteractionPreview() const { return _mode == Mode::Interact; }

    void setPreviewPreset(int index);
    void setPreviewSettings(const LayoutPreviewSettings& settings);
    const LayoutPreviewSettings& previewSettings() const {
        return _previewModel.settings();
    }
    void setSafeAreaVisible(bool visible);
    void refreshTextureResources();
    void setInteractionContracts(
        std::vector<LayoutControllerContract> controllers);
    void clearInteractionContracts();
    void setInteractionContractProvider(
        InteractionContractProvider provider);
    void refreshInteractionContracts();
    const LayoutInteractionGraphModel& interactionGraph() const {
        return _interactionGraph;
    }
    void refreshValidation();
    const std::vector<LayoutDiagnostic>& diagnostics() const {
        return _validationModel.diagnostics();
    }
    bool defineReusableBlock(const std::string& name);
    bool insertReusableBlock(const std::string& name);
    bool removeReusableBlock(const std::string& name);
    const LayoutReuseLibrary& reuseLibrary() const { return _reuseLibrary; }
    void setExternalComponentLibraryPath(std::string path);
    const std::string& externalComponentLibraryPath() const {
        return _externalComponentLibraryPath;
    }
    bool refreshExternalComponentLibrary();
    bool defineExternalComponent(const std::string& id,
                                 const std::string& displayName,
                                 const std::string& category,
                                 const std::string& description = {},
                                 const std::vector<std::string>& tags = {});
    bool insertExternalComponent(const std::string& id);
    bool removeExternalComponent(const std::string& id);
    const LayoutComponentLibrary& externalComponentLibrary() const {
        return _externalComponentLibrary;
    }
    bool openThemeDocument(const std::string& path);
    bool saveThemeDocument();
    const std::string& themeDocumentPath() const { return _themeDocumentPath; }
    const LayoutThemeEditorModel& themeEditorModel() const {
        return _themeEditorModel;
    }
    void setResponsiveVisibility(int breakpointIndex,
                                 ResponsiveVisibility visibility);
    void captureResponsiveAnchors(int breakpointIndex);
    void clearResponsiveRule(int breakpointIndex);
    bool createAnimationClip(const std::string& name);
    bool removeAnimationClip(int clipIndex);
    bool addAnimationTrack(int clipIndex, UIAnimationProperty property);
    bool removeAnimationTrack(int clipIndex, int trackIndex);
    bool captureAnimationKeyframe(int clipIndex, int trackIndex,
                                  float timeMs, AnimationCurve curve);
    bool setAnimationKeyframeCurve(
        int clipIndex, int trackIndex, int keyframeIndex,
        AnimationCurve curve,
        const CubicBezierParameters& bezier = {},
        const SpringParameters& spring = {});
    bool removeAnimationKeyframe(int clipIndex, int trackIndex,
                                 int keyframeIndex);
    bool setAnimationPlayback(int clipIndex, int repeatCount, bool yoyo,
                              AnimationImportance importance);
    bool previewAnimationFrame(int clipIndex, float timeMs);
    bool playAnimationPreview(int clipIndex);
    void pauseAnimationPreview();
    void stopAnimationPreview();
    void advanceAnimationPreview(float deltaSeconds);
    void setAnimationPreviewLoop(bool enabled);
    bool animationPreviewLoop() const { return _animationPreviewLoop; }
    bool isAnimationPreviewPlaying() const;
    bool isAnimationPreviewPaused() const;
    float animationPreviewTimeMs() const { return _animationPreviewTimeMs; }
    bool beginAnimationKeyframeDrag(int clipIndex, int trackIndex,
                                    int keyframeIndex);
    int updateAnimationKeyframeDrag(float timeMs);
    void endAnimationKeyframeDrag();
    bool isAnimationPreviewing() const {
        return _animationPreviewBaseline.has_value();
    }
    const UIAnimationLibrary& animationLibrary() const {
        return _animationLibrary;
    }

    // button: 0=LMB, 1=RMB, 2=MMB
    bool onPointerDown(const math::FVector2& worldPos, int button = 0);
    bool onPointerMove(const math::FVector2& worldPos);
    bool onPointerUp(const math::FVector2& worldPos, int button = 0);
    bool onWheel(const math::FVector2& worldPos, float deltaY);

    bool onKeyDown(int uiKeyCode);
    void onKeyUp(int uiKeyCode);
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
    const math::FVector2& viewPan() const { return _viewPan; }
    bool snapEnabled() const { return _snapEnabled; }
    float gridSize() const { return _gridSize; }

    void setOpenPathPicker(PathPicker picker) { _openPicker = std::move(picker); }
    void setSavePathPicker(PathPicker picker) { _savePicker = std::move(picker); }
    void setTexturePathPicker(PathPicker picker) {
        _texturePicker = std::move(picker);
    }
    void setTexturePreviewLoader(TexturePreviewLoader loader);
    void setTextureResourceProvider(TextureResourceProvider provider);
    void setThemePathPicker(ThemePathPicker picker) {
        _themePicker = std::move(picker);
    }
    void setOpenOwningFlowAction(ProjectWorkflowAction action) {
        _openOwningFlowAction = std::move(action);
    }
    void setCompleteFlowSignalsAction(ProjectWorkflowAction action) {
        _completeFlowSignalsAction = std::move(action);
    }
    void setProjectRefactorKinds(
        std::vector<LayoutProjectRefactorKind> kinds);
    void setProjectRefactorAction(ProjectRefactorAction action) {
        _projectRefactorAction = std::move(action);
        syncProjectRefactorEditor();
    }
    void setTitleUpdater(TitleUpdater updater) { _titleUpdater = std::move(updater); }
    void setDocumentStateUpdater(DocumentStateUpdater updater) {
        _documentStateUpdater = std::move(updater);
    }

private:
    class SnapshotHistoryCommand;
    class CallbackHistoryCommand;
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

    using Snapshot = LayoutEditorSnapshot;

    struct HierDropTarget {
        Widget* target = nullptr;
        DropPlace place = DropPlace::Before;
        int listIndex = -1;
        bool valid = false;
    };

    void wireChrome();
    Widget* findChromeById(const std::string& id) const;
    void clearDocument();
    void setDocumentRoot(Widget* root);
    void ensureEmptyDocument();
    void ensureCanvasViewport();
    void syncCanvasViewportGeometry();
    void applyViewportTransform();
    math::FVector2 documentToScreen(const math::FVector2& point) const;
    math::FVector2 screenToDocument(const math::FVector2& point) const;
    math::FRectangle documentToScreen(const math::FRectangle& bounds) const;
    void freezeDocumentInteraction(Widget* root);
    void restoreDocumentInteraction(Widget* root);
    void rehydrateRuntimePresentation(Widget* root);
    void setStatus(const std::wstring& text);
    std::wstring localizedText(std::string_view key,
                               std::wstring_view fallback) const;
    void syncPropertyStrip();
    void updatePropPanelVisibility();
    void setChromeVisible(const char* id, bool visible);
    void setChromeEnabled(const char* id, bool enabled);
    void syncHierarchySelection();
    void syncHierarchySummary();
    bool hierarchyFilterActive() const;
    void syncStyleCombo();
    void syncStyleInspector();
    void syncTextAlignCombos();
    void syncEnumCombos();
    void ensureSchemaPropertyChrome();
    void bindSchemaPropertyFields();
    void syncSchemaPropertyFields();
    std::wstring schemaPropertyValue(const std::string& field) const;
    std::wstring schemaPropertyValueForWidget(
        Widget* widget, const std::string& field) const;
    std::wstring commonPropertyValue(const std::string& field) const;
    void refreshWindowTitle();
    void bindBoolCombo(ComboBox*& slot, const char* id, const char* field);
    void bindGravityCombo();
    void markDirty(bool dirty = true);
    bool isUnderCanvas(Widget* widget) const;
    Widget* pickDocumentWidget(const math::FVector2& worldPos) const;
    Widget* findInDocument(const std::string& id) const;
    Widget* pickParentForAdd() const;
    void collectAuthoredChildren(Widget* node,
                                 std::vector<Widget*>& out) const;
    void collectHierarchy(Widget* node, int depth,
                          std::vector<std::wstring>& labels,
                          std::vector<Widget*>& widgets) const;
    void updateContainerHint();
    std::string makeUniqueId(const std::string& prefix,
                             Widget* detachedRoot = nullptr) const;
    bool setTextPayload(Widget* widget, const std::wstring& text);
    bool getTextPayload(Widget* widget, std::wstring& out) const;
    void chooseTexture();
    void clearTexture();
    void applyTextureName(const std::string& textureName);
    void syncTextureBrowser();
    void syncStructuredEditor();
    void structuredAdd(bool asChild);
    void structuredRemove();
    void structuredMove(int delta);
    void commitStructuredText();
    void commitStructuredRichStyle();
    void syncPreviewControls();
    void commitPreviewFields();
    void applyPreviewToDocument();
    void syncPreviewChrome();
    void syncReuseEditor();
    void syncExternalComponentEditor();
    bool saveExternalComponentLibrary();
    void syncThemeEditor(const std::string& preferredToken = {},
                         const std::string& preferredStyle = {});
    void applyThemeToken();
    void openThemeTokenColorPicker();
    void renameThemeToken();
    void removeThemeToken();
    void applyThemeStyleBinding();
    void createThemeStyle();
    void duplicateThemeStyle();
    void removeThemeStyle();
    void syncProjectRefactorEditor();
    void seedProjectRefactorValue();
    void runProjectRefactor(bool apply);
    void revealProjectRefactorEditor();
    void syncResponsiveEditor();
    void syncAnimationEditor();
    void syncAnimationTimelineView();
    void syncAnimationTimelineTrack(int trackIndex);
    void syncAnimationTransportStatus();
    void syncAnimationCurveParameterVisibility(AnimationCurve curve,
                                               bool hasKeyframe);
    void commitAnimationCurveParameters();
    void ensureAnimationTimelineView();
    void syncAnimationTimelineGeometry();
    void relayoutAfterAnimationSample();
    void previewAnimationKeyframeDrag(float timeMs);
    bool captureAnimationPreviewBaseline(int clipIndex);
    void cancelAnimationKeyframeDrag();
    float animationDisplayDurationMs(const UIAnimationClip* clip) const;
    void previewResponsiveBreakpoint(int breakpointIndex);
    math::FVector2 authoredRootSize() const;
    bool previewOverridesDocumentSize() const;
    void bindPropField(const char* id, const char* field, TextInput*& slot,
                       bool numericScrub);
    void commitPropField(const std::string& field, TextInput* slot);
    bool validateWidgetId(const std::string& candidate, Widget* edited,
                          std::wstring* reason = nullptr) const;

    void beginMutation(LayoutEditKind kind = LayoutEditKind::Property,
                       const char* label = nullptr);
    void endMutation();
    struct PropertyValueChange {
        std::string widgetId;
        std::string field;
        std::wstring beforeValue;
        std::wstring afterValue;
    };
    struct PendingPropertyMutation {
        std::string field;
        std::string label;
        std::vector<PropertyValueChange> changes;
        bool dirtyBefore = false;
    };
    void beginPropertyMutation(const std::string& field,
                               const char* label = nullptr);
    void endPropertyMutation();
    bool propertyMutationOpen() const {
        return _pendingPropertyMutation.has_value();
    }
    std::wstring propertyValueForWidget(
        Widget* widget, const std::string& field) const;
    void applyTypedPropertyChanges(
        const std::vector<PropertyValueChange>& changes,
        bool useAfterValues, bool dirtyState);
    void pushUndo(LayoutEditKind kind = LayoutEditKind::SnapshotFallback,
                  const char* label = nullptr);
    Snapshot captureSnapshot();
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
    bool canUseAnchorLayout(Widget* widget) const;
    void refreshAnchorOffsets(Widget* widget);
    void syncAnchorPresetStyles();
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
    Widget* structuredContentOwner(Widget* content) const;
    bool isAncestorOf(Widget* ancestor, Widget* node) const;
    int siblingIndexOf(Widget* child) const;
    bool detachFromTree(Widget* child);
    bool attachAt(Widget* parent, Widget* child, size_t index);
    Widget* createWidgetInstance(const std::string& typeName);
    bool placeNewWidget(Widget* created, Widget* parent, int insertIndex,
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
    std::vector<std::string> serializeSelectionItems() const;
    void pasteSerializedItems(const std::vector<std::string>& items);

    // Phase 2 core models are authoritative. References keep the mature
    // interaction code source-compatible while storage and invariants move
    // out of the Session monolith.
    LayoutDocumentModel _documentModel;
    Widget*& _docRoot;
    std::string& _documentPath;
    bool& _dirty;
    LayoutSelectionModel _selectionModel;
    Widget*& _selected;
    std::vector<Widget*>& _selection;
    ayt::editor::EditorCommandHistory _history;
    bool _mutationOpen = false;
    std::optional<PendingPropertyMutation> _pendingPropertyMutation;

    UIManager* _ui = nullptr;
    Widget* _canvasHost = nullptr;
    Widget* _canvasViewport = nullptr;
    Widget* _inspectorSelection = nullptr;
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

    ToolDrag _toolDrag = ToolDrag::None;
    std::string _paletteType;
    Widget* _hierDragWidget = nullptr;
    math::FVector2 _toolPressPos{0.0f, 0.0f};
    static constexpr float kDragThreshold = 6.0f;

    Panel* _selBox = nullptr;
    Panel* _handles[8] = {};
    Panel* _anchorBox = nullptr;
    Panel* _anchorPoints[4] = {};
    std::vector<Panel*> _selOutlines;
    Panel* _marqueeBox = nullptr;
    Panel* _safeAreaBox = nullptr;
    static constexpr float kHandleSize = 8.0f;

    Widget* _hierarchyCol = nullptr;
    Panel* _hierDropLine = nullptr;
    Panel* _hierDropInto = nullptr;

    Widget* _chromeRoot = nullptr;
    Panel* _paletteGhost = nullptr;
    TextLabel* _paletteGhostLabel = nullptr;

    float _viewZoom = 1.0f;
    math::FVector2 _viewPan{0.0f, 0.0f};
    bool _snapEnabled = true;
    bool _spaceDown = false;
    float _gridSize = 8.0f;
    std::vector<std::string> _clipboardItems;

    LayoutStructuredContentModel _structuredModel;
    LayoutResourceCatalog _textureCatalog;
    LayoutStyleInspectorModel _styleInspectorModel;
    LayoutInteractionRegistry _interactionRegistry;
    LayoutInteractionGraphModel _interactionGraph;
    bool _interactionContractsConfigured = false;
    LayoutValidationModel _validationModel;
    LayoutPreviewModel _previewModel;
    LayoutResponsiveModel _responsiveModel;
    LayoutReuseLibrary _reuseLibrary;
    LayoutComponentLibrary _externalComponentLibrary;
    std::string _externalComponentLibraryPath;
    LayoutThemeEditorModel _themeEditorModel;
    std::string _themeDocumentPath;
    bool _themeDocumentDirty = false;
    UIAnimationLibrary _animationLibrary;
    Mode _mode = Mode::Edit;
    std::optional<Snapshot> _interactionSnapshot;
    struct AnimationPreviewValue {
        std::string targetId;
        UIAnimationProperty property = UIAnimationProperty::Opacity;
        math::FVector4 value{0.0f, 0.0f, 0.0f, 0.0f};
    };
    std::optional<std::vector<AnimationPreviewValue>>
        _animationPreviewBaseline;
    std::optional<AnimationTimeline> _animationRuntimePreview;
    int _animationPreviewClipIndex = -1;
    float _animationPreviewTimeMs = 0.0f;
    bool _animationPreviewLoop = false;
    bool _animationKeyDragActive = false;
    bool _animationKeyDragChanged = false;
    std::optional<Snapshot> _animationKeyDragSnapshot;
    int _animationKeyDragClip = -1;
    int _animationKeyDragTrack = -1;
    int _animationKeyDragKey = -1;
    int _animationKeyDragOriginalKey = -1;
    math::FVector2 _authoredRootSize{640.0f, 480.0f};

    struct InteractionState {
        bool enabled = true;
        bool readOnly = false;
        bool hasReadOnly = false;
    };
    std::unordered_map<Widget*, InteractionState> _interactionStates;

    DeferredAction _deferred = DeferredAction::None;

    ListView* _hierarchy = nullptr;
    TextInput* _hierarchySearch = nullptr;
    TextLabel* _hierarchySummary = nullptr;
    std::size_t _hierarchyMatchCount = 0u;
    TextInput* _propId = nullptr;
    TextInput* _propX = nullptr;
    TextInput* _propY = nullptr;
    TextInput* _propW = nullptr;
    TextInput* _propH = nullptr;
    TextInput* _propAnchorMinX = nullptr;
    TextInput* _propAnchorMinY = nullptr;
    TextInput* _propAnchorMaxX = nullptr;
    TextInput* _propAnchorMaxY = nullptr;
    TextInput* _propOffsetMinX = nullptr;
    TextInput* _propOffsetMinY = nullptr;
    TextInput* _propOffsetMaxX = nullptr;
    TextInput* _propOffsetMaxY = nullptr;
    TextInput* _propPivotX = nullptr;
    TextInput* _propPivotY = nullptr;
    TextInput* _propText = nullptr;
    TextInput* _propTexture = nullptr;
    TextInput* _propItems = nullptr;
    TextInput* _propController = nullptr;
    TextInput* _propOnClick = nullptr;
    TextInput* _propOnToggled = nullptr;
    TextInput* _propOnValueChanged = nullptr;
    TextInput* _propOnTextChanged = nullptr;
    TextInput* _propOnSubmit = nullptr;
    TextInput* _propOnSelectionChanged = nullptr;
    TextInput* _propOnItemActivated = nullptr;
    TextInput* _propOnClose = nullptr;
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
    std::unordered_map<std::string, TextInput*> _schemaPropertyInputs;
    std::unordered_map<std::string, ComboBox*> _schemaPropertyCombos;
    TextLabel* _status = nullptr;
    ListView* _structuredList = nullptr;
    TextInput* _structuredText = nullptr;
    TextInput* _structuredFontSize = nullptr;
    TextInput* _structuredColor = nullptr;
    bool _suppressStructured = false;
    ListView* _textureResourceList = nullptr;
    TextInput* _textureSearch = nullptr;
    TextLabel* _textureStatus = nullptr;
    bool _suppressTextureResources = false;
    ComboBox* _stylePreviewStateCombo = nullptr;
    Panel* _stylePreviewSwatch = nullptr;
    TextLabel* _styleSourceStatus = nullptr;
    TextLabel* _styleColorStatus = nullptr;
    StyleState _stylePreviewState = StyleState::Normal;
    ListView* _validationList = nullptr;
    TextLabel* _validationStatus = nullptr;
    bool _suppressValidation = false;
    bool _validationDirty = true;
    std::vector<std::wstring> _validationLabels;
    ListView* _interactionGraphList = nullptr;
    TextLabel* _interactionGraphStatus = nullptr;
    bool _suppressInteractionGraph = false;
    std::vector<std::wstring> _interactionGraphLabels;
    ComboBox* _previewPreset = nullptr;
    ComboBox* _previewDpi = nullptr;
    TextInput* _previewWidth = nullptr;
    TextInput* _previewHeight = nullptr;
    TextInput* _previewSafeL = nullptr;
    TextInput* _previewSafeT = nullptr;
    TextInput* _previewSafeR = nullptr;
    TextInput* _previewSafeB = nullptr;
    bool _suppressPreview = false;
    ListView* _reuseList = nullptr;
    TextInput* _reuseName = nullptr;
    TextLabel* _reuseStatus = nullptr;
    bool _suppressReuse = false;
    ListView* _externalComponentList = nullptr;
    TextInput* _externalComponentSearch = nullptr;
    ComboBox* _externalComponentCategoryFilter = nullptr;
    TextInput* _externalComponentId = nullptr;
    TextInput* _externalComponentDisplayName = nullptr;
    TextInput* _externalComponentCategory = nullptr;
    TextInput* _externalComponentDescription = nullptr;
    TextInput* _externalComponentTags = nullptr;
    TextLabel* _externalComponentStatus = nullptr;
    std::vector<std::size_t> _externalComponentFilteredIndices;
    bool _suppressExternalComponents = false;
    ListView* _themeTokenList = nullptr;
    TextInput* _themeTokenKey = nullptr;
    TextInput* _themeTokenValue = nullptr;
    Button* _themeTokenSwatch = nullptr;
    ListView* _themeStyleList = nullptr;
    TextInput* _themeStyleFragment = nullptr;
    TextInput* _themeStyleId = nullptr;
    ComboBox* _themeStyleProperty = nullptr;
    TextInput* _themeStyleBinding = nullptr;
    TextLabel* _themeEditorStatus = nullptr;
    bool _suppressThemeEditor = false;
    ComboBox* _projectRefactorKind = nullptr;
    TextInput* _projectRefactorOld = nullptr;
    TextInput* _projectRefactorNew = nullptr;
    ListView* _projectRefactorPreview = nullptr;
    TextLabel* _projectRefactorStatus = nullptr;
    bool _suppressProjectRefactor = false;
    std::vector<LayoutProjectRefactorKind> _projectRefactorKinds;
    ComboBox* _responsiveBreakpoint = nullptr;
    ComboBox* _responsiveVisibility = nullptr;
    TextLabel* _responsiveStatus = nullptr;
    bool _suppressResponsive = false;
    int _responsiveAuthoringIndex = 0;
    ListView* _animationClipList = nullptr;
    TextInput* _animationClipName = nullptr;
    ListView* _animationTrackList = nullptr;
    ComboBox* _animationProperty = nullptr;
    ListView* _animationKeyList = nullptr;
    TextInput* _animationTime = nullptr;
    ComboBox* _animationCurve = nullptr;
    TextInput* _animationBezierX1 = nullptr;
    TextInput* _animationBezierY1 = nullptr;
    TextInput* _animationBezierX2 = nullptr;
    TextInput* _animationBezierY2 = nullptr;
    TextInput* _animationSpringMass = nullptr;
    TextInput* _animationSpringStiffness = nullptr;
    TextInput* _animationSpringDamping = nullptr;
    TextInput* _animationSpringVelocity = nullptr;
    ComboBox* _animationSpringClamp = nullptr;
    TextInput* _animationRepeat = nullptr;
    ComboBox* _animationYoyo = nullptr;
    ComboBox* _animationImportance = nullptr;
    TextLabel* _animationStatus = nullptr;
    TextLabel* _animationTransportStatus = nullptr;
    Widget* _animationTimelineHost = nullptr;
    LayoutAnimationTimelineView* _animationTimelineView = nullptr;
    bool _suppressAnimation = false;
    int _animationClipIndex = -1;
    int _animationTrackIndex = -1;
    int _animationKeyIndex = -1;
    std::vector<Widget*> _hierarchyIndex;
    std::vector<std::string> _styleIds;

    PathPicker _openPicker;
    PathPicker _savePicker;
    PathPicker _texturePicker;
    TexturePreviewLoader _texturePreviewLoader;
    TextureResourceProvider _textureResourceProvider;
    ThemePathPicker _themePicker;
    ProjectWorkflowAction _openOwningFlowAction;
    ProjectWorkflowAction _completeFlowSignalsAction;
    ProjectRefactorAction _projectRefactorAction;
    InteractionContractProvider _interactionContractProvider;
    TitleUpdater _titleUpdater;
    DocumentStateUpdater _documentStateUpdater;
    UILayoutLoader _docLoader;
};

} // namespace ayt::ui
