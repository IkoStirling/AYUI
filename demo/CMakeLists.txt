# AYUI/demo — standalone visual gallery (UI-only, no 3D scene).

set(AY_SHADER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../AYShader")
include("${AY_SHADER_ROOT}/cmake/AYBgfx.cmake")

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
)

target_include_directories(AYUI_LayoutEditor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/layout_editor
)

target_link_libraries(AYUI_LayoutEditor PRIVATE
    AYUILayoutEditorCore
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

if(WIN32)
    add_custom_target(AYUI_DesignerGoldenRegression
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass
            -File ${CMAKE_CURRENT_SOURCE_DIR}/layout_editor/RunDesignerGoldenRegression.ps1
            -DesignerExe $<TARGET_FILE:AYUI_LayoutEditor>
            -OutputDir ${CMAKE_CURRENT_BINARY_DIR}/designer-golden-output
        DEPENDS AYUI_LayoutEditor
        USES_TERMINAL
        COMMENT "Run the real-window AYUI Designer golden regression"
    )
endif()

# Headless open→mutate→save→reload check (no HWND).
add_executable(AYUI_LayoutEditor_RoundTrip
    layout_editor/_roundtrip_check.cpp
)
target_include_directories(AYUI_LayoutEditor_RoundTrip PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/layout_editor
)
target_link_libraries(AYUI_LayoutEditor_RoundTrip PRIVATE AYUILayoutEditorCore)
if(MSVC)
    target_compile_options(AYUI_LayoutEditor_RoundTrip PRIVATE /Zc:__cplusplus /Zc:preprocessor)
    target_compile_definitions(AYUI_LayoutEditor_RoundTrip PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
add_custom_command(TARGET AYUI_LayoutEditor_RoundTrip POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/layout_editor/assets
        $<TARGET_FILE_DIR:AYUI_LayoutEditor_RoundTrip>/assets
)
