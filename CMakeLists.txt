cmake_minimum_required(VERSION 3.12)
project(AYUI)

set(MODULE_NAME AYUI)

set(SRC_FILES
    Controls/AYButton.cpp
    Controls/AYImage.cpp
    Controls/AYTextLabel.cpp
    Controls/AYWidget.cpp
    Controls/AYWindow.cpp
    Layout/AYBox.cpp
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
    Controls/AYImage.h
    Controls/AYTextLabel.h
    Controls/AYWidget.h
    Controls/AYWindow.h
    Layout/AYBox.h
    Loader/AYLayoutLoader.h
    Loader/AYWidgetFactory.h
    Loader/AYWidgetSerializer.h
    Style/AYMockRenderer.h
    Style/AYStyle.h
    i18n/AYI18n.h
)

find_package(nlohmann_json REQUIRED)

add_library(${MODULE_NAME} STATIC ${SRC_FILES} ${HEAD_FILES})
target_include_directories(${MODULE_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Controls
    ${CMAKE_CURRENT_SOURCE_DIR}/Events
    ${CMAKE_CURRENT_SOURCE_DIR}/Layout
    ${CMAKE_CURRENT_SOURCE_DIR}/Loader
    ${CMAKE_CURRENT_SOURCE_DIR}/Style
    ${CMAKE_CURRENT_SOURCE_DIR}/i18n
)

target_link_libraries(${MODULE_NAME} PUBLIC AYMath AYTest AYFont PRIVATE nlohmann_json::nlohmann_json)

enable_testing()
add_subdirectory(unittest)
