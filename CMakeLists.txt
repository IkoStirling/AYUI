cmake_minimum_required(VERSION 3.12)
project(AYUI
    VERSION 1.1.0
    DESCRIPTION "AYUI — retained-mode engine UI, data-driven layouts, docking, styling and animation"
)

set(MODULE_NAME AYUI)

set(SRC_FILES
    Controls/AYAnimation.cpp
    Controls/AYUIAnimation.cpp
    Controls/AYButton.cpp
    Controls/AYCheckBox.cpp
    Controls/AYRadioButton.cpp
    Controls/AYRadioGroup.cpp
    Controls/AYSlider.cpp
    Controls/AYColorPicker.cpp
    Controls/AYProgressBar.cpp
    Controls/AYFocusableWidget.cpp
    Controls/AYTextInput.cpp
    Controls/AYTextArea.cpp
    Controls/AYScrollableWidget.cpp
    Controls/AYScrollBar.cpp
    Controls/AYScrollView.cpp
    Controls/AYImage.cpp
    Controls/AYSvgIcon.cpp
    Controls/AYTextureRegistry.cpp
    Controls/AYInteractiveWidget.cpp
    Controls/AYSelectableWidget.cpp
    Controls/AYTooltip.cpp
    Controls/AYPopupAnchor.cpp
    Controls/AYSeparator.cpp
    Controls/AYMenuItem.cpp
    Controls/AYMenu.cpp
    Controls/AYMenuBar.cpp
    Controls/AYToolBar.cpp
    Controls/AYToolBarSeparator.cpp
    Controls/AYStatusBar.cpp
    Controls/AYListView.cpp
    Controls/AYTileView.cpp
    Controls/AYComboBox.cpp
    Controls/AYTabControl.cpp
    Controls/AYTypeaheadBuffer.cpp
    Controls/AYCompoundFocusableWidget.cpp
    Controls/AYPanel.cpp
    Controls/AYTextLabel.cpp
    Controls/AYTreeNode.cpp
    Controls/AYTreeView.cpp
    Controls/AYRichText.cpp
    Controls/AYWidget.cpp
    Controls/AYDisplayList.cpp
    Controls/AYWindow.cpp
    Controls/AYModal.cpp
    Controls/AYModalDialog.cpp
    Controls/AYDimmer.cpp
    Controls/AYClipboard.cpp
    Controls/AYClipboard_Win32.cpp
    Controls/AYClipboard_Posix.cpp
    Controls/AYTabStrip.cpp
    Controls/AYSpinner.cpp
    Controls/AYDockArea.cpp
    Controls/AYDockCard.cpp
    Controls/AYDockOverlay.cpp
    Controls/AYDockTabGroup.cpp
    Controls/AYDockTrace.cpp
    Controls/AYUIManager.cpp
    Controls/AYDeviceInputBridge.cpp
    Controls/AYAccessibility.cpp
    Controls/AYAccessibilityAdapter.cpp
    Controls/AYUnicodeText.cpp
    Layout/AYBox.cpp
    Layout/AYSplitterHandle.cpp
    Layout/AYGridPanel.cpp
    Layout/AYConstraint.cpp
    Loader/AYLayoutLoader.cpp
    Loader/AYUIFlow.cpp
    Loader/AYUIFlowGraphNodeRegistry.cpp
    Loader/AYWidgetFactory.cpp
    Loader/AYWidgetSerializer.cpp
    Style/AYMockRenderer.cpp
    Style/AYStyle.cpp
    Style/AYTheme.cpp
    i18n/AYI18n.cpp
    Controls/UIKeyCode.cpp
)

set(HEAD_FILES
    AYUI.h
    include/AYUI/Version.h
    include/AYUI/Animation.h
    include/AYUI/UIAnimation.h
    include/AYUI/Event.h
    include/AYUI/Accessibility.h
    include/AYUI/AccessibilityAdapter.h
    include/AYUI/UnicodeText.h
    include/AYUI/BoxSlotLimits.h
    include/AYUI/DragDrop.h
    include/AYUI/DockTrace.h
    include/AYUI/Button.h
    include/AYUI/CheckBox.h
    include/AYUI/RadioButton.h
    include/AYUI/RadioGroup.h
    include/AYUI/Slider.h
    include/AYUI/ColorPicker.h
    include/AYUI/ProgressBar.h
    include/AYUI/FocusableWidget.h
    include/AYUI/TextInput.h
    include/AYUI/ImeTypes.h
    include/AYUI/TextArea.h
    include/AYUI/ScrollableWidget.h
    include/AYUI/ScrollBar.h
    include/AYUI/ScrollBarSync.h
    include/AYUI/ScrollView.h
    include/AYUI/Image.h
    include/AYUI/SvgIcon.h
    include/AYUI/ImageTexture.h
    include/AYUI/TextureRegistry.h
    include/AYUI/InteractiveWidget.h
    include/AYUI/LeafWidget.h
    include/AYUI/ListView.h
    include/AYUI/TileView.h
    include/AYUI/ComboBox.h
    include/AYUI/TabControl.h
    include/AYUI/TypeaheadBuffer.h
    include/AYUI/CompoundFocusableWidget.h
    include/AYUI/SelectableWidget.h
    include/AYUI/Tooltip.h
    include/AYUI/PopupAnchor.h
    include/AYUI/Separator.h
    include/AYUI/MenuItem.h
    include/AYUI/Menu.h
    include/AYUI/MenuBar.h
    include/AYUI/ToolBar.h
    include/AYUI/ToolBarSeparator.h
    include/AYUI/StatusBar.h
    include/AYUI/Panel.h
    include/AYUI/TextLabel.h
    include/AYUI/TreeNode.h
    include/AYUI/TreeView.h
    include/AYUI/RichText.h
    include/AYUI/Widget.h
    include/AYUI/DisplayList.h
    include/AYUI/UIManager.h
    include/AYUI/DeviceInputBridge.h
    include/AYUI/Window.h
    include/AYUI/Modal.h
    include/AYUI/ModalDialog.h
    include/AYUI/Dimmer.h
    include/AYUI/Clipboard.h
    include/AYUI/TabStrip.h
    include/AYUI/Spinner.h
    include/AYUI/Tween.h
    include/AYUI/DockArea.h
    include/AYUI/DockCard.h
    include/AYUI/DockOverlay.h
    include/AYUI/DockTabGroup.h
    include/AYUI/Box.h
    include/AYUI/SplitterHandle.h
    include/AYUI/GridPanel.h
    include/AYUI/Constraint.h
    include/AYUI/ValueWidget.h
    include/AYUI/LayoutLoader.h
    include/AYUI/UIFlow.h
    include/AYUI/UIFlowGraphNodeRegistry.h
    include/AYUI/WidgetFactory.h
    include/AYUI/WidgetSerializer.h
    include/AYUI/MockRenderer.h
    include/AYUI/Style.h
    include/AYUI/Theme.h
    include/AYUI/Thickness.h
    include/AYUI/TextContent.h
    include/AYUI/TextMeasure.h
    include/AYUI/I18n.h
    include/AYUI/UIKeyCode.h
    interface/AYUI/IRenderBackend.h
)

set(AYUI_NLOHMANN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty_)

find_package(nlohmann_json QUIET CONFIG)

add_library(${MODULE_NAME} STATIC ${SRC_FILES} ${HEAD_FILES})
target_compile_definitions(${MODULE_NAME} PUBLIC AYUI_SOURCE_ABI_VERSION=127)
target_include_directories(${MODULE_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/interface
        ${AYUI_NLOHMANN_DIR}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Controls
        ${CMAKE_CURRENT_SOURCE_DIR}/Events
        ${CMAKE_CURRENT_SOURCE_DIR}/Layout
        ${CMAKE_CURRENT_SOURCE_DIR}/Loader
        ${CMAKE_CURRENT_SOURCE_DIR}/Style
        ${CMAKE_CURRENT_SOURCE_DIR}/i18n
)

if(nlohmann_json_FOUND)
    target_link_libraries(${MODULE_NAME} PUBLIC AYMath AYFont AYDevice PRIVATE nlohmann_json::nlohmann_json)
else()
    target_link_libraries(${MODULE_NAME} PUBLIC AYMath AYFont AYDevice)
    message(STATUS "AYUI: using bundled nlohmann/json from ${AYUI_NLOHMANN_DIR}")
endif()

# R-4: AYUI's hot-reload uses ayio::FileWatcher (ReadDirectoryChangesW on
# Windows, inotify on POSIX). Linked PRIVATE — FileWatcher is not exposed in
# AYUI's public surface.
target_link_libraries(${MODULE_NAME} PRIVATE AYIO)

if(WIN32)
    # Native Unicode itemization, bidi levels and UAX #14 line-break data.
    target_link_libraries(${MODULE_NAME} PRIVATE usp10 uiautomationcore oleaut32 user32)
endif()

# Module-owned, headless package contribution. The project packager supplies
# only root *.ui.json / *.theme.json assets; AYUI owns parsing its wire formats
# and discovering transitive texture and Theme inheritance dependencies. Keep
# this target independent from runtime Widgets, AYRenderer and authoring core.
add_library(AYUIPackaging STATIC
    Packaging/UIAssetCollector.cpp
    include/AYUI/Packaging/UIAssetCollector.h
)
target_include_directories(AYUIPackaging
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${AYUI_NLOHMANN_DIR}
)
if(nlohmann_json_FOUND)
    target_link_libraries(AYUIPackaging PRIVATE nlohmann_json::nlohmann_json)
endif()
if(MSVC)
    target_compile_options(AYUIPackaging PRIVATE
        /Zc:__cplusplus /Zc:preprocessor)
    target_compile_definitions(AYUIPackaging PRIVATE
        NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# Product-grade layout authoring core shared by the standalone Designer,
# AYEditor and headless regression tools. Keep it separate from AYUI runtime:
# games that only load .ui.json do not pay for editor models or chrome logic.
add_library(AYUILayoutEditorCore STATIC
    LayoutEditor/LayoutAnimationTimelineView.cpp
    LayoutEditor/LayoutCanvasViewport.cpp
    LayoutEditor/LayoutDocumentModel.cpp
    LayoutEditor/LayoutInteractionModel.cpp
    LayoutEditor/LayoutSelectionModel.cpp
    LayoutEditor/LayoutCommandStack.cpp
    LayoutEditor/LayoutComponentLibrary.cpp
    LayoutEditor/LayoutPreviewModel.cpp
    LayoutEditor/LayoutPublicationModel.cpp
    LayoutEditor/LayoutPropertyEditors.cpp
    LayoutEditor/LayoutResponsiveModel.cpp
    LayoutEditor/LayoutResourceCatalog.cpp
    LayoutEditor/LayoutReuseLibrary.cpp
    LayoutEditor/LayoutStyleInspectorModel.cpp
    LayoutEditor/LayoutThemeEditorModel.cpp
    LayoutEditor/LayoutStructuredContentModel.cpp
    LayoutEditor/LayoutValidationModel.cpp
    LayoutEditor/PropertySchema.cpp
    LayoutEditor/WidgetAuthoringRegistry.cpp
    LayoutEditor/LayoutEditorSession.cpp
    include/AYUI/LayoutEditor/LayoutAnimationTimelineView.h
    include/AYUI/LayoutEditor/LayoutEditorSession.h
    include/AYUI/LayoutEditor/LayoutCanvasViewport.h
    include/AYUI/LayoutEditor/LayoutDocumentModel.h
    include/AYUI/LayoutEditor/LayoutInteractionModel.h
    include/AYUI/LayoutEditor/LayoutSelectionModel.h
    include/AYUI/LayoutEditor/LayoutCommandStack.h
    include/AYUI/LayoutEditor/LayoutComponentLibrary.h
    include/AYUI/LayoutEditor/LayoutPreviewModel.h
    include/AYUI/LayoutEditor/LayoutPublicationModel.h
    include/AYUI/LayoutEditor/LayoutPropertyEditors.h
    include/AYUI/LayoutEditor/LayoutResponsiveModel.h
    include/AYUI/LayoutEditor/LayoutResourceCatalog.h
    include/AYUI/LayoutEditor/LayoutReuseLibrary.h
    include/AYUI/LayoutEditor/LayoutStyleInspectorModel.h
    include/AYUI/LayoutEditor/LayoutThemeEditorModel.h
    include/AYUI/LayoutEditor/LayoutStructuredContentModel.h
    include/AYUI/LayoutEditor/LayoutValidationModel.h
    include/AYUI/LayoutEditor/PropertySchema.h
    include/AYUI/LayoutEditor/WidgetAuthoringRegistry.h
)
target_include_directories(AYUILayoutEditorCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/LayoutEditor
)
target_link_libraries(AYUILayoutEditorCore PUBLIC AYUI)
if(MSVC)
    target_compile_options(AYUILayoutEditorCore PRIVATE
        /Zc:__cplusplus /Zc:preprocessor)
    target_compile_definitions(AYUILayoutEditorCore PRIVATE
        NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

enable_testing()
add_subdirectory(unittest)
add_subdirectory(demo)

message(STATUS "AYUI v${PROJECT_VERSION} — see ${CMAKE_CURRENT_SOURCE_DIR}/design.md")
