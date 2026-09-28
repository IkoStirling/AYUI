cmake_minimum_required(VERSION 3.12)
include(TestGroups.cmake)

set(AYUI_TEST_OBJECT_TARGETS)
set(AYUI_TEST_GROUP_TARGETS)
set(AYUI_OWNED_TEST_SOURCES)
foreach(group IN LISTS AYUI_TEST_GROUPS)
    set(objects AYUI_Test_${group})
    set(binary AYUI_${group}Tests)
    add_library(${objects} OBJECT ${AYUI_TEST_${group}_SOURCES})
    # Direct object-library linkage keeps static test registrars alive. Do not
    # place tests in an ordinary static archive or enable a unity build here.
    set_target_properties(${objects} PROPERTIES UNITY_BUILD OFF)
    target_link_libraries(${objects} PUBLIC AYUI AYTest)
    if(group STREQUAL "LayoutAuthoring")
        target_link_libraries(${objects} PUBLIC AYUILayoutEditorCore)
    elseif(group STREQUAL "Packaging")
        target_link_libraries(${objects} PUBLIC AYUIPackaging)
    endif()
    target_include_directories(${objects} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../Controls
        ${CMAKE_CURRENT_SOURCE_DIR}/../Events
        ${CMAKE_CURRENT_SOURCE_DIR}/../Layout
        ${CMAKE_CURRENT_SOURCE_DIR}/../Loader
        ${CMAKE_CURRENT_SOURCE_DIR}/../Style
        ${CMAKE_CURRENT_SOURCE_DIR}/../i18n)
    target_compile_definitions(${objects} PRIVATE
        AYUI_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")
    add_executable(${binary} main.cpp)
    target_compile_definitions(${binary} PRIVATE AYUI_TEST_MODULE="${binary}")
    target_link_libraries(${binary} PRIVATE ${objects})
    list(APPEND AYUI_TEST_OBJECT_TARGETS ${objects})
    list(APPEND AYUI_TEST_GROUP_TARGETS ${binary})
    list(APPEND AYUI_OWNED_TEST_SOURCES ${AYUI_TEST_${group}_SOURCES})
endforeach()

# Fail configuration if a source is omitted, duplicated, or no longer exists.
file(GLOB discovered CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/Test_*.cpp")
list(SORT discovered)
list(SORT AYUI_OWNED_TEST_SOURCES)
if(NOT "${discovered}" STREQUAL "${AYUI_OWNED_TEST_SOURCES}")
    message(FATAL_ERROR "UI test source ownership differs from TestGroups.cmake; assign each source exactly once")
endif()

# Compatibility executable: same test inventory, now independently compiled.
add_executable(AYUI_UnitTests main.cpp)
target_link_libraries(AYUI_UnitTests PRIVATE ${AYUI_TEST_OBJECT_TARGETS})
ay_add_test(AYUI_UnitTests)
add_custom_target(AYUI_TestGroups DEPENDS ${AYUI_TEST_GROUP_TARGETS})

add_executable(AYUICurveEditorTest CurveEditorTest.cpp)
target_link_libraries(AYUICurveEditorTest PRIVATE AYUICurveEditor AYTest)
ay_add_test(AYUICurveEditorTest)
