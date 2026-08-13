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
#include <windowsx.h>
#include <commdlg.h>

#include "LayoutEditorSession.h"

#include "AYUI.h"
#include "AYUIManager.h"
#include "AYUIRenderBackend.h"
#include "AYRenderer.h"
#include "AYRenderTypes.h"
#include "AYTheme.h"
#include "AYDeviceManager.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {

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

struct AppState {
    ayt::ui::UIManager* ui = nullptr;
    ayt::ui::LayoutEditorSession* session = nullptr;
    ayt::render::Renderer* renderer = nullptr;
    ayt::render::UIRenderBackend* uiBackend = nullptr;
    ayt::device::DeviceManager* devices = nullptr;
    int clientW = kWidth;
    int clientH = kHeight;
    bool running = true;
};

std::intptr_t handleMessage(AppState* state, unsigned msg, std::uintptr_t wParam,
                            std::intptr_t lParam, bool& handled) {
    handled = false;
    if (state == nullptr || state->ui == nullptr) {
        return 0;
    }
    ayt::ui::UIManager& ui = *state->ui;

    switch (msg) {
    case WM_SIZE: {
        state->clientW = LOWORD(lParam);
        state->clientH = HIWORD(lParam);
        if (state->clientW < 32) {
            state->clientW = 32;
        }
        if (state->clientH < 32) {
            state->clientH = 32;
        }
        ui.setClientSize(static_cast<float>(state->clientW),
                         static_cast<float>(state->clientH));
        if (state->renderer != nullptr) {
            state->renderer->resize(static_cast<uint32_t>(state->clientW),
                                   static_cast<uint32_t>(state->clientH));
        }
        if (state->uiBackend != nullptr) {
            state->uiBackend->setFramebufferSize(
                static_cast<uint16_t>(state->clientW),
                static_cast<uint16_t>(state->clientH));
        }
        handled = true;
        return 0;
    }
    case WM_MOUSEMOVE: {
        ui.onMouseMove(static_cast<float>(GET_X_LPARAM(lParam)),
                       static_cast<float>(GET_Y_LPARAM(lParam)));
        return 0;
    }
    case WM_LBUTTONDOWN: {
        const float x = static_cast<float>(GET_X_LPARAM(lParam));
        const float y = static_cast<float>(GET_Y_LPARAM(lParam));
        ui.onMouseButtonDown(x, y, 0);
        if (state->session != nullptr) {
            state->session->onCanvasClick(ayt::math::FVector2(x, y));
        }
        handled = true;
        return 0;
    }
    case WM_LBUTTONUP: {
        ui.onMouseButtonUp(static_cast<float>(GET_X_LPARAM(lParam)),
                           static_cast<float>(GET_Y_LPARAM(lParam)), 0);
        handled = true;
        return 0;
    }
    case WM_RBUTTONDOWN: {
        ui.onMouseButtonDown(static_cast<float>(GET_X_LPARAM(lParam)),
                             static_cast<float>(GET_Y_LPARAM(lParam)), 1);
        handled = true;
        return 0;
    }
    case WM_RBUTTONUP: {
        ui.onMouseButtonUp(static_cast<float>(GET_X_LPARAM(lParam)),
                           static_cast<float>(GET_Y_LPARAM(lParam)), 1);
        handled = true;
        return 0;
    }
    case WM_MOUSEWHEEL: {
        constexpr float kPixelsPerNotch = 40.0f;
        const short raw = static_cast<short>(HIWORD(wParam));
        const float deltaY =
            -(static_cast<float>(raw) / static_cast<float>(WHEEL_DELTA)) * kPixelsPerNotch;
        POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (state->devices != nullptr) {
            HWND hwnd = static_cast<HWND>(state->devices->window().getWindowHandle());
            if (hwnd != nullptr) {
                ::ScreenToClient(hwnd, &pt);
            }
        }
        ui.onMouseWheel(static_cast<float>(pt.x), static_cast<float>(pt.y), deltaY);
        handled = true;
        return 0;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        ui.onKeyDown(static_cast<int>(wParam));
        handled = true;
        return 0;
    }
    case WM_KEYUP:
    case WM_SYSKEYUP: {
        ui.onKeyUp(static_cast<int>(wParam));
        handled = true;
        return 0;
    }
    default:
        break;
    }
    return 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    AllocConsole();
    FILE* dummy = nullptr;
    freopen_s(&dummy, "CONOUT$", "w", stdout);
    freopen_s(&dummy, "CONOUT$", "w", stderr);

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
    state.devices = &devices;
    state.clientW = kWidth;
    state.clientH = kHeight;

    ui.onTextEditingFocusChanged = [&devices](bool editing) {
        devices.textInput().setEnabled(editing);
    };
    devices.textInput().onCommit = [&ui](const std::string& chunk) {
        if (!chunk.empty()) {
            ui.onDeviceChar(chunk.data(), static_cast<int>(chunk.size()));
        }
    };
    devices.textInput().onCompositionUpdate =
        [&ui, &devices](const std::string& text, int caret) {
            if (text.empty() && !devices.textInput().isComposing()) {
                ui.onDeviceCompositionEnd("");
                return;
            }
            ui.onDeviceCompositionUpdate(text, caret);
        };

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
    window.setWindowMessageCallback(
        [&state](unsigned msg, std::uintptr_t wParam, std::intptr_t lParam,
                 bool& handled) -> std::intptr_t {
            return handleMessage(&state, msg, wParam, lParam, handled);
        });

    std::fprintf(stderr, "[AYUI_LayoutEditor] chrome=%s\n", chromePath.c_str());

    LARGE_INTEGER qpcFreq{};
    LARGE_INTEGER qpcPrev{};
    ::QueryPerformanceFrequency(&qpcFreq);
    ::QueryPerformanceCounter(&qpcPrev);

    while (state.running && window.isWindowValid()) {
        devices.pollEvents();

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
    uiBackend.shutdown();
    renderer.shutdown();
    devices.shutdown();
    return 0;
}
