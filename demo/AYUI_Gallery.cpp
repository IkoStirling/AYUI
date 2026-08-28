// AYUI_Gallery.cpp ??standalone AYUI visual check (no 3D / no Editor shell).
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
#include "AYUI/UIManager.h"
#include "AYUI/Button.h"
#include "AYUI/Image.h"
#include "AYUI/TextureRegistry.h"
#include "AYUI/CheckBox.h"
#include "AYUI/Slider.h"
#include "AYUI/Box.h"
#include "AYUI/ProgressBar.h"
#include "AYUI/TextLabel.h"
#include "AYDevice/TextInput.h"
#include "AYUI/TabStrip.h"
#include "AYUI/Spinner.h"
#include "AYUI/ListView.h"
#include "AYUI/ComboBox.h"
#include "AYUI/MenuBar.h"
#include "AYUI/Menu.h"
#include "AYUI/MenuItem.h"
#include "AYUI/StatusBar.h"
#include "AYUI/ModalDialog.h"
#include "AYUI/Separator.h"
#include "AYUI/Tooltip.h"
#include "AYUI/Window.h"
#include "AYUI/Widget.h"
#include "AYUI/DockArea.h"
#include "AYUI/DockCard.h"
#include "AYUI/DockTrace.h"
#include "GalleryChildWindows.h"

#include "AYRenderer/UIRenderBackend.h"
#include "AYRenderer.h"
#include "AYRenderer/RenderTypes.h"
#include "AYUI/Theme.h"

#include "AYDevice/DeviceManager.h"

#include <algorithm>
#include <cmath>
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
constexpr const char* kGalleryImageTextureName = "gallery/composition_atlas";

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
    // Durable path for Reload JSON ??must NOT live only inside the
    // button's onClicked lambda: loadLayout destroys that button (and
    // the lambda) mid-callback, leaving a dangling std::string& for
    // ifstream::open.
    std::string layoutPath;

    int clickCount = 0;
    int imageClickCount = 0;
    // Images page -- one shared procedural atlas, acquired by every
    // standard Image through TextureRegistry. The Gallery deliberately
    // tears the named refs down before releasing this backend handle.
    void* imageTexAtlas = nullptr;
    // Animation page: fade-panel visibility latch (survives reload like
    // the other knobs; lives here, not in a button lambda, so the state
    // is not destroyed with the JSON tree).
    bool panelVisible = true;
    std::unique_ptr<ayt::ui::ModalDialog> modal;
    std::unique_ptr<ayt::ui::TextLabel> modalBody;

    // Capabilities page ??long-lived overlay widgets. Tooltip is attached
    // to a target via attachTo() (lives on overlay); Window is mounted on
    // the overlay directly. They live on the overlay AND in raw pointers
    // here ??the overlay owns lifetime EXCLUSIVELY. We never wrap these
    // in unique_ptr / shared_ptr because the overlay's destroyWidgetTree
    // would otherwise double-free alongside our own destructor.
    //
    // Cleanup contract: teardownCapabilitiesOverlay() MUST be called
    // BEFORE ui.shutdown() and BEFORE loadLayout (which destroys the
    // tree behind the overlay). It removes the widget from the overlay
    // via detachForHostDestruction() (Tooltip's analog: detach()) so
    // the overlay has no live reference, THEN deletes the raw pointer
    // ourselves. After teardown, the raw pointer is dangling ??caller
    // must null it out.
    ayt::ui::Tooltip* tooltip = nullptr;
    ayt::ui::Window*   window  = nullptr;
    // Window's body TextLabel ??kept separately so teardown can free
    // it explicitly. The body was added via window->addChildExternal,
    // which means destroyWidgetTree on the Window would detach but not
    // delete it (UI-OWN-2 invariant for external children).
    ayt::ui::TextLabel* windowBody = nullptr;

    // Backend page -- host-drawn demo. The widget itself is owned by the
    // loaded tree (page_backend's VBox slot) -- loadLayout deletes it on
    // reload; we only hold the raw pointer to feed it texture handles.
    // The textures are owned by UIRenderBackend's registry (createUiTexture
    // refcount); teardownBackendPage() MUST release them before loadLayout
    // and before uiBackend.shutdown(), otherwise the GPU textures leak.
    ayt::ui::Widget* backendDemo = nullptr;
    void* backendTexGradient = nullptr;  // 64x64 smooth stretch demo
    void* backendTexNine     = nullptr;  // clean 32x32 rounded 9-patch (BK7)
    void* backendTexNineStyle = nullptr; // stylized 16x16 tan+ring (optional)

    // Live knobs for BackendDemoWidget (survive reload; sliders rebind).
    struct BackendDemoParams {
        float borderWidth   = 3.0f;
        float borderR0      = 2.0f;
        float borderR1      = 8.0f;
        float borderR2      = 20.0f;
        float shadowBlurA   = 4.0f;
        float shadowBlurB   = 12.0f;
        float shadowOffset  = 4.0f;
        float shadowCorner  = 10.0f;
        float comboRadius   = 12.0f;
        float comboStroke   = 2.0f;
        float comboBlur     = 6.0f;
        float ninePad       = 8.0f;
        float blendAlpha    = 0.60f;
    } backendParams;

    // PR-B2 ??Theme toggle state. F5 swaps dark <-> light via
    // ThemeManager::setActiveTheme(). The composer's composed sheet is
    // re-applied automatically, and onThemeChanged listeners (if any)
    // get notified. We track the active name on the host so the next
    // F5 knows which way to flip.
    std::string activeThemeName = "dark";

    // Set when handleMessage consumes WM_MOUSEWHEEL this poll. Prevents
    // the Device?UI bridge from double-applying the same gesture when
    // WM_INPUT RI_MOUSE_WHEEL also fires (precision trackpads).
    bool wheelHandledThisFrame = false;

    // PR-Dock-TearOff: promoted DockCards' top-level child windows.
    // Constructed ONCE in wWinMain BEFORE the first loadAndWire (the
    // promote callback captured by wireDockPromotion closes over this
    // pointer); reset() before ui.shutdown() so child windows die
    // before the primary UI (K-INV-D5-6).
    std::unique_ptr<ayt::gallery::GalleryChildWindows> childWindows;
};

void showPage(ayt::ui::UIManager& ui, const char* pageId)
{
    static const char* kPages[] = {
        "page_basics", "page_images", "page_input", "page_collections",
        "page_overlay", "page_layout", "page_capabilities",
        "page_backend", "page_animation",
    };
    for (const char* id : kPages) {
        if (ayt::ui::Widget* w = ui.findById(id)) {
            w->setVisible(std::strcmp(id, pageId) == 0);
        }
    }
    // Page height changes on switch ??reset scroll so a tall page's
    // scrollbar/start offset aren't left over from a short page (or vice
    // versa). Content size is refreshed in ScrollView::performLayout.
    if (auto* scroll = dynamic_cast<ayt::ui::ScrollView*>(
            ui.findById("content_scroll"))) {
        scroll->setContentSize(ayt::math::FVector2(0.0f, 0.0f));
        scroll->setScrollOffset(ayt::math::FVector2(0.0f, 0.0f));
    }
    // Visibility changes which VBox fill slot owns content_host ??force a
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
            else if (std::strcmp(pageId, "page_images") == 0) msg += L"images";
            else if (std::strcmp(pageId, "page_input") == 0) msg += L"input";
            else if (std::strcmp(pageId, "page_collections") == 0) msg += L"collections";
            else if (std::strcmp(pageId, "page_overlay") == 0) msg += L"overlay";
            else if (std::strcmp(pageId, "page_layout") == 0) msg += L"layout";
            else if (std::strcmp(pageId, "page_capabilities") == 0) msg += L"capabilities";
            else if (std::strcmp(pageId, "page_backend") == 0) msg += L"backend";
            else if (std::strcmp(pageId, "page_animation") == 0) msg += L"animation";
            lbl->setText(msg);
        }
    }
}

// Backend page -- host-drawn demo canvas. A plain Widget whose onRender
// paints straight through IRenderBackend every frame: P1 gradients + blend
// modes, P2 SDF borders/shadows, P3 texture stretch + 9-patch. It lives in
// page_backend's VBox (fixed-height slot), so it scrolls and hides with the
// page. Textures come from UIRenderBackend::createUiTexture (backend
// specific, not part of the AYUI interface -- the interface only passes
// opaque handles).
class BackendDemoWidget : public ayt::ui::Widget {
public:
    void setTextures(void* gradientTex, void* nineTex, void* nineStyleTex = nullptr)
    {
        _gradientTex  = gradientTex;
        _nineTex      = nineTex;
        _nineStyleTex = nineStyleTex;
    }
    void setParams(GalleryState::BackendDemoParams* params) { _params = params; }

protected:
    void onRender(ayt::ui::IRenderBackend& r) override
    {
        using ayt::math::FRectangle;
        using ayt::math::FVector2;
        using ayt::math::FVector4;

        const GalleryState::BackendDemoParams p =
            (_params != nullptr) ? *_params : GalleryState::BackendDemoParams{};

        const FRectangle b = getWorldBounds();
        if (b.maxX <= b.minX || b.maxY <= b.minY) {
            return;
        }
        float x = b.minX + 12.0f;
        float y = b.minY + 8.0f;
        const FVector4 labelColor(0.85f, 0.85f, 0.92f, 1.0f);

        auto section = [&](const wchar_t* title) {
            r.drawText(FRectangle(x, y, x + 700.0f, y + 20.0f), title, 13, labelColor);
            y += 24.0f;
        };
        auto rect = [&](float w, float h) {
            FRectangle rc(x, y, x + w, y + h);
            x += w + 14.0f;
            return rc;
        };
        auto endRow = [&](float h) {
            y += h + 10.0f;
            x = b.minX + 12.0f;
        };

        // ---- P1: gradients ------------------------------------------------
        section(L"P1 - Gradients: 2-color (vertical) and 4-color corner blend");
        r.drawGradientRect(rect(120.0f, 72.0f),
                           FVector4(1.0f, 0.30f, 0.30f, 1.0f),
                           FVector4(0.25f, 0.60f, 1.0f, 1.0f));
        r.drawGradientRect(rect(120.0f, 72.0f),
                           FVector4(1.0f, 1.0f, 0.20f, 1.0f),
                           FVector4(0.25f, 0.95f, 0.45f, 1.0f),
                           FVector4(0.95f, 0.25f, 0.90f, 1.0f),
                           FVector4(0.20f, 0.40f, 0.90f, 1.0f));
        r.drawGradientRect(rect(120.0f, 72.0f),
                           FVector4(0.10f, 0.10f, 0.14f, 1.0f),
                           FVector4(0.30f, 0.55f, 0.95f, 1.0f));
        endRow(72.0f);

        // ---- P1: blend modes ----------------------------------------------
        section(L"P1 - Blend modes over a gray base: Additive / Multiply / Screen");
        {
            const FRectangle base = rect(280.0f, 64.0f);
            r.drawRect(base, FVector4(0.45f, 0.45f, 0.48f, 1.0f));

            const float pad = 8.0f;
            const float sw  = 78.0f;
            const float gap = 10.0f;
            const float a   = p.blendAlpha;
            auto slice = [&](int i) {
                const float sx = base.minX + pad + static_cast<float>(i) * (sw + gap);
                return FRectangle(sx, base.minY + pad, sx + sw, base.maxY - pad);
            };
            r.setBlendMode(ayt::ui::BlendMode::Additive);
            r.drawRect(slice(0), FVector4(1.0f, 0.40f, 0.10f, a));
            r.setBlendMode(ayt::ui::BlendMode::Multiply);
            r.drawRect(slice(1), FVector4(0.20f, 0.80f, 0.60f, 1.0f));
            r.setBlendMode(ayt::ui::BlendMode::Screen);
            r.drawRect(slice(2), FVector4(0.80f, 0.25f, 0.25f, a));
            r.setBlendMode(ayt::ui::BlendMode::Normal);

            const FRectangle panel = rect(140.0f, 64.0f);
            r.drawRect(panel, FVector4(0.20f, 0.45f, 0.85f, 0.55f));
            r.setBlendMode(ayt::ui::BlendMode::Additive);
            r.drawRect(panel, FVector4(1.0f, 0.55f, 0.15f, a));
            r.setBlendMode(ayt::ui::BlendMode::Normal);
        }
        endRow(64.0f);

        // ---- P2: SDF borders ----------------------------------------------
        section(L"P2 - SDF borders: live corner radius / stroke (panel below)");
        for (float radius : {p.borderR0, p.borderR1, p.borderR2}) {
            r.drawBorderRect(rect(96.0f, 96.0f),
                             FVector4(0.35f, 0.75f, 1.0f, 1.0f), p.borderWidth, radius);
        }
        endRow(96.0f);

        // ---- P2: SDF shadows ----------------------------------------------
        section(L"P2 - SDF shadows + combo card (rounded fill, not square drawRect)");
        for (float blur : {p.shadowBlurA, p.shadowBlurB}) {
            ayt::ui::IRenderBackend::ShadowStyle shadow;
            shadow.color        = FVector4(0.0f, 0.0f, 0.0f, 0.55f);
            shadow.offset       = FVector2(p.shadowOffset, p.shadowOffset);
            shadow.blurRadius   = blur;
            shadow.cornerRadius = p.shadowCorner;
            r.drawRectShadow(rect(120.0f, 96.0f), shadow);
            r.drawBorderRect(rect(60.0f, 96.0f),
                             FVector4(0.55f, 0.60f, 0.70f, 1.0f), 1.0f, p.shadowCorner);
        }
        {
            const FRectangle card = rect(150.0f, 96.0f);
            ayt::ui::IRenderBackend::ShadowStyle shadow;
            shadow.color        = FVector4(0.0f, 0.0f, 0.0f, 0.50f);
            shadow.offset       = FVector2(3.0f, 3.0f);
            shadow.blurRadius   = p.comboBlur;
            shadow.cornerRadius = p.comboRadius;
            r.drawRectShadow(card, shadow);
            // Rounded fill — square drawRect used to poke corners past the stroke.
            r.drawRoundedRect(card, FVector4(0.16f, 0.30f, 0.44f, 1.0f), p.comboRadius);
            r.drawBorderRect(card, FVector4(0.92f, 0.92f, 0.96f, 1.0f),
                             p.comboStroke, p.comboRadius);
        }
        endRow(96.0f);

        // ---- P3: texture stretch ------------------------------------------
        if (_gradientTex != nullptr) {
            section(L"P3 - 64x64 gradient texture, LINEAR stretch (no mip blockiness)");
            r.drawRect(rect(240.0f, 96.0f), _gradientTex,
                       FRectangle(0.0f, 0.0f, 1.0f, 1.0f));
            r.drawRect(rect(180.0f, 96.0f), _gradientTex,
                       FRectangle(0.0f, 0.0f, 1.0f, 1.0f));
            endRow(96.0f);
        }

        // ---- P3: 9-patch --------------------------------------------------
        if (_nineTex != nullptr) {
            section(L"P3 - 32x32 clean 9-patch (padding from panel)");
            const FVector4 padding(p.ninePad, p.ninePad, p.ninePad, p.ninePad);
            r.drawNinePatch(rect(48.0f, 40.0f), _nineTex,
                            FRectangle(0.0f, 0.0f, 1.0f, 1.0f), padding);
            r.drawNinePatch(rect(96.0f, 48.0f), _nineTex,
                            FRectangle(0.0f, 0.0f, 1.0f, 1.0f), padding);
            r.drawNinePatch(rect(192.0f, 60.0f), _nineTex,
                            FRectangle(0.0f, 0.0f, 1.0f, 1.0f), padding);
            endRow(60.0f);
        }
        if (_nineStyleTex != nullptr) {
            section(L"P3 - stylized 16x16 9-patch (tan fill + sketched ring; style sample)");
            const FVector4 padStyle(4.0f, 4.0f, 4.0f, 4.0f);
            r.drawNinePatch(rect(48.0f, 40.0f), _nineStyleTex,
                            FRectangle(0.0f, 0.0f, 1.0f, 1.0f), padStyle);
            r.drawNinePatch(rect(96.0f, 48.0f), _nineStyleTex,
                            FRectangle(0.0f, 0.0f, 1.0f, 1.0f), padStyle);
            r.drawNinePatch(rect(192.0f, 60.0f), _nineStyleTex,
                            FRectangle(0.0f, 0.0f, 1.0f, 1.0f), padStyle);
            endRow(60.0f);
        }
    }

private:
    void* _gradientTex  = nullptr;
    void* _nineTex      = nullptr;
    void* _nineStyleTex = nullptr;
    GalleryState::BackendDemoParams* _params = nullptr;
};

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
    bindNav("nav_images", "page_images");
    bindNav("nav_input", "page_input");
    bindNav("nav_collections", "page_collections");
    bindNav("nav_overlay", "page_overlay");
    bindNav("nav_layout", "page_layout");
    bindNav("nav_capabilities", "page_capabilities");
    bindNav("nav_backend", "page_backend");
    bindNav("nav_animation", "page_animation");

    // --- Animation (UI animation lane, cut 1) ---
    // Demo-only loud hover (accent blue). Global Button fallback stays a
    // restrained grey; soft-clip coverPx is what makes the tween visible.
    {
        const ayt::math::FVector4 accentHover(0.40f, 0.65f, 1.00f, 1.0f);
        for (const char* id : {"anim_btn1", "anim_btn2", "anim_btn3"}) {
            if (auto* btn = dynamic_cast<ayt::ui::Button*>(ui.findById(id))) {
                btn->setFallbackHoverColor(accentHover);
            }
        }
    }
    // Fade toggle: the panel's opacity tweens 1 ↔ 0 over 200ms.
    if (auto* btn = dynamic_cast<ayt::ui::Button*>(ui.findById("btn_fade_toggle"))) {
        btn->setOnClicked([&state, &ui]() {
            state.panelVisible = !state.panelVisible;
            if (auto* panel = ui.findById("fade_panel")) {
                panel->animateOpacity(state.panelVisible ? 1.0f : 0.0f, 200.0f,
                                      ayt::ui::AnimationCurve::EaseOut);
            }
        });
    }
    // Indeterminate scan toggle: the ProgressBar sweeps a 30% accent
    // segment on a 1.6s loop instead of drawing _value.
    if (auto* btn = dynamic_cast<ayt::ui::Button*>(ui.findById("btn_scan_toggle"))) {
        btn->setOnClicked([&ui]() {
            if (auto* prg = dynamic_cast<ayt::ui::ProgressBar*>(ui.findById("prg_scan"))) {
                prg->setIndeterminate(!prg->isIndeterminate());
            }
        });
    }
    // TabStrip is cpp-built (not factory-registered): four tabs whose
    // underline indicator slides between tabs on a 120ms tween.
    if (auto* page = ui.findById("page_animation")) {
        auto* strip = new ayt::ui::TabStrip();
        strip->setSize(ayt::math::FVector2(600.0f, 28.0f));
        strip->addTab(L"One");
        strip->addTab(L"Two");
        strip->addTab(L"Three");
        strip->addTab(L"Four");
        strip->setOnSelectionChanged([&ui](int index) {
            if (auto* hint = dynamic_cast<ayt::ui::TextLabel*>(
                    ui.findById("anim_ts_hint"))) {
                wchar_t buf[64];
                std::swprintf(buf, 64, L"TabStrip indicator slid to tab %d (120ms)",
                              index + 1);
                hint->setText(buf);
            }
        });
        page->addChild(strip);
        ui.invalidateLayout();
        ui.layout();
    }

    // In-UI build stamp (OS title / console are easy to miss). If Layout
    // header doesn't contain this id, the running Gallery is stale.
    if (auto* hdr = dynamic_cast<ayt::ui::TextLabel*>(ui.findById("layout_hdr"))) {
        hdr->setText(L"Layout - mini DockArea [AYUI-Gallery-20260827-Images]");
    }

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
                        // "modal: open" ??report Cancel for button dismiss.
                        if (lbl->getText() == L"modal: open") {
                            lbl->setText(L"modal: Cancel");
                        } else if (lbl->getText().find(L"idle") != std::wstring::npos) {
                            lbl->setText(L"modal: dismissed");
                        }
                    }
                });
            }
            // Modal::openModal (not UIManager::openModal alone) mounts the
            // dimmer, sets focus, and runs layout ??required for OK/Cancel.
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

    // Wrap content_host inside ScrollView so every page remains reachable
    // when the window is smaller than the page stack's natural height.
    // The JSON puts content_host as a child of content_scroll via
    // addChild ??ScrollView expects setContent, not addChild, so we
    // re-bind explicitly here. removeChild + addChild keeps the widget
    // tree intact (content_host still owns all page VBoxes).
    if (auto* scroll = dynamic_cast<ayt::ui::ScrollView*>(
            ui.findById("content_scroll"))) {
        if (auto* host = dynamic_cast<ayt::ui::Widget*>(
                ui.findById("content_host"))) {
            scroll->setContent(host);
        }
    }
}

// PR-Dock-TearOff: wire the promote callback into every DockCard of the
// mini_dock (slot cards + overlay floating cards). Dragging a card's
// title bar OUTSIDE the dock now detaches it into a real top-level OS
// window (live-card migration, no JSON rebuild).
//
// MUST run after every loadAndWire (including btn_reload hot reload) ??
// loadLayout rebuilds the dock tree with fresh DockCards that have no
// callback. The promote callback is a member of each DockCard, so wiring
// once per card lifetime is enough (float/dock moves don't reset it).
void wireDockPromotion(GalleryState& state)
{
    ayt::ui::DockArea* dock = dynamic_cast<ayt::ui::DockArea*>(
        state.ui->findById("mini_dock"));
    if (dock == nullptr) {
        std::fprintf(stderr,
            "[AYUI_Gallery] wireDockPromotion: mini_dock not found\n");
        return;
    }
    // Mirror Editor's wirePromoteCallbackRecursive: walk the slot +
    // overlay enumerations, DON'T descend into card subtrees (the dock
    // owns its whole tree through these two enumerations).
    const auto wire = [&state, dock](ayt::ui::DockCard* card) {
        if (card == nullptr) {
            return;
        }
        // Tear-off-capable cards show a header close affordance.
        if (card->isFloatable()) {
            card->setClosable(true);
        }
        card->setOnCloseRequested(
            [&state, dock](ayt::ui::DockCard* closing) {
                if (closing == nullptr) {
                    return;
                }
                // Promoted child HWND first (destroys card with the window).
                if (state.childWindows
                    && state.childWindows->closeCardHost(closing)) {
                    return;
                }
                const std::string id = closing->getId();
                if (!id.empty() && dock != nullptr) {
                    dock->closeCard(id);
                }
            });
        card->setPromoteCallback(
            [&state](
                ayt::ui::DockCard* promoted,
                const std::wstring& title,
                int x, int y, int w, int h) -> bool {
                return state.childWindows->promoteCard(
                    promoted, title, x, y, w, h);
            });
    };
    for (int s = 0; s < static_cast<int>(ayt::ui::DockArea::Slot::Count); ++s) {
        const size_t n = dock->getCardCount(static_cast<ayt::ui::DockArea::Slot>(s));
        for (size_t i = 0; i < n; ++i) {
            wire(dock->getCard(static_cast<ayt::ui::DockArea::Slot>(s), i));
        }
    }
    if (auto* overlay = dock->getOverlay()) {
        for (size_t i = 0; i < overlay->getFloatingCardCount(); ++i) {
            wire(overlay->getFloatingCard(i));
        }
    }
    // D5-redock: promoted cards return to THIS dock. Re-bound on every
    // tree rebuild (btn_reload) ??the old raw pointer dies with the tree.
    if (state.childWindows != nullptr) {
        state.childWindows->setRedockTarget(dock);
    }
    std::fprintf(stderr, "[AYUI_Gallery] dock promote wired\n");
}

// =============================================================================
// Capabilities page ??single-page demo of every shipped PR not yet visible
// in any of the 5 baseline pages. Each block is intentionally small (one or
// two widgets + a status label) so the failure mode is obvious if a PR
// regresses. Order matches the JSON: A3 / C1 / C2 / C3 / B3 / B1 (B1 last
// because Window is mounted on the overlay via C++ rather than JSON).
// =============================================================================

// Pull the long-lived overlay widgets (Tooltip + Window) OFF the overlay
// and free them. Must run BEFORE ui.shutdown() AND BEFORE loadLayout (which
// would otherwise leave the overlay holding a stale pointer to a soon-
// deleted target button ??read-after-free on the next tick()).
//
// Why a separate helper instead of relying on ~GalleryState: the overlay
// outlives GalleryState (it lives inside the UIManager). Without explicit
// detach + delete here, ~GalleryState would free the Widget while the
// overlay's _children still references it; the next update() / render()
// would deref freed memory. Window has the same hazard ??its overlay
// parent would otherwise double-free when the overlay tears down.
//
// Order matters: detach BEFORE delete. Tooltip::detach() pulls itself off
// the overlay AND unregisters from the hover-timer driver; after that the
// raw delete is safe. Window is removed via detachFromParent() so the
// overlay's child list doesn't see a dangling pointer. The Window has no
// dedicated "detachForHostDestruction" analog, but the same trick
// Menu::detachForHostDestruction() uses (break parent back-pointer +
// removeChild) is fine here ??Window's overlay isn't its owner, just a
// mount site.
//
// Idempotent: calling twice is safe ??the second call sees tooltip/window
// already null and short-circuits.
void teardownCapabilitiesOverlay(GalleryState& state) {
    if (state.tooltip != nullptr) {
        // Tooltip::detach() pulls itself off the overlay + unregisters
        // from the hover-timer driver. After detach the tooltip is no
        // longer reachable from any UI tree, so destroyWidgetTree (NOT
        // `delete` ??Tooltip owns its TextLabel child via addChild, and
        // UI-OWN-1 says ~Widget does not free children; destroyWidgetTree
        // walks the tree and frees every reachable widget recursively).
        state.tooltip->detach();
        ayt::ui::destroyWidgetTree(state.tooltip);
        state.tooltip = nullptr;
    }
    if (state.window != nullptr) {
        // Pull off the overlay (no equivalent to Menu::detachForHost
        // Destruction for Window ??but removeChild on the overlay works
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
    // mirrors the text-changed callback ??typing / undo / redo all
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
            // pointer (NOT unique_ptr ??the overlay owns the lifetime;
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
                // Look up the item name via the same ComboBox ??we keep
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
            // PR-C3 hotfix ??capture `fruits` (not just `&ui`) so the
            // hover/activate callbacks can dereference it. Previous build
            // silently fell back to the stale exe (lambda capture was a
            // compile error that ships never caught because the Gallery
            // wasn't rebuilt after the PR-C3 wire-up).
            fruits->setOnHoverChanged([&ui, fruits](int idx) {
                // PR-C3 feedback ??typeahead jumps the highlight via
                // setHoveredIndex; the state label mirrors it so the
                // user sees 'A' ??Apple, 'B' ??Banana, etc. without
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
                    // message with a plain close ??only annotate when
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
            // PR-C3 hotfix ??see Fruits above; same lambda-capture fix needed
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
        // MenuBar exposes its anchors via _menus (private) ??but the
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
    // selection-changed callback ??selecting an item proves wheel scrolled
    // the list and the click resolved correctly.
    if (auto* list = dynamic_cast<ayt::ui::ListView*>(ui.findById("cap_b3_list"))) {
        for (int i = 0; i < 30; ++i) {
            wchar_t buf[32];
            std::swprintf(buf, 32, L"row-%02d", i);
            list->addItem(buf);
        }
        // B3 hotfix ??fire on BOTH selection AND scroll so the user sees
        // feedback whether they clicked a row or just wheel-scrolled.
        // Previously setOnSelectionChanged only fired on click, so a
        // pure wheel-scroll left the label stuck at "(idle)" and the
        // user thought the wheel wasn't routing. The scrollbar callback
        // fires for both wheel (which routes through onMouseWheel ??
        // scrollBy ??setScrollOffset ??syncBarToOffset which mutates the
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
        // Do NOT replace vbar->setOnValueChanged ??that wipes ListView's
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
    // PR-B1 hotfix ??offset below the title bar (28px) with a small
    // breathing margin. Previous wire-up placed the body at local
    // (0,0), which lives under the title bar's gray strip ??the text
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
void bindDockPersistence(GalleryState& state);

// PR-B2 ??flip dark <-> light via F5. Uses ThemeManager::setActiveTheme
// so the composer's composed sheet is swapped into the global
// StyleManager and any onThemeChanged listeners get notified. We do NOT
// touch individual widget style ids ??resolveStyle() reads the active
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

// Backend page -- creates the two UI textures (64x64 smooth gradient +
// 16x16 rounded button) and mounts the host-drawn demo widget into
// page_backend's VBox (fixed-height slot). Textures are registered with
// UIRenderBackend (backend-specific API -- AYUI's IRenderBackend only
// passes opaque handles). Textures are created once; the widget is
// re-created on every reload (loadLayout built a fresh page_backend).
void wireBackendPage(GalleryState& state)
{
    if (state.uiBackend == nullptr || !state.uiBackend->isInitialized()) {
        return;
    }
    ayt::ui::VBox* page = dynamic_cast<ayt::ui::VBox*>(
        state.ui->findById("page_backend"));
    if (page == nullptr) {
        return;
    }

    if (state.backendTexGradient == nullptr) {
        // 64x64 diagonal gradient. Smooth on purpose -- LINEAR filtering is
        // the point; hard edges would hide sampling artifacts.
        std::vector<uint8_t> px(64u * 64u * 4u, 255u);
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                const float fx = x / 63.0f;
                const float fy = y / 63.0f;
                uint8_t* p = &px[(y * 64 + x) * 4u];
                p[0] = static_cast<uint8_t>((0.15f + 0.80f * fx) * 255.0f);           // B
                p[1] = static_cast<uint8_t>((0.35f + 0.45f * fy) * 255.0f);           // G
                p[2] = static_cast<uint8_t>((0.10f + 0.85f * (1.0f - fx)) * 255.0f);  // R
                p[3] = 255u;
            }
        }
        state.backendTexGradient = state.uiBackend->createUiTexture(64, 64, px.data());
    }
    if (state.backendTexNine == nullptr) {
        // Clean 32x32 rounded button (radius 8, ~2.5px bright ring, blue
        // body). Larger source keeps corner arcs sharp under LINEAR 9-slice
        // at 48/96/192 — the old 16x16 looked sketched/wobbly (kept below
        // as an explicit style sample).
        constexpr int N = 32;
        std::vector<uint8_t> px(static_cast<size_t>(N * N * 4), 0u);
        const float hx = (N - 1) * 0.5f;
        const float hy = (N - 1) * 0.5f;
        const float rad = 8.0f;
        for (int y = 0; y < N; ++y) {
            for (int x = 0; x < N; ++x) {
                const float qx = std::fabs(static_cast<float>(x) - hx) - (hx - rad);
                const float qy = std::fabs(static_cast<float>(y) - hy) - (hy - rad);
                const float d =
                    std::sqrt(std::max(qx, 0.0f) * std::max(qx, 0.0f) +
                              std::max(qy, 0.0f) * std::max(qy, 0.0f)) +
                    std::min(std::max(qx, qy), 0.0f) - rad;
                const float cover = std::clamp(0.5f - d, 0.0f, 1.0f);
                if (cover <= 0.0f) {
                    continue;
                }
                uint8_t* p = &px[static_cast<size_t>((y * N + x) * 4)];
                const bool ring = d > -2.5f;
                // BGRA upload path (createUiTexture swizzles to RGBA).
                p[0] = ring ? 252u : 217u;  // B
                p[1] = ring ? 248u : 122u;  // G
                p[2] = ring ? 245u : 55u;   // R
                p[3] = static_cast<uint8_t>(cover * 255.0f + 0.5f);
            }
        }
        state.backendTexNine = state.uiBackend->createUiTexture(
            static_cast<uint16_t>(N), static_cast<uint16_t>(N), px.data());
    }
    if (state.backendTexNineStyle == nullptr) {
        // Stylized 16x16 tan + sketched bright ring — intentional look,
        // not the BK7 baseline.
        std::vector<uint8_t> px(16u * 16u * 4u, 0u);
        const float hx = 7.5f, hy = 7.5f, r = 4.0f;
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                const float qx = std::fabs(x - hx) - (hx - r);
                const float qy = std::fabs(y - hy) - (hy - r);
                const float d =
                    std::sqrt(std::max(qx, 0.0f) * std::max(qx, 0.0f) +
                              std::max(qy, 0.0f) * std::max(qy, 0.0f)) +
                    std::min(std::max(qx, qy), 0.0f) - r;
                if (d <= 0.0f) {
                    uint8_t* p = &px[(y * 16 + x) * 4u];
                    const bool ring = d > -2.0f;
                    p[0] = ring ? 235u : 150u;  // B
                    p[1] = ring ? 240u : 168u;  // G
                    p[2] = ring ? 245u : 186u;  // R
                    p[3] = 255u;
                }
            }
        }
        state.backendTexNineStyle =
            state.uiBackend->createUiTexture(16, 16, px.data());
    }

    auto* demo = new BackendDemoWidget();
    demo->setTextures(state.backendTexGradient, state.backendTexNine,
                      state.backendTexNineStyle);
    demo->setParams(&state.backendParams);

    // Live parameter panel — each slider writes into backendParams;
    // BackendDemoWidget reads them every frame.
    auto* panel = new ayt::ui::VBox();
    panel->setSpacing(4.0f);
    panel->setPadding(0.0f, 4.0f, 0.0f, 4.0f);

    auto addSliderRow = [&](const wchar_t* label, float* target,
                            float minV, float maxV, float rowH = 26.0f) {
        auto* row = new ayt::ui::HBox();
        row->setSpacing(8.0f);
        row->setPadding(0, 0, 0, 0);
        auto* lbl = new ayt::ui::TextLabel();
        lbl->setText(label);
        lbl->setSize(ayt::math::FVector2(150.0f, rowH));
        auto* val = new ayt::ui::TextLabel();
        {
            wchar_t buf[32];
            std::swprintf(buf, 32, L"%.1f", *target);
            val->setText(buf);
        }
        val->setSize(ayt::math::FVector2(48.0f, rowH));
        auto* sld = new ayt::ui::Slider();
        sld->setSize(ayt::math::FVector2(280.0f, rowH));
        sld->setValueRange(minV, maxV);
        sld->setValue(*target);
        sld->setOnValueChanged([target, val](float v) {
            *target = v;
            wchar_t buf[32];
            std::swprintf(buf, 32, L"%.1f", v);
            val->setText(buf);
        });
        row->addWidget(lbl, 150.0f);
        row->addWidget(sld, 0.0f);   // fill
        row->addWidget(val, 48.0f);
        panel->addWidget(row, rowH);
    };

    auto* panelHdr = new ayt::ui::TextLabel();
    panelHdr->setText(L"Live params (drag — canvas updates every frame)");
    panelHdr->setSize(ayt::math::FVector2(640.0f, 20.0f));
    panel->addWidget(panelHdr, 20.0f);

    addSliderRow(L"P1 blend alpha", &state.backendParams.blendAlpha, 0.1f, 1.0f);
    addSliderRow(L"P2 border width", &state.backendParams.borderWidth, 1.0f, 12.0f);
    addSliderRow(L"P2 border R0", &state.backendParams.borderR0, 0.0f, 40.0f);
    addSliderRow(L"P2 border R1", &state.backendParams.borderR1, 0.0f, 40.0f);
    addSliderRow(L"P2 border R2", &state.backendParams.borderR2, 0.0f, 48.0f);
    addSliderRow(L"P2 shadow blurA", &state.backendParams.shadowBlurA, 0.0f, 32.0f);
    addSliderRow(L"P2 shadow blurB", &state.backendParams.shadowBlurB, 0.0f, 32.0f);
    addSliderRow(L"P2 shadow offset", &state.backendParams.shadowOffset, 0.0f, 16.0f);
    addSliderRow(L"P2 shadow corner", &state.backendParams.shadowCorner, 0.0f, 40.0f);
    addSliderRow(L"P2 combo radius", &state.backendParams.comboRadius, 0.0f, 40.0f);
    addSliderRow(L"P2 combo stroke", &state.backendParams.comboStroke, 0.0f, 12.0f);
    addSliderRow(L"P2 combo blur", &state.backendParams.comboBlur, 0.0f, 32.0f);
    addSliderRow(L"P3 nine padding", &state.backendParams.ninePad, 1.0f, 14.0f);

    // 20 header + 13*26 + spacing/padding ≈ 380
    page->addWidget(panel, 380.0f);
    page->addWidget(demo, 900.0f);
    state.backendDemo = demo;

    if (auto* hdr = dynamic_cast<ayt::ui::TextLabel*>(
            state.ui->findById("backend_hdr"))) {
        hdr->setText(
            L"Backend - UIRenderBackend [SdfClipShape-20260812k]");
    }
}

// Images page -- exercises the public Image widget rather than the backend
// canvas: one named/shared atlas, per-widget UVs, opacity, an Image child
// layered over a Button, and controls layered over image siblings in a Panel.
void wireImageCompositionPage(GalleryState& state)
{
    if (state.uiBackend == nullptr || !state.uiBackend->isInitialized()) {
        return;
    }

    auto findImage = [&state](const char* id) {
        return dynamic_cast<ayt::ui::Image*>(state.ui->findById(id));
    };
    ayt::ui::Image* full = findImage("img_texture_full");
    ayt::ui::Image* crop = findImage("img_texture_crop");
    ayt::ui::Image* icon = findImage("img_icon_button_icon");
    ayt::ui::Image* backdrop = findImage("img_card_backdrop");
    ayt::ui::Image* wash = findImage("img_card_wash");
    ayt::ui::Image* badge = findImage("img_card_badge");
    if (full == nullptr || crop == nullptr || icon == nullptr ||
        backdrop == nullptr || wash == nullptr || badge == nullptr) {
        std::fprintf(stderr,
                     "[AYUI_Gallery] Images page incomplete; texture demo skipped\n");
        return;
    }

    constexpr int N = 128;
    std::vector<uint8_t> px(static_cast<size_t>(N * N * 4), 255u);
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            const bool right = x >= N / 2;
            const bool bottom = y >= N / 2;
            const float u = static_cast<float>(x % (N / 2)) / 63.0f;
            const float v = static_cast<float>(y % (N / 2)) / 63.0f;
            uint8_t b = 0, g = 0, r = 0, a = 255;
            if (!right && !bottom) {
                // Blue/cyan gradient tile.
                b = static_cast<uint8_t>((0.55f + 0.40f * u) * 255.0f);
                g = static_cast<uint8_t>((0.25f + 0.60f * v) * 255.0f);
                r = static_cast<uint8_t>((0.08f + 0.18f * u) * 255.0f);
            } else if (right && !bottom) {
                // Warm checker tile -- makes UV cropping unmistakable.
                const bool light = ((x / 8) + (y / 8)) % 2 == 0;
                b = light ? 45u : 28u;
                g = light ? 154u : 93u;
                r = light ? 246u : 203u;
            } else if (!right && bottom) {
                // Green diagonal stripe tile used as a translucent wash.
                const bool stripe = ((x + y) / 7) % 2 == 0;
                b = stripe ? 92u : 50u;
                g = stripe ? 205u : 142u;
                r = stripe ? 66u : 28u;
            } else {
                // Alpha-backed circular badge tile.
                const float dx = u - 0.5f;
                const float dy = v - 0.5f;
                const float d = std::sqrt(dx * dx + dy * dy);
                const float cover = std::clamp((0.48f - d) * 24.0f, 0.0f, 1.0f);
                b = static_cast<uint8_t>((0.72f + 0.20f * v) * 255.0f);
                g = static_cast<uint8_t>((0.22f + 0.22f * u) * 255.0f);
                r = static_cast<uint8_t>((0.72f + 0.24f * u) * 255.0f);
                a = static_cast<uint8_t>(cover * 255.0f + 0.5f);
            }
            // createUiTexture consumes BGRA and swizzles for the renderer.
            uint8_t* p = &px[static_cast<size_t>((y * N + x) * 4)];
            p[0] = b;
            p[1] = g;
            p[2] = r;
            p[3] = a;
        }
    }

    state.imageTexAtlas = state.uiBackend->createUiTexture(N, N, px.data());
    if (state.imageTexAtlas == nullptr) {
        std::fprintf(stderr, "[AYUI_Gallery] Images page texture creation failed\n");
        return;
    }
    ayt::ui::TextureRegistry::get().registerExternal(
        kGalleryImageTextureName, state.imageTexAtlas, N, N,
        ayt::ui::TextureFormat::RGBA8);

    auto bindTexture = [](ayt::ui::Image* image,
                          const ayt::math::FRectangle& uv,
                          float opacity = 1.0f) {
        image->setTexture(kGalleryImageTextureName);
        image->setUV(uv);
        image->setOpacity(opacity);
    };
    bindTexture(full,     ayt::math::FRectangle(0.0f, 0.0f, 1.0f, 1.0f));
    bindTexture(crop,     ayt::math::FRectangle(0.5f, 0.0f, 1.0f, 0.5f));
    bindTexture(icon,     ayt::math::FRectangle(0.0f, 0.0f, 0.5f, 0.5f));
    bindTexture(backdrop, ayt::math::FRectangle(0.0f, 0.0f, 1.0f, 1.0f));
    bindTexture(wash,     ayt::math::FRectangle(0.0f, 0.5f, 0.5f, 1.0f), 0.28f);
    bindTexture(badge,    ayt::math::FRectangle(0.5f, 0.5f, 1.0f, 1.0f), 0.90f);

    if (auto* button = dynamic_cast<ayt::ui::Button*>(
            state.ui->findById("img_icon_button"))) {
        button->setPadding(48.0f, 4.0f, 10.0f, 4.0f);
    }
    auto bindClick = [&state](const char* id) {
        if (auto* button = dynamic_cast<ayt::ui::Button*>(state.ui->findById(id))) {
            button->setOnClicked([&state]() {
                ++state.imageClickCount;
                if (auto* label = dynamic_cast<ayt::ui::TextLabel*>(
                        state.ui->findById("img_click_state"))) {
                    wchar_t text[64];
                    std::swprintf(text, 64, L"image composition clicks: %d",
                                  state.imageClickCount);
                    label->setText(text);
                }
            });
        }
    };
    bindClick("img_icon_button");
    bindClick("img_overlay_button");
}

// Drop every Image's named registry ref before releasing the one backend
// texture they share. This runs before loadLayout destroys the old tree and
// before UIRenderBackend shutdown, keeping hot reload and exit leak-free.
void teardownImageCompositionPage(GalleryState& state)
{
    for (const char* id : {
             "img_texture_full", "img_texture_crop", "img_icon_button_icon",
             "img_card_backdrop", "img_card_wash", "img_card_badge"}) {
        if (auto* image = dynamic_cast<ayt::ui::Image*>(state.ui->findById(id))) {
            if (image->getTextureName() == kGalleryImageTextureName) {
                image->setTexture(std::string{});
            }
        }
    }
    if (state.imageTexAtlas != nullptr) {
        state.uiBackend->releaseUiTexture(state.imageTexAtlas);
        state.imageTexAtlas = nullptr;
    }
}

// Releases the backend textures and drops the demo pointer. The widget is
// tree-owned (loadLayout deletes it); the textures are backend-owned and
// MUST be released here -- before loadLayout on reload, and before
// uiBackend.shutdown() on exit (else the GPU textures leak).
void teardownBackendPage(GalleryState& state)
{
    state.backendDemo = nullptr;
    if (state.backendTexGradient != nullptr) {
        state.uiBackend->releaseUiTexture(state.backendTexGradient);
        state.backendTexGradient = nullptr;
    }
    if (state.backendTexNine != nullptr) {
        state.uiBackend->releaseUiTexture(state.backendTexNine);
        state.backendTexNine = nullptr;
    }
    if (state.backendTexNineStyle != nullptr) {
        state.uiBackend->releaseUiTexture(state.backendTexNineStyle);
        state.backendTexNineStyle = nullptr;
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
    wireImageCompositionPage(state);
    wireCapabilities(state);
    wireBackendPage(state);
    bindReload(state);
    // PR-Dock-TearOff: reload rebuilt the dock tree with fresh cards ??
    // re-inject the promote callback every load (hot reload included).
    wireDockPromotion(state);
    // PR-DockTree-Phase4: save/load buttons die with the reloaded tree ??
    // rebind them on every load too.
    bindDockPersistence(state);
    std::fprintf(stderr, "[AYUI_Gallery] loaded %s\n", state.layoutPath.c_str());

    // Unmistakable build fingerprint (console can be missed under WIN32).
    // Window title + file next to cwd: if you don't see these, wrong exe.
    constexpr const char* kDockBuildId =
        "AYUI-Gallery-20260827-Images";
    std::fprintf(stderr, "[AYUI_Gallery] BUILD %s\n", kDockBuildId);
    std::fprintf(stderr, "[AYUI_Gallery] dock trace log: %s\n",
                 ayt::ui::dockTracePath());
    ayt::ui::dockTrace("[gallery] ===== session start BUILD %s layout=%s =====\n",
                       kDockBuildId, state.layoutPath.c_str());
    if (FILE* stamp = std::fopen("ayui_gallery_build.txt", "w")) {
        std::fprintf(stamp, "%s\nlayout=%s\ntrace=%s\n", kDockBuildId,
                     state.layoutPath.c_str(), ayt::ui::dockTracePath());
        std::fclose(stamp);
    }
    if (auto* dock = dynamic_cast<ayt::ui::DockArea*>(
            state.ui->findById("mini_dock"))) {
        (void)dock;
        std::fprintf(stderr, "[AYUI_Gallery] mini_dock OK (%s)\n", kDockBuildId);
    } else {
        std::fprintf(stderr, "[AYUI_Gallery] mini_dock MISSING\n");
    }
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
            teardownImageCompositionPage(state);
            teardownBackendPage(state);
            state.clickCount = 0;
            state.imageClickCount = 0;
            // Use state.layoutPath (lives in GalleryState), not a path
            // captured inside this lambda ??loadLayout destroys this
            // Button / std::function before ifstream::open returns.
            (void)loadAndWire(state);
        });
    }
}

// PR-DockTree-Phase4: Save/Load the dock tree (structure + tab ids +
// active + floating rects) to gallery_dock_tree.json next to cwd ??
// same cwd-relative file pattern as the build stamp above.
void bindDockPersistence(GalleryState& state)
{
    if (auto* btn = dynamic_cast<ayt::ui::Button*>(state.ui->findById("btn_save_dock"))) {
        btn->setOnClicked([&state]() {
            auto* dock = dynamic_cast<ayt::ui::DockArea*>(
                state.ui->findById("mini_dock"));
            if (dock == nullptr) {
                return;
            }
            const std::string s = dock->serializeDockTree();
            if (FILE* f = std::fopen("gallery_dock_tree.json", "w")) {
                std::fputs(s.c_str(), f);
                std::fclose(f);
                std::fprintf(stderr,
                             "[AYUI_Gallery] dock tree saved (%zu bytes)\n",
                             s.size());
            } else {
                std::fprintf(stderr,
                             "[AYUI_Gallery] FAILED to write gallery_dock_tree.json\n");
            }
        });
    }
    if (auto* btn = dynamic_cast<ayt::ui::Button*>(state.ui->findById("btn_load_dock"))) {
        btn->setOnClicked([&state]() {
            auto* dock = dynamic_cast<ayt::ui::DockArea*>(
                state.ui->findById("mini_dock"));
            if (dock == nullptr) {
                return;
            }
            std::string s;
            if (FILE* f = std::fopen("gallery_dock_tree.json", "rb")) {
                char buf[4096];
                size_t n;
                while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
                    s.append(buf, n);
                }
                std::fclose(f);
            } else {
                std::fprintf(stderr,
                             "[AYUI_Gallery] FAILED to read gallery_dock_tree.json\n");
                return;
            }
            if (!dock->applyDockTree(s)) {
                std::fprintf(stderr,
                             "[AYUI_Gallery] FAILED to apply dock tree\n");
                return;
            }
            // applyDockTree moves live card objects ??re-inject the
            // promote callback so torn-off child windows keep working.
            wireDockPromotion(state);
        });
    }
}

// PR-S5: UiCursorHint ??Win32 cursor. The UI layer computes hints
// (Window resize edges / title-bar Move, ScrollBar SizeNs, Hand, Beam)
// but Gallery never applied them ??resize/drag felt "functional but the
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
    // drags a finger ??same code path as a real wheel on a desktop.
    struct TouchState {
        bool    active       = false;
        // PR-B5 ??dragStarted flips true once the finger crosses the
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
    // Wheel scale: a 100-px finger drag ??1 notch of mouse wheel delta
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
        // of a valid cursor (Device pos stayed at 0,0 ??pickTopmost miss).
        return 0;
    }
    case WM_SETCURSOR: {
        // PR-S5: apply the UI layer's cursor hint (resize edges,
        // title-bar Move, Beam, Hand). SetCursor here
        // and skip DefWindowProc so the OS doesn't snap back to arrow.
        // _hoverWidget/_capturedWidget are already fresh ??WM_SETCURSOR
        // follows the WM_MOUSEMOVE that updated them.
        ::SetCursor(cursorForHint(state->ui->getCursorHint()));
        handled = true;
        return 0;
    }
    case WM_MOUSEWHEEL: {
        // Primary wheel path: client coords from the message (not Device
        // mouse pos). Gallery swallows move for UI but must own wheel too
        // ??otherwise only the post-poll Device bridge runs, often at a
        // stale (0,0) pick point.
        //
        // Sign: Win32 positive = wheel away / natural trackpad "swipe up"
        // often arrives as negative. UI scrollOffset increases to reveal
        // lower content (browser-like: finger up ??content up). Negate
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
        // PR-Dock-TearOff: while a G12 dock drag is active, capture the
        // mouse so moves + the release keep routing to the MAIN window
        // even when the cursor crosses a promoted child top-level window
        // (otherwise the drag session stalls over the child and the
        // void-drop promote fires from a stale position). Released on the
        // matching UP below (and re-synced on any DOWN if the drag was
        // Esc-cancelled).
        if (state->devices != nullptr) {
            HWND hwnd = static_cast<HWND>(
                state->devices->window().getWindowHandle());
            if (state->ui->isDragging()) {
                if (hwnd != nullptr) ::SetCapture(hwnd);
            } else if (hwnd != nullptr && ::GetCapture() == hwnd) {
                ::ReleaseCapture();
            }
        }
        handled = true;
        return 0;
    }
    case WM_LBUTTONUP: {
        const float x = static_cast<float>(GET_X_LPARAM(lParam));
        const float y = static_cast<float>(GET_Y_LPARAM(lParam));
        state->ui->onMouseButtonUp(x, y, 0);
        if (state->devices != nullptr) {
            HWND hwnd = static_cast<HWND>(
                state->devices->window().getWindowHandle());
            if (hwnd != nullptr && ::GetCapture() == hwnd) {
                ::ReleaseCapture();
            }
        }
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
        // PR-B2 ??F5 toggles dark <-> light. Intercept BEFORE the UI key
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
        // track the FIRST active touch ??the Gallery is single-finger
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
            // First contact ??record the tentative tap. We DO NOT
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

            // PR-B5 ??once the finger crosses the drag threshold,
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
                // Threshold = 8 px (one wheel notch ??6-8 px on most
                // precision touchpads).
                constexpr float kTouchWheelThresholdPx = 8.0f;
                if (std::fabs(gTouch.accumulatedDy) >= kTouchWheelThresholdPx) {
                    // Sign convention: UIManager::onMouseWheel deltaY is
                    // "content to move by -deltaY in y" (scroll wheel up
                    // has positive deltaY per Win32 convention). Our
                    // accumulatedDy is "finger moved down" which feels
                    // like "scroll content up" ??so we negate.
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
    // Title carries the build id so a wrong/old exe is obvious without
    // hunting the AllocConsole window.
    cfg.window.title = "AYUI Gallery [Images / Composition]";
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

    // PR-B2 ??install built-in dark theme. ensureDefaultThemes is
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

    // TextInput focus gate + Device?UI text/IME bridge.
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

    // PR-Dock-TearOff: child-window host ??must exist BEFORE the first
    // loadAndWire because wireDockPromotion's lambdas close over
    // state.childWindows.
    state.childWindows =
        std::make_unique<ayt::gallery::GalleryChildWindows>(window, ui);

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
                 "[AYUI_Gallery] ready ??UI-only composite (no RenderScene)\n"
                 "[AYUI_Gallery] sections: Basics / Images / Input / Collections / "
                 "Overlay / Layout / Capabilities / Backend / Animation\n");

    LARGE_INTEGER qpcFreq{};
    LARGE_INTEGER qpcPrev{};
    ::QueryPerformanceFrequency(&qpcFreq);
    ::QueryPerformanceCounter(&qpcPrev);

    while (state.running && window.isWindowValid()) {
        state.wheelHandledThisFrame = false;
        devices.pollEvents();

        // Fallback bridge: precision trackpads may deliver wheel only via
        // WM_INPUT ??MouseDevice (no WM_MOUSEWHEEL). Use UIManager's last
        // mouse (updated by WM_MOUSEMOVE), never Device pos alone ??move
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
                    // Same Win32?UI sign flip as WM_MOUSEWHEEL handler.
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
        // PR-Dock-TearOff: tick promoted child windows BEFORE the primary
        // (each child updates + GDI-renders under its own ActiveScope,
        // then the primary takes the active slot back).
        state.childWindows->tickAll(dt);
        ui.update(dt); // caret blink, hover revalidate, hot-reload
        ui.layout();
        // Drop guides paint inside DockArea::render (after its children).
        // Do NOT call paintDropGuide again here ??that stacked a second
        // copy of the Phase-3 join/split preview on top of the first.
        // Child-window redock still works: tickAll ??updateRedockHover
        // sets setExternalDropPos before populateFrame.
        ui.populateFrame();
        ui.flushFrame();

        renderer.endFrame();
    }

    state.modal.reset();
    state.modalBody.reset();
    // CRITICAL: tear down Capabilities overlay widgets BEFORE ui.shutdown.
    // Otherwise ~UIManager tears down the overlay (deleting Tooltip /
    // Window), and the dangling raw pointers in state.tooltip / state.window
    // survive ??the SECOND free would happen in ~GalleryState (after this
    // function returns) and SEGV. teardownCapabilitiesOverlay pulls the
    // widgets off the overlay AND deletes them here so the overlay's
    // subsequent shutdown sees no children to free.
    teardownCapabilitiesOverlay(state);
    teardownImageCompositionPage(state);
    // Backend page textures (UIRenderBackend registry) MUST be released
    // before uiBackend.shutdown() -- the registry frees GPU textures in
    // shutdown, double-release is a no-op, but the handles here would
    // dangle across the shutdown boundary.
    teardownBackendPage(state);
    // PR-Dock-TearOff: destroy child windows BEFORE the primary UI
    // (K-INV-D5-6) ??the child UIManagers (and the promoted cards living
    // in their roots) free here; the primary's shutdown then finds a
    // clean tree.
    state.childWindows.reset();
    ui.shutdown();
    uiBackend.shutdown();
    renderer.shutdown();
    devices.shutdown();
    return 0;
}
