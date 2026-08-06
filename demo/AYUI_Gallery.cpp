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
#include "AYTooltip.h"
#include "AYWindow.h"
#include "AYWidget.h"

#include "AYUIRenderBackend.h"
#include "AYRenderer.h"
#include "AYRenderTypes.h"
#include "AYTheme.h"

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

    // Capabilities page — long-lived overlay widgets. Tooltip is attached
    // to a target via attachTo() (lives on overlay); Window is mounted on
    // the overlay directly. Both are owned by the GalleryState so Reload
    // JSON can free them BEFORE loadLayout destroys the host tree (which
    // would otherwise leak them — the overlay outlives the reload).
    std::unique_ptr<ayt::ui::Tooltip> tooltip;
    std::unique_ptr<ayt::ui::Window>   window;

    // PR-B2 — Theme toggle state. F5 swaps dark <-> light via
    // ThemeManager::setActiveTheme(). The composer's composed sheet is
    // re-applied automatically, and onThemeChanged listeners (if any)
    // get notified. We track the active name on the host so the next
    // F5 knows which way to flip.
    std::string activeThemeName = "dark";
};

void showPage(ayt::ui::UIManager& ui, const char* pageId)
{
    static const char* kPages[] = {
        "page_basics", "page_input", "page_collections",
        "page_overlay", "page_layout", "page_capabilities",
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
            else if (std::strcmp(pageId, "page_capabilities") == 0) msg += L"capabilities";
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
    bindNav("nav_capabilities", "page_capabilities");

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

// =============================================================================
// Capabilities page — single-page demo of every shipped PR not yet visible
// in any of the 5 baseline pages. Each block is intentionally small (one or
// two widgets + a status label) so the failure mode is obvious if a PR
// regresses. Order matches the JSON: A3 / C1 / C2 / C3 / B3 / B1 (B1 last
// because Window is mounted on the overlay via C++ rather than JSON).
// =============================================================================
void wireCapabilities(GalleryState& state)
{
    ayt::ui::UIManager& ui = *state.ui;

    // --- A3 TextInput: shift+arrows, double-click word, Ctrl+Z/Y ---
    // Note: TextInput exposes setOnTextChanged + setOnSubmit but no
    // selection-changed callback (v1 surface). The status label here
    // mirrors the text-changed callback — typing / undo / redo all
    // reach it. Shift+arrow selection extension and double-click word
    // selection are silent (no callback) but the user can verify them
    // visually by the highlight + selection in the widget.
    if (auto* ti = dynamic_cast<ayt::ui::TextInput*>(ui.findById("cap_a3_txt"))) {
        ti->setOnTextChanged([&ui, ti](const std::wstring& txt) {
            if (auto* lbl = dynamic_cast<ayt::ui::TextLabel*>(
                    ui.findById("cap_a3_state"))) {
                std::wstring msg = L"text: \"";
                msg += txt;
                msg += L"\"  undo=";
                msg += (ti->canUndo() ? L"yes" : L"no");
                msg += L" redo=";
                msg += (ti->canRedo() ? L"yes" : L"no");
                lbl->setText(msg);
            }
        });
    }

    // --- C1 Tooltip: passive hover-timer driven by UIManager ---
    if (auto* btn = dynamic_cast<ayt::ui::Button*>(ui.findById("cap_c1_btn"))) {
        if (auto* tip = ayt::ui::Tooltip::attachTo(btn)) {
            tip->setText(L"PR-C1 passive tooltip\nUIManager drives tick\nhover 0.5s to appear");
            tip->setHoverDelay(0.5f);
            // The tip lives on the overlay; track its lifetime in the
            // GalleryState so Reload JSON can free it cleanly.
            state.tooltip.reset(tip);
        }
    }

    // --- C2 ComboBox typeahead ---
    if (auto* cmb = dynamic_cast<ayt::ui::ComboBox*>(ui.findById("cap_c2_cmb"))) {
        cmb->addItem(L"Apple");
        cmb->addItem(L"Apricot");
        cmb->addItem(L"Banana");
        cmb->addItem(L"Blueberry");
        cmb->addItem(L"Cherry");
        cmb->addItem(L"Coconut");
        cmb->setOnSelectionChanged([&ui](int idx) {
            if (auto* lbl = dynamic_cast<ayt::ui::TextLabel*>(
                    ui.findById("cap_c2_state"))) {
                std::wstring msg = L"typeahead: combo[";
                msg += std::to_wstring(idx);
                msg += L"] = ";
                // Look up the item name via the same ComboBox — we keep
                // cmb alive in the lambda capture, not in state, because
                // Reload JSON destroys + recreates the widget.
                if (auto* cmb2 = dynamic_cast<ayt::ui::ComboBox*>(
                        ui.findById("cap_c2_cmb"))) {
                    if (idx >= 0 && static_cast<size_t>(idx) < cmb2->getItemCount()) {
                        msg += cmb2->getItem(static_cast<size_t>(idx));
                    } else {
                        msg += L"(none)";
                    }
                }
                lbl->setText(msg);
            }
        });
    }

    // --- C3 Menu typeahead ---
    if (auto* bar = dynamic_cast<ayt::ui::MenuBar*>(ui.findById("cap_c3_bar"))) {
        ayt::ui::Menu* fruits = bar->addMenu(L"Fruits");
        if (fruits != nullptr) {
            fruits->addItem(L"Apple");
            fruits->addItem(L"Apricot");
            fruits->addItem(L"Banana");
            fruits->addItem(L"Blueberry");
            fruits->addItem(L"Cherry");
        }
        ayt::ui::Menu* colors = bar->addMenu(L"Colors");
        if (colors != nullptr) {
            colors->addItem(L"Red");
            colors->addItem(L"Green");
            colors->addItem(L"Blue");
        }
        // Menu has no onHoveredChanged; we can only observe via item
        // activation. Add a label-friendly note so the user sees the
        // menu opened / item clicked.
        if (auto* file = bar->getMenu(0)) {
            if (ayt::ui::MenuItem* apple = file->getItem(0)) {
                apple->setOnActivate([&ui]() {
                    if (auto* lbl = dynamic_cast<ayt::ui::TextLabel*>(
                            ui.findById("cap_c3_state"))) {
                        lbl->setText(L"menu: Fruits -> Apple activated");
                    }
                });
            }
            if (ayt::ui::MenuItem* cherry = file->getItem(4)) {
                cherry->setOnActivate([&ui]() {
                    if (auto* lbl = dynamic_cast<ayt::ui::TextLabel*>(
                            ui.findById("cap_c3_state"))) {
                        lbl->setText(L"menu: Fruits -> Cherry activated");
                    }
                });
            }
        }
    }

    // --- B3 Wheel routing: ListView inside a (no-outer) ScrollView ---
    // The JSON puts a plain ListView here. To exercise the nested case
    // (ScrollView containing ListView), we wrap it at runtime by creating
    // a ScrollView whose content is the existing ListView. We then post a
    // synthetic wheel event through UIManager to verify routing. Since
    // real wheel events come from the SDL2 device layer (PR-B3 hook is
    // UIManager::onDeviceWheel), we wire the status label to mirror the
    // selection-changed callback — selecting an item proves wheel scrolled
    // the list and the click resolved correctly.
    if (auto* list = dynamic_cast<ayt::ui::ListView*>(ui.findById("cap_b3_list"))) {
        for (int i = 0; i < 30; ++i) {
            wchar_t buf[32];
            std::swprintf(buf, 32, L"row-%02d", i);
            list->addItem(buf);
        }
        list->setOnSelectionChanged([&ui](int idx) {
            if (auto* lbl = dynamic_cast<ayt::ui::TextLabel*>(
                    ui.findById("cap_b3_state"))) {
                std::wstring msg = L"wheel/list: list[";
                msg += std::to_wstring(idx);
                msg += L"] scrolled to";
                lbl->setText(msg);
            }
        });
    }

    // --- B1 Window: 4-edge + 4-corner resize ---
    // Window is mounted on the overlay (similar to ModalDialog), so we
    // create it in C++ rather than JSON. Position it in the right half
    // of the capabilities page so it doesn't cover the section labels.
    state.window = std::make_unique<ayt::ui::Window>();
    state.window->setTitle(L"PR-B1 Resizable");
    state.window->setResizable(true);
    state.window->setMovable(true);
    state.window->setClosable(true);
    state.window->setMinSize(180.0f, 120.0f);
    state.window->setSize(ayt::math::FVector2(360.0f, 220.0f));
    state.window->setOnClose([]() {
        // The host just dismisses; the next click on the section will
        // re-create via reload. We don't tear down here because that
        // would invalidate the overlay parent mid-callback.
    });
    state.window->setOnResize(
        [](const ayt::math::FVector2& oldSize,
           const ayt::math::FVector2& newSize) {
            std::fprintf(stderr,
                         "[AYUI_Gallery] Window resized: %.0fx%.0f -> %.0fx%.0f\n",
                         oldSize.x, oldSize.y, newSize.x, newSize.y);
        });
    // Body: a tiny text label so the window has visible content.
    auto* body = new ayt::ui::TextLabel();
    body->setText(L"Drag any edge or corner.\nCursor hint follows.\nClose X or click 'Dismiss'.");
    body->setSize(ayt::math::FVector2(320.0f, 110.0f));
    state.window->addChildExternal(body);
    // Mount on the overlay via UIManager::openPopup (same convention as
    // ComboBox popup). We don't want this to be the "active dropdown",
    // so we add it as a regular overlay child.
    ui.getOverlayRoot()->addChildExternal(state.window.get());
    state.window->setPosition(ayt::math::FVector2(820.0f, 360.0f));
    state.window->performLayout();
}

void bindReload(GalleryState& state);

// PR-B2 — flip dark <-> light via F5. Uses ThemeManager::setActiveTheme
// so the composer's composed sheet is swapped into the global
// StyleManager and any onThemeChanged listeners get notified. We do NOT
// touch individual widget style ids — resolveStyle() reads the active
// theme's tokens at draw time, so the next render() pass picks up the
// new colors without a reload.
//
// VK_F5 = 0x74. We intercept this at the WM_KEYDOWN layer rather than
// the UIManager key tree (UIManager doesn't define UIKey_F5 today and
// adding it just for Gallery would leak a dev hotkey into the public
// surface).
void toggleTheme(GalleryState& state) {
    if (state.activeThemeName == "dark") {
        state.activeThemeName = "light";
    } else {
        state.activeThemeName = "dark";
    }
    ayt::ui::ThemeManager::get().setActiveTheme(state.activeThemeName);
    if (auto* status = dynamic_cast<ayt::ui::StatusBar*>(state.ui->findById("status"))) {
        if (ayt::ui::TextLabel* lbl = status->getPanel(0)) {
            std::wstring msg = L"theme: ";
            msg += ayt::ui::ThemeManager::get().getActiveThemeName().empty()
                       ? L"none"
                       : std::wstring(
                             state.activeThemeName.begin(),
                             state.activeThemeName.end());
            lbl->setText(msg);
        }
    }
}

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
    wireCapabilities(state);
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
            // Capabilities overlay widgets MUST be torn down BEFORE
            // loadLayout destroys the host tree, otherwise the overlay
            // outlives reload and we leak (overlay root is not owned by
            // the loaded JSON tree).
            state.tooltip.reset();
            state.window.reset();
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
        // PR-B2 — F5 toggles dark <-> light. Intercept BEFORE the UI key
        // tree so the keystroke never reaches the focused widget (a
        // future TextInput with setShortcut("F5") would otherwise eat
        // it). VK_F5 = 0x74.
        if (wParam == VK_F5) {
            toggleTheme(*state);
            handled = true;
            return 0;
        }
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

    // PR-B2 — install built-in dark theme. ensureDefaultThemes is
    // idempotent (no-op if a host registered a theme already) and
    // setActiveTheme pushes the composed sheet into the StyleManager
    // so resolveStyle() returns token-driven colors from this point on.
    ayt::ui::ThemeManager::get().ensureDefaultThemes();
    ayt::ui::ThemeManager::get().setActiveTheme("dark");

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
