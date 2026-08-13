# AYUI/demo — standalone visual gallery (UI-only, no 3D scene).

set(AY_SHADER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../AYShader")

list(APPEND CMAKE_PREFIX_PATH
    "${AY_SHADER_ROOT}/thirdParty/bgfx-install/debug"
    "${AY_SHADER_ROOT}/thirdParty/bgfx-install/release"
)
find_package(bgfx CONFIG REQUIRED)

if(NOT DEFINED AY_SHADER_SHADERC_PATH)
    if(WIN32)
        set(_ays_shaderc_name "shaderc.exe")
    else()
        set(_ays_shaderc_name "shaderc")
    endif()
    set(AY_SHADER_SHADERC_PATH
        "${AY_SHADER_ROOT}/thirdParty/bgfx-install/debug/bin/${_ays_shaderc_name}")
endif()

if(NOT DEFINED AY_SHADER_BGFX_COMMON_DIR)
    set(_ays_bgfx_search
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../thirdparty/bgfx/examples/common"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/bgfx/examples/common"
    )
    set(AY_SHADER_BGFX_COMMON_DIR "")
    foreach(_dir IN LISTS _ays_bgfx_search)
        if(EXISTS "${_dir}/common.sh")
            set(AY_SHADER_BGFX_COMMON_DIR "${_dir}")
            break()
        endif()
    endforeach()
endif()
if(NOT DEFINED AY_SHADER_BGFX_SRC_DIR AND AY_SHADER_BGFX_COMMON_DIR)
    set(AY_SHADER_BGFX_SRC_DIR "${AY_SHADER_BGFX_COMMON_DIR}/../../src")
endif()

get_filename_component(AY_SHADER_SHADERC_PATH "${AY_SHADER_SHADERC_PATH}" ABSOLUTE)

add_executable(AYUI_Gallery WIN32
    AYUI_Gallery.cpp
    GalleryChildBackend.h
    GalleryChildBackend.cpp
    GalleryChildWindows.h
    GalleryChildWindows.cpp
)

target_link_libraries(AYUI_Gallery PRIVATE
    AYUI
    AYRenderer
    AYDevice
)

if(WIN32)
    target_link_libraries(AYUI_Gallery PRIVATE user32 gdi32)
endif()

if(MSVC)
    target_compile_options(AYUI_Gallery PRIVATE /Zc:__cplusplus /Zc:preprocessor)
    target_compile_definitions(AYUI_Gallery PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

target_compile_definitions(AYUI_Gallery PRIVATE
    AY_SHADER_SHADERC_HINT="${AY_SHADER_SHADERC_PATH}"
    AY_SHADER_BGFX_COMMON_HINT="${AY_SHADER_BGFX_COMMON_DIR}"
    AY_SHADER_BGFX_SRC_HINT="${AY_SHADER_BGFX_SRC_DIR}"
)

add_custom_command(TARGET AYUI_Gallery POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:AYUI_Gallery>/assets
    COMMENT "Copy AYUI Gallery assets next to AYUI_Gallery"
)

# ---------------------------------------------------------------------------
# AYUI_LayoutEditor — standalone *.ui.json layout authoring (v0)
# ---------------------------------------------------------------------------
add_executable(AYUI_LayoutEditor WIN32
    layout_editor/AYUI_LayoutEditor.cpp
    layout_editor/LayoutEditorSession.cpp
    layout_editor/LayoutEditorSession.h
)

target_include_directories(AYUI_LayoutEditor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/layout_editor
)

target_link_libraries(AYUI_LayoutEditor PRIVATE
    AYUI
    AYRenderer
    AYDevice
)

if(WIN32)
    target_link_libraries(AYUI_LayoutEditor PRIVATE user32 gdi32 comdlg32)
endif()

if(MSVC)
    target_compile_options(AYUI_LayoutEditor PRIVATE /Zc:__cplusplus /Zc:preprocessor)
    target_compile_definitions(AYUI_LayoutEditor PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

target_compile_definitions(AYUI_LayoutEditor PRIVATE
    AY_SHADER_SHADERC_HINT="${AY_SHADER_SHADERC_PATH}"
    AY_SHADER_BGFX_COMMON_HINT="${AY_SHADER_BGFX_COMMON_DIR}"
    AY_SHADER_BGFX_SRC_HINT="${AY_SHADER_BGFX_SRC_DIR}"
)

add_custom_command(TARGET AYUI_LayoutEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/layout_editor/assets
        $<TARGET_FILE_DIR:AYUI_LayoutEditor>/assets
    COMMENT "Copy Layout Editor assets next to AYUI_LayoutEditor"
)

# Headless open→mutate→save→reload check (no HWND).
add_executable(AYUI_LayoutEditor_RoundTrip
    layout_editor/_roundtrip_check.cpp
    layout_editor/LayoutEditorSession.cpp
    layout_editor/LayoutEditorSession.h
)
target_include_directories(AYUI_LayoutEditor_RoundTrip PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/layout_editor
)
target_link_libraries(AYUI_LayoutEditor_RoundTrip PRIVATE AYUI)
if(MSVC)
    target_compile_options(AYUI_LayoutEditor_RoundTrip PRIVATE /Zc:__cplusplus /Zc:preprocessor)
    target_compile_definitions(AYUI_LayoutEditor_RoundTrip PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
add_custom_command(TARGET AYUI_LayoutEditor_RoundTrip POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/layout_editor/assets
        $<TARGET_FILE_DIR:AYUI_LayoutEditor_RoundTrip>/assets
)
