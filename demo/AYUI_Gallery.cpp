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

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>

#include "AYUI.h"
#include "AYUI/DeviceInputBridge.h"
#include "AYUI/UIManager.h"
#include "AYUI/Button.h"
#include "AYUI/Image.h"
#include "AYUI/TextureRegistry.h"
#include "AYUI/CheckBox.h"
#include "AYUI/Slider.h"
#include "AYUI/Box.h"
#include "AYUI/ProgressBar.h"
#include "AYUI/TextLabel.h"
#include "AYUI/RichText.h"
#include "AYDevice/TextInput.h"
#include "AYUI/TabStrip.h"
#include "AYUI/Spinner.h"
#include "AYUI/ListView.h"
#include "AYUI/TileView.h"
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
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <utility>
#include <vector>

namespace {

constexpr int kWidth  = 1280;
constexpr int kHeight = 720;
constexpr const char* kGalleryImageTextureName = "gallery/composition_atlas";

struct VisualCaptureConfig {
    bool enabled = false;
    ayt::render::UIRenderBackend::BatchMode batchMode =
        ayt::render::UIRenderBackend::BatchMode::OverlapAware;
    std::string pageId = "page_backend";
    std::string action;
    std::string outputBase;
    std::string layerProbeScenario;
    std::string layerMatrixScenario;
    ayt::render::Backend backend = ayt::render::Backend::Auto;
    bool rootLayerEnabled = true;
    float scrollY = 0.0f;
    float captureScale = 1.0f;
    int captureFrame = 12;
    int mutationFrame = 6;
    int exitFrame = 14;
};

void writeCaptureStartupStage(const VisualCaptureConfig& capture,
                              const char* stage)
{
    if (!capture.enabled || capture.outputBase.empty() || stage == nullptr) return;
    const std::string path = capture.outputBase + ".startup.txt";
    FILE* output = nullptr;
    if (fopen_s(&output, path.c_str(), "ab") == 0 && output != nullptr) {
        std::fprintf(output, "%s\n", stage);
        std::fclose(output);
    }
}

VisualCaptureConfig visualCaptureConfig()
{
    VisualCaptureConfig config;

    if (const char* base = std::getenv("AY_UI_GALLERY_CAPTURE_BASE");
        base != nullptr && base[0] != '\0') {
        config.enabled = true;
        config.outputBase = base;
    }
    if (const char* page = std::getenv("AY_UI_GALLERY_CAPTURE_PAGE");
        page != nullptr && page[0] != '\0') {
        config.pageId = page;
    }
    if (const char* mode = std::getenv("AY_UI_GALLERY_BATCH_MODE");
        mode != nullptr && std::strcmp(mode, "ordered") == 0) {
        config.batchMode = ayt::render::UIRenderBackend::BatchMode::OrderedRuns;
    }
    if (const char* action = std::getenv("AY_UI_GALLERY_CAPTURE_ACTION");
        action != nullptr && action[0] != '\0') {
        config.action = action;
    }
    if (const char* scroll = std::getenv("AY_UI_GALLERY_CAPTURE_SCROLL_Y");
        scroll != nullptr && scroll[0] != '\0') {
        char* end = nullptr;
        const float parsed = std::strtof(scroll, &end);
        if (end != scroll && parsed > 0.0f) {
            config.scrollY = parsed;
        }
    }
    if (const char* scenario = std::getenv("AY_UI_GALLERY_LAYER_PROBE_SCENARIO");
        scenario != nullptr && scenario[0] != '\0') {
        config.layerProbeScenario = scenario;
        config.pageId = "page_layer_visual";
    }
    if (const char* scenario = std::getenv("AY_UI_GALLERY_LAYER_MATRIX_SCENARIO");
        scenario != nullptr && scenario[0] != '\0') {
        config.layerMatrixScenario = scenario;
    }
    if (const char* rootLayer = std::getenv("AY_UI_GALLERY_ROOT_LAYER");
        rootLayer != nullptr && std::strcmp(rootLayer, "immediate") == 0) {
        config.rootLayerEnabled = false;
    }
    if (const char* scale = std::getenv("AY_UI_GALLERY_CAPTURE_SCALE");
        scale != nullptr && scale[0] != '\0') {
        char* end = nullptr;
        const float parsed = std::strtof(scale, &end);
        if (end != scale && std::isfinite(parsed) && parsed > 0.0f) {
            config.captureScale = parsed;
        }
    }
    if (const char* frame = std::getenv("AY_UI_GALLERY_CAPTURE_FRAME");
        frame != nullptr && frame[0] != '\0') {
        char* end = nullptr;
        const long parsed = std::strtol(frame, &end, 10);
        if (end != frame && parsed > 0 && parsed < 10000) {
            config.captureFrame = static_cast<int>(parsed);
        }
    }
    if (const char* frame = std::getenv("AY_UI_GALLERY_MUTATION_FRAME");
        frame != nullptr && frame[0] != '\0') {
        char* end = nullptr;
        const long parsed = std::strtol(frame, &end, 10);
        if (end != frame && parsed > 0 && parsed < 10000) {
            config.mutationFrame = static_cast<int>(parsed);
        }
    }
    if (const char* backend = std::getenv("AY_UI_GALLERY_BACKEND");
        backend != nullptr && backend[0] != '\0') {
        if (std::strcmp(backend, "d3d11") == 0) {
            config.backend = ayt::render::Backend::Direct3D11;
        } else if (std::strcmp(backend, "d3d12") == 0) {
            config.backend = ayt::render::Backend::Direct3D12;
        } else if (std::strcmp(backend, "vulkan") == 0) {
            config.backend = ayt::render::Backend::Vulkan;
        } else if (std::strcmp(backend, "opengl") == 0) {
            config.backend = ayt::render::Backend::OpenGL;
        }
    }
    config.exitFrame = config.captureFrame + 3;
    return config;
}

const char* backendName(ayt::render::Backend backend)
{
    switch (backend) {
    case ayt::render::Backend::Direct3D11: return "d3d11";
    case ayt::render::Backend::Direct3D12: return "d3d12";
    case ayt::render::Backend::Vulkan: return "vulkan";
    case ayt::render::Backend::OpenGL: return "opengl";
    case ayt::render::Backend::Metal: return "metal";
    case ayt::render::Backend::Noop: return "noop";
    default: return "auto";
    }
}

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

struct LoadedBgraImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
};

std::string resolveVisualAssetPath(const char* fileName)
{
    const std::string relative = std::string("visual_regression/") + fileName;
    const std::vector<std::string> candidates = {
        std::string("assets/") + relative,
        std::string("AYRuntime/AYUI/demo/assets/") + relative,
        std::string("../AYRuntime/AYUI/demo/assets/") + relative,
        std::string("../../AYRuntime/AYUI/demo/assets/") + relative,
    };
    for (const std::string& path : candidates) {
        if (fileExists(path)) return path;
    }
    return candidates.front();
}

bool loadVisualPngBgra(const char* fileName, LoadedBgraImage& out)
{
    const std::string path = resolveVisualAssetPath(fileName);
    int components = 0;
    stbi_uc* rgba = stbi_load(path.c_str(), &out.width, &out.height, &components, 4);
    if (rgba == nullptr || out.width <= 0 || out.height <= 0) {
        std::fprintf(stderr, "[AYUI_Gallery] visual texture load failed: %s (%s)\n",
                     path.c_str(), stbi_failure_reason());
        if (rgba != nullptr) stbi_image_free(rgba);
        out = {};
        return false;
    }
    const size_t byteCount = static_cast<size_t>(out.width)
        * static_cast<size_t>(out.height) * 4u;
    out.pixels.assign(rgba, rgba + byteCount);
    stbi_image_free(rgba);
    // UIRenderBackend::createUiTexture currently accepts BGRA input and
    // swizzles it to the renderer's RGBA texture. Keep file decoding explicit
    // so the probe covers the same public upload path as production widgets.
    for (size_t i = 0; i < byteCount; i += 4u) {
        std::swap(out.pixels[i + 0u], out.pixels[i + 2u]);
    }
    return true;
}

class LayerVisualProbeWidget;

struct GalleryState {
    ayt::ui::UIManager* ui = nullptr;
    ayt::render::Renderer* renderer = nullptr;
    ayt::render::UIRenderBackend* uiBackend = nullptr;
    ayt::device::DeviceManager* devices = nullptr;
    int clientW = kWidth;
    int clientH = kHeight;
    float dpiScale = 1.0f;
    float uiScale = 1.0f;
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
    bool enableLayerVisualProbe = false;
    LayerVisualProbeWidget* layerVisualProbe = nullptr;
    void* layerProbeChecker = nullptr;
    void* layerProbeArrow = nullptr;
    void* layerProbeOrc = nullptr;
    // Animation page: fade-panel visibility latch (survives reload like
    // the other knobs; lives here, not in a button lambda, so the state
    // is not destroyed with the JSON tree).
    bool panelVisible = true;
    std::unique_ptr<ayt::ui::AnimationTimeline> showcaseTimeline;
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

    struct TouchGesture {
        bool active = false;
        bool dragStarted = false;
        int64_t pointerId = -1;
        float x = 0.0f;
        float y = 0.0f;
        float startX = 0.0f;
        float startY = 0.0f;
        float lastY = 0.0f;
        float accumulatedDy = 0.0f;
    } touch;

    // PR-Dock-TearOff: promoted DockCards' top-level child windows.
    // Constructed ONCE in wWinMain BEFORE the first loadAndWire (the
    // promote callback captured by wireDockPromotion closes over this
    // pointer); reset() before ui.shutdown() so child windows die
    // before the primary UI (K-INV-D5-6).
    std::unique_ptr<ayt::gallery::GalleryChildWindows> childWindows;
    std::unique_ptr<ayt::ui::AccessibilityAdapter> accessibility;
};

void showPage(ayt::ui::UIManager& ui, const char* pageId)
{
    static const char* kPages[] = {
        "page_basics", "page_images", "page_input", "page_collections",
        "page_overlay", "page_layout", "page_capabilities",
        "page_backend", "page_animation", "page_productization",
        "page_layer_visual",
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
            else if (std::strcmp(pageId, "page_productization") == 0) msg += L"productization";
            else if (std::strcmp(pageId, "page_layer_visual") == 0) msg += L"layer visual probe";
            lbl->setText(msg);
        }
    }
}

void updateProductScaleLabel(GalleryState& state)
{
    if (state.ui == nullptr) return;
    if (auto* label = dynamic_cast<ayt::ui::TextLabel*>(
            state.ui->findById("product_scale_state"))) {
        wchar_t text[160];
        const ayt::math::FVector2 logical = state.ui->getClientSize();
        std::swprintf(text, std::size(text),
                      L"DPI %.2fx × UI %.2fx = %.2fx; logical viewport %.0f × %.0f DIP",
                      state.dpiScale, state.uiScale,
                      state.ui->getEffectiveScale(), logical.x, logical.y);
        label->setText(text);
    }
}

void wireProductizationPage(GalleryState& state)
{
    ayt::ui::UIManager& ui = *state.ui;
    auto bindScale = [&state, &ui](const char* id, float scale) {
        if (auto* button = dynamic_cast<ayt::ui::Button*>(ui.findById(id))) {
            button->setOnClicked([&state, scale]() {
                state.uiScale = scale;
                state.ui->setUiScale(scale);
                updateProductScaleLabel(state);
            });
        }
    };
    bindScale("product_scale_100", 1.0f);
    bindScale("product_scale_125", 1.25f);
    bindScale("product_scale_150", 1.5f);

    ayt::ui::Theme inherited;
    inherited.setParentThemeName("dark");
    inherited.setColorToken("color.accent",
                            ayt::math::FVector4(0.72f, 0.34f, 0.96f, 1.0f));
    ayt::ui::ThemeManager::get().registerTheme("gallery-product", inherited);
    if (auto* button = dynamic_cast<ayt::ui::Button*>(ui.findById("product_theme_inherited"))) {
        button->setOnClicked([&state]() {
            state.activeThemeName = "gallery-product";
            ayt::ui::ThemeManager::get().setActiveTheme(state.activeThemeName);
            if (auto* label = dynamic_cast<ayt::ui::TextLabel*>(
                    state.ui->findById("product_theme_state"))) {
                label->setText(L"theme: gallery-product extends dark; only accent is overridden");
            }
        });
    }
    if (auto* button = dynamic_cast<ayt::ui::Button*>(ui.findById("product_theme_dark"))) {
        button->setOnClicked([&state]() {
            state.activeThemeName = "dark";
            ayt::ui::ThemeManager::get().setActiveTheme("dark");
            if (auto* label = dynamic_cast<ayt::ui::TextLabel*>(
                    state.ui->findById("product_theme_state"))) {
                label->setText(L"theme: dark (base theme)");
            }
        });
    }

    if (auto* button = dynamic_cast<ayt::ui::Button*>(ui.findById("product_semantics"))) {
        button->setOnClicked([&ui]() {
            const ayt::ui::AccessibilityNode root = ui.buildAccessibilityTree();
            size_t nodes = 0;
            size_t actionable = 0;
            std::function<void(const ayt::ui::AccessibilityNode&)> visit =
                [&](const ayt::ui::AccessibilityNode& node) {
                    ++nodes;
                    if (node.actions != 0) ++actionable;
                    for (const auto& child : node.children) visit(child);
                };
            visit(root);
            if (auto* label = dynamic_cast<ayt::ui::TextLabel*>(
                    ui.findById("product_semantics_state"))) {
                wchar_t text[128];
                std::swprintf(text, std::size(text),
                              L"semantic snapshot: %zu nodes, %zu actionable; native bridge consumes stable IDs",
                              nodes, actionable);
                label->setText(text);
            }
        });
    }
    updateProductScaleLabel(state);
}

// Backend page -- host-drawn demo canvas. A plain Widget whose onRender
// paints straight through IRenderBackend every frame: P1 gradients + blend
// modes, P2 SDF borders/shadows, P3 texture stretch + 9-patch, and P4
// tessellated vector paths + stencil clips. It lives in
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

        // ---- P4: vector paths + stencil clipping -------------------------
        section(L"P4 - Vector paths: concave fill/stroke, hole winding, Bezier, path clip");
        {
            const FRectangle cell = rect(120.0f, 104.0f);
            const FVector2 star[] = {
                {cell.minX + 60.0f, cell.minY + 4.0f},
                {cell.minX + 75.0f, cell.minY + 38.0f},
                {cell.minX + 114.0f, cell.minY + 40.0f},
                {cell.minX + 84.0f, cell.minY + 64.0f},
                {cell.minX + 94.0f, cell.minY + 100.0f},
                {cell.minX + 60.0f, cell.minY + 79.0f},
                {cell.minX + 26.0f, cell.minY + 100.0f},
                {cell.minX + 36.0f, cell.minY + 64.0f},
                {cell.minX + 6.0f, cell.minY + 40.0f},
                {cell.minX + 45.0f, cell.minY + 38.0f},
            };
            auto path = r.createPath();
            r.addPathPolygon(path, star, static_cast<int>(std::size(star)));
            r.setPathFillColor(path, FVector4(0.18f, 0.62f, 0.96f, 0.90f));
            r.setPathStrokeColor(path, FVector4(0.82f, 0.94f, 1.0f, 1.0f));
            r.setPathStrokeWidth(path, 3.0f);
            r.drawPath(path, ayt::ui::PathFillMode::FillAndStroke);
            r.releasePath(path); // queued commands own an immutable snapshot
        }
        {
            const FRectangle cell = rect(126.0f, 104.0f);
            auto path = r.createPath();
            r.addPathRoundedRect(path,
                FRectangle(cell.minX + 3.0f, cell.minY + 6.0f,
                           cell.maxX - 3.0f, cell.maxY - 6.0f),
                22.0f, ayt::ui::PathWinding::CounterClockwise);
            r.addPathEllipse(path,
                FVector2((cell.minX + cell.maxX) * 0.5f,
                         (cell.minY + cell.maxY) * 0.5f),
                24.0f, 19.0f, ayt::ui::PathWinding::Clockwise);
            r.setPathFillColor(path, FVector4(0.92f, 0.48f, 0.18f, 0.95f));
            r.drawPath(path, ayt::ui::PathFillMode::Fill);
            r.releasePath(path);
        }
        {
            const FRectangle cell = rect(180.0f, 104.0f);
            auto path = r.createPath();
            r.addPathBezier(path,
                FVector2(cell.minX + 4.0f, cell.maxY - 10.0f),
                FVector2(cell.minX + 42.0f, cell.minY - 14.0f),
                FVector2(cell.maxX - 42.0f, cell.maxY + 14.0f),
                FVector2(cell.maxX - 4.0f, cell.minY + 10.0f));
            r.setPathStrokeColor(path, FVector4(0.62f, 0.94f, 0.42f, 1.0f));
            r.setPathStrokeWidth(path, 5.0f);
            r.drawPath(path, ayt::ui::PathFillMode::Stroke);
            r.releasePath(path);
        }
        {
            const FRectangle cell = rect(190.0f, 104.0f);
            auto clip = r.createPath();
            r.addPathRoundedRect(clip,
                FRectangle(cell.minX + 2.0f, cell.minY + 5.0f,
                           cell.maxX - 2.0f, cell.maxY - 5.0f),
                28.0f, ayt::ui::PathWinding::CounterClockwise);
            r.addPathEllipse(clip,
                FVector2(cell.minX + 95.0f, cell.minY + 52.0f),
                22.0f, 22.0f, ayt::ui::PathWinding::Clockwise);
            r.pushPathClip(clip);
            r.releasePath(clip);
            r.drawGradientRect(cell,
                FVector4(0.92f, 0.20f, 0.58f, 1.0f),
                FVector4(0.16f, 0.72f, 0.96f, 1.0f),
                FVector4(0.96f, 0.72f, 0.18f, 1.0f),
                FVector4(0.32f, 0.18f, 0.76f, 1.0f));
            r.popClip();
        }
        endRow(104.0f);
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
    bindNav("nav_productization", "page_productization");

    wireProductizationPage(state);

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
    // Productized timeline demo: one physical spring segment is repeated
    // four times and alternates direction. GalleryState owns the clock so a
    // JSON hot reload can explicitly tear it down before replacing the tree.
    if (auto* btn = dynamic_cast<ayt::ui::Button*>(
            ui.findById("btn_timeline_yoyo"))) {
        btn->setOnClicked([&state]() {
            ayt::ui::SpringParameters spring;
            spring.mass = 1.0f;
            spring.stiffness = 120.0f;
            spring.damping = 13.0f;
            spring.clampOvershoot = true;

            auto timeline = std::make_unique<ayt::ui::AnimationTimeline>();
            timeline->addFloatTrack(
                {ayt::ui::AnimationKeyframe<float>(0.0f, 0.0f),
                 ayt::ui::AnimationKeyframe<float>(650.0f, 1.0f, spring)},
                [&state](float value) {
                    if (auto* progress = dynamic_cast<ayt::ui::ProgressBar*>(
                            state.ui->findById("anim_timeline_progress"))) {
                        progress->setValue(value);
                    }
                    if (auto* label = dynamic_cast<ayt::ui::TextLabel*>(
                            state.ui->findById("anim_timeline_state"))) {
                        wchar_t text[96];
                        std::swprintf(text, 96,
                            L"Timeline sample: %.3f (physical spring)", value);
                        label->setText(text);
                    }
                });
            timeline->setRepeatCount(3).setYoyo(true);
            ayt::ui::AnimationCallbacks callbacks;
            callbacks.onCompleted = [&state]() {
                if (auto* label = dynamic_cast<ayt::ui::TextLabel*>(
                        state.ui->findById("anim_timeline_state"))) {
                    label->setText(
                        L"Completed: 4 iterations, yoyo returned to start");
                }
            };
            timeline->setCallbacks(std::move(callbacks));
            state.showcaseTimeline = std::move(timeline);
            state.showcaseTimeline->play();
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
    // TabStrip is cpp-built here to demonstrate a programmatic sibling of
    // the JSON/factory-built productization example: four tabs whose
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
    if (auto* tiles = dynamic_cast<ayt::ui::TileView*>(
            ui.findById("tiles_demo"))) {
        std::vector<std::wstring> items;
        items.reserve(2500);
        for (int index = 0; index < 2500; ++index) {
            items.push_back(L"Asset_" + std::to_wstring(index));
        }
        tiles->setSelectionMode(ayt::ui::TileView::SelectionMode::Extended);
        tiles->setCellBinder([](ayt::ui::TileCell& cell, int index,
                                const std::wstring&) {
            const std::wstring type = (index % 3) == 0 ? L"IMAGE"
                : ((index % 3) == 1 ? L"SURFACE" : L"SCENE");
            const ayt::math::FVector4 categoryColor = (index % 3) == 0
                ? ayt::math::FVector4(0.30f, 0.65f, 0.92f, 1.0f)
                : ((index % 3) == 1
                    ? ayt::math::FVector4(0.76f, 0.48f, 0.92f, 1.0f)
                    : ayt::math::FVector4(0.38f, 0.78f, 0.52f, 1.0f));
            cell.setInfoStrip(type, categoryColor,
                ayt::math::FVector4(1.0f, 1.0f, 1.0f, 1.0f));
            cell.setCornerMarkerVisible((index % 7) == 0);
            cell.setBadgeText((index % 11) == 0 ? L"NEW" : L"");
            cell.setAccentColor(categoryColor);
        });
        tiles->setItems(items);
        tiles->setOnSelectionChanged([&ui, tiles](int index) {
            if (auto* label = dynamic_cast<ayt::ui::TextLabel*>(
                    ui.findById("lbl_selection"))) {
                wchar_t text[128];
                std::swprintf(text, 128,
                    L"selection: tile[%d], visible pool=%zu",
                    index, tiles->getCellPoolSize());
                label->setText(text);
            }
        });
        tiles->setOnRenameRequested([&ui](int index) {
            if (auto* label = dynamic_cast<ayt::ui::TextLabel*>(
                    ui.findById("lbl_selection"))) {
                wchar_t text[96];
                std::swprintf(text, 96, L"F2 rename requested: tile[%d]", index);
                label->setText(text);
            }
        });
        tiles->setOnItemDoubleClicked(
            [&ui](int index, ayt::ui::TileCell::HitRegion region) {
                if (auto* label = dynamic_cast<ayt::ui::TextLabel*>(
                        ui.findById("lbl_selection"))) {
                    const wchar_t* regionName = L"body";
                    if (region == ayt::ui::TileCell::HitRegion::Thumbnail) {
                        regionName = L"thumbnail";
                    } else if (region == ayt::ui::TileCell::HitRegion::Label) {
                        regionName = L"label";
                    }
                    wchar_t text[128];
                    std::swprintf(text, 128,
                        L"double click: tile[%d] / %ls", index, regionName);
                    label->setText(text);
                }
            });
    }
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
// The AYDevice bridge intercepts KeyCode::F5 before the UIManager key tree,
// so this host-only shortcut never leaks into focused-widget behavior.
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

// Dedicated real-GPU probe for retained root-Layer validation. It combines
// an asymmetric checker texture, an alpha sprite, an atlas under a stencil
// path clip, gradients and text. The same widget is rendered through the
// immediate reference path and the retained Layer path; screenshots must be
// byte-identical on the same backend.
class LayerVisualProbeWidget final : public ayt::ui::Widget {
public:
    void setTextures(void* checker, void* arrow, void* orc)
    {
        _checker = checker;
        _arrow = arrow;
        _orc = orc;
    }

    void invalidateFullProbe() { markDirty(); }

    void moveArrowAndInvalidate()
    {
        if (_arrowMoved) return;
        const ayt::math::FRectangle before = arrowBounds(false);
        _arrowMoved = true;
        const ayt::math::FRectangle after = arrowBounds(true);
        const ayt::math::FRectangle damage(
            std::min(before.minX, after.minX) - 6.0f,
            std::min(before.minY, after.minY) - 6.0f,
            std::max(before.maxX, after.maxX) + 6.0f,
            std::max(before.maxY, after.maxY) + 6.0f);
        // Explicit damage uses root-canvas logical coordinates. The union of
        // old and new sprite bounds proves that partial Transparent clear
        // erases stale alpha pixels at the old position.
        markDirty(damage);
    }

protected:
    void onRender(ayt::ui::IRenderBackend& r) override
    {
        using ayt::math::FRectangle;
        using ayt::math::FVector4;
        const FRectangle b = getWorldBounds();
        if (b.maxX <= b.minX || b.maxY <= b.minY) return;

        r.drawGradientRect(b,
            FVector4(0.055f, 0.070f, 0.105f, 1.0f),
            FVector4(0.095f, 0.125f, 0.185f, 1.0f));
        r.drawText(offsetRect(b, 18, 10, 790, 34),
                   L"Retained Layer GPU probe: orientation / alpha clear / UV / stencil",
                   15, FVector4(0.90f, 0.94f, 1.0f, 1.0f));

        const FRectangle checkerRect = offsetRect(b, 18, 44, 318, 214);
        r.drawRect(checkerRect, FVector4(0.02f, 0.02f, 0.025f, 1.0f));
        if (_checker != nullptr) {
            r.drawRect(checkerRect, _checker, FRectangle(0, 0, 1, 1));
        }
        r.drawBorderRect(checkerRect, FVector4(0.30f, 0.72f, 1.0f, 1.0f), 2.0f);

        const FRectangle atlasRect = offsetRect(b, 338, 44, 798, 274);
        const auto atlasClip = r.createPath();
        r.addPathRoundedRect(atlasClip, atlasRect, 20.0f);
        r.pushPathClip(atlasClip);
        r.drawRect(atlasRect, FVector4(0.025f, 0.03f, 0.04f, 1.0f));
        if (_orc != nullptr) {
            r.drawRect(atlasRect, _orc, FRectangle(0, 0, 1, 1));
        }
        r.popClip();
        r.releasePath(atlasClip);
        r.drawBorderRect(atlasRect, FVector4(0.82f, 0.60f, 0.22f, 1.0f), 2.0f);

        const FRectangle stage = offsetRect(b, 18, 296, 798, 490);
        r.drawGradientRect(stage,
            FVector4(0.15f, 0.17f, 0.22f, 1.0f),
            FVector4(0.055f, 0.065f, 0.09f, 1.0f));
        for (int i = 0; i < 8; ++i) {
            const float x = stage.minX + static_cast<float>(i) * 97.5f;
            r.drawRect(FRectangle(x, stage.minY, x + 1.0f, stage.maxY),
                       FVector4(0.20f, 0.24f, 0.31f, 1.0f));
        }
        r.drawText(FRectangle(stage.minX + 12.0f, stage.minY + 8.0f,
                              stage.maxX - 12.0f, stage.minY + 30.0f),
                   L"partial test: old sprite pixels must be gone after the move",
                   13, FVector4(0.82f, 0.86f, 0.94f, 1.0f));
        const FRectangle sprite = arrowBounds(_arrowMoved);
        r.drawBorderRect(FRectangle(sprite.minX - 4.0f, sprite.minY - 4.0f,
                                    sprite.maxX + 4.0f, sprite.maxY + 4.0f),
                         FVector4(0.82f, 0.32f, 0.55f, 0.75f), 1.0f);
        if (_arrow != nullptr) {
            r.drawRect(sprite, _arrow, FRectangle(0, 0, 1, 1));
        }
        r.drawBorderRect(stage, FVector4(0.35f, 0.42f, 0.58f, 1.0f), 2.0f);
    }

private:
    static ayt::math::FRectangle offsetRect(const ayt::math::FRectangle& b,
                                             float minX, float minY,
                                             float maxX, float maxY)
    {
        return ayt::math::FRectangle(
            b.minX + minX, b.minY + minY, b.minX + maxX, b.minY + maxY);
    }

    ayt::math::FRectangle arrowBounds(bool moved) const
    {
        const ayt::math::FRectangle b = getWorldBounds();
        return moved ? offsetRect(b, 632, 352, 732, 452)
                     : offsetRect(b, 88, 352, 188, 452);
    }

    void* _checker = nullptr;
    void* _arrow = nullptr;
    void* _orc = nullptr;
    bool _arrowMoved = false;
};

void teardownLayerVisualProbe(GalleryState& state);

bool wireLayerVisualProbe(GalleryState& state)
{
    if (!state.enableLayerVisualProbe) return true;
    ayt::ui::Widget* host = state.ui->findById("layer_visual_probe_host");
    if (host == nullptr || state.uiBackend == nullptr) return false;

    LoadedBgraImage checker;
    LoadedBgraImage arrow;
    LoadedBgraImage orc;
    if (!loadVisualPngBgra("checkerboard.png", checker)
        || !loadVisualPngBgra("arrow.png", arrow)
        || !loadVisualPngBgra("orc.png", orc)) {
        return false;
    }
    state.layerProbeChecker = state.uiBackend->createUiTexture(
        static_cast<uint16_t>(checker.width), static_cast<uint16_t>(checker.height),
        checker.pixels.data());
    state.layerProbeArrow = state.uiBackend->createUiTexture(
        static_cast<uint16_t>(arrow.width), static_cast<uint16_t>(arrow.height),
        arrow.pixels.data());
    state.layerProbeOrc = state.uiBackend->createUiTexture(
        static_cast<uint16_t>(orc.width), static_cast<uint16_t>(orc.height),
        orc.pixels.data());
    if (state.layerProbeChecker == nullptr || state.layerProbeArrow == nullptr
        || state.layerProbeOrc == nullptr) {
        teardownLayerVisualProbe(state);
        return false;
    }

    auto* probe = new LayerVisualProbeWidget();
    probe->setPosition(ayt::math::FVector2(0.0f, 0.0f));
    probe->setSize(ayt::math::FVector2(820.0f, 510.0f));
    probe->setTextures(state.layerProbeChecker, state.layerProbeArrow,
                       state.layerProbeOrc);
    host->addChild(probe);
    state.layerVisualProbe = probe;
    return true;
}

void teardownLayerVisualProbe(GalleryState& state)
{
    state.layerVisualProbe = nullptr;
    if (state.uiBackend == nullptr) return;
    for (void** texture : {&state.layerProbeChecker, &state.layerProbeArrow,
                           &state.layerProbeOrc}) {
        if (*texture != nullptr) {
            state.uiBackend->releaseUiTexture(*texture);
            *texture = nullptr;
        }
    }
}

// Capture-only probe that exercises Layer semantics without the Gallery shell.
// The main framebuffer first receives an asymmetric opaque backdrop; the test
// content is then either drawn immediately or painted into a retained Layer and
// composited. This makes transparent pixels and group composition observable,
// while keeping the two paths driven by the exact same primitive functions.
class LayerMatrixProbe final {
public:
    LayerMatrixProbe(std::string scenario, bool useLayer, float initialScale)
        : _scenario(std::move(scenario)),
          _useLayer(useLayer),
          _uiScale(std::max(0.5f, initialScale))
    {
    }

    bool enabled() const { return !_scenario.empty(); }

    void beforeFrame(GalleryState& state,
                     int frameNumber,
                     int mutationFrame)
    {
        if (_mutated || frameNumber != mutationFrame) return;
        _mutated = true;

        if (_scenario == "resize") {
            const int width = std::max(64, static_cast<int>(std::lround(1440.0f * _uiScale)));
            const int height = std::max(64, static_cast<int>(std::lround(810.0f * _uiScale)));
            resizeFramebuffer(state, width, height);
        } else if (_scenario == "dpi") {
            _uiScale = 1.5f;
            state.dpiScale = _uiScale;
            if (state.ui != nullptr) state.ui->setDpiScale(_uiScale);
            resizeFramebuffer(state,
                static_cast<int>(std::lround(kWidth * _uiScale)),
                static_cast<int>(std::lround(kHeight * _uiScale)));
        } else if (_scenario == "reset") {
            state.renderer->resize(static_cast<uint32_t>(state.clientW),
                                   static_cast<uint32_t>(state.clientH));
            _expectLeaseInvalidation = _useLayer;
        } else if (_scenario == "msaa" || _scenario == "msaa_fresh") {
            state.renderer->setMsaaSampleCount(2);
            _expectLeaseInvalidation = _useLayer && _scenario == "msaa";
        }

        if (isPartialClearScenario()) {
            _contentGeneration = 1;
            _pendingPartial = _useLayer;
        }
    }

    void render(ayt::render::UIRenderBackend& backend,
                int physicalWidth,
                int physicalHeight)
    {
        using ayt::math::FRectangle;

        const float logicalWidth = static_cast<float>(physicalWidth) / _uiScale;
        const float logicalHeight = static_cast<float>(physicalHeight) / _uiScale;
        backend.beginFrame();
        backend.setUiScale(_uiScale);
        backend.beginCanvas(FRectangle(0.0f, 0.0f,
                                       static_cast<float>(physicalWidth),
                                       static_cast<float>(physicalHeight)));

        drawBackdrop(backend, logicalWidth, logicalHeight);
        const ayt::ui::IRenderBackend::LayerDesc desc =
            makeLayerDesc(logicalWidth, logicalHeight);
        if (_useLayer && !(_scenario == "msaa_fresh" && !_mutated)) {
            renderLayer(backend, desc);
        } else if (!_useLayer) {
            drawImmediate(backend, desc.logicalBounds);
        }

        backend.endCanvas();
        backend.endFrame();
        _dirtyAtEnd = _useLayer && backend.isLayerDirty(_layer);
    }

    void release(ayt::render::UIRenderBackend& backend)
    {
        if (_layer.isValid()) backend.releaseLayer(_layer);
        _layer = {-1};
        _descValid = false;
        _painted = false;
    }

    const std::string& scenario() const { return _scenario; }
    const char* pathName() const { return _useLayer ? "layer" : "immediate"; }
    float uiScale() const { return _uiScale; }
    int layerPaints() const { return _layerPaints; }
    int fullPaints() const { return _fullPaints; }
    int partialPaints() const { return _partialPaints; }
    int layerUpdates() const { return _layerUpdates; }
    bool observedLeaseInvalidation() const { return _observedLeaseInvalidation; }
    bool recoveredLease() const { return _recoveredLease; }
    bool dirtyAtEnd() const { return _dirtyAtEnd; }

private:
    using Rect = ayt::math::FRectangle;
    using Color = ayt::math::FVector4;
    using LayerDesc = ayt::ui::IRenderBackend::LayerDesc;

    bool isPartialClearScenario() const
    {
        return _scenario == "clear_transparent"
            || _scenario == "clear_color"
            || _scenario == "clear_preserve";
    }

    static void resizeFramebuffer(GalleryState& state, int width, int height)
    {
        int actualWidth = std::max(32, width);
        int actualHeight = std::max(32, height);
        // Vulkan swapchains must use the HWND surface extent. Resize the
        // capture window together with renderer/reset state; resizing only
        // bgfx works on D3D but creates a hidden second scale on Vulkan.
        if (state.devices != nullptr) {
            ayt::device::WindowManager& window = state.devices->window();
            window.setSize(actualWidth, actualHeight);
            actualWidth = std::max(32, window.getWidth());
            actualHeight = std::max(32, window.getHeight());
        }
        state.clientW = actualWidth;
        state.clientH = actualHeight;
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

    static bool sameShape(const LayerDesc& a, const LayerDesc& b)
    {
        return a.logicalBounds.minX == b.logicalBounds.minX
            && a.logicalBounds.minY == b.logicalBounds.minY
            && a.logicalBounds.maxX == b.logicalBounds.maxX
            && a.logicalBounds.maxY == b.logicalBounds.maxY
            && a.dpiScale == b.dpiScale
            && a.hasAlpha == b.hasAlpha
            && a.clearMode == b.clearMode;
    }

    LayerDesc makeLayerDesc(float logicalWidth, float logicalHeight) const
    {
        LayerDesc desc;
        const float marginX = std::max(80.0f, logicalWidth * 0.105f);
        const float marginY = std::max(54.0f, logicalHeight * 0.105f);
        desc.logicalBounds = Rect(marginX, marginY,
                                  logicalWidth - marginX,
                                  logicalHeight - marginY);
        desc.dpiScale = _uiScale;
        desc.hasAlpha = true;
        if (_scenario == "clear_color") {
            desc.clearMode = ayt::ui::IRenderBackend::LayerClearMode::Color;
            desc.clearColor = Color(0.065f, 0.115f, 0.185f, 0.82f);
        } else if (_scenario == "clear_preserve") {
            desc.clearMode = ayt::ui::IRenderBackend::LayerClearMode::Preserve;
        } else {
            desc.clearMode = ayt::ui::IRenderBackend::LayerClearMode::Transparent;
        }
        return desc;
    }

    static Rect inset(const Rect& b, float left, float top,
                      float right, float bottom)
    {
        return Rect(b.minX + left, b.minY + top,
                    b.maxX - right, b.maxY - bottom);
    }

    static Rect markerBounds(const Rect& b, bool moved)
    {
        const float width = b.maxX - b.minX;
        const float height = b.maxY - b.minY;
        const float size = std::min(112.0f, height * 0.29f);
        const float x = moved ? b.maxX - size - width * 0.10f
                              : b.minX + width * 0.10f;
        const float y = b.minY + height * 0.57f;
        return Rect(x, y, x + size, y + size);
    }

    static Rect unionRect(const Rect& a, const Rect& b)
    {
        return Rect(std::min(a.minX, b.minX) - 3.0f,
                    std::min(a.minY, b.minY) - 3.0f,
                    std::max(a.maxX, b.maxX) + 3.0f,
                    std::max(a.maxY, b.maxY) + 3.0f);
    }

    static void drawBackdrop(ayt::ui::IRenderBackend& r,
                             float width, float height)
    {
        const float midX = width * 0.5f;
        const float midY = height * 0.5f;
        r.drawRect(Rect(0, 0, midX, midY),
                   Color(0.055f, 0.11f, 0.19f, 1.0f));
        r.drawRect(Rect(midX, 0, width, midY),
                   Color(0.23f, 0.075f, 0.11f, 1.0f));
        r.drawRect(Rect(0, midY, midX, height),
                   Color(0.075f, 0.20f, 0.13f, 1.0f));
        r.drawRect(Rect(midX, midY, width, height),
                   Color(0.21f, 0.15f, 0.045f, 1.0f));
        for (int i = 0; i < 9; ++i) {
            const float x = width * (0.07f + static_cast<float>(i) * 0.105f);
            r.drawRect(Rect(x, 0.0f, x + 3.0f, height),
                       Color(0.62f, 0.72f, 0.92f, 0.16f));
        }
        r.drawGradientRect(Rect(width * 0.04f, height * 0.045f,
                                width * 0.43f, height * 0.105f),
                           Color(0.18f, 0.78f, 0.96f, 0.88f),
                           Color(0.96f, 0.28f, 0.52f, 0.88f));
    }

    static void drawMarker(ayt::ui::IRenderBackend& r, const Rect& b,
                           const Color& color)
    {
        r.drawRect(b, color);
        const float w = b.maxX - b.minX;
        const float h = b.maxY - b.minY;
        r.drawGradientRect(Rect(b.minX + w * 0.18f, b.minY + h * 0.18f,
                                b.maxX - w * 0.18f, b.maxY - h * 0.18f),
                           Color(1.0f, 0.82f, 0.24f, 0.70f),
                           Color(0.20f, 0.88f, 1.0f, 0.38f));
    }

    static void drawTransparentContent(ayt::ui::IRenderBackend& r,
                                       const Rect& b)
    {
        const float w = b.maxX - b.minX;
        const float h = b.maxY - b.minY;
        r.drawRect(Rect(b.minX + w * 0.06f, b.minY + h * 0.08f,
                        b.minX + w * 0.52f, b.minY + h * 0.48f),
                   Color(0.10f, 0.70f, 0.96f, 0.58f));
        r.drawGradientRect(Rect(b.minX + w * 0.31f, b.minY + h * 0.22f,
                                b.minX + w * 0.80f, b.minY + h * 0.62f),
                           Color(0.96f, 0.18f, 0.42f, 0.66f),
                           Color(0.38f, 0.12f, 0.90f, 0.34f));
        r.drawRect(Rect(b.minX + w * 0.69f, b.minY + h * 0.10f,
                        b.minX + w * 0.92f, b.minY + h * 0.34f),
                   Color(0.96f, 0.78f, 0.18f, 0.72f));
        // Deliberately leave the lower middle empty: the asymmetric main
        // backdrop must remain visible through the Layer texture.
    }

    static void drawOpacityContent(ayt::ui::IRenderBackend& r,
                                   const Rect& b)
    {
        const float w = b.maxX - b.minX;
        const float h = b.maxY - b.minY;
        r.drawRect(Rect(b.minX + w * 0.06f, b.minY + h * 0.10f,
                        b.minX + w * 0.28f, b.minY + h * 0.42f),
                   Color(0.12f, 0.82f, 0.96f, 0.86f));
        r.pushOpacity(0.60f);
        r.pushOpacity(0.50f);
        r.drawGradientRect(Rect(b.minX + w * 0.39f, b.minY + h * 0.10f,
                                b.minX + w * 0.61f, b.minY + h * 0.42f),
                           Color(0.98f, 0.24f, 0.46f, 0.90f),
                           Color(0.56f, 0.20f, 0.94f, 0.74f));
        r.popOpacity();
        r.popOpacity();
        r.drawRect(Rect(b.minX + w * 0.72f, b.minY + h * 0.10f,
                        b.minX + w * 0.94f, b.minY + h * 0.42f),
                   Color(0.96f, 0.74f, 0.16f, 0.64f));
        r.drawRect(Rect(b.minX + w * 0.14f, b.minY + h * 0.64f,
                        b.minX + w * 0.86f, b.minY + h * 0.82f),
                   Color(0.24f, 0.92f, 0.48f, 0.52f));
    }

    static void drawBlendContent(ayt::ui::IRenderBackend& r, const Rect& b)
    {
        const float w = b.maxX - b.minX;
        const float h = b.maxY - b.minY;
        r.setBlendMode(ayt::ui::BlendMode::Normal);
        r.drawGradientRect(b, Color(0.12f, 0.23f, 0.42f, 1.0f),
                           Color(0.74f, 0.26f, 0.13f, 1.0f));
        const float top = b.minY + h * 0.18f;
        const float bottom = b.minY + h * 0.82f;
        const float gap = w * 0.035f;
        const float cell = (w - gap * 4.0f) / 3.0f;
        const Rect additive(b.minX + gap, top,
                            b.minX + gap + cell, bottom);
        const Rect multiply(additive.maxX + gap, top,
                            additive.maxX + gap + cell, bottom);
        const Rect screen(multiply.maxX + gap, top,
                          multiply.maxX + gap + cell, bottom);
        r.setBlendMode(ayt::ui::BlendMode::Additive);
        r.drawGradientRect(additive, Color(0.18f, 0.76f, 0.94f, 0.74f),
                           Color(0.92f, 0.22f, 0.44f, 0.62f));
        r.setBlendMode(ayt::ui::BlendMode::Multiply);
        r.drawGradientRect(multiply, Color(0.22f, 0.92f, 0.46f, 0.82f),
                           Color(0.92f, 0.70f, 0.16f, 0.68f));
        r.setBlendMode(ayt::ui::BlendMode::Screen);
        r.drawGradientRect(screen, Color(0.56f, 0.22f, 0.94f, 0.72f),
                           Color(0.12f, 0.86f, 0.90f, 0.78f));
        r.setBlendMode(ayt::ui::BlendMode::Normal);
    }

    void drawClearContent(ayt::ui::IRenderBackend& r, const Rect& b,
                          bool fullPaint) const
    {
        const bool preserve = _scenario == "clear_preserve";
        const Rect oldMarker = markerBounds(b, false);
        const Rect newMarker = markerBounds(b, true);
        if (preserve && fullPaint) {
            r.drawGradientRect(b, Color(0.055f, 0.085f, 0.15f, 1.0f),
                               Color(0.12f, 0.22f, 0.17f, 1.0f));
        }
        if (fullPaint) {
            const float w = b.maxX - b.minX;
            const float h = b.maxY - b.minY;
            r.drawRect(Rect(b.minX + w * 0.40f, b.minY + h * 0.12f,
                            b.minX + w * 0.60f, b.minY + h * 0.28f),
                       Color(0.18f, 0.82f, 0.64f, 0.78f));
        }
        if (_contentGeneration == 0 || (preserve && fullPaint)) {
            drawMarker(r, oldMarker, Color(0.92f, 0.18f, 0.38f, 0.76f));
        } else {
            drawMarker(r, newMarker, Color(0.18f, 0.72f, 0.98f, 0.76f));
        }
    }

    void drawImmediate(ayt::ui::IRenderBackend& r, const Rect& b) const
    {
        if (_scenario == "opacity") {
            r.pushOpacity(0.55f);
            drawOpacityContent(r, b);
            r.popOpacity();
        } else if (_scenario == "blend") {
            drawBlendContent(r, b);
        } else if (isPartialClearScenario()) {
            if (_scenario == "clear_color") {
                r.drawRect(b, Color(0.065f, 0.115f, 0.185f, 0.82f));
            } else if (_scenario == "clear_preserve") {
                r.drawGradientRect(b, Color(0.055f, 0.085f, 0.15f, 1.0f),
                                   Color(0.12f, 0.22f, 0.17f, 1.0f));
            }
            const float w = b.maxX - b.minX;
            const float h = b.maxY - b.minY;
            r.drawRect(Rect(b.minX + w * 0.40f, b.minY + h * 0.12f,
                            b.minX + w * 0.60f, b.minY + h * 0.28f),
                       Color(0.18f, 0.82f, 0.64f, 0.78f));
            if (_scenario == "clear_preserve" && _contentGeneration > 0) {
                drawMarker(r, markerBounds(b, false),
                           Color(0.92f, 0.18f, 0.38f, 0.76f));
            }
            drawMarker(r, markerBounds(b, _contentGeneration > 0),
                       _contentGeneration > 0
                           ? Color(0.18f, 0.72f, 0.98f, 0.76f)
                           : Color(0.92f, 0.18f, 0.38f, 0.76f));
        } else {
            drawTransparentContent(r, b);
        }
    }

    void drawLayerPaint(ayt::ui::IRenderBackend& r, const Rect& b,
                        bool fullPaint) const
    {
        if (_scenario == "opacity") {
            drawOpacityContent(r, b);
        } else if (_scenario == "blend") {
            drawBlendContent(r, b);
        } else if (isPartialClearScenario()) {
            drawClearContent(r, b, fullPaint);
        } else {
            drawTransparentContent(r, b);
        }
    }

    Rect partialDamage(const Rect& b) const
    {
        const Rect oldMarker = markerBounds(b, false);
        const Rect newMarker = markerBounds(b, true);
        return _scenario == "clear_preserve"
            ? inset(newMarker, -3.0f, -3.0f, -3.0f, -3.0f)
            : unionRect(oldMarker, newMarker);
    }

    void renderLayer(ayt::render::UIRenderBackend& backend,
                     const LayerDesc& desired)
    {
        bool shapeChanged = false;
        if (!_layer.isValid()) {
            _layer = backend.createLayer(desired);
            _desc = desired;
            _descValid = _layer.isValid();
            _painted = false;
            shapeChanged = true;
        } else if (!_descValid || !sameShape(_desc, desired)) {
            shapeChanged = true;
            ++_layerUpdates;
            if (!backend.updateLayer(_layer, desired)) {
                backend.releaseLayer(_layer);
                _layer = backend.createLayer(desired);
                _painted = false;
            }
            _desc = desired;
            _descValid = _layer.isValid();
        }
        if (!_layer.isValid()) return;

        Rect damage = desired.logicalBounds;
        if (_pendingPartial && !shapeChanged) {
            damage = partialDamage(desired.logicalBounds);
            backend.invalidateLayer(_layer, damage);
        }

        const bool backendDirty = backend.isLayerDirty(_layer);
        if (_expectLeaseInvalidation && backendDirty) {
            _observedLeaseInvalidation = true;
        }
        if (backendDirty) {
            const bool partial = _pendingPartial && _painted && !shapeChanged;
            ayt::ui::IRenderBackend::LayerPaint paint;
            paint.fullRedraw = !partial;
            paint.damage = partial ? damage : desired.logicalBounds;
            if (backend.beginLayerPaint(_layer, paint)) {
                if (partial) backend.pushClip(damage);
                drawLayerPaint(backend, desired.logicalBounds, !partial);
                if (partial) backend.popClip();
                backend.endLayerPaint(_layer);
                _painted = true;
                ++_layerPaints;
                if (partial) ++_partialPaints;
                else ++_fullPaints;
                _pendingPartial = false;
            }
        }

        if (_observedLeaseInvalidation && !backend.isLayerDirty(_layer)) {
            _recoveredLease = true;
        }
        if (!backend.isLayerDirty(_layer)) {
            backend.compositeLayer(_layer, desired.logicalBounds,
                                   _scenario == "opacity" ? 0.55f : 1.0f);
        }
    }

    std::string _scenario;
    bool _useLayer = false;
    float _uiScale = 1.0f;
    ayt::ui::IRenderBackend::LayerHandle _layer{-1};
    LayerDesc _desc{};
    bool _descValid = false;
    bool _painted = false;
    bool _mutated = false;
    bool _pendingPartial = false;
    bool _expectLeaseInvalidation = false;
    bool _observedLeaseInvalidation = false;
    bool _recoveredLease = false;
    bool _dirtyAtEnd = false;
    int _contentGeneration = 0;
    int _layerPaints = 0;
    int _fullPaints = 0;
    int _partialPaints = 0;
    int _layerUpdates = 0;
};

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
    page->addWidget(demo, 1060.0f);
    state.backendDemo = demo;

    if (auto* hdr = dynamic_cast<ayt::ui::TextLabel*>(
            state.ui->findById("backend_hdr"))) {
        hdr->setText(
            L"Backend - UIRenderBackend [vector path + stencil clip]");
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
    if (!wireLayerVisualProbe(state)) {
        std::fprintf(stderr, "[AYUI_Gallery] Layer visual probe wiring failed\n");
        return false;
    }
    if (!state.enableLayerVisualProbe) {
        wireCapabilities(state);
    }
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
            state.showcaseTimeline.reset();
            state.modal.reset();
            state.modalBody.reset();
            // Capabilities overlay widgets MUST be torn down BEFORE
            // loadLayout destroys the host tree, otherwise the overlay
            // outlives reload and we leak (overlay root is not owned by
            // the loaded JSON tree).
            teardownCapabilitiesOverlay(state);
            teardownImageCompositionPage(state);
            teardownLayerVisualProbe(state);
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

void resizeGallery(GalleryState& state, int width, int height)
{
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

void handleGalleryTouch(GalleryState& state, int64_t pointerId, float x,
                        float y, ayt::device::TouchPhase phase)
{
    if (state.ui == nullptr) {
        return;
    }

    constexpr float kTouchWheelScale = 1.2f;
    constexpr float kTouchDragThresholdPx = 6.0f;
    constexpr float kTouchWheelThresholdPx = 8.0f;
    GalleryState::TouchGesture& touch = state.touch;

    if (phase == ayt::device::TouchPhase::Began) {
        if (touch.active) {
            return;
        }
        touch.active = true;
        touch.dragStarted = false;
        touch.pointerId = pointerId;
        touch.x = x;
        touch.y = y;
        touch.startX = x;
        touch.startY = y;
        touch.lastY = y;
        touch.accumulatedDy = 0.0f;
        state.ui->onMouseMove(x, y);
        return;
    }
    if (!touch.active || touch.pointerId != pointerId) {
        return;
    }

    touch.x = x;
    touch.y = y;
    if (phase == ayt::device::TouchPhase::Moved
        || phase == ayt::device::TouchPhase::Stationary) {
        touch.accumulatedDy += y - touch.lastY;
        touch.lastY = y;
        state.ui->onMouseMove(x, y);
        if (!touch.dragStarted) {
            const float totalDx = x - touch.startX;
            const float totalDy = y - touch.startY;
            touch.dragStarted = std::fabs(totalDx) >= kTouchDragThresholdPx
                || std::fabs(totalDy) >= kTouchDragThresholdPx;
        }
        if (touch.dragStarted
            && std::fabs(touch.accumulatedDy) >= kTouchWheelThresholdPx) {
            state.ui->onMouseWheel(x, y,
                -touch.accumulatedDy * kTouchWheelScale);
            touch.accumulatedDy = 0.0f;
        }
        return;
    }

    if (phase == ayt::device::TouchPhase::Ended
        || phase == ayt::device::TouchPhase::Cancelled) {
        state.ui->onMouseMove(x, y);
        if (phase == ayt::device::TouchPhase::Ended && !touch.dragStarted) {
            state.ui->onMouseButtonDown(x, y, 0);
            state.ui->onMouseButtonUp(x, y, 0);
        }
        touch = {};
        touch.pointerId = -1;
    }
}

std::intptr_t handleNativeHostMessage(GalleryState* state, unsigned msg,
                                      std::uintptr_t wParam,
                                      std::intptr_t lParam, bool& handled)
{
    handled = false;
    if (state == nullptr || state->ui == nullptr) {
        return 0;
    }

    if (state->accessibility != nullptr) {
        std::intptr_t nativeResult = 0;
        if (state->accessibility->handleNativeMessage(
                msg, wParam, lParam, nativeResult)) {
            handled = true;
            return nativeResult;
        }
    }

    // DPI remains a native window concern. All keyboard, pointer, touch,
    // wheel, text and IME traffic is routed through DeviceInputBridge.
    if (msg == WM_DPICHANGED) {
        const UINT dpi = LOWORD(wParam);
        state->dpiScale = std::max(0.5f, static_cast<float>(dpi) / 96.0f);
        state->ui->setDpiScale(state->dpiScale);
        updateProductScaleLabel(*state);
        if (state->devices != nullptr && lParam != 0) {
            HWND hwnd = static_cast<HWND>(
                state->devices->window().getWindowHandle());
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            if (hwnd != nullptr && suggested != nullptr) {
                ::SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                               suggested->right - suggested->left,
                               suggested->bottom - suggested->top,
                               SWP_NOACTIVATE | SWP_NOZORDER);
            }
        }
        handled = true;
    }
    return 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    // Keep HWND client pixels, bgfx swapchain extent and AYUI's explicit
    // capture scale in one coordinate system. Without per-monitor awareness,
    // Vulkan must honor the DPI-virtualized surface extent while D3D can keep
    // the requested reset size, making a 1.5x Layer comparison perform an
    // accidental second resample on Vulkan.
    if (!::SetProcessDpiAwarenessContext(
            DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        ::SetProcessDPIAware();
    }
    AllocConsole();
    FILE* dummy = nullptr;
    freopen_s(&dummy, "CONOUT$", "w", stdout);
    freopen_s(&dummy, "CONOUT$", "w", stderr);

    const VisualCaptureConfig capture = visualCaptureConfig();
    if (capture.enabled) {
        std::remove((capture.outputBase + ".startup.txt").c_str());
        writeCaptureStartupStage(capture, "capture_config_ready");
    }
    // Capture scale models a denser framebuffer, not a smaller logical
    // viewport. Keep the probe at 1280x720 DIP and grow physical pixels so
    // the 1.5x matrix still observes every tested primitive.
    const int initialPixelWidth = capture.enabled
        ? std::max(1, static_cast<int>(std::lround(kWidth * capture.captureScale)))
        : kWidth;
    const int initialPixelHeight = capture.enabled
        ? std::max(1, static_cast<int>(std::lround(kHeight * capture.captureScale)))
        : kHeight;

    ayt::device::DeviceManager devices;
    ayt::device::DeviceConfig cfg{};
    // Title carries the build id so a wrong/old exe is obvious without
    // hunting the AllocConsole window.
    cfg.window.title = "AYUI Gallery [Images / Composition]";
    cfg.window.width = initialPixelWidth;
    cfg.window.height = initialPixelHeight;
    cfg.window.hidden = capture.enabled;
    cfg.enableTouch = true;
    if (!devices.initialize(cfg)) {
        writeCaptureStartupStage(capture, "device_initialize_failed");
        std::fprintf(stderr, "[AYUI_Gallery] DeviceManager initialize failed\n");
        return 1;
    }
    writeCaptureStartupStage(capture, "device_ready");

    ayt::device::WindowManager& window = devices.window();
    HWND hwnd = static_cast<HWND>(window.getWindowHandle());
    if (hwnd == nullptr) {
        writeCaptureStartupStage(capture, "native_window_missing");
        std::fprintf(stderr, "[AYUI_Gallery] no HWND\n");
        devices.shutdown();
        return 1;
    }

    ayt::render::Renderer renderer;
    ayt::render::InitDesc init{};
    init.windowHandle = hwnd;
    init.width = static_cast<uint32_t>(initialPixelWidth);
    init.height = static_cast<uint32_t>(initialPixelHeight);
    init.vsync = !capture.enabled;
    init.msaa = 0; // UI-only: crisp edges, no need for MSAA
    init.backend = capture.backend;
    writeCaptureStartupStage(capture, "renderer_initialize_begin");
    if (!renderer.initialize(init)) {
        writeCaptureStartupStage(capture, "renderer_initialize_failed");
        std::fprintf(stderr, "[AYUI_Gallery] Renderer initialize failed\n");
        devices.shutdown();
        return 1;
    }
    writeCaptureStartupStage(capture, "renderer_ready");

    ayt::render::UIRenderBackend uiBackend;
    writeCaptureStartupStage(capture, "ui_backend_initialize_begin");
    if (!uiBackend.initialize(renderer)) {
        writeCaptureStartupStage(capture, "ui_backend_initialize_failed");
        std::fprintf(stderr, "[AYUI_Gallery] UIRenderBackend initialize failed\n");
        renderer.shutdown();
        devices.shutdown();
        return 1;
    }
    writeCaptureStartupStage(capture, "ui_backend_ready");
    uiBackend.setFramebufferSize(static_cast<uint16_t>(initialPixelWidth),
                                 static_cast<uint16_t>(initialPixelHeight));
    uiBackend.setBatchMode(capture.batchMode);

    ayt::ui::UIManager ui;
    ui.initialize(&uiBackend);
    // Production UI Layer: retain the stable main tree as pixels. Popups,
    // tooltips and drag visuals remain immediate and preserve painter order.
    ui.setRootLayerCachingEnabled(capture.rootLayerEnabled);

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
    state.clientW = initialPixelWidth;
    state.clientH = initialPixelHeight;
    state.enableLayerVisualProbe = !capture.layerProbeScenario.empty();
    if (!capture.enabled) {
        state.dpiScale = std::max(0.5f,
            static_cast<float>(::GetDpiForWindow(hwnd)) / 96.0f);
        ui.setDpiScale(state.dpiScale);
    } else {
        state.dpiScale = capture.captureScale;
        ui.setDpiScale(state.dpiScale);
    }
    LayerMatrixProbe layerMatrix(capture.layerMatrixScenario,
                                 capture.rootLayerEnabled,
                                 capture.captureScale);

    // PR-Dock-TearOff: child-window host ??must exist BEFORE the first
    // loadAndWire because wireDockPromotion's lambdas close over
    // state.childWindows.
    state.childWindows =
        std::make_unique<ayt::gallery::GalleryChildWindows>(window, ui);

    state.layoutPath = resolveLayoutPath();
    if (!loadAndWire(state)) {
        writeCaptureStartupStage(capture, "gallery_layout_failed");
        ui.shutdown();
        uiBackend.shutdown();
        renderer.shutdown();
        devices.shutdown();
        return 1;
    }
    writeCaptureStartupStage(capture, "gallery_ready");
    state.accessibility = ayt::ui::createNativeAccessibilityAdapter(ui, hwnd);
    if (capture.enabled) {
        showPage(ui, capture.pageId.c_str());
        if (capture.scrollY > 0.0f) {
            if (auto* scroll = dynamic_cast<ayt::ui::ScrollView*>(
                    ui.findById("content_scroll"))) {
                scroll->setScrollOffset(ayt::math::FVector2(0.0f, capture.scrollY));
            }
        }
        if (capture.action == "open_modal") {
            if (ayt::ui::Widget* trigger = ui.findById("btn_modal")) {
                const ayt::math::FRectangle bounds = trigger->getWorldBounds();
                const float x = (bounds.minX + bounds.maxX) * 0.5f;
                const float y = (bounds.minY + bounds.maxY) * 0.5f;
                ui.onMouseButtonDown(x, y, 0);
                ui.onMouseButtonUp(x, y, 0);
                ui.layout();
            }
        }
        std::fprintf(stderr,
                     "[AYUI_Gallery] visual capture: page=%s mode=%s scrollY=%.1f "
                     "action=%s layerProbe=%s layerMatrix=%s rootLayer=%s "
                     "scale=%.2f backend=%s "
                     "output=%s\n",
                     capture.pageId.c_str(),
                     capture.batchMode == ayt::render::UIRenderBackend::BatchMode::OrderedRuns
                         ? "ordered" : "overlap",
                     capture.scrollY, capture.action.c_str(),
                     capture.layerProbeScenario.c_str(),
                     capture.layerMatrixScenario.c_str(),
                     capture.rootLayerEnabled ? "enabled" : "immediate",
                     capture.captureScale, backendName(capture.backend),
                     capture.outputBase.c_str());
    }

    window.setWindowCloseCallback([&state]() { state.running = false; });
    window.setWindowResizeCallback([&state](int width, int height) {
        resizeGallery(state, width, height);
    });
    window.setWindowFocusCallback([&ui](bool focused) {
        if (focused) {
            return;
        }
        ui.onKeyUp(ayt::ui::UIKey_Shift);
        ui.onKeyUp(ayt::ui::UIKey_Control);
        ui.onKeyUp(ayt::ui::UIKey_Alt);
        ui.cancelDrag();
    });
    window.setWindowMessageCallback(
        [&state](unsigned msg, std::uintptr_t wParam, std::intptr_t lParam,
                 bool& handled) -> std::intptr_t {
            return handleNativeHostMessage(&state, msg, wParam, lParam, handled);
        });

    ayt::ui::DeviceInputBridge::Callbacks inputCallbacks{};
    inputCallbacks.onMouseMove = [&ui, &window](float x, float y) {
        const bool handled = ui.onMouseMove(x, y);
        window.setCursorShape(ayt::ui::systemCursorFromUi(ui.getCursorHint()));
        return handled;
    };
    inputCallbacks.onMouseLeave = [&ui, &window]() {
        ui.onMouseLeave();
        window.setCursorShape(ayt::device::SystemCursorShape::Arrow);
    };
    inputCallbacks.onMouseButton = [&ui, &window](float x, float y, int button,
                                                  bool pressed) {
        const bool handled = pressed ? ui.onMouseButtonDown(x, y, button)
                                     : ui.onMouseButtonUp(x, y, button);
        window.setCursorShape(ayt::ui::systemCursorFromUi(ui.getCursorHint()));
        return handled;
    };
    inputCallbacks.onMouseWheel = [&ui](float x, float y, float deltaY) {
        return ui.onMouseWheel(x, y, deltaY);
    };
    inputCallbacks.onKey = [&state, &ui](ayt::device::KeyCode key, bool pressed,
                                         bool repeat) {
        if (key == ayt::device::KeyCode::F5 && pressed && !repeat) {
            toggleTheme(state);
            return true;
        }
        const int uiKey = static_cast<int>(ayt::ui::fromDeviceKey(key));
        return pressed ? ui.onKeyDown(uiKey) : ui.onKeyUp(uiKey);
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
    inputCallbacks.onTouch = [&state](int64_t pointerId, float x, float y,
                                      ayt::device::TouchPhase phase) {
        handleGalleryTouch(state, pointerId, x, y, phase);
    };
    ayt::ui::DeviceInputBridge inputBridge(std::move(inputCallbacks));
    inputBridge.connect(devices);
    inputBridge.bindTextInputFocus(ui);
    window.setCursorShape(ayt::ui::systemCursorFromUi(ui.getCursorHint()));

    std::fprintf(stderr,
                 "[AYUI_Gallery] ready ??UI-only composite (no RenderScene)\n"
                 "[AYUI_Gallery] sections: Basics / Images / Input / Collections / "
                 "Overlay / Layout / Capabilities / Backend / Animation / Productization\n");

    LARGE_INTEGER qpcFreq{};
    LARGE_INTEGER qpcPrev{};
    ::QueryPerformanceFrequency(&qpcFreq);
    ::QueryPerformanceCounter(&qpcPrev);
    int visualFrame = 0;
    bool captureQueued = false;

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
        if (capture.enabled) dt = 1.0f / 60.0f; // deterministic visual regression step

        const int frameNumber = visualFrame + 1;
        if (layerMatrix.enabled()) {
            // resize/reset must occur before beginCompositeFrame: bgfx reset
            // invalidates pool leases and the following UI beginFrame is
            // responsible for observing and recovering the Layer.
            layerMatrix.beforeFrame(state, frameNumber,
                                    capture.mutationFrame);
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
        if (layerMatrix.enabled()) {
            layerMatrix.render(uiBackend, state.clientW, state.clientH);
        } else {
            // PR-Dock-TearOff: tick promoted child windows BEFORE the primary
            // (each child updates + GDI-renders under its own ActiveScope,
            // then the primary takes the active slot back).
            if (capture.layerProbeScenario.empty()) {
                if (state.showcaseTimeline != nullptr
                    && state.showcaseTimeline->isRunning()) {
                    state.showcaseTimeline->tick(dt);
                }
                state.childWindows->tickAll(dt);
                ui.update(dt); // caret blink, hover revalidate, hot-reload
            }
            ui.layout();
            if (capture.layerProbeScenario.empty()) {
                state.accessibility->update();
            }
            if (capture.enabled && capture.scrollY > 0.0f) {
                if (auto* scroll = dynamic_cast<ayt::ui::ScrollView*>(
                        ui.findById("content_scroll"))) {
                    // Apply after layout has established contentSize; applying
                    // only during bootstrap would clamp against the initial 0.
                    scroll->setScrollOffset(ayt::math::FVector2(0.0f, capture.scrollY));
                }
            }
            if (!capture.layerProbeScenario.empty()
                && frameNumber == capture.mutationFrame
                && state.layerVisualProbe != nullptr) {
                if (capture.layerProbeScenario == "full") {
                    state.layerVisualProbe->invalidateFullProbe();
                } else if (capture.layerProbeScenario == "move") {
                    state.layerVisualProbe->moveArrowAndInvalidate();
                }
            }
            // Drop guides paint inside DockArea::render (after its children).
            // Child-window redock still works: tickAll/updateRedockHover sets
            // setExternalDropPos before populateFrame.
            ui.populateFrame();
            ui.flushFrame();
        }

        ++visualFrame;
        if (capture.enabled && visualFrame == capture.captureFrame) {
            captureQueued = renderer.captureScreenshot(capture.outputBase);
            writeCaptureStartupStage(capture,
                captureQueued ? "capture_queued" : "capture_queue_failed");
            const std::string metricsPath = capture.outputBase + ".metrics.txt";
            FILE* metrics = nullptr;
            if (fopen_s(&metrics, metricsPath.c_str(), "wb") == 0 && metrics != nullptr) {
                std::fprintf(metrics,
                             "page=%s\nmode=%s\nframe=%d\ndrawCalls=%d\n"
                             "scrollY=%.1f\naction=%s\nlayerProbe=%s\n"
                             "rootLayer=%s\nscale=%.2f\nframebuffer=%dx%d\n"
                             "backend=%s\nqueued=%s\nlayerMatrix=%s\n"
                             "matrixPath=%s\nmatrixScale=%.2f\n"
                             "layerPaints=%d\nfullPaints=%d\npartialPaints=%d\n"
                             "layerUpdates=%d\nobservedLeaseInvalidation=%s\n"
                             "recoveredLease=%s\nlayerDirtyAtCapture=%s\nmsaa=%u\n",
                             capture.pageId.c_str(),
                             capture.batchMode
                                     == ayt::render::UIRenderBackend::BatchMode::OrderedRuns
                                 ? "ordered" : "overlap",
                             visualFrame, uiBackend.getDrawCallCount(),
                             capture.scrollY, capture.action.c_str(),
                             capture.layerProbeScenario.c_str(),
                             capture.rootLayerEnabled ? "enabled" : "immediate",
                             capture.captureScale, state.clientW, state.clientH,
                             backendName(capture.backend),
                             captureQueued ? "yes" : "no",
                             layerMatrix.scenario().c_str(),
                             layerMatrix.pathName(), layerMatrix.uiScale(),
                             layerMatrix.layerPaints(), layerMatrix.fullPaints(),
                             layerMatrix.partialPaints(), layerMatrix.layerUpdates(),
                             layerMatrix.observedLeaseInvalidation() ? "yes" : "no",
                             layerMatrix.recoveredLease() ? "yes" : "no",
                             layerMatrix.dirtyAtEnd() ? "yes" : "no",
                             renderer.msaaSampleCount());
                std::fclose(metrics);
            }
            std::fprintf(stderr,
                         "[AYUI_Gallery] capture frame=%d drawCalls=%d queued=%s\n",
                         visualFrame, uiBackend.getDrawCallCount(),
                         captureQueued ? "yes" : "no");
        }

        renderer.endFrame();

        if (capture.enabled && visualFrame >= capture.exitFrame) {
            state.running = false;
        }
    }

    if (capture.enabled && !captureQueued) {
        std::fprintf(stderr, "[AYUI_Gallery] visual capture failed to queue\n");
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
    teardownLayerVisualProbe(state);
    layerMatrix.release(uiBackend);
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
    state.accessibility.reset();
    ui.shutdown();
    uiBackend.shutdown();
    renderer.shutdown();
    devices.shutdown();
    writeCaptureStartupStage(capture, "shutdown_complete");
    return 0;
}
