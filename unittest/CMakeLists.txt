cmake_minimum_required(VERSION 3.12)

set(MODULE_NAME AYUI_UnitTests)

add_executable(${MODULE_NAME} main.cpp)
target_link_libraries(${MODULE_NAME} PRIVATE AYUI AYTest)
target_include_directories(${MODULE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../Controls
    ${CMAKE_CURRENT_SOURCE_DIR}/../Events
    ${CMAKE_CURRENT_SOURCE_DIR}/../Layout
    ${CMAKE_CURRENT_SOURCE_DIR}/../Loader
    ${CMAKE_CURRENT_SOURCE_DIR}/../Style
    ${CMAKE_CURRENT_SOURCE_DIR}/../i18n
)

add_test(NAME ${MODULE_NAME} COMMAND ${MODULE_NAME})
