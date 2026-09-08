// AYUI_LayoutEditor.cpp — standalone layout authoring host (no 3D / no Editor shell).
// Mirrors AYUI_Gallery bootstrap: DeviceManager + Renderer + UIRenderBackend + UIManager.

#ifndef UNICODE
#  define UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <Windows.h>
#include <commdlg.h>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "AYUI/LayoutEditor/LayoutEditorSession.h"

#include "AYUI.h"
#include "AYUI/DeviceInputBridge.h"
#include "AYUI/UIManager.h"
#include "AYRenderer/UIRenderBackend.h"
#include "AYRenderer.h"
#include "AYRenderer/RenderTypes.h"
#include "AYUI/Theme.h"
#include "AYDevice/DeviceManager.h"
#include "AYUI/UIKeyCode.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <utility>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#  include <DbgHelp.h>
#  pragma comment(lib, "Dbghelp.lib")
#endif

namespace {

#if defined(_WIN32)
LONG WINAPI layoutEditorCrashFilter(EXCEPTION_POINTERS* info) {
    FILE* f = nullptr;
    if (fopen_s(&f, "layout_editor_crash.log", "w") == 0 && f != nullptr) {
        std::fprintf(f, "ExceptionCode=0x%08lX\n",
            static_cast<unsigned long>(info->ExceptionRecord->ExceptionCode));
        std::fprintf(f, "ExceptionAddress=%p\n",
            info->ExceptionRecord->ExceptionAddress);
        void* stack[62] = {};
        const USHORT n = CaptureStackBackTrace(0, 62, stack, nullptr);
        std::fprintf(f, "Stack (%u frames):\n", static_cast<unsigned>(n));
        for (USHORT i = 0; i < n; ++i) {
            std::fprintf(f, "  [%u] %p\n", static_cast<unsigned>(i), stack[i]);
        }
        std::fclose(f);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

constexpr int kWidth = 1280;
constexpr int kHeight = 720;

bool fileExists(const std::string& path) {
    struct stat st {};
    return !path.empty() && ::stat(path.c_str(), &st) == 0;
}

std::string resolveChromePath() {
    const std::vector<std::string> candidates = {
        "assets/layout_editor.ui.json",
        "AYRuntime/AYUI/demo/layout_editor/assets/layout_editor.ui.json",
        "../AYRuntime/AYUI/demo/layout_editor/assets/layout_editor.ui.json",
        "../../AYRuntime/AYUI/demo/layout_editor/assets/layout_editor.ui.json",
    };
    for (const std::string& path : candidates) {
        if (fileExists(path)) {
            return path;
        }
    }
    return candidates.front();
}

std::string resolveSamplePath() {
    const std::vector<std::string> candidates = {
        "assets/sample_blank.ui.json",
        "AYRuntime/AYUI/demo/layout_editor/assets/sample_blank.ui.json",
        "../AYRuntime/AYUI/demo/layout_editor/assets/sample_blank.ui.json",
        "../../AYRuntime/AYUI/demo/layout_editor/assets/sample_blank.ui.json",
    };
    for (const std::string& path : candidates) {
        if (fileExists(path)) {
            return path;
        }
    }
    return {};
}

std::string wideToUtf8Path(const wchar_t* w) {
    if (w == nullptr || w[0] == L'\0') {
        return {};
    }
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) {
        return {};
    }
    std::string out(static_cast<size_t>(n - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), n, nullptr, nullptr);
    return out;
}

std::string showOpenUiJsonDialog(HWND owner) {
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"AYUI Layout (*.ui.json)\0*.ui.json\0JSON (*.json)\0*.json\0All\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    ofn.lpstrDefExt = L"ui.json";
    if (!::GetOpenFileNameW(&ofn)) {
        return {};
    }
    return wideToUtf8Path(file);
}

std::string showSaveUiJsonDialog(HWND owner) {
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"AYUI Layout (*.ui.json)\0*.ui.json\0JSON (*.json)\0*.json\0All\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    ofn.lpstrDefExt = L"ui.json";
    if (!::GetSaveFileNameW(&ofn)) {
        return {};
    }
    return wideToUtf8Path(file);
}

std::string showOpenTextureDialog(HWND owner) {
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter =
        L"Images (*.png;*.jpg;*.jpeg;*.bmp;*.tga)\0*.png;*.jpg;*.jpeg;*.bmp;*.tga\0"
        L"All\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!::GetOpenFileNameW(&ofn)) return {};
    return wideToUtf8Path(file);
}

std::vector<ayt::ui::LayoutTextureResource> enumeratePreviewTextures() {
    namespace fs = std::filesystem;
    std::vector<ayt::ui::LayoutTextureResource> resources;
    const std::vector<fs::path> roots = {
        fs::path("assets"),
        fs::path("../AliyatRenderer/assets/core/textures"),
        fs::path("../../../../../../../AliyatRenderer/assets/core/textures")
    };
    std::error_code error;
    for (const fs::path& root : roots) {
        if (!fs::is_directory(root, error)) {
            error.clear();
            continue;
        }
        for (fs::recursive_directory_iterator it(
                 root, fs::directory_options::skip_permission_denied, error), end;
             it != end && resources.size() < 2048u; it.increment(error)) {
            if (error) {
                error.clear();
                continue;
            }
            if (!it->is_regular_file(error)) continue;
            std::string extension = it->path().extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char ch) {
                               return static_cast<char>(std::tolower(ch));
                           });
            if (extension != ".png" && extension != ".jpg" &&
                extension != ".jpeg" && extension != ".bmp" &&
                extension != ".tga") continue;
            ayt::ui::LayoutTextureResource resource;
            resource.key = it->path().lexically_normal().string();
            resource.displayName = it->path().filename().wstring();
            resources.push_back(std::move(resource));
        }
    }
    return resources;
}

ayt::ui::ImageTextureHandle loadPreviewTexture(
    const std::string& path, ayt::render::UIRenderBackend& backend,
    std::unordered_map<std::string, ayt::ui::ImageTextureHandle>& cache) {
    const auto found = cache.find(path);
    if (found != cache.end()) return found->second;

    int width = 0;
    int height = 0;
    int components = 0;
    stbi_uc* rgba = stbi_load(path.c_str(), &width, &height, &components, 4);
    if (rgba == nullptr || width <= 0 || height <= 0 || width > 65535 || height > 65535) {
        if (rgba != nullptr) stbi_image_free(rgba);
        return {};
    }
    const size_t byteCount = static_cast<size_t>(width)
        * static_cast<size_t>(height) * 4u;
    std::vector<uint8_t> bgra(rgba, rgba + byteCount);
    stbi_image_free(rgba);
    for (size_t i = 0; i < byteCount; i += 4u) {
        std::swap(bgra[i], bgra[i + 2u]);
    }
    void* handle = backend.createUiTexture(
        static_cast<uint16_t>(width), static_cast<uint16_t>(height), bgra.data());
    if (handle == nullptr) return {};
    ayt::ui::ImageTextureHandle result;
    result.handle = handle;
    result.width = width;
    result.height = height;
    result.name = path;
    cache.emplace(path, result);
    return result;
}

struct AppState {
    ayt::ui::UIManager* ui = nullptr;
    ayt::ui::LayoutEditorSession* session = nullptr;
    ayt::render::Renderer* renderer = nullptr;
    ayt::render::UIRenderBackend* uiBackend = nullptr;
    int clientW = kWidth;
    int clientH = kHeight;
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    bool running = true;
    std::unordered_map<std::string, ayt::ui::ImageTextureHandle> previewTextures;
};

void updateCursor(AppState& state, ayt::device::WindowManager& window) {
    ayt::ui::UiCursorHint hint = ayt::ui::UiCursorHint::Default;
    if (state.session != nullptr) {
        hint = state.session->canvasCursorHint(
            ayt::math::FVector2(state.mouseX, state.mouseY));
    }
    if (hint == ayt::ui::UiCursorHint::Default && state.ui != nullptr) {
        hint = state.ui->getCursorHint();
    }
    window.setCursorShape(ayt::ui::systemCursorFromUi(hint));
}

void resizeApp(AppState& state, int width, int height) {
    state.clientW = std::max(width, 32);
    state.clientH = std::max(height, 32);
    if (state.ui != nullptr) {
        state.ui->setClientSize(static_cast<float>(state.clientW),
                                static_cast<float>(state.clientH));
    }
    if (state.renderer != nullptr) {
        state.renderer->resize(static_cast<uint32_t>(state.clientW),
                               static_cast<uint32_t>(state.clientH));
    }
    if (state.uiBackend != nullptr) {
        state.uiBackend->setFramebufferSize(
            static_cast<uint16_t>(state.clientW),
            static_cast<uint16_t>(state.clientH));
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
#if defined(_WIN32)
    ::SetUnhandledExceptionFilter(layoutEditorCrashFilter);
#endif
    const bool reproShiftRoot =
        (std::wcsstr(::GetCommandLineW(), L"--repro-shift-root") != nullptr);

    if (!reproShiftRoot) {
        AllocConsole();
        FILE* dummy = nullptr;
        freopen_s(&dummy, "CONOUT$", "w", stdout);
        freopen_s(&dummy, "CONOUT$", "w", stderr);
    } else {
        FILE* log = nullptr;
        freopen_s(&log, "layout_editor_repro.log", "w", stderr);
        freopen_s(&log, "layout_editor_repro.log", "a", stdout);
    }

    ayt::device::DeviceManager devices;
    ayt::device::DeviceConfig cfg{};
    cfg.window.title = "AYUI Layout Editor";
    cfg.window.width = kWidth;
    cfg.window.height = kHeight;
    if (!devices.initialize(cfg)) {
        std::fprintf(stderr, "[AYUI_LayoutEditor] DeviceManager initialize failed\n");
        return 1;
    }

    ayt::device::WindowManager& window = devices.window();
    HWND hwnd = static_cast<HWND>(window.getWindowHandle());
    if (hwnd == nullptr) {
        std::fprintf(stderr, "[AYUI_LayoutEditor] no HWND\n");
        devices.shutdown();
        return 1;
    }

    ayt::render::Renderer renderer;
    ayt::render::InitDesc init{};
    init.windowHandle = hwnd;
    init.width = kWidth;
    init.height = kHeight;
    init.vsync = true;
    init.msaa = 0;
    if (!renderer.initialize(init)) {
        std::fprintf(stderr, "[AYUI_LayoutEditor] Renderer initialize failed\n");
        devices.shutdown();
        return 1;
    }

    ayt::render::UIRenderBackend uiBackend;
    if (!uiBackend.initialize(renderer)) {
        std::fprintf(stderr, "[AYUI_LayoutEditor] UIRenderBackend initialize failed\n");
        renderer.shutdown();
        devices.shutdown();
        return 1;
    }
    uiBackend.setFramebufferSize(static_cast<uint16_t>(kWidth),
                                 static_cast<uint16_t>(kHeight));

    ayt::ui::UIManager ui;
    ui.initialize(&uiBackend);
    ayt::ui::ThemeManager::get().ensureDefaultThemes();
    ayt::ui::ThemeManager::get().setActiveTheme("dark");

    AppState state{};
    state.ui = &ui;
    state.renderer = &renderer;
    state.uiBackend = &uiBackend;
    state.clientW = kWidth;
    state.clientH = kHeight;

    const std::string chromePath = resolveChromePath();
    if (!ui.loadLayout(chromePath)) {
        std::fprintf(stderr, "[AYUI_LayoutEditor] failed to load chrome: %s\n",
                     chromePath.c_str());
        ui.shutdown();
        uiBackend.shutdown();
        renderer.shutdown();
        devices.shutdown();
        return 1;
    }
    ui.setClientSize(static_cast<float>(kWidth), static_cast<float>(kHeight));

    ayt::ui::LayoutEditorSession session;
    state.session = &session;
    session.setOpenPathPicker([hwnd]() { return showOpenUiJsonDialog(hwnd); });
    session.setSavePathPicker([hwnd]() { return showSaveUiJsonDialog(hwnd); });
    session.setTexturePathPicker([hwnd]() { return showOpenTextureDialog(hwnd); });
    session.setTextureResourceProvider([]() { return enumeratePreviewTextures(); });
    session.setTexturePreviewLoader([&state, &uiBackend](const std::string& path) {
        return loadPreviewTexture(path, uiBackend, state.previewTextures);
    });
    session.setTitleUpdater([hwnd](const std::wstring& title) {
        if (hwnd != nullptr) {
            ::SetWindowTextW(hwnd, title.c_str());
        }
    });
    if (!session.attach(ui)) {
        std::fprintf(stderr, "[AYUI_LayoutEditor] session.attach failed\n");
        ui.shutdown();
        uiBackend.shutdown();
        renderer.shutdown();
        devices.shutdown();
        return 1;
    }

    const std::string sample = resolveSamplePath();
    if (!sample.empty()) {
        session.open(sample);
    }

    window.setWindowCloseCallback([&state]() { state.running = false; });
    window.setWindowResizeCallback([&state](int width, int height) {
        resizeApp(state, width, height);
    });
    window.setWindowFocusCallback([&ui, &session](bool focused) {
        if (focused) {
            return;
        }
        session.onKeyUp(ayt::ui::UIKey_Space);
        ui.onKeyUp(ayt::ui::UIKey_Shift);
        ui.onKeyUp(ayt::ui::UIKey_Control);
        ui.onKeyUp(ayt::ui::UIKey_Alt);
        ui.cancelDrag();
    });

    ayt::ui::DeviceInputBridge::Callbacks inputCallbacks{};
    inputCallbacks.onMouseMove = [&state, &ui, &session, &window](float x, float y) {
        state.mouseX = x;
        state.mouseY = y;
        const ayt::math::FVector2 pos(x, y);
        const bool handled = session.onPointerMove(pos) || ui.onMouseMove(x, y);
        updateCursor(state, window);
        return handled;
    };
    inputCallbacks.onMouseLeave = [&ui, &window]() {
        ui.onMouseLeave();
        window.setCursorShape(ayt::device::SystemCursorShape::Arrow);
    };
    inputCallbacks.onMouseButton =
        [&state, &ui, &session, &window](float x, float y, int button, bool pressed) {
            state.mouseX = x;
            state.mouseY = y;
            const ayt::math::FVector2 pos(x, y);
            bool handled = false;
            if (button == 0 || button == 2) {
                handled = pressed ? session.onPointerDown(pos, button)
                                  : session.onPointerUp(pos, button);
            }
            if (!handled) {
                handled = pressed ? ui.onMouseButtonDown(x, y, button)
                                  : ui.onMouseButtonUp(x, y, button);
            }
            updateCursor(state, window);
            return handled;
        };
    inputCallbacks.onMouseWheel = [&ui, &session](float x, float y, float deltaY) {
        const ayt::math::FVector2 pos(x, y);
        return session.onWheel(pos, deltaY) || ui.onMouseWheel(x, y, deltaY);
    };
    inputCallbacks.onKey = [&ui, &session](ayt::device::KeyCode key, bool pressed,
                                           bool /*repeat*/) {
        const int uiKey = static_cast<int>(ayt::ui::fromDeviceKey(key));
        if (pressed) {
            if (uiKey == ayt::ui::UIKey_Shift
                || uiKey == ayt::ui::UIKey_Control
                || uiKey == ayt::ui::UIKey_Alt) {
                return ui.onKeyDown(uiKey);
            }
            return session.onKeyDown(uiKey) || ui.onKeyDown(uiKey);
        }
        session.onKeyUp(uiKey);
        return ui.onKeyUp(uiKey);
    };
    inputCallbacks.onTextCommit = [&ui](const std::string& text) {
        return !text.empty()
            && ui.onDeviceChar(text.data(), static_cast<int>(text.size()));
    };
    inputCallbacks.onComposition = [&ui](ayt::device::DeviceInputEventType type,
                                          const std::string& text, int caret) {
        switch (type) {
        case ayt::device::DeviceInputEventType::CompositionStart:
            ui.onDeviceCompositionStart(text, caret);
            break;
        case ayt::device::DeviceInputEventType::CompositionUpdate:
            ui.onDeviceCompositionUpdate(text, caret);
            break;
        case ayt::device::DeviceInputEventType::CompositionEnd:
            ui.onDeviceCompositionEnd("");
            break;
        default:
            break;
        }
    };
    ayt::ui::DeviceInputBridge inputBridge(std::move(inputCallbacks));
    inputBridge.connect(devices);
    inputBridge.bindTextInputFocus(ui);
    updateCursor(state, window);

    std::fprintf(stderr, "[AYUI_LayoutEditor] chrome=%s\n", chromePath.c_str());

    LARGE_INTEGER qpcFreq{};
    LARGE_INTEGER qpcPrev{};
    ::QueryPerformanceFrequency(&qpcFreq);
    ::QueryPerformanceCounter(&qpcPrev);

    int reproPhase = reproShiftRoot ? 0 : -1;
    int reproFrames = 0;

    while (state.running && window.isWindowValid()) {
        devices.pollEvents();
        if (state.session != nullptr) {
            state.session->pumpDeferred();
        }

        if (reproPhase >= 0) {
            ++reproFrames;
            // Warm up a few frames, then mirror: select child → Shift-select root.
            if (reproPhase == 0 && reproFrames >= 5) {
                std::fprintf(stderr, "[repro] select btn_hello\n");
                session.selectById("btn_hello");
                reproPhase = 1;
                reproFrames = 0;
            } else if (reproPhase == 1 && reproFrames >= 5) {
                std::fprintf(stderr, "[repro] focus prop_text + hierarchy Shift-click root\n");
                if (ayt::ui::Widget* prop = ui.findById("prop_text")) {
                    ui.setFocus(prop);
                }
                ui.onKeyDown(ayt::ui::UIKey_Shift);
                ayt::ui::Widget* hier = ui.findById("list_hierarchy");
                if (hier != nullptr) {
                    const ayt::math::FRectangle hb = hier->getWorldBounds();
                    const ayt::math::FVector2 clickPos(
                        hb.minX + 40.0f, hb.minY + 12.0f);
                    std::fprintf(stderr, "[repro] pointer down/up at %.1f,%.1f\n",
                        clickPos.x, clickPos.y);
                    session.onPointerDown(clickPos, 0);
                    session.onPointerUp(clickPos, 0);
                    // Orphaned UIManager up (real path when up isn't consumed):
                    // previously UAF'd after ListView::rebuildRows deleted the
                    // hovered Row during the following layout pass.
                    ui.onMouseButtonUp(clickPos.x, clickPos.y, 0);
                } else {
                    session.select(session.documentRoot(), true);
                }
                ui.onKeyUp(ayt::ui::UIKey_Shift);
                std::fprintf(stderr, "[repro] after select sel=%zu primary=%s\n",
                    session.selection().size(),
                    session.selected() != nullptr
                        ? session.selected()->getId().c_str() : "(null)");
                reproPhase = 2;
                reproFrames = 0;
            } else if (reproPhase == 2) {
                if (reproFrames >= 10) {
                    std::fprintf(stderr, "[repro] OK — no crash\n");
                    std::fflush(stderr);
                    state.running = false;
                }
            }
        }

        LARGE_INTEGER qpcNow{};
        ::QueryPerformanceCounter(&qpcNow);
        float dt = static_cast<float>(
            static_cast<double>(qpcNow.QuadPart - qpcPrev.QuadPart) /
            static_cast<double>(qpcFreq.QuadPart));
        qpcPrev = qpcNow;
        if (dt < 0.0f) {
            dt = 0.0f;
        }
        if (dt > 0.1f) {
            dt = 0.1f;
        }

        ayt::render::ClearDesc clear;
        clear.r = 0.10f;
        clear.g = 0.10f;
        clear.b = 0.12f;
        clear.a = 1.0f;
        renderer.beginCompositeFrame(clear,
                                     static_cast<uint16_t>(state.clientW),
                                     static_cast<uint16_t>(state.clientH));
        uiBackend.setFramebufferSize(static_cast<uint16_t>(state.clientW),
                                     static_cast<uint16_t>(state.clientH));
        ui.update(dt);
        ui.layout();
        ui.populateFrame();
        ui.flushFrame();
        renderer.endFrame();
    }

    session.detach();
    ui.shutdown();
    for (const auto& [path, texture] : state.previewTextures) {
        (void)path;
        uiBackend.releaseUiTexture(texture.handle);
    }
    state.previewTextures.clear();
    uiBackend.shutdown();
    renderer.shutdown();
    devices.shutdown();
    return 0;
}
