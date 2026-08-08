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
    // the overlay directly. They live on the overlay AND in raw pointers
    // here — the overlay owns lifetime EXCLUSIVELY. We never wrap these
    // in unique_ptr / shared_ptr because the overlay's destroyWidgetTree
    // would otherwise double-free alongside our own destructor.
    //
    // Cleanup contract: teardownCapabilitiesOverlay() MUST be called
    // BEFORE ui.shutdown() and BEFORE loadLayout (which destroys the
    // tree behind the overlay). It removes the widget from the overlay
    // via detachForHostDestruction() (Tooltip's analog: detach()) so
    // the overlay has no live reference, THEN deletes the raw pointer
    // ourselves. After teardown, the raw pointer is dangling — caller
    // must null it out.
    ayt::ui::Tooltip* tooltip = nullptr;
    ayt::ui::Window*   window  = nullptr;
    // Window's body TextLabel — kept separately so teardown can free
    // it explicitly. The body was added via window->addChildExternal,
    // which means destroyWidgetTree on the Window would detach but not
    // delete it (UI-OWN-2 invariant for external children).
    ayt::ui::TextLabel* windowBody = nullptr;

    // PR-B2 — Theme toggle state. F5 swaps dark <-> light via
    // ThemeManager::setActiveTheme(). The composer's composed sheet is
    // re-applied automatically, and onThemeChanged listeners (if any)
    // get notified. We track the active name on the host so the next
    // F5 knows which way to flip.
    std::string activeThemeName = "dark";

    // Set when handleMessage consumes WM_MOUSEWHEEL this poll. Prevents
    // the Device→UI bridge from double-applying the same gesture when
    // WM_INPUT RI_MOUSE_WHEEL also fires (precision trackpads).
    bool wheelHandledThisFrame = false;
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
    // Page height changes on switch — reset scroll so a tall page's
    // scrollbar/start offset aren't left over from a short page (or vice
    // versa). Content size is refreshed in ScrollView::performLayout.
    if (auto* scroll = dynamic_cast<ayt::ui::ScrollView*>(
            ui.findById("content_scroll"))) {
        scroll->setContentSize(ayt::math::FVector2(0.0f, 0.0f));
        scroll->setScrollOffset(ayt::math::FVector2(0.0f, 0.0f));
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

    // Wrap content_host inside ScrollView so all 6 pages are reachable
    // when the window is smaller than the page stack's natural height.
    // The JSON puts content_host as a child of content_scroll via
    // addChild — ScrollView expects setContent, not addChild, so we
    // re-bind explicitly here. removeChild + addChild keeps the widget
    // tree intact (content_host still owns the 5 page VBoxes).
    if (auto* scroll = dynamic_cast<ayt::ui::ScrollView*>(
            ui.findById("content_scroll"))) {
        if (auto* host = dynamic_cast<ayt::ui::Widget*>(
                ui.findById("content_host"))) {
            scroll->setContent(host);
        }
    }
}

// =============================================================================
// Capabilities page — single-page demo of every shipped PR not yet visible
// in any of the 5 baseline pages. Each block is intentionally small (one or
// two widgets + a status label) so the failure mode is obvious if a PR
// regresses. Order matches the JSON: A3 / C1 / C2 / C3 / B3 / B1 (B1 last
// because Window is mounted on the overlay via C++ rather than JSON).
// =============================================================================

// Pull the long-lived overlay widgets (Tooltip + Window) OFF the overlay
// and free them. Must run BEFORE ui.shutdown() AND BEFORE loadLayout (which
// would otherwise leave the overlay holding a stale pointer to a soon-
// deleted target button — read-after-free on the next tick()).
//
// Why a separate helper instead of relying on ~GalleryState: the overlay
// outlives GalleryState (it lives inside the UIManager). Without explicit
// detach + delete here, ~GalleryState would free the Widget while the
// overlay's _children still references it; the next update() / render()
// would deref freed memory. Window has the same hazard — its overlay
// parent would otherwise double-free when the overlay tears down.
//
// Order matters: detach BEFORE delete. Tooltip::detach() pulls itself off
// the overlay AND unregisters from the hover-timer driver; after that the
// raw delete is safe. Window is removed via detachFromParent() so the
// overlay's child list doesn't see a dangling pointer. The Window has no
// dedicated "detachForHostDestruction" analog, but the same trick
// Menu::detachForHostDestruction() uses (break parent back-pointer +
// removeChild) is fine here — Window's overlay isn't its owner, just a
// mount site.
//
// Idempotent: calling twice is safe — the second call sees tooltip/window
// already null and short-circuits.
void teardownCapabilitiesOverlay(GalleryState& state) {
    if (state.tooltip != nullptr) {
        // Tooltip::detach() pulls itself off the overlay + unregisters
        // from the hover-timer driver. After detach the tooltip is no
        // longer reachable from any UI tree, so destroyWidgetTree (NOT
        // `delete` — Tooltip owns its TextLabel child via addChild, and
        // UI-OWN-1 says ~Widget does not free children; destroyWidgetTree
        // walks the tree and frees every reachable widget recursively).
        state.tooltip->detach();
        ayt::ui::destroyWidgetTree(state.tooltip);
        state.tooltip = nullptr;
    }
    if (state.window != nullptr) {
        // Pull off the overlay (no equivalent to Menu::detachForHost
        // Destruction for Window — but removeChild on the overlay works
        // because we mounted via addChildExternal which kept ownership
        // with us).
        if (state.window->getParent() != nullptr) {
            state.window->getParent()->removeChild(state.window);
        }
        // Body TextLabel was attached via addChildExternal (we own it).
        // destroyWidgetTree skips external children, so we free it first.
        if (state.windowBody != nullptr) {
            // Detach so Window's destructor doesn't see a stale parent
            // back-pointer (it doesn't free children either way, but
            // keeping the tree consistent helps sanitizers).
            if (state.windowBody->getParent() != nullptr) {
                state.windowBody->getParent()->removeChild(state.windowBody);
            }
            delete state.windowBody;
            state.windowBody = nullptr;
        }
        delete state.window;
        state.window = nullptr;
    }
}

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
            // The tip lives on the overlay. GalleryState holds a raw
            // pointer (NOT unique_ptr — the overlay owns the lifetime;
            // teardownCapabilitiesOverlay() pulls it off the overlay
            // before freeing it here).
            state.tooltip = tip;
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
            // PR-C3 hotfix — capture `fruits` (not just `&ui`) so the
            // hover/activate callbacks can dereference it. Previous build
            // silently fell back to the stale exe (lambda capture was a
            // compile error that ships never caught because the Gallery
            // wasn't rebuilt after the PR-C3 wire-up).
            fruits->setOnHoverChanged([&ui, fruits](int idx) {
                // PR-C3 feedback — typeahead jumps the highlight via
                // setHoveredIndex; the state label mirrors it so the
                // user sees 'A' → Apple, 'B' → Banana, etc. without
                // needing to look at the menu bar's highlight color.
                if (auto* lbl = dynamic_cast<ayt::ui::TextLabel*>(
                        ui.findById("cap_c3_state"))) {
                    if (auto* item = fruits->getItem(static_cast<size_t>(idx))) {
                        std::wstring msg = L"menu: Fruits highlight=[";
                        msg += std::to_wstring(idx);
                        msg += L"] \"";
                        msg += item->getText();
                        msg += L"\" (typeahead pre-activation)";
                        lbl->setText(msg);
                    }
                }
            });
            fruits->setOnItemActivated([&ui, fruits](int idx) {
                if (auto* lbl = dynamic_cast<ayt::ui::TextLabel*>(
                        ui.findById("cap_c3_state"))) {
                    std::wstring msg = L"menu: Fruits -> [";
                    msg += std::to_wstring(idx);
                    msg += L"] (typeahead while open)";
                    lbl->setText(msg);
                }
            });
            fruits->setOnClose([&ui]() {
                if (auto* lbl = dynamic_cast<ayt::ui::TextLabel*>(
                        ui.findById("cap_c3_state"))) {
                    // Don't clobber a "Fruits -> Cherry activated"
                    // message with a plain close — only annotate when
                    // the state still says the menu is open.
                    const std::wstring& cur = lbl->getText();
                    if (cur == L"menu: (idle)" ||
                        cur == L"menu: Fruits opened - type a letter") {
                        lbl->setText(L"menu: Fruits closed");
                    }
                }
            });
        }
        ayt::ui::Menu* colors = bar->addMenu(L"Colors");
        if (colors != nullptr) {
            colors->addItem(L"Red");
            colors->addItem(L"Green");
            colors->addItem(L"Blue");
            // PR-C3 hotfix — see Fruits above; same lambda-capture fix needed
            // for Colors to avoid referencing a non-captured local.
            colors->setOnHoverChanged([&ui, colors](int idx) {
                if (auto* lbl = dynamic_cast<ayt::ui::TextLabel*>(
                        ui.findById("cap_c3_state"))) {
                    if (auto* item = colors->getItem(static_cast<size_t>(idx))) {
                        std::wstring msg = L"menu: Colors highlight=[";
                        msg += std::to_wstring(idx);
                        msg += L"] \"";
                        msg += item->getText();
                        msg += L"\" (typeahead pre-activation)";
                        lbl->setText(msg);
                    }
                }
            });
            colors->setOnItemActivated([&ui, colors](int idx) {
                if (auto* lbl = dynamic_cast<ayt::ui::TextLabel*>(
                        ui.findById("cap_c3_state"))) {
                    std::wstring msg = L"menu: Colors -> [";
                    msg += std::to_wstring(idx);
                    msg += L"] (typeahead while open)";
                    lbl->setText(msg);
                }
            });
        }
        // Update the status label when an anchor button is clicked so
        // the user knows the menu is now open and typeahead is live.
        // MenuBar exposes its anchors via _menus (private) — but the
        // public API has getMenuCount() / getMenu(); we instead hook
        // the rendered anchor buttons through the overlay's hit list.
        // Since MenuBar's anchor buttons are children of the bar, the
        // cleanest seam is to wire each MenuBar's child button at
        // construction. MenuBar doesn't expose them publicly; we
        // instead simply stamp the hint into the status label when
        // wireCapabilities finishes so the user knows to click.
        if (auto* lbl = dynamic_cast<ayt::ui::TextLabel*>(
                ui.findById("cap_c3_state"))) {
            lbl->setText(L"menu: click 'Fruits' or 'Colors' to open, then type a letter");
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
        // B3 hotfix — fire on BOTH selection AND scroll so the user sees
        // feedback whether they clicked a row or just wheel-scrolled.
        // Previously setOnSelectionChanged only fired on click, so a
        // pure wheel-scroll left the label stuck at "(idle)" and the
        // user thought the wheel wasn't routing. The scrollbar callback
        // fires for both wheel (which routes through onMouseWheel →
        // scrollBy → setScrollOffset → syncBarToOffset which mutates the
        // bar's value) and direct vbar drag.
        list->setOnSelectionChanged([&ui](int idx) {
            if (auto* lbl = dynamic_cast<ayt::ui::TextLabel*>(
                    ui.findById("cap_b3_state"))) {
                std::wstring msg = L"wheel/list: list[";
                msg += std::to_wstring(idx);
                msg += L"] selected";
                lbl->setText(msg);
            }
        });
        // Do NOT replace vbar->setOnValueChanged — that wipes ListView's
        // scroll/rebind mapping. Use setOnScroll for status feedback.
        list->setOnScroll([&ui](const ayt::math::FVector2& off) {
            if (auto* lbl = dynamic_cast<ayt::ui::TextLabel*>(
                    ui.findById("cap_b3_state"))) {
                std::wstring msg = L"wheel/list: scrollOffset.y=";
                msg += std::to_wstring(static_cast<int>(off.y));
                lbl->setText(msg);
            }
        });
    }

    // --- B1 Window: 4-edge + 4-corner resize ---
    // Window is mounted on the overlay (similar to ModalDialog), so we
    // create it in C++ rather than JSON. Position it in the right half
    // of the capabilities page so it doesn't cover the section labels.
    state.window = new ayt::ui::Window();
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
    // Body: a tiny text label so the window has visible content. We keep
    // the pointer in state.windowBody so teardown can free it explicitly
    // (addChildExternal makes destroyWidgetTree skip it).
    //
    // PR-B1 hotfix — offset below the title bar (28px) with a small
    // breathing margin. Previous wire-up placed the body at local
    // (0,0), which lives under the title bar's gray strip — the text
    // was technically following the window's drag/resize (Widget's
    // worldPosition chains via _parent), but visually it overlapped
    // the chrome and looked "stuck" to the user. The Window class
    // doesn't reserve a body region itself (no Window::setBodyInsets),
    // so the host is responsible for picking the inset.
    state.windowBody = new ayt::ui::TextLabel();
    state.windowBody->setText(
        L"Title bar (center) = move.\n"
        L"Outer rim / corners = resize.\n"
        L"Body text follows the window.");
    state.windowBody->setSize(ayt::math::FVector2(320.0f, 110.0f));
    // layoutChildren places body below the title bar; local (0,0) is fine.
    state.window->addChildExternal(state.windowBody);
    // Mount on the overlay via UIManager::openPopup (same convention as
    // ComboBox popup). We don't want this to be the "active dropdown",
    // so we add it as a regular overlay child.
    ui.getOverlayRoot()->addChildExternal(state.window);
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
        // Also dump to file so we can debug when stderr is detached
        // (Gallery launches its own console via AllocConsole and the
        // bash redirect may miss the early writes).
        std::FILE* f = std::fopen("gallery_load_error.txt", "w");
        if (f) { std::fputs("[AYUI_Gallery] loadLayout failed: empty path\n", f); std::fclose(f); }
        return false;
    }
    if (!state.ui->loadLayout(state.layoutPath)) {
        std::fprintf(stderr, "[AYUI_Gallery] loadLayout failed: %s\n",
                     state.layoutPath.c_str());
        std::FILE* f = std::fopen("gallery_load_error.txt", "w");
        if (f) {
            std::fprintf(f, "[AYUI_Gallery] loadLayout failed: %s\n",
                         state.layoutPath.c_str());
            std::fclose(f);
        }
        // Surface the failure even when the debugger swallows first-chance
        // nlohmann::parse_error (caught inside UILayoutLoader) and CONOUT
        // is easy to miss. Loader also writes ayui_loader_error.txt.
        std::string detail = "loadLayout failed:\n";
        detail += state.layoutPath;
        detail += "\n\nSee gallery_load_error.txt / ayui_loader_error.txt "
                  "next to the working directory.";
        ::MessageBoxA(nullptr, detail.c_str(), "AYUI Gallery", MB_OK | MB_ICONERROR);
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
            teardownCapabilitiesOverlay(state);
            state.clickCount = 0;
            // Use state.layoutPath (lives in GalleryState), not a path
            // captured inside this lambda — loadLayout destroys this
            // Button / std::function before ifstream::open returns.
            (void)loadAndWire(state);
        });
    }
}

// PR-S5: UiCursorHint → Win32 cursor. The UI layer computes hints
// (Window resize edges / title-bar Move, ScrollBar SizeNs, Hand, Beam)
// but Gallery never applied them — resize/drag felt "functional but the
// cursor never changed". LoadCursor lazily caches the system cursors.
static HCURSOR cursorForHint(ayt::ui::UiCursorHint hint)
{
    static const HCURSOR arrow = ::LoadCursorW(nullptr, IDC_ARROW);
    static const HCURSOR hand  = ::LoadCursorW(nullptr, IDC_HAND);
    static const HCURSOR we    = ::LoadCursorW(nullptr, IDC_SIZEWE);
    static const HCURSOR ns    = ::LoadCursorW(nullptr, IDC_SIZENS);
    static const HCURSOR nwse  = ::LoadCursorW(nullptr, IDC_SIZENWSE);
    static const HCURSOR nesw  = ::LoadCursorW(nullptr, IDC_SIZENESW);
    static const HCURSOR move  = ::LoadCursorW(nullptr, IDC_SIZEALL);
    static const HCURSOR beam  = ::LoadCursorW(nullptr, IDC_IBEAM);
    switch (hint) {
    case ayt::ui::UiCursorHint::Hand:  return hand;
    case ayt::ui::UiCursorHint::SizeWe:
    case ayt::ui::UiCursorHint::SizeHorizontal: return we;
    case ayt::ui::UiCursorHint::SizeNs:
    case ayt::ui::UiCursorHint::SizeVertical:   return ns;
    case ayt::ui::UiCursorHint::SizeNwse: return nwse;
    case ayt::ui::UiCursorHint::SizeNesw: return nesw;
    case ayt::ui::UiCursorHint::Move:  return move;
    case ayt::ui::UiCursorHint::Beam:  return beam;
    case ayt::ui::UiCursorHint::Default:
    default: return arrow;
    }
}

std::intptr_t handleMessage(HWND, GalleryState* state, unsigned msg,
                            std::uintptr_t wParam, std::intptr_t lParam, bool& handled)
{
    handled = false;
    if (state == nullptr || state->ui == nullptr) {
        return 0;
    }

    // ---- Touch tracking (touchscreen laptops w/o mouse emulation) ----
    // WM_TOUCH is delivered when RegisterTouchWindow was called on the
    // hwnd. We map the primary touch into onMouseMove / onMouseButtonDown
    // / onMouseButtonUp so the entire UI tree (hover tooltips, button
    // click, focus) works on a finger tap. Vertical pan delta is fed
    // into onMouseWheel so ScrollView / ListView scroll when the user
    // drags a finger — same code path as a real wheel on a desktop.
    struct TouchState {
        bool    active       = false;
        // PR-B5 — dragStarted flips true once the finger crosses the
        // drag threshold. While false, the gesture is a "tentative tap"
        // and we suppress the synthesized mouse-down so a ListView
        // row click doesn't fire before the user has shown they want
        // to scroll (touchscreen users naturally flick instead of
        // press-and-drag like a mouse).
        bool    dragStarted  = false;
        float   lastY        = 0.0f;
        float   accumulatedDy = 0.0f; // // positive = finger moved down
        float   x            = 0.0f;
        float   y            = 0.0f;
        float   startX       = 0.0f;
        float   startY       = 0.0f;
        DWORD   pointerId    = 0;
    };
    static thread_local TouchState gTouch;
    // Wheel scale: a 100-px finger drag ≈ 1 notch of mouse wheel delta
    // (typical deltaY = 120). Flip sign so dragging finger UP scrolls
    // content DOWN (matches native scrolling convention).
    constexpr float kTouchWheelScale = 1.2f;
    // Threshold beyond which a tentative tap becomes a drag-scroll:
    // beyond this distance, the gesture is treated as a scroll and
    // the mouse-down (which would have selected a row) is suppressed.
    constexpr float kTouchDragThresholdPx = 6.0f;

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
        // Keep handled=false so Device also updates MouseDevice position.
        // Returning handled=true previously starved getWheelDelta bridging
        // of a valid cursor (Device pos stayed at 0,0 → pickTopmost miss).
        return 0;
    }
    case WM_SETCURSOR: {
        // PR-S5: apply the UI layer's cursor hint (resize edges,
        // title-bar Move, Beam, Hand). SetCursor here
        // and skip DefWindowProc so the OS doesn't snap back to arrow.
        // _hoverWidget/_capturedWidget are already fresh — WM_SETCURSOR
        // follows the WM_MOUSEMOVE that updated them.
        ::SetCursor(cursorForHint(state->ui->getCursorHint()));
        handled = true;
        return 0;
    }
    case WM_MOUSEWHEEL: {
        // Primary wheel path: client coords from the message (not Device
        // mouse pos). Gallery swallows move for UI but must own wheel too
        // — otherwise only the post-poll Device bridge runs, often at a
        // stale (0,0) pick point.
        //
        // Sign: Win32 positive = wheel away / natural trackpad "swipe up"
        // often arrives as negative. UI scrollOffset increases to reveal
        // lower content (browser-like: finger up → content up). Negate
        // Win32 notches so swipe/wheel matches browser natural scrolling.
        constexpr float kPixelsPerNotch = 40.0f;
        const short raw = static_cast<short>(HIWORD(wParam));
        const float deltaY =
            -(static_cast<float>(raw) / static_cast<float>(WHEEL_DELTA))
            * kPixelsPerNotch;
        POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (state->devices != nullptr) {
            HWND hwnd = static_cast<HWND>(
                state->devices->window().getWindowHandle());
            if (hwnd != nullptr) {
                ::ScreenToClient(hwnd, &pt);
            }
        }
        state->ui->onMouseWheel(static_cast<float>(pt.x),
                                static_cast<float>(pt.y), deltaY);
        state->wheelHandledThisFrame = true;
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
    case WM_TOUCH: {
        // Decode TOUCHINPUT array (count in LOWORD(wParam)). We only
        // track the FIRST active touch — the Gallery is single-finger
        // interaction. Multi-touch gestures (pinch-zoom etc.) are out
        // of scope.
        const int count = LOWORD(wParam);
        if (count <= 0) {
            handled = true;
            return 0;
        }
        std::vector<TOUCHINPUT> inputs(count);
        if (!::GetTouchInputInfo(reinterpret_cast<HTOUCHINPUT>(lParam),
                                 static_cast<UINT>(count),
                                 inputs.data(),
                                 sizeof(TOUCHINPUT))) {
            handled = true;
            return 0;
        }

        // Screen -> client coords (TOUCHINPUT gives screen coords).
        POINT pt{};
        for (const TOUCHINPUT& ti : inputs) {
            if (ti.dwID != gTouch.pointerId) continue;
            pt.x = TOUCH_COORD_TO_PIXEL(ti.x);
            pt.y = TOUCH_COORD_TO_PIXEL(ti.y);
            ::ScreenToClient(static_cast<HWND>(state->devices->window().getWindowHandle()), &pt);
            gTouch.x = static_cast<float>(pt.x);
            gTouch.y = static_cast<float>(pt.y);
            break;
        }

        bool sawDown = false;
        bool sawUp   = false;
        bool sawMove = false;
        for (const TOUCHINPUT& ti : inputs) {
            if (ti.dwID != gTouch.pointerId) continue;
            if (ti.dwFlags & TOUCHEVENTF_DOWN) sawDown = true;
            if (ti.dwFlags & TOUCHEVENTF_UP)   sawUp   = true;
            if (ti.dwFlags & TOUCHEVENTF_MOVE) sawMove = true;
            break;
        }

        if (sawDown) {
            // First contact — record the tentative tap. We DO NOT
            // synthesize mouse-down yet; the previously-shipped code
            // fired onMouseButtonDown immediately, which on a ListView
            // selected a row before the user had a chance to scroll.
            // The new flow: track start, fire mouse-down only after
            // we're sure the gesture is a tap (no drag within the
            // threshold). dragStarted flips true on the first move
            // past kTouchDragThresholdPx and the gesture becomes a
            // pure scroll.
            gTouch.active        = true;
            gTouch.dragStarted   = false;
            gTouch.lastY         = gTouch.y;
            gTouch.accumulatedDy = 0.0f;
            gTouch.pointerId     = inputs.empty() ? 0 : inputs[0].dwID;
            // Find the actual pointer id we tracked (in case it differs).
            for (const TOUCHINPUT& ti : inputs) {
                if (ti.dwID == gTouch.pointerId) break;
            }
            // Re-resolve pointerId from the first input (single-finger).
            gTouch.pointerId = inputs[0].dwID;
            pt.x = TOUCH_COORD_TO_PIXEL(inputs[0].x);
            pt.y = TOUCH_COORD_TO_PIXEL(inputs[0].y);
            ::ScreenToClient(static_cast<HWND>(state->devices->window().getWindowHandle()), &pt);
            gTouch.x = static_cast<float>(pt.x);
            gTouch.y = static_cast<float>(pt.y);
            gTouch.startX = gTouch.x;
            gTouch.startY = gTouch.y;
            state->ui->onMouseMove(gTouch.x, gTouch.y);
        } else if (sawMove && gTouch.active) {
            const float dy = gTouch.y - gTouch.lastY;
            gTouch.accumulatedDy += dy;
            gTouch.lastY = gTouch.y;
            state->ui->onMouseMove(gTouch.x, gTouch.y);

            // PR-B5 — once the finger crosses the drag threshold,
            // commit the gesture as a scroll. We DON'T synthesize a
            // mouse-down (which would have selected a row); the
            // accumulated dy is fed straight into onMouseWheel.
            if (!gTouch.dragStarted) {
                const float totalDx = gTouch.x - gTouch.startX;
                const float totalDy = gTouch.y - gTouch.startY;
                if (std::fabs(totalDy) >= kTouchDragThresholdPx ||
                    std::fabs(totalDx) >= kTouchDragThresholdPx) {
                    gTouch.dragStarted = true;
                }
            }
            if (gTouch.dragStarted) {
                // Threshold = 8 px (one wheel notch ≈ 6-8 px on most
                // precision touchpads).
                constexpr float kTouchWheelThresholdPx = 8.0f;
                if (std::fabs(gTouch.accumulatedDy) >= kTouchWheelThresholdPx) {
                    // Sign convention: UIManager::onMouseWheel deltaY is
                    // "content to move by -deltaY in y" (scroll wheel up
                    // has positive deltaY per Win32 convention). Our
                    // accumulatedDy is "finger moved down" which feels
                    // like "scroll content up" — so we negate.
                    const float wheelDelta =
                        -gTouch.accumulatedDy * kTouchWheelScale;
                    state->ui->onMouseWheel(gTouch.x, gTouch.y, wheelDelta);
                    gTouch.accumulatedDy = 0.0f;
                }
            }
        } else if (sawUp && gTouch.active) {
            // Only fire mouse-up if the gesture was actually a tap
            // (no drag). For a drag-scroll, we never sent a
            // mouse-down, so the up is a no-op.
            if (!gTouch.dragStarted) {
                state->ui->onMouseButtonDown(gTouch.x, gTouch.y, 0);
                state->ui->onMouseButtonUp(gTouch.x, gTouch.y, 0);
            }
            gTouch.active        = false;
            gTouch.dragStarted   = false;
            gTouch.accumulatedDy = 0.0f;
            gTouch.pointerId     = 0;
        }

        ::CloseTouchInputHandle(reinterpret_cast<HTOUCHINPUT>(lParam));
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

    // Enable WM_TOUCH delivery so touchscreens (laptop trackpads,
    // Surface, etc.) can drive onMouseMove + onMouseWheel. TWF_FINETOUCH
    // gives us the highest-fidelity coordinates for the single-finger
    // drag-to-scroll path in handleMessage. Without this call, touch
    // devices fall back to synthesized mouse messages which SDL2/our
    // window proc may not see on every touch panel.
    ::RegisterTouchWindow(hwnd, TWF_FINETOUCH);

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
        state.wheelHandledThisFrame = false;
        devices.pollEvents();

        // Fallback bridge: precision trackpads may deliver wheel only via
        // WM_INPUT → MouseDevice (no WM_MOUSEWHEEL). Use UIManager's last
        // mouse (updated by WM_MOUSEMOVE), never Device pos alone — move
        // used to be handled=true and starved Device coordinates.
        if (!state.wheelHandledThisFrame) {
            if (ayt::device::MouseDevice* mouse = devices.mouse()) {
                const float notches = mouse->getWheelDelta();
                if (notches != 0.0f) {
                    constexpr float kPixelsPerNotch = 40.0f;
                    const ayt::math::FVector2 pos = ui.hasMousePos()
                        ? ui.getMousePos()
                        : ayt::math::FVector2(mouse->getPosition().x,
                                              mouse->getPosition().y);
                    // Same Win32→UI sign flip as WM_MOUSEWHEEL handler.
                    ui.onMouseWheel(pos.x, pos.y, -notches * kPixelsPerNotch);
                }
            }
        }

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
    // CRITICAL: tear down Capabilities overlay widgets BEFORE ui.shutdown.
    // Otherwise ~UIManager tears down the overlay (deleting Tooltip /
    // Window), and the dangling raw pointers in state.tooltip / state.window
    // survive — the SECOND free would happen in ~GalleryState (after this
    // function returns) and SEGV. teardownCapabilitiesOverlay pulls the
    // widgets off the overlay AND deletes them here so the overlay's
    // subsequent shutdown sees no children to free.
    teardownCapabilitiesOverlay(state);
    ui.shutdown();
    uiBackend.shutdown();
    renderer.shutdown();
    devices.shutdown();
    return 0;
}
