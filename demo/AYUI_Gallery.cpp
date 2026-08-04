// AYUI_Gallery.cpp — standalone AYUI visual check (no 3D / no Editor shell).
//
// Host loop (path A):
//   DeviceManager window + input
//   Renderer (bgfx clear only via beginCompositeFrame)
//   UIRenderBackend + UIManager populate/flush
//
// Layout: assets/gallery.ui.json (copied next to the exe at build time).

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

#include "AYUI.h"
#include "AYUIManager.h"
#include "AYButton.h"
#include "AYCheckBox.h"
#include "AYSlider.h"
#include "AYProgressBar.h"
#include "AYTextLabel.h"
#include "AYTextInput.h"
#include "AYListView.h"
#include "AYComboBox.h"
#include "AYMenuBar.h"
#include "AYMenu.h"
#include "AYMenuItem.h"
#include "AYStatusBar.h"
#include "AYModalDialog.h"
#include "AYSeparator.h"
#include "AYWidget.h"

#include "AYUIRenderBackend.h"
#include "AYRenderer.h"
#include "AYRenderTypes.h"

#include "AYDeviceManager.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {

constexpr int kWidth  = 1280;
constexpr int kHeight = 720;

bool fileExists(const std::string& path)
{
    struct stat st {};
    return !path.empty() && ::stat(path.c_str(), &st) == 0;
}

std::string resolveLayoutPath()
{
    const std::vector<std::string> candidates = {
        "assets/gallery.ui.json",
        "AYRuntime/AYUI/demo/assets/gallery.ui.json",
        "../AYRuntime/AYUI/demo/assets/gallery.ui.json",
        "../../AYRuntime/AYUI/demo/assets/gallery.ui.json",
    };
    for (const std::string& path : candidates) {
        if (fileExists(path)) {
            return path;
        }
    }
    return candidates.front();
}

struct GalleryState {
    ayt::ui::UIManager* ui = nullptr;
    ayt::render::Renderer* renderer = nullptr;
    ayt::render::UIRenderBackend* uiBackend = nullptr;
    ayt::device::DeviceManager* devices = nullptr;
    int clientW = kWidth;
    int clientH = kHeight;
    bool running = true;
    // Durable path for Reload JSON — must NOT live only inside the
    // button's onClicked lambda: loadLayout destroys that button (and
    // the lambda) mid-callback, leaving a dangling std::string& for
    // ifstream::open.
    std::string layoutPath;

    int clickCount = 0;
    std::unique_ptr<ayt::ui::ModalDialog> modal;
    std::unique_ptr<ayt::ui::TextLabel> modalBody;
};

void showPage(ayt::ui::UIManager& ui, const char* pageId)
{
    static const char* kPages[] = {
        "page_basics", "page_input", "page_collections",
        "page_overlay", "page_layout",
    };
    for (const char* id : kPages) {
        if (ayt::ui::Widget* w = ui.findById(id)) {
            w->setVisible(std::strcmp(id, pageId) == 0);
        }
    }
    // Visibility changes which VBox fill slot owns content_host — force a
    // layout pass so the newly shown page gets real width/height.
    ui.clearDragStateNoDispatch(nullptr);
    ui.invalidateLayout();
    ui.layout();
    if (auto* status = dynamic_cast<ayt::ui::StatusBar*>(ui.findById("status"))) {
        // Keep a single status panel updated.
        if (status->getPanelCount() == 0) {
            status->addPanel(L"section: basics");
        }
        if (ayt::ui::TextLabel* lbl = status->getPanel(0)) {
            std::wstring msg = L"section: ";
            if (std::strcmp(pageId, "page_basics") == 0) msg += L"basics";
            else if (std::strcmp(pageId, "page_input") == 0) msg += L"input";
            else if (std::strcmp(pageId, "page_collections") == 0) msg += L"collections";
            else if (std::strcmp(pageId, "page_overlay") == 0) msg += L"overlay";
            else if (std::strcmp(pageId, "page_layout") == 0) msg += L"layout";
            lbl->setText(msg);
        }
    }
}

void wireGallery(GalleryState& state)
{
    ayt::ui::UIManager& ui = *state.ui;

    if (auto* sep = dynamic_cast<ayt::ui::Separator*>(ui.findById("nav_content_sep"))) {
        sep->setOrientation(ayt::ui::Separator::Orientation::Vertical);
        sep->setColor(ayt::math::FVector4(0.35f, 0.35f, 0.38f, 1.0f));
    }

    auto bindNav = [&ui](const char* btnId, const char* pageId) {
        if (auto* btn = dynamic_cast<ayt::ui::Button*>(ui.findById(btnId))) {
            btn->setOnClicked([&ui, pageId]() { showPage(ui, pageId); });
        }
    };
    bindNav("nav_basics", "page_basics");
    bindNav("nav_input", "page_input");
    bindNav("nav_collections", "page_collections");
    bindNav("nav_overlay", "page_overlay");
    bindNav("nav_layout", "page_layout");

    // --- Basics ---
    if (auto* btn = dynamic_cast<ayt::ui::Button*>(ui.findById("btn_disabled"))) {
        btn->setEnabled(false);
    }
    if (auto* btn = dynamic_cast<ayt::ui::Button*>(ui.findById("btn_demo"))) {
        btn->setOnClicked([&state]() {
            ++state.clickCount;
            if (auto* lbl = dynamic_cast<ayt::ui::TextLabel*>(
                    state.ui->findById("lbl_click_count"))) {
                wchar_t buf[64];
                std::swprintf(buf, 64, L"clicks: %d", state.clickCount);
                lbl->setText(buf);
            }
        });
    }

    // --- Input ---
    if (auto* sld = dynamic_cast<ayt::ui::Slider*>(ui.findById("sld_demo"))) {
        sld->setValue(0.5f);
        sld->setOnValueChanged([&ui](float v) {
            if (auto* lbl = dynamic_cast<ayt::ui::TextLabel*>(ui.findById("lbl_slider"))) {
                wchar_t buf[64];
                std::swprintf(buf, 64, L"Slider  %.2f", v);
                lbl->setText(buf);
            }
            if (auto* prg = dynamic_cast<ayt::ui::ProgressBar*>(ui.findById("prg_demo"))) {
                prg->setValue(v);
            }
        });
    }
    if (auto* prg = dynamic_cast<ayt::ui::ProgressBar*>(ui.findById("prg_demo"))) {
        prg->setValue(0.5f);
    }
    if (auto* chk = dynamic_cast<ayt::ui::CheckBox*>(ui.findById("chk_demo"))) {
        chk->setOnToggled([&ui](bool on) {
            if (auto* lbl = dynamic_cast<ayt::ui::TextLabel*>(ui.findById("lbl_check_state"))) {
                lbl->setText(on ? L"checkbox: on" : L"checkbox: off");
            }
        });
    }

    // --- Collections ---
    if (auto* list = dynamic_cast<ayt::ui::ListView*>(ui.findById("lst_demo"))) {
        list->addItem(L"Alpha");
        list->addItem(L"Bravo");
        list->addItem(L"Charlie");
        list->addItem(L"Delta");
        list->addItem(L"Echo");
        list->setOnSelectionChanged([&ui](int idx) {
            if (auto* lbl = dynamic_cast<ayt::ui::TextLabel*>(ui.findById("lbl_selection"))) {
                wchar_t buf[64];
                std::swprintf(buf, 64, L"selection: list[%d]", idx);
                lbl->setText(buf);
            }
        });
    }
    if (auto* cmb = dynamic_cast<ayt::ui::ComboBox*>(ui.findById("cmb_demo"))) {
        cmb->addItem(L"Red");
        cmb->addItem(L"Green");
        cmb->addItem(L"Blue");
        cmb->setOnSelectionChanged([&ui](int idx) {
            if (auto* lbl = dynamic_cast<ayt::ui::TextLabel*>(ui.findById("lbl_selection"))) {
                wchar_t buf[64];
                std::swprintf(buf, 64, L"selection: combo[%d]", idx);
                lbl->setText(buf);
            }
        });
    }

    // --- Overlay / MenuBar ---
    if (auto* bar = dynamic_cast<ayt::ui::MenuBar*>(ui.findById("menubar"))) {
        ayt::ui::Menu* file = bar->addMenu(L"File");
        if (file != nullptr) {
            if (ayt::ui::MenuItem* open = file->addItem(L"Open...")) {
                open->setOnActivate([&ui]() {
                    if (auto* lbl = dynamic_cast<ayt::ui::TextLabel*>(
                            ui.findById("lbl_menu_action"))) {
                        lbl->setText(L"menu: File -> Open");
                    }
                });
            }
            if (ayt::ui::MenuItem* save = file->addItem(L"Save")) {
                save->setOnActivate([&ui]() {
                    if (auto* lbl = dynamic_cast<ayt::ui::TextLabel*>(
                            ui.findById("lbl_menu_action"))) {
                        lbl->setText(L"menu: File -> Save");
                    }
                });
            }
        }
        ayt::ui::Menu* help = bar->addMenu(L"Help");
        if (help != nullptr) {
            if (ayt::ui::MenuItem* about = help->addItem(L"About Gallery")) {
                about->setOnActivate([&ui]() {
                    if (auto* lbl = dynamic_cast<ayt::ui::TextLabel*>(
                            ui.findById("lbl_menu_action"))) {
                        lbl->setText(L"menu: Help -> About");
                    }
                });
            }
        }
    }

    if (auto* btn = dynamic_cast<ayt::ui::Button*>(ui.findById("btn_modal"))) {
        btn->setOnClicked([&state]() {
            if (state.modal == nullptr) {
                state.modal = std::make_unique<ayt::ui::ModalDialog>();
                state.modalBody = std::make_unique<ayt::ui::TextLabel>();
                state.modalBody->setText(L"Standalone ModalDialog - OK / Cancel / Esc");
                state.modalBody->setSize(ayt::math::FVector2(360.0f, 40.0f));
                state.modal->setBodyContent(state.modalBody.get());
                state.modal->setOnResult([&state](int result) {
                    if (auto* lbl = dynamic_cast<ayt::ui::TextLabel*>(
                            state.ui->findById("lbl_modal"))) {
                        lbl->setText(result == ayt::ui::ModalDialog::Ok
                                         ? L"modal: OK"
                                         : L"modal: Cancel");
                    }
                });
                state.modal->setOnClose([&state]() {
                    if (auto* lbl = dynamic_cast<ayt::ui::TextLabel*>(
                            state.ui->findById("lbl_modal"))) {
                        // OK already wrote via setOnResult; Cancel/Esc leave
                        // "modal: open" — report Cancel for button dismiss.
                        if (lbl->getText() == L"modal: open") {
                            lbl->setText(L"modal: Cancel");
                        } else if (lbl->getText().find(L"idle") != std::wstring::npos) {
                            lbl->setText(L"modal: dismissed");
                        }
                    }
                });
            }
            // Modal::openModal (not UIManager::openModal alone) mounts the
            // dimmer, sets focus, and runs layout — required for OK/Cancel.
            state.modal->openModal();
            const ayt::math::FVector2 vp = state.ui->getClientSize();
            const ayt::math::FVector2 sz = state.modal->getSize();
            state.modal->setLayoutPositionManaged(false);
            state.modal->setPosition(ayt::math::FVector2(
                std::max(0.0f, (vp.x - sz.x) * 0.5f),
                std::max(0.0f, (vp.y - sz.y) * 0.5f)));
            state.modal->performLayout();
            if (auto* lbl = dynamic_cast<ayt::ui::TextLabel*>(
                    state.ui->findById("lbl_modal"))) {
                lbl->setText(L"modal: open");
            }
        });
    }

    // --- Layout dock ping ---
    if (auto* btn = dynamic_cast<ayt::ui::Button*>(ui.findById("dock_left_btn"))) {
        btn->setOnClicked([&ui]() {
            if (auto* lbl = dynamic_cast<ayt::ui::TextLabel*>(ui.findById("lbl_dock_ping"))) {
                lbl->setText(L"dock: Left Ping");
            }
        });
    }
    if (auto* sld = dynamic_cast<ayt::ui::Slider*>(ui.findById("dock_right_sld"))) {
        sld->setOnValueChanged([&ui](float v) {
            if (auto* lbl = dynamic_cast<ayt::ui::TextLabel*>(ui.findById("lbl_dock_ping"))) {
                wchar_t buf[64];
                std::swprintf(buf, 64, L"dock: Right slider %.2f", v);
                lbl->setText(buf);
            }
        });
    }

    showPage(ui, "page_basics");
}

void bindReload(GalleryState& state);

bool loadAndWire(GalleryState& state)
{
    if (state.layoutPath.empty()) {
        std::fprintf(stderr, "[AYUI_Gallery] loadLayout failed: empty path\n");
        return false;
    }
    if (!state.ui->loadLayout(state.layoutPath)) {
        std::fprintf(stderr, "[AYUI_Gallery] loadLayout failed: %s\n",
                     state.layoutPath.c_str());
        return false;
    }
    state.ui->setClientSize(static_cast<float>(state.clientW),
                            static_cast<float>(state.clientH));
    wireGallery(state);
    bindReload(state);
    std::fprintf(stderr, "[AYUI_Gallery] loaded %s\n", state.layoutPath.c_str());
    return true;
}

void bindReload(GalleryState& state)
{
    if (auto* btn = dynamic_cast<ayt::ui::Button*>(state.ui->findById("btn_reload"))) {
        btn->setOnClicked([&state]() {
            state.modal.reset();
            state.modalBody.reset();
            state.clickCount = 0;
            // Use state.layoutPath (lives in GalleryState), not a path
            // captured inside this lambda — loadLayout destroys this
            // Button / std::function before ifstream::open returns.
            (void)loadAndWire(state);
        });
    }
}

std::intptr_t handleMessage(HWND, GalleryState* state, unsigned msg,
                            std::uintptr_t wParam, std::intptr_t lParam, bool& handled)
{
    handled = false;
    if (state == nullptr || state->ui == nullptr) {
        return 0;
    }

    switch (msg) {
    case WM_SIZE: {
        state->clientW = LOWORD(lParam);
        state->clientH = HIWORD(lParam);
        if (state->clientW < 32) state->clientW = 32;
        if (state->clientH < 32) state->clientH = 32;
        state->ui->setClientSize(static_cast<float>(state->clientW),
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
        const float x = static_cast<float>(GET_X_LPARAM(lParam));
        const float y = static_cast<float>(GET_Y_LPARAM(lParam));
        state->ui->onMouseMove(x, y);
        handled = true;
        return 0;
    }
    case WM_LBUTTONDOWN: {
        const float x = static_cast<float>(GET_X_LPARAM(lParam));
        const float y = static_cast<float>(GET_Y_LPARAM(lParam));
        state->ui->onMouseButtonDown(x, y, 0);
        handled = true;
        return 0;
    }
    case WM_LBUTTONUP: {
        const float x = static_cast<float>(GET_X_LPARAM(lParam));
        const float y = static_cast<float>(GET_Y_LPARAM(lParam));
        state->ui->onMouseButtonUp(x, y, 0);
        handled = true;
        return 0;
    }
    case WM_RBUTTONDOWN: {
        const float x = static_cast<float>(GET_X_LPARAM(lParam));
        const float y = static_cast<float>(GET_Y_LPARAM(lParam));
        state->ui->onMouseButtonDown(x, y, 1);
        handled = true;
        return 0;
    }
    case WM_RBUTTONUP: {
        const float x = static_cast<float>(GET_X_LPARAM(lParam));
        const float y = static_cast<float>(GET_Y_LPARAM(lParam));
        state->ui->onMouseButtonUp(x, y, 1);
        handled = true;
        return 0;
    }
    case WM_MOUSELEAVE:
        state->ui->onMouseLeave();
        handled = true;
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        state->ui->onKeyDown(static_cast<int>(wParam));
        handled = true;
        return 0;
    }
    case WM_KEYUP:
    case WM_SYSKEYUP: {
        state->ui->onKeyUp(static_cast<int>(wParam));
        handled = true;
        return 0;
    }
    // WM_CHAR / IME: leave unhandled so DeviceManager::textInput() receives
    // them; the main loop pumps committed UTF-8 into UIManager.
    default:
        break;
    }
    return 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    AllocConsole();
    FILE* dummy = nullptr;
    freopen_s(&dummy, "CONOUT$", "w", stdout);
    freopen_s(&dummy, "CONOUT$", "w", stderr);

    ayt::device::DeviceManager devices;
    ayt::device::DeviceConfig cfg{};
    cfg.window.title = "AYUI Gallery";
    cfg.window.width = kWidth;
    cfg.window.height = kHeight;
    if (!devices.initialize(cfg)) {
        std::fprintf(stderr, "[AYUI_Gallery] DeviceManager initialize failed\n");
        return 1;
    }

    ayt::device::WindowManager& window = devices.window();
    HWND hwnd = static_cast<HWND>(window.getWindowHandle());
    if (hwnd == nullptr) {
        std::fprintf(stderr, "[AYUI_Gallery] no HWND\n");
        devices.shutdown();
        return 1;
    }

    ayt::render::Renderer renderer;
    ayt::render::InitDesc init{};
    init.windowHandle = hwnd;
    init.width = kWidth;
    init.height = kHeight;
    init.vsync = true;
    init.msaa = 0; // UI-only: crisp edges, no need for MSAA
    if (!renderer.initialize(init)) {
        std::fprintf(stderr, "[AYUI_Gallery] Renderer initialize failed\n");
        devices.shutdown();
        return 1;
    }

    ayt::render::UIRenderBackend uiBackend;
    if (!uiBackend.initialize(renderer)) {
        std::fprintf(stderr, "[AYUI_Gallery] UIRenderBackend initialize failed\n");
        renderer.shutdown();
        devices.shutdown();
        return 1;
    }
    uiBackend.setFramebufferSize(static_cast<uint16_t>(kWidth),
                                 static_cast<uint16_t>(kHeight));

    ayt::ui::UIManager ui;
    ui.initialize(&uiBackend);

    GalleryState state{};
    state.ui = &ui;
    state.renderer = &renderer;
    state.uiBackend = &uiBackend;
    state.devices = &devices;
    state.clientW = kWidth;
    state.clientH = kHeight;

    // TextInput focus gate + Device→UI text/IME bridge.
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
            // endComposition() clears _composing then fires empty update.
            if (text.empty() && !devices.textInput().isComposing()) {
                ui.onDeviceCompositionEnd("");
                return;
            }
            ui.onDeviceCompositionUpdate(text, caret);
        };

    state.layoutPath = resolveLayoutPath();
    if (!loadAndWire(state)) {
        ui.shutdown();
        uiBackend.shutdown();
        renderer.shutdown();
        devices.shutdown();
        return 1;
    }

    window.setWindowCloseCallback([&state]() { state.running = false; });
    window.setWindowMessageCallback(
        [&state](unsigned msg, std::uintptr_t wParam, std::intptr_t lParam,
                 bool& handled) -> std::intptr_t {
            return handleMessage(nullptr, &state, msg, wParam, lParam, handled);
        });

    std::fprintf(stderr,
                 "[AYUI_Gallery] ready — UI-only composite (no RenderScene)\n"
                 "[AYUI_Gallery] sections: Basics / Input / Collections / Overlay / Layout\n");

    LARGE_INTEGER qpcFreq{};
    LARGE_INTEGER qpcPrev{};
    ::QueryPerformanceFrequency(&qpcFreq);
    ::QueryPerformanceCounter(&qpcPrev);

    while (state.running && window.isWindowValid()) {
        devices.pollEvents();

        LARGE_INTEGER qpcNow{};
        ::QueryPerformanceCounter(&qpcNow);
        float dt = static_cast<float>(
            static_cast<double>(qpcNow.QuadPart - qpcPrev.QuadPart)
            / static_cast<double>(qpcFreq.QuadPart));
        qpcPrev = qpcNow;
        if (dt < 0.0f) dt = 0.0f;
        if (dt > 0.1f) dt = 0.1f; // clamp hitch spikes

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
        ui.update(dt); // caret blink, hover revalidate, hot-reload
        ui.layout();
        ui.render(); // populateFrame + flushFrame (endFrame flushes text)

        renderer.endFrame();
    }

    state.modal.reset();
    state.modalBody.reset();
    ui.shutdown();
    uiBackend.shutdown();
    renderer.shutdown();
    devices.shutdown();
    return 0;
}
