cmake_minimum_required(VERSION 3.12)
include(TestGroups.cmake)

# Partition, do not duplicate: these cases run separately from fast domains.
set(AYUI_TIER_CASES
    "Layout|AYUI_LayoutLoader|test_layout_loader_hot_reload_via_file_watcher|integration"
    "Interaction|AYUI_UIManager|test_uimanager_reload_during_capture|integration"
    "Text|AYUI_I18n|I18n_RejectsHugeJSON|stress"
    "Controls|AYUI_TreeView|treeview_flatten_1000_nodes_under_5ms|stress")

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
    string(TOLOWER "${group}" group_label)
    set(fast_args)
    foreach(entry IN LISTS AYUI_TIER_CASES)
        string(REPLACE "|" ";" fields "${entry}")
        list(GET fields 0 owner)
        if(NOT owner STREQUAL group)
            continue()
        endif()
        list(GET fields 1 suite)
        list(GET fields 2 case_name)
        list(GET fields 3 tier)
        list(APPEND fast_args --exclude-case "${suite}.${case_name}")
        set(execution_policy)
        if(tier STREQUAL "stress")
            # Performance bounds must not compete with other CTest work.
            set(execution_policy RUN_SERIAL)
        endif()
        ay_add_test(${binary} CASE_REPORT NAME "${binary}.${case_name}"
            SUITE "${suite}" ARGS --case "${case_name}"
            LABELS "ui;ui-full;ui-${tier};${tier};ui-${group_label}"
            TIMEOUT 60 ${execution_policy})
    endforeach()
    ay_add_test(${binary} CASE_REPORT ARGS ${fast_args}
        LABELS "ui;ui-full;ui-fast;unit;ui-${group_label}"
        TIMEOUT 30)
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
# Do not register the aggregate as well: CTest's domain partition covers every
# case exactly once. The compatibility executable is still available manually.
add_custom_target(AYUI_TestGroups DEPENDS ${AYUI_TEST_GROUP_TARGETS})

add_executable(AYUICurveEditorTest CurveEditorTest.cpp)
target_link_libraries(AYUICurveEditorTest PRIVATE AYUICurveEditor AYTest)
ay_add_test(AYUICurveEditorTest CASE_REPORT LABELS "ui;ui-full;ui-fast;unit;ui-authoring"
    TIMEOUT 30)
