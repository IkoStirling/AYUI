cmake_minimum_required(VERSION 3.12)
project(AYUI)

set(MODULE_NAME AYUI)

set(SRC_FILES
    Controls/AYButton.cpp
    Controls/AYCheckBox.cpp
    Controls/AYRadioButton.cpp
    Controls/AYSlider.cpp
    Controls/AYProgressBar.cpp
    Controls/AYFocusableWidget.cpp
    Controls/AYTextInput.cpp
    Controls/AYTextArea.cpp
    Controls/AYScrollableWidget.cpp
    Controls/AYScrollBar.cpp
    Controls/AYScrollView.cpp
    Controls/AYImage.cpp
    Controls/AYInteractiveWidget.cpp
    Controls/AYListView.cpp
    Controls/AYComboBox.cpp
    Controls/AYTabControl.cpp
    Controls/AYPanel.cpp
    Controls/AYTextLabel.cpp
    Controls/AYWidget.cpp
    Controls/AYWindow.cpp
    Controls/AYUIManager.cpp
    Layout/AYBox.cpp
    Layout/AYSplitterHandle.cpp
    Layout/AYGridPanel.cpp
    Loader/AYLayoutLoader.cpp
    Loader/AYWidgetFactory.cpp
    Loader/AYWidgetSerializer.cpp
    Style/AYMockRenderer.cpp
    Style/AYStyle.cpp
    i18n/AYI18n.cpp
)

set(HEAD_FILES
    AYUI.h
    Events/AYEvent.h
    Controls/AYButton.h
    Controls/AYCheckBox.h
    Controls/AYRadioButton.h
    Controls/AYSlider.h
    Controls/AYProgressBar.h
    Controls/AYFocusableWidget.h
    Controls/AYTextInput.h
    Controls/AYTextArea.h
    Controls/AYScrollableWidget.h
    Controls/AYScrollBar.h
    Controls/AYScrollView.h
    Controls/AYImage.h
    Controls/AYInteractiveWidget.h
    Controls/AYLeafWidget.h
    Controls/AYListView.h
    Controls/AYComboBox.h
    Controls/AYTabControl.h
    Controls/AYSelectableWidget.h
    Controls/AYPanel.h
    Controls/AYTextLabel.h
    Controls/AYWidget.h
    Controls/AYUIManager.h
    Controls/AYWindow.h
    Layout/AYBox.h
    Layout/AYSplitterHandle.h
    Layout/AYGridPanel.h
    Loader/AYLayoutLoader.h
    Loader/AYWidgetFactory.h
    Loader/AYWidgetSerializer.h
    Style/AYMockRenderer.h
    Style/AYStyle.h
    Style/AYThickness.h
    Style/AYTextContent.h
    i18n/AYI18n.h
)

set(AYUI_NLOHMANN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty_)

find_package(nlohmann_json QUIET CONFIG)

add_library(${MODULE_NAME} STATIC ${SRC_FILES} ${HEAD_FILES})
target_include_directories(${MODULE_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Controls
    ${CMAKE_CURRENT_SOURCE_DIR}/Events
    ${CMAKE_CURRENT_SOURCE_DIR}/Layout
    ${CMAKE_CURRENT_SOURCE_DIR}/Loader
    ${CMAKE_CURRENT_SOURCE_DIR}/Style
    ${CMAKE_CURRENT_SOURCE_DIR}/i18n
    ${AYUI_NLOHMANN_DIR}
)

if(nlohmann_json_FOUND)
    target_link_libraries(${MODULE_NAME} PUBLIC AYMath AYTest AYFont PRIVATE nlohmann_json::nlohmann_json)
else()
    target_link_libraries(${MODULE_NAME} PUBLIC AYMath AYTest AYFont)
    message(STATUS "AYUI: using bundled nlohmann/json from ${AYUI_NLOHMANN_DIR}")
endif()

# R-4: AYUI's hot-reload uses ayio::FileWatcher (ReadDirectoryChangesW on
# Windows, inotify on POSIX). Linked PRIVATE — FileWatcher is not exposed in
# AYUI's public surface.
target_link_libraries(${MODULE_NAME} PRIVATE AYIO)

enable_testing()
add_subdirectory(unittest)
