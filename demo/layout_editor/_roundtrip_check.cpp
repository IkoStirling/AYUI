// Headless round-trip check for LayoutEditorSession (no HWND).
#include "AYUI/LayoutEditor/LayoutEditorSession.h"
#include "AYUI/LayoutEditor/LayoutAnimationTimelineView.h"
#include "AYUI/LayoutEditor/PropertySchema.h"
#include "AYUI/LayoutEditor/LayoutPropertyEditors.h"

#include "AYUI/LayoutLoader.h"
#include "AYUI/Button.h"
#include "AYUI/Clipboard.h"
#include "AYUI/ComboBox.h"
#include "AYUI/GridPanel.h"
#include "AYUI/Image.h"
#include "AYUI/ListView.h"
#include "AYUI/MenuBar.h"
#include "AYUI/Modal.h"
#include "AYUI/ModalDialog.h"
#include "AYUI/RadioButton.h"
#include "AYUI/RichText.h"
#include "AYUI/ScrollView.h"
#include "AYUI/Separator.h"
#include "AYUI/Slider.h"
#include "AYUI/ProgressBar.h"
#include "AYUI/TabControl.h"
#include "AYUI/TabStrip.h"
#include "AYUI/TextLabel.h"
#include "AYUI/TextInput.h"
#include "AYUI/TileView.h"
#include "AYUI/Theme.h"
#include "AYUI/UIKeyCode.h"
#include "AYUI/UIManager.h"
#include "AYUI/Widget.h"
#include "AYUI/WidgetFactory.h"

#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

class TestClipboard final : public ayt::ui::IClipboard {
public:
    bool setText(const std::wstring& value) override {
        text = value;
        return true;
    }
    bool getText(std::wstring& out) override {
        out = text;
        return true;
    }
    std::wstring text;
};

} // namespace

int main() {
    struct LoaderErrorCleanup {
        ~LoaderErrorCleanup() {
            std::error_code error;
            std::filesystem::remove("ayui_loader_error.txt", error);
        }
    } loaderErrorCleanup;

    ayt::ui::ThemeManager::get().ensureDefaultThemes();
    ayt::ui::ThemeManager::get().setActiveTheme("dark");

    ayt::ui::UIManager ui;
    ui.initialize(nullptr);
    ui.loader().setTextResolver(
        [](std::string_view key, std::wstring_view fallback) {
            if (key == "ui.editor.ui_designer.menu.file") return std::wstring(L"文件");
            if (key == "ui.editor.ui_designer.action.open") return std::wstring(L"打开");
            return std::wstring(fallback);
        });
    if (!ui.loadLayout("assets/layout_editor.ui.json")) {
        std::fprintf(stderr, "chrome load failed\n");
        return 1;
    }

    ayt::ui::LayoutEditorSession session;
    int textureResourceProviderCalls = 0;
    session.setTextureResourceProvider([&textureResourceProviderCalls]() {
        ++textureResourceProviderCalls;
        return std::vector<ayt::ui::LayoutTextureResource>{
            {"Assets/ui/test.png", L"Project / ui / test.png",
             "D:/PreviewRoot/ui/test.png", L"PNG"}
        };
    });
    if (textureResourceProviderCalls != 0) {
        std::fprintf(stderr, "texture provider ran before attach\n");
        return 121;
    }
    int openPickerCalls = 0;
    session.setOpenPathPicker([&openPickerCalls]() {
        ++openPickerCalls;
        return std::string{};
    });
    int refactorPreviewCalls = 0;
    int refactorApplyCalls = 0;
    session.setProjectRefactorKinds({
        {"widget-id", L"Widget ID",
         ayt::ui::LayoutProjectRefactorKind::Seed::SelectedWidgetId},
    });
    session.setProjectRefactorAction(
        [&refactorPreviewCalls, &refactorApplyCalls](
            const std::string&, const std::string& kind,
            const std::string& oldValue, const std::string& newValue,
            bool apply) {
            ayt::ui::LayoutProjectRefactorResult result;
            result.succeeded = kind == "widget-id"
                && oldValue == "btn_hello" && newValue == "btn_primary";
            result.safe = result.succeeded;
            result.changedFiles = result.succeeded ? 2u : 0u;
            result.details = {L"ui/sample.ui.json — 1 typed reference(s)",
                              L"ui/main.uiflow.json — 1 typed reference(s)"};
            result.message = result.succeeded ? "Safe project rename" : "Bad request";
            if (apply) ++refactorApplyCalls;
            else ++refactorPreviewCalls;
            return result;
        });
    auto* clipboard = new TestClipboard();
    ayt::ui::setClipboardImpl(clipboard);
    if (!session.attach(ui)) {
        std::fprintf(stderr, "attach failed\n");
        return 2;
    }
    if (textureResourceProviderCalls != 1) {
        std::fprintf(stderr,
            "texture provider first-attach count=%d (expected 1)\n",
            textureResourceProviderCalls);
        return 122;
    }
    if (session.documentRoot() == nullptr ||
        session.documentRoot()->isLayoutSizeManaged()) {
        std::fprintf(stderr, "new document root size is not authorable\n");
        return 64;
    }
    auto* menuBar = dynamic_cast<ayt::ui::MenuBar*>(
        ui.findById("designer_menubar"));
    if (menuBar == nullptr || menuBar->getMenuCount() != 4u) {
        std::fprintf(stderr, "designer menu regression: count=%zu\n",
            menuBar != nullptr ? menuBar->getMenuCount() : 0u);
        return 9;
    }
    ayt::ui::Menu* fileMenu = menuBar->getMenu(0);
    ayt::ui::MenuItem* openItem = fileMenu != nullptr
        ? fileMenu->getItem(0) : nullptr;
    if (menuBar->getMenuTitle(0) != L"文件" || openItem == nullptr
        || openItem->getText() != L"打开" || !openItem->handleClick()) {
        std::fprintf(stderr, "localized designer menu binding failed\n");
        return 123;
    }
    session.pumpDeferred(0.0f);
    if (openPickerCalls != 1) {
        std::fprintf(stderr,
            "localized designer Open callback count=%d (expected 1)\n",
            openPickerCalls);
        return 124;
    }
    static const char* requiredPaletteIds[] = {
        "btn_add_image", "btn_add_list", "btn_add_tiles", "btn_add_tree",
        "btn_add_tabstrip", "btn_add_tabs", "btn_add_modal", "btn_add_dialog"
    };
    int missingPaletteEntries = 0;
    for (const char* id : requiredPaletteIds) {
        if (ui.findById(id) == nullptr) ++missingPaletteEntries;
    }
    if (missingPaletteEntries != 0) {
        std::fprintf(stderr, "expanded palette regression: missing=%d\n",
                     missingPaletteEntries);
        return 13;
    }
    static const char* requiredAnchorIds[] = {
        "btn_anchor_00", "btn_anchor_03", "btn_anchor_30",
        "btn_anchor_33", "btn_anchor_none", "prop_anchor_min_x",
        "prop_anchor_max_y", "prop_offset_min_x", "prop_offset_max_y",
        "prop_pivot_x"
    };
    int missingAnchorEntries = 0;
    for (const char* id : requiredAnchorIds) {
        if (ui.findById(id) == nullptr) ++missingAnchorEntries;
    }
    if (missingAnchorEntries != 0) {
        std::fprintf(stderr, "anchor inspector regression: missing=%d\n",
                     missingAnchorEntries);
        return 45;
    }
    static const char* requiredProductIds[] = {
        "preview_preset", "preview_width", "preview_height", "preview_dpi",
        "btn_preview_safe", "btn_preview_mode", "structured_items",
        "structured_text", "texture_resource_list", "texture_resource_status"
    };
    int missingProductEntries = 0;
    for (const char* id : requiredProductIds) {
        if (ui.findById(id) == nullptr) ++missingProductEntries;
    }
    if (missingProductEntries != 0) {
        std::fprintf(stderr, "phase-3 chrome regression: missing=%d\n",
                     missingProductEntries);
        return 80;
    }
    static const char* requiredQualityIds[] = {
        "style_preview_state", "style_preview_swatch",
        "style_source_status", "btn_reset_style", "validation_list",
        "validation_status", "btn_validate", "interaction_graph_list",
        "interaction_graph_status", "btn_refresh_interactions",
        "reuse_name", "reuse_list",
        "btn_reuse_define", "btn_reuse_insert", "responsive_breakpoint",
        "responsive_visibility", "btn_responsive_preview",
        "btn_responsive_capture", "animation_clip_name",
        "animation_clip_list", "animation_track_property",
        "animation_track_list", "animation_time", "animation_curve",
        "animation_bezier_x1", "animation_bezier_y1",
        "animation_bezier_x2", "animation_bezier_y2",
        "animation_spring_mass", "animation_spring_stiffness",
        "animation_spring_damping", "animation_spring_velocity",
        "animation_spring_clamp", "btn_animation_curve_apply",
        "animation_key_list", "btn_animation_key_capture",
        "btn_animation_preview", "btn_animation_reset",
        "animation_timeline_workspace", "animation_timeline_host",
        "btn_animation_play", "btn_animation_pause",
        "btn_animation_stop", "btn_animation_loop",
        "animation_transport_status", "project_refactor_kind",
        "project_refactor_old",
        "project_refactor_new", "project_refactor_preview",
        "btn_project_refactor_preview", "btn_project_refactor_apply",
        "component_library_search", "component_library_filter",
        "component_library_display_name", "component_library_tags",
        "component_library_description", "btn_theme_token_swatch",
        "theme_style_fragment", "theme_style_id", "btn_theme_style_new",
        "btn_theme_style_duplicate", "btn_theme_style_remove",
        "hierarchy_search", "btn_hierarchy_search_clear",
        "lbl_hierarchy_summary"
    };
    int missingQualityEntries = 0;
    for (const char* id : requiredQualityIds) {
        if (ui.findById(id) == nullptr) ++missingQualityEntries;
    }
    if (missingQualityEntries != 0) {
        std::fprintf(stderr, "authoring-quality chrome regression: missing=%d\n",
                     missingQualityEntries);
        return 81;
    }
    int missingTypedInspectorEntries = 0;
    for (const ayt::ui::PropertyFieldSchema& schema :
         ayt::ui::allPropertyFieldSchemas()) {
        if (ui.findById(schema.controlId) == nullptr)
            ++missingTypedInspectorEntries;
        for (const std::string& componentId : schema.componentControlIds) {
            if (ui.findById(componentId) == nullptr)
                ++missingTypedInspectorEntries;
        }
    }
    if (missingTypedInspectorEntries != 0) {
        std::fprintf(stderr, "typed inspector regression: missing=%d\n",
                     missingTypedInspectorEntries);
        return 116;
    }
    std::ifstream chromeSourceFile("assets/layout_editor.ui.json",
                                   std::ios::binary);
    const bool chromeSourceOpened = chromeSourceFile.is_open();
    const std::string chromeSource{
        std::istreambuf_iterator<char>(chromeSourceFile),
        std::istreambuf_iterator<char>()};
    int serializedGeneratedControls = 0;
    for (const ayt::ui::PropertyFieldSchema& schema :
         ayt::ui::allPropertyFieldSchemas()) {
        if (chromeSource.find(std::string("\"") + schema.rowId + "\"") !=
            std::string::npos) {
            ++serializedGeneratedControls;
        }
    }
    auto* generatedMinimum = dynamic_cast<ayt::ui::TextInput*>(
        ui.findById("prop_min"));
    auto* generatedSelection = dynamic_cast<ayt::ui::ComboBox*>(
        ui.findById("prop_selection_mode"));
    auto* generatedColorEditor =
        dynamic_cast<ayt::ui::LayoutColorPropertyEditor*>(
            ui.findById("prop_image_tint_editor"));
    auto* generatedResourceEditor =
        dynamic_cast<ayt::ui::LayoutResourcePropertyEditor*>(
            ui.findById("prop_texture_editor"));
    if (!chromeSourceOpened || serializedGeneratedControls != 0 ||
        generatedMinimum == nullptr || generatedSelection == nullptr ||
        generatedColorEditor == nullptr || generatedResourceEditor == nullptr) {
        std::fprintf(stderr,
            "schema-generated inspector chrome regression: serialized=%d\n",
            serializedGeneratedControls);
        return 116;
    }
    ayt::ui::Widget* timelineHost = ui.findById("animation_timeline_host");
    bool timelineSurfaceMounted = false;
    if (timelineHost != nullptr) {
        for (ayt::ui::Widget* child : timelineHost->getChildren()) {
            if (child != nullptr &&
                child->getId() == "__le_animation_timeline_view") {
                timelineSurfaceMounted = true;
                break;
            }
        }
    }
    if (!timelineSurfaceMounted) {
        std::fprintf(stderr, "animation timeline surface was not mounted\n");
        return 105;
    }
    if (!session.open("assets/sample_blank.ui.json")) {
        std::fprintf(stderr, "open failed\n");
        return 3;
    }

    session.selectById("btn_hello");
    ayt::ui::Widget* button = session.selected();
    if (button == nullptr) {
        std::fprintf(stderr, "button selection failed\n");
        return 10;
    }
    auto* hierarchySearch = dynamic_cast<ayt::ui::TextInput*>(
        ui.findById("hierarchy_search"));
    auto* hierarchyList = dynamic_cast<ayt::ui::ListView*>(
        ui.findById("list_hierarchy"));
    auto* hierarchySummary = dynamic_cast<ayt::ui::TextLabel*>(
        ui.findById("lbl_hierarchy_summary"));
    if (hierarchySearch == nullptr || hierarchyList == nullptr
        || hierarchySummary == nullptr) {
        std::fprintf(stderr, "hierarchy workflow controls missing\n");
        return 125;
    }
    hierarchySearch->setText(L"Text Label");
    if (hierarchyList->getItemCount() != 2u
        || hierarchySummary->getText().find(L"1 match(es)")
            == std::wstring::npos) {
        std::fprintf(stderr, "hierarchy type filter/context failed\n");
        return 126;
    }
    hierarchySearch->setText(L"does-not-exist");
    if (hierarchyList->getItemCount() != 0u || session.selected() != button) {
        std::fprintf(stderr, "hierarchy empty filter changed selection\n");
        return 127;
    }
    hierarchySearch->setText(L"");
    if (hierarchyList->getItemCount() != 3u) {
        std::fprintf(stderr, "hierarchy clear filter failed\n");
        return 128;
    }
    auto* refactorOld = dynamic_cast<ayt::ui::TextInput*>(
        ui.findById("project_refactor_old"));
    auto* refactorNew = dynamic_cast<ayt::ui::TextInput*>(
        ui.findById("project_refactor_new"));
    auto* refactorPreview = dynamic_cast<ayt::ui::ListView*>(
        ui.findById("project_refactor_preview"));
    auto* refactorPreviewButton = dynamic_cast<ayt::ui::Button*>(
        ui.findById("btn_project_refactor_preview"));
    auto* refactorApplyButton = dynamic_cast<ayt::ui::Button*>(
        ui.findById("btn_project_refactor_apply"));
    if (refactorOld == nullptr || refactorNew == nullptr
        || refactorPreview == nullptr || refactorPreviewButton == nullptr
        || refactorApplyButton == nullptr) {
        std::fprintf(stderr, "project refactor controls missing\n");
        return 123;
    }
    refactorOld->setText(L"btn_hello");
    refactorNew->setText(L"btn_primary");
    const auto clickButton = [](ayt::ui::Button& target) {
        const ayt::math::FRectangle bounds = target.getWorldBounds();
        const ayt::math::FVector2 center{
            0.5f * (bounds.minX + bounds.maxX),
            0.5f * (bounds.minY + bounds.maxY)};
        return target.onMouseButtonDown(ayt::ui::UIMouseEvent(center, 0))
            && target.onMouseButtonUp(ayt::ui::UIMouseEvent(center, 0));
    };
    const bool previewClicked = clickButton(*refactorPreviewButton);
    const std::size_t previewItems = refactorPreview->getItemCount();
    const bool applyClicked = clickButton(*refactorApplyButton);
    if (!previewClicked || refactorPreviewCalls != 1 || previewItems != 2u
        || !applyClicked || refactorApplyCalls != 1) {
        std::fprintf(stderr,
            "project refactor preview/apply routing failed "
            "(previewClick=%d previewCalls=%d items=%zu applyClick=%d applyCalls=%d)\n",
            previewClicked ? 1 : 0, refactorPreviewCalls, previewItems,
            applyClicked ? 1 : 0, refactorApplyCalls);
        return 124;
    }
    session.applyProperty("controller", L"SampleController");
    session.applyProperty("event:onClick", L"handleHello");
    session.setInteractionContracts({
        {"SampleController", {{"handleHello", {"onClick"}}}},
    });
    if (session.interactionGraph().edges().size() != 1u ||
        session.interactionGraph().resolvedCount() != 1u) {
        std::fprintf(stderr, "interaction contract graph failed\n");
        return 118;
    }
    session.applyProperty("event:onClick", L"");
    session.applyProperty("controller", L"");
    session.clearInteractionContracts();
    auto* alignLeft = dynamic_cast<ayt::ui::Button*>(
        ui.findById("btn_align_left"));
    if (alignLeft == nullptr || alignLeft->isEnabled()) {
        std::fprintf(stderr, "single-selection arrange enablement failed\n");
        return 52;
    }
    auto* propsScroll = dynamic_cast<ayt::ui::ScrollView*>(
        ui.findById("props_scroll"));
    if (propsScroll == nullptr) {
        std::fprintf(stderr, "inspector scroll missing\n");
        return 58;
    }
    propsScroll->setScrollOffset(ayt::math::FVector2(0.0f, 600.0f));
    if (propsScroll->getScrollOffset().y <= 0.0f) {
        std::fprintf(stderr, "inspector scroll setup failed\n");
        return 63;
    }
    session.select(session.documentRoot(), false);
    ayt::ui::Widget* rootWidthRow = ui.findById("row_prop_w");
    ayt::ui::Widget* rootHeightRow = ui.findById("row_prop_h");
    if (propsScroll->getScrollOffset().y != 0.0f ||
        rootWidthRow == nullptr || !rootWidthRow->isVisible() ||
        rootHeightRow == nullptr || !rootHeightRow->isVisible()) {
        std::fprintf(stderr,
            "root inspector visibility/scroll reset failed: scroll=%g w=%d h=%d\n",
            propsScroll->getScrollOffset().y,
            rootWidthRow != nullptr && rootWidthRow->isVisible(),
            rootHeightRow != nullptr && rootHeightRow->isVisible());
        return 59;
    }
    session.select(button, false);
    const ayt::math::FVector2 buttonBeforeNudge = button->getPosition();
    if (!session.onKeyDown(ayt::ui::UIKey_Right) ||
        button->getPosition().x != buttonBeforeNudge.x + 1.0f ||
        button->getPosition().y != buttonBeforeNudge.y) {
        std::fprintf(stderr, "arrow nudge regression\n");
        return 11;
    }
    session.onKeyDown(ayt::ui::UIKey_Left);

    // Regression: child then Shift-select root must not nest ancestor+child.
    session.select(session.documentRoot(), true);
    if (session.selection().size() != 1 ||
        session.selected() != session.documentRoot()) {
        std::fprintf(stderr,
            "nested-select regression: expected exclusive root, got sel=%zu\n",
            session.selection().size());
        return 8;
    }

    const ayt::math::FVector2 rootBeforeInput =
        session.documentRoot()->getPosition();
    session.applyProperty("x", L"96");
    session.onKeyDown(ayt::ui::UIKey_Right);
    ayt::ui::UILayoutLoader viewStateSerializer;
    std::string documentBeforeViewChange;
    viewStateSerializer.saveLayoutToString(
        session.documentRoot(), documentBeforeViewChange, false);
    const ayt::math::FVector2 rootSizeBeforeViewChange =
        session.documentRoot()->getSize();
    const ayt::math::FVector2 buttonPositionBeforeViewChange =
        button->getPosition();
    const ayt::math::FVector2 buttonSizeBeforeViewChange = button->getSize();
    const bool dirtyBeforeViewChange = session.isDirty();
    session.setViewZoom(1.1f, {420.0f, 320.0f});
    std::string documentAfterZoom;
    viewStateSerializer.saveLayoutToString(
        session.documentRoot(), documentAfterZoom, false);
    if (session.documentRoot()->getPosition() != rootBeforeInput ||
        session.documentRoot()->getSize() != rootSizeBeforeViewChange ||
        button->getPosition() != buttonPositionBeforeViewChange ||
        button->getSize() != buttonSizeBeforeViewChange ||
        session.isDirty() != dirtyBeforeViewChange ||
        documentAfterZoom != documentBeforeViewChange) {
        std::fprintf(stderr, "viewport zoom mutated document state\n");
        return 12;
    }
    session.setViewZoom(1.0f, {420.0f, 320.0f});

    ayt::ui::Widget* canvasHost = ui.findById("canvas_host");
    if (canvasHost == nullptr) return 65;
    const ayt::math::FRectangle canvasBounds = canvasHost->getWorldBounds();
    const ayt::math::FVector2 panStart(canvasBounds.minX + 200.0f,
                                       canvasBounds.minY + 160.0f);
    if (!session.onPointerDown(panStart, 2) ||
        !session.onPointerMove(panStart + ayt::math::FVector2(24.0f, 18.0f)) ||
        !session.onPointerUp(panStart + ayt::math::FVector2(24.0f, 18.0f), 2) ||
        session.viewPan() != ayt::math::FVector2(24.0f, 18.0f)) {
        std::fprintf(stderr, "viewport pan interaction failed\n");
        return 66;
    }
    std::string documentAfterPan;
    viewStateSerializer.saveLayoutToString(
        session.documentRoot(), documentAfterPan, false);
    if (session.documentRoot()->getPosition() != rootBeforeInput ||
        session.documentRoot()->getSize() != rootSizeBeforeViewChange ||
        button->getPosition() != buttonPositionBeforeViewChange ||
        session.isDirty() != dirtyBeforeViewChange ||
        documentAfterPan != documentBeforeViewChange) {
        std::fprintf(stderr, "viewport pan mutated document state\n");
        return 67;
    }
    session.onPointerDown(panStart, 2);
    session.onPointerMove(panStart - ayt::math::FVector2(24.0f, 18.0f));
    session.onPointerUp(panStart - ayt::math::FVector2(24.0f, 18.0f), 2);
    if (session.viewPan() != ayt::math::FVector2(0.0f, 0.0f)) return 68;

    session.select(button, false);
    const std::string originalButtonId = button->getId();
    session.applyProperty("id", L"document_root");
    session.applyProperty("id", L"9 invalid id");
    if (button->getId() != originalButtonId) {
        std::fprintf(stderr, "duplicate/invalid ID was accepted\n");
        return 69;
    }

    clipboard->setText(L"external clipboard sentinel");
    const size_t childrenBeforeDuplicate =
        session.documentRoot()->getChildren().size();
    session.duplicateSelection();
    if (clipboard->text != L"external clipboard sentinel" ||
        session.documentRoot()->getChildren().size() !=
            childrenBeforeDuplicate + 1u) {
        std::fprintf(stderr, "duplicate overwrote clipboard or failed\n");
        return 70;
    }
    session.select(button, false);
    session.copySelection();
    clipboard->setText(L"this is external text, not stale AYUI JSON");
    const size_t childrenBeforeExternalPaste =
        session.documentRoot()->getChildren().size();
    session.pasteClipboard();
    if (session.documentRoot()->getChildren().size() !=
        childrenBeforeExternalPaste) {
        std::fprintf(stderr, "paste ignored current external clipboard\n");
        return 71;
    }

    // Regression: palette content beyond ScrollView's client rectangle must
    // neither paint nor activate over Document Outline. Session has its own
    // drag shortcut, so this verifies the editor-level hit gate in addition
    // to ScrollView's normal UIManager hit testing.
    auto* paletteScroll = dynamic_cast<ayt::ui::ScrollView*>(
        ui.findById("palette_scroll"));
    ayt::ui::Widget* leakedPaletteButton =
        ui.findById("btn_add_separator");
    if (paletteScroll == nullptr || leakedPaletteButton == nullptr) {
        std::fprintf(stderr, "palette clipping fixtures missing\n");
        return 38;
    }
    const ayt::math::FRectangle leakedBounds =
        leakedPaletteButton->getWorldBounds();
    const ayt::math::FVector2 leakedPoint(
        (leakedBounds.minX + leakedBounds.maxX) * 0.5f,
        (leakedBounds.minY + leakedBounds.maxY) * 0.5f);
    if (paletteScroll->getClientRect().contains(leakedPoint)) {
        std::fprintf(stderr, "palette overflow fixture unexpectedly visible\n");
        return 39;
    }
    const size_t documentChildrenBeforeOverflowClick =
        session.documentRoot()->getChildren().size();
    session.onPointerDown(leakedPoint, 0);
    session.onPointerUp(leakedPoint, 0);
    if (session.documentRoot()->getChildren().size() !=
        documentChildrenBeforeOverflowClick) {
        std::fprintf(stderr, "offscreen palette entry accepted input\n");
        return 40;
    }

    session.addWidget("Slider");
    auto* slider = dynamic_cast<ayt::ui::Slider*>(session.selected());
    ayt::ui::Widget* generatedMinRow =
        ui.findById("row_prop_min");
    ayt::ui::Widget* generatedTintRow =
        ui.findById("row_prop_image_tint");
    if (generatedMinRow == nullptr || !generatedMinRow->isVisible() ||
        generatedTintRow == nullptr || generatedTintRow->isVisible()) {
        std::fprintf(stderr, "schema-generated row visibility failed\n");
        return 117;
    }
    session.applyProperty("min", L"-2");
    session.applyProperty("max", L"8");
    session.applyProperty("value", L"6.5");
    if (slider == nullptr || slider->getMin() != -2.0f ||
        slider->getMax() != 8.0f || slider->getValue() != 6.5f) {
        std::fprintf(stderr, "typed Slider inspector properties failed\n");
        return 116;
    }

    session.select(session.documentRoot(), false);
    session.addWidget("ProgressBar");
    auto* progress = dynamic_cast<ayt::ui::ProgressBar*>(session.selected());
    session.applyProperty("max", L"50");
    session.applyProperty("min", L"10");
    session.applyProperty("value", L"35");
    if (progress == nullptr || progress->getMin() != 10.0f ||
        progress->getMax() != 50.0f || progress->getValue() != 35.0f) {
        std::fprintf(stderr, "typed ProgressBar inspector properties failed\n");
        return 117;
    }

    session.select(session.documentRoot(), false);
    session.addWidget("RichText");
    auto* rich = dynamic_cast<ayt::ui::RichText*>(session.selected());
    session.applyProperty("richWrapMode", L"Word");
    session.applyProperty("richOverflow", L"Ellipsis");
    session.applyProperty("lineHeight", L"1.6");
    session.applyProperty("maxLines", L"3");
    if (rich == nullptr || rich->getWrapMode() != ayt::ui::RichTextWrapMode::Word ||
        rich->getOverflow() != ayt::ui::RichTextOverflow::Ellipsis ||
        std::fabs(rich->getLineHeight() - 1.6f) > 0.001f ||
        rich->getMaxLines() != 3u) {
        std::fprintf(stderr, "typed RichText inspector properties failed\n");
        return 118;
    }

    session.select(session.documentRoot(), false);
    session.addWidget("TileView");
    auto* tiles = dynamic_cast<ayt::ui::TileView*>(session.selected());
    session.applyProperty("selectionMode", L"Extended");
    session.applyProperty("tileWidth", L"112");
    session.applyProperty("tileHeight", L"148");
    session.applyProperty("tileSpacing", L"12");
    if (tiles == nullptr ||
        tiles->getSelectionMode() != ayt::ui::TileView::SelectionMode::Extended ||
        tiles->getTileSize() != ayt::math::FVector2(112.0f, 148.0f) ||
        tiles->getTileSpacing() != 12.0f) {
        std::fprintf(stderr, "typed TileView inspector properties failed\n");
        return 119;
    }

    session.select(session.documentRoot(), false);
    session.addWidget("Image");
    auto* image = dynamic_cast<ayt::ui::Image*>(session.selected());
    if (image == nullptr) {
        std::fprintf(stderr, "Image palette creation failed\n");
        return 14;
    }
    session.applyProperty("texture", L"Assets/ui/test.png");
    session.applyProperty("imageTint", L"#804020FF");
    session.applyProperty("uvMinX", L"0.1");
    session.applyProperty("uvMinY", L"0.2");
    session.applyProperty("uvMaxX", L"0.8");
    session.applyProperty("uvMaxY", L"0.9");
    session.applyProperty("controller", L"HudController");
    session.applyProperty("event:onClick", L"inspectImage");
    if (image->getTextureName() != "Assets/ui/test.png" ||
        image->getControllerId() != "HudController" ||
        image->getEventBinding("onClick") != "inspectImage" ||
        std::fabs(image->getColor().x - 128.0f / 255.0f) > 0.001f ||
        std::fabs(image->getUV().minX - 0.1f) > 0.001f ||
        std::fabs(image->getUV().maxY - 0.9f) > 0.001f) {
        std::fprintf(stderr, "Image authoring properties failed\n");
        return 15;
    }
    int previewLoads = 0;
    std::string decodedPreviewPath;
    session.setTexturePreviewLoader(
        [&previewLoads, &decodedPreviewPath](const std::string& texturePath) {
            ++previewLoads;
            decodedPreviewPath = texturePath;
            ayt::ui::ImageTextureHandle handle;
            handle.handle = reinterpret_cast<void*>(
                static_cast<uintptr_t>(0x1234));
            handle.width = 2;
            handle.height = 2;
            handle.name = texturePath;
            return handle;
        });
    if (previewLoads != 1 || !image->hasTexture() ||
        decodedPreviewPath != "D:/PreviewRoot/ui/test.png" ||
        image->getTextureName() != "Assets/ui/test.png") {
        std::fprintf(stderr, "image preview was not rehydrated\n");
        return 72;
    }
    session.refreshValidation();
    int missingTextureDiagnostics = 0;
    for (const ayt::ui::LayoutDiagnostic& diagnostic : session.diagnostics()) {
        if (diagnostic.code == ayt::ui::LayoutDiagnosticCode::MissingTexture &&
            diagnostic.widget == image) {
            ++missingTextureDiagnostics;
        }
    }
    if (missingTextureDiagnostics != 0) {
        std::fprintf(stderr, "stable resource key reported as missing\n");
        return 82;
    }
    const std::string imageId = image->getId();

    session.addWidget("ListView");
    auto* list = dynamic_cast<ayt::ui::ListView*>(session.selected());
    session.applyProperty("items", L"Alpha | Beta | Gamma");
    session.applyProperty("selectionMode", L"Extended");
    session.applyProperty("itemHeight", L"31");
    session.applyProperty("event:onSelectionChanged", L"selectEntry");
    if (list == nullptr || list->getItemCount() != 3u ||
        list->getItem(1) != L"Beta" ||
        list->getSelectionMode() != ayt::ui::ListView::SelectionMode::Extended ||
        list->getItemHeight() != 31.0f) {
        std::fprintf(stderr, "ListView palette/items creation failed\n");
        return 16;
    }
    const std::string listId = list->getId();

    // Collection controls are authoring leaves. Adding while one is selected
    // creates a sibling, never a hidden implementation child. Repeating the
    // operation must also mint distinct semantic IDs instead of `w_3`.
    ayt::ui::Widget* listParent = list->getParent();
    const size_t listRuntimeChildCount = list->getChildren().size();
    session.select(list, false);
    session.addWidget("Separator");
    auto* separator1 = dynamic_cast<ayt::ui::Separator*>(session.selected());
    session.select(list, false);
    session.addWidget("Separator");
    auto* separator2 = dynamic_cast<ayt::ui::Separator*>(session.selected());
    if (separator1 == nullptr || separator2 == nullptr ||
        separator1->getParent() != listParent ||
        separator2->getParent() != listParent ||
        list->getChildren().size() != listRuntimeChildCount ||
        separator1->getId() == separator2->getId() ||
        separator1->getPosition() == separator2->getPosition() ||
        separator1->getId().rfind("separator_", 0) != 0 ||
        separator2->getId().rfind("separator_", 0) != 0) {
        std::fprintf(stderr, "collection-leaf placement/ID regression\n");
        return 41;
    }

    session.select(session.documentRoot(), false);
    session.addWidget("RadioButton");
    auto* radio = dynamic_cast<ayt::ui::RadioButton*>(session.selected());
    if (radio == nullptr || radio->getText() != L"Option") {
        std::fprintf(stderr, "RadioButton default presentation failed\n");
        return 36;
    }
    session.applyProperty("text", L"Choice");
    session.applyProperty("checked", L"true");
    session.applyProperty("controller", L"FormController");
    session.applyProperty("event:onToggled", L"toggleChoice");
    if (radio->getText() != L"Choice" || !radio->isChecked() ||
        radio->getEventBinding("onToggled") != "toggleChoice") {
        std::fprintf(stderr, "RadioButton authoring properties failed\n");
        return 37;
    }
    const std::string radioId = radio->getId();

    session.select(session.documentRoot(), false);
    session.addWidget("GridPanel");
    auto* grid = dynamic_cast<ayt::ui::GridPanel*>(session.selected());
    if (grid == nullptr || grid->getRowCount() != 2 ||
        grid->getColumnCount() != 2) {
        std::fprintf(stderr, "GridPanel authoring defaults failed\n");
        return 42;
    }
    session.applyProperty("gridRows", L"3");
    session.applyProperty("gridColumns", L"3");
    session.applyProperty("gridSpacingX", L"7");
    session.applyProperty("gridSpacingY", L"9");
    if (grid->getRowCount() != 3 || grid->getColumnCount() != 3 ||
        grid->getHorizontalSpacing() != 7.0f ||
        grid->getVerticalSpacing() != 9.0f) {
        std::fprintf(stderr, "typed GridPanel inspector properties failed\n");
        return 120;
    }
    session.applyProperty("gridRows", L"2");
    session.applyProperty("gridColumns", L"2");
    for (int i = 0; i < 4; ++i) {
        session.select(grid, false);
        session.addWidget("Button");
    }
    grid->performLayout();
    ayt::ui::Widget* grid00 = grid->getCell(0, 0);
    ayt::ui::Widget* grid01 = grid->getCell(0, 1);
    ayt::ui::Widget* grid10 = grid->getCell(1, 0);
    ayt::ui::Widget* grid11 = grid->getCell(1, 1);
    if (grid->getRowCount() != 2 || grid->getColumnCount() != 2 ||
        grid00 == nullptr || grid01 == nullptr || grid10 == nullptr ||
        grid11 == nullptr || grid00->getPosition().x == grid01->getPosition().x ||
        grid00->getPosition().y == grid10->getPosition().y ||
        !grid00->isLayoutPositionManaged() ||
        !grid11->isLayoutSizeManaged() || grid00->hasAnchorLayout() ||
        grid11->hasAnchorLayout()) {
        std::fprintf(stderr,
            "GridPanel children did not form a 2x2 layout: rows=%d cols=%d\n",
            grid->getRowCount(), grid->getColumnCount());
        return 43;
    }
    const std::string gridId = grid->getId();

    session.select(session.documentRoot(), false);
    session.addWidget("TabControl");
    auto* tabs = dynamic_cast<ayt::ui::TabControl*>(session.selected());
    if (tabs == nullptr || tabs->getTabCount() != 2u ||
        tabs->getTabContent(0) == nullptr || tabs->getTabContent(1) == nullptr ||
        tabs->getTabContent(0)->getId() == tabs->getTabContent(1)->getId()) {
        std::fprintf(stderr, "TabControl default model failed\n");
        return 17;
    }
    session.applyProperty("items", L"General | Advanced | Input");
    if (tabs->getTabCount() != 3u || tabs->getTabLabel(2) != L"Input") {
        std::fprintf(stderr, "TabControl label model editing failed\n");
        return 20;
    }
    ayt::ui::Widget* inactivePage = tabs->getTabContent(2);
    if (inactivePage == nullptr || inactivePage->getId().empty()) {
        std::fprintf(stderr, "TabControl inactive page missing\n");
        return 21;
    }
    const std::string tabsId = tabs->getId();
    const std::string inactivePageId = inactivePage->getId();
    session.selectById(inactivePage->getId());
    if (session.selected() != inactivePage || tabs->getSelectedIndex() != 2 ||
        inactivePage->getParent() != tabs->getBodyPanel()) {
        std::fprintf(stderr, "inactive TabControl page selection failed\n");
        return 22;
    }
    session.select(tabs, false);
    session.addWidget("Button");
    if (session.selected() == nullptr ||
        session.selected()->getParent() != inactivePage) {
        std::fprintf(stderr, "TabControl page child placement failed\n");
        return 23;
    }
    const std::string tabChildId = session.selected()->getId();

    session.select(session.documentRoot(), false);
    session.addWidget("ScrollView");
    auto* scroll = dynamic_cast<ayt::ui::ScrollView*>(session.selected());
    if (scroll == nullptr || scroll->getContent() == nullptr) {
        std::fprintf(stderr, "ScrollView editable content model failed\n");
        return 24;
    }
    session.applyProperty("verticalScrollBarVisibility", L"Always");
    session.applyProperty("horizontalScrollBarVisibility", L"Auto");
    if (scroll->getVerticalScrollBarVisibility() !=
            ayt::ui::ScrollView::ScrollBarVisibility::Always ||
        scroll->getHorizontalScrollBarVisibility() !=
            ayt::ui::ScrollView::ScrollBarVisibility::Auto) {
        std::fprintf(stderr, "typed ScrollView inspector properties failed\n");
        return 121;
    }
    ayt::ui::Widget* scrollContent = scroll->getContent();
    const std::string scrollId = scroll->getId();
    const std::string scrollContentId = scrollContent->getId();
    session.addWidget("Button");
    if (session.selected() == nullptr ||
        session.selected()->getParent() != scrollContent) {
        std::fprintf(stderr, "ScrollView content child placement failed\n");
        return 25;
    }
    const std::string scrollChildId = session.selected()->getId();

    session.select(session.documentRoot(), false);
    session.addWidget("Modal");
    auto* modal = dynamic_cast<ayt::ui::Modal*>(session.selected());
    if (modal == nullptr || modal->getContent() == nullptr) {
        std::fprintf(stderr, "Modal default content failed\n");
        return 18;
    }
    ayt::ui::Widget* modalContent = modal->getContent();
    const std::string modalId = modal->getId();
    const std::string modalContentId = modalContent->getId();
    session.addWidget("TextLabel");
    if (session.selected() == nullptr ||
        session.selected()->getParent() != modalContent) {
        std::fprintf(stderr, "Modal content child placement failed\n");
        return 26;
    }
    const std::string modalChildId = session.selected()->getId();

    session.select(session.documentRoot(), false);
    session.addWidget("TabStrip");
    auto* strip = dynamic_cast<ayt::ui::TabStrip*>(session.selected());
    if (strip == nullptr || strip->getTabCount() != 2) {
        std::fprintf(stderr, "TabStrip default model failed\n");
        return 27;
    }
    session.applyProperty("items", L"Scene | Game | Profiler");
    session.applyProperty("overflowMode", L"Compress");
    session.applyProperty("minTabWidth", L"96");
    if (strip->getTabCount() != 3 || strip->getTabLabel(2) != L"Profiler") {
        std::fprintf(stderr, "TabStrip label editing failed\n");
        return 28;
    }
    if (strip->getOverflowMode() != ayt::ui::TabStrip::OverflowMode::Compress ||
        strip->getMinTabWidth() != 96.0f) {
        std::fprintf(stderr, "typed TabStrip inspector properties failed\n");
        return 122;
    }
    const std::string stripId = strip->getId();

    session.select(session.documentRoot(), false);
    session.addWidget("ModalDialog");
    auto* dialog = dynamic_cast<ayt::ui::ModalDialog*>(session.selected());
    if (dialog == nullptr || dialog->getBodyContent() == nullptr) {
        std::fprintf(stderr, "ModalDialog default body failed\n");
        return 29;
    }
    ayt::ui::Widget* dialogBody = dialog->getBodyContent();
    const std::string dialogId = dialog->getId();
    const std::string dialogBodyId = dialogBody->getId();
    session.addWidget("TextInput");
    if (session.selected() == nullptr ||
        session.selected()->getParent() != dialogBody) {
        std::fprintf(stderr, "ModalDialog body child placement failed\n");
        return 30;
    }
    const std::string dialogChildId = session.selected()->getId();

    // New free-positioned widgets receive a top-left fixed anchor. Changing
    // to bottom-right preserves the current rectangle, participates in
    // parent resize, and round-trips through editor undo/redo.
    session.select(session.documentRoot(), false);
    session.addWidget("Button");
    ayt::ui::Widget* anchored = session.selected();
    if (anchored == nullptr || !anchored->hasAnchorLayout() ||
        anchored->getAnchorLayout().anchorMin.x != 0.0f ||
        anchored->getAnchorLayout().anchorMin.y != 0.0f) {
        std::fprintf(stderr, "default free-layout anchor failed\n");
        return 46;
    }
    const std::string anchoredId = anchored->getId();
    const ayt::math::FVector2 anchoredBefore = anchored->getPosition();
    const ayt::math::FVector2 anchoredSize = anchored->getSize();
    session.setAnchorPreset(
        ayt::ui::LayoutEditorSession::AnchorAxisMode::End,
        ayt::ui::LayoutEditorSession::AnchorAxisMode::End);
    if (!anchored->hasAnchorLayout() ||
        anchored->getAnchorLayout().anchorMin.x != 1.0f ||
        anchored->getAnchorLayout().anchorMin.y != 1.0f ||
        anchored->getPosition() != anchoredBefore ||
        anchored->getSize() != anchoredSize) {
        std::fprintf(stderr, "bottom-right anchor preset failed\n");
        return 47;
    }
    auto* activeAnchorPreset = dynamic_cast<ayt::ui::Button*>(
        ui.findById("btn_anchor_22"));
    if (activeAnchorPreset == nullptr ||
        activeAnchorPreset->getStyleId() != "__le_primary") {
        std::fprintf(stderr, "active anchor preset highlight failed\n");
        return 53;
    }
    session.undo();
    anchored = session.selected();
    if (anchored == nullptr || anchored->getId() != anchoredId ||
        !anchored->hasAnchorLayout() ||
        anchored->getAnchorLayout().anchorMin.x != 0.0f) {
        std::fprintf(stderr, "anchor preset undo failed\n");
        return 48;
    }
    session.redo();
    anchored = session.selected();
    if (anchored == nullptr || anchored->getId() != anchoredId ||
        !anchored->hasAnchorLayout() ||
        anchored->getAnchorLayout().anchorMin.x != 1.0f) {
        std::fprintf(stderr, "anchor preset redo failed\n");
        return 49;
    }
    ayt::ui::Widget* anchorRoot = session.documentRoot();
    const ayt::math::FVector2 rootSizeBeforeAnchorResize = anchorRoot->getSize();
    session.select(anchorRoot, false);
    session.applyProperty(
        "w", std::to_wstring(rootSizeBeforeAnchorResize.x + 80.0f));
    session.applyProperty(
        "h", std::to_wstring(rootSizeBeforeAnchorResize.y + 60.0f));
    ui.layout();
    if (std::fabs(anchored->getPosition().x -
                  (anchoredBefore.x + 80.0f)) > 0.001f ||
        std::fabs(anchored->getPosition().y -
                  (anchoredBefore.y + 60.0f)) > 0.001f ||
        std::fabs(anchored->getSize().x - anchoredSize.x) > 0.001f ||
        std::fabs(anchored->getSize().y - anchoredSize.y) > 0.001f) {
        std::fprintf(stderr,
            "editor parent-resize anchor response failed: before=(%.3f,%.3f) "
            "after=(%.3f,%.3f) expected=(%.3f,%.3f) size=(%.3f,%.3f) "
            "flags=(%d,%d)\n",
            anchoredBefore.x, anchoredBefore.y,
            anchored->getPosition().x, anchored->getPosition().y,
            anchoredBefore.x + 80.0f, anchoredBefore.y + 60.0f,
            anchored->getSize().x, anchored->getSize().y,
            anchored->isLayoutPositionManaged() ? 1 : 0,
            anchored->isLayoutSizeManaged() ? 1 : 0);
        return 50;
    }
    session.applyProperty("w", std::to_wstring(rootSizeBeforeAnchorResize.x));
    session.applyProperty("h", std::to_wstring(rootSizeBeforeAnchorResize.y));
    ui.layout();

    // The root keeps its fixed origin but exposes right/bottom resize
    // handles. Dragging the lower-right handle must resize the preview and
    // update its bottom-right anchored child in the same pointer move.
    session.select(anchorRoot, false);
    const ayt::math::FRectangle rootBounds = anchorRoot->getWorldBounds();
    const ayt::math::FVector2 rootResizeStart(rootBounds.maxX,
                                              rootBounds.maxY);
    if (session.canvasCursorHint(rootResizeStart) !=
            ayt::ui::UiCursorHint::SizeNwse ||
        !session.onPointerDown(rootResizeStart, 0) ||
        !session.onPointerMove(rootResizeStart +
                               ayt::math::FVector2(32.0f, 24.0f)) ||
        !session.onPointerUp(rootResizeStart +
                             ayt::math::FVector2(32.0f, 24.0f), 0)) {
        std::fprintf(stderr, "root resize handle interaction failed\n");
        return 60;
    }
    if (anchorRoot->getSize() != rootSizeBeforeAnchorResize +
            ayt::math::FVector2(32.0f, 24.0f) ||
        anchored->getPosition() != anchoredBefore +
            ayt::math::FVector2(32.0f, 24.0f)) {
        std::fprintf(stderr, "root resize anchor preview failed\n");
        return 61;
    }
    session.undo();
    anchorRoot = session.documentRoot();
    session.selectById(anchoredId);
    anchored = session.selected();
    if (anchorRoot == nullptr || anchored == nullptr) {
        std::fprintf(stderr, "root resize undo restore failed\n");
        return 62;
    }
    session.select(anchored, false);
    session.setAnchorPreset(
        ayt::ui::LayoutEditorSession::AnchorAxisMode::Center,
        ayt::ui::LayoutEditorSession::AnchorAxisMode::Center, true);
    const ayt::math::FVector2 snappedCenter(
        (rootSizeBeforeAnchorResize.x - anchoredSize.x) * 0.5f,
        (rootSizeBeforeAnchorResize.y - anchoredSize.y) * 0.5f);
    if (std::fabs(anchored->getPosition().x - snappedCenter.x) > 0.001f ||
        std::fabs(anchored->getPosition().y - snappedCenter.y) > 0.001f) {
        std::fprintf(stderr, "ctrl anchor preset snap failed\n");
        return 56;
    }
    session.undo();
    anchored = session.selected();
    if (anchored == nullptr || anchored->getId() != anchoredId ||
        anchored->getAnchorLayout().anchorMin.x != 1.0f ||
        anchored->getPosition() != anchoredBefore) {
        std::fprintf(stderr, "anchor snap undo failed\n");
        return 57;
    }
    session.clearSelectedAnchors();
    if (session.selected() == nullptr || session.selected()->hasAnchorLayout()) {
        std::fprintf(stderr, "absolute anchor mode failed\n");
        return 54;
    }
    session.undo();
    anchored = session.selected();
    if (anchored == nullptr || anchored->getId() != anchoredId ||
        !anchored->hasAnchorLayout()) {
        std::fprintf(stderr, "absolute anchor undo failed\n");
        return 55;
    }

    // Reusable blocks persist with the document but instantiate expanded,
    // independently editable copies with reminted IDs.
    session.selectById("btn_hello");
    if (!session.defineReusableBlock("Primary Action") ||
        session.reuseLibrary().size() != 1u ||
        !session.insertReusableBlock("Primary Action")) {
        std::fprintf(stderr, "reusable block authoring failed\n");
        return 86;
    }
    ayt::ui::Widget* reusableCopy = session.selected();
    if (reusableCopy == nullptr || reusableCopy->getId() == "btn_hello") {
        std::fprintf(stderr, "reusable block ID remint failed\n");
        return 87;
    }
    const std::string reusableCopyId = reusableCopy->getId();
    session.setResponsiveVisibility(
        0, ayt::ui::ResponsiveVisibility::Hidden);
    if (reusableCopy->getResponsiveLayoutRules().size() != 1u ||
        !reusableCopy->isVisible()) {
        std::fprintf(stderr, "responsive compact rule authoring failed\n");
        return 88;
    }

    session.selectById("btn_hello");
    ayt::ui::Widget* animationTarget = session.selected();
    if (animationTarget == nullptr ||
        !session.createAnimationClip("Intro") ||
        !session.addAnimationTrack(
            0, ayt::ui::UIAnimationProperty::Opacity) ||
        !session.captureAnimationKeyframe(
            0, 0, 0.0f, ayt::ui::AnimationCurve::Linear)) {
        std::fprintf(stderr, "animation clip/track authoring failed\n");
        return 93;
    }
    animationTarget->setOpacity(0.0f);
    if (!session.captureAnimationKeyframe(
            0, 0, 100.0f, ayt::ui::AnimationCurve::Linear) ||
        !session.setAnimationPlayback(
            0, 1, true, ayt::ui::AnimationImportance::Essential)) {
        std::fprintf(stderr, "animation key/playback authoring failed\n");
        return 94;
    }
    animationTarget->setOpacity(1.0f);
    if (!session.previewAnimationFrame(0, 50.0f) ||
        std::fabs(session.selected()->getOpacity() - 0.5f) > 0.001f) {
        std::fprintf(stderr, "animation scrub preview failed\n");
        return 95;
    }
    session.stopAnimationPreview();
    session.selectById("btn_hello");
    if (session.selected() == nullptr ||
        std::fabs(session.selected()->getOpacity() - 1.0f) > 0.001f ||
        session.animationLibrary().size() != 1u) {
        std::fprintf(stderr, "animation scrub reset failed\n");
        return 96;
    }
    if (!session.playAnimationPreview(0)) {
        std::fprintf(stderr, "animation continuous play failed\n");
        return 97;
    }
    session.advanceAnimationPreview(0.05f);
    if (!session.isAnimationPreviewPlaying() ||
        std::fabs(session.selected()->getOpacity() - 0.5f) > 0.001f) {
        std::fprintf(stderr, "animation continuous midpoint failed\n");
        return 98;
    }
    session.pauseAnimationPreview();
    const float pausedOpacity = session.selected()->getOpacity();
    session.advanceAnimationPreview(0.05f);
    if (!session.isAnimationPreviewPaused() ||
        std::fabs(session.selected()->getOpacity() - pausedOpacity) > 0.001f) {
        std::fprintf(stderr, "animation pause gate failed\n");
        return 99;
    }
    if (!session.playAnimationPreview(0)) {
        std::fprintf(stderr, "animation resume failed\n");
        return 100;
    }
    session.advanceAnimationPreview(0.05f);
    session.stopAnimationPreview();
    if (std::fabs(session.selected()->getOpacity() - 1.0f) > 0.001f) {
        std::fprintf(stderr, "animation stop authored-value restore failed\n");
        return 101;
    }

    if (!session.playAnimationPreview(0)) {
        std::fprintf(stderr, "animation large-delta preview failed to start\n");
        return 106;
    }
    session.advanceAnimationPreview(0.15f);
    auto* transportStatus = dynamic_cast<ayt::ui::TextLabel*>(
        ui.findById("animation_transport_status"));
    if (!session.isAnimationPreviewPlaying() ||
        std::fabs(session.selected()->getOpacity() - 0.5f) > 0.001f ||
        transportStatus == nullptr ||
        transportStatus->getText().find(L"Playing") == std::wstring::npos) {
        std::fprintf(stderr, "animation real-delta/status update failed\n");
        return 107;
    }
    session.stopAnimationPreview();

    session.setAnimationPreviewLoop(true);
    if (!session.playAnimationPreview(0)) {
        std::fprintf(stderr, "animation loop preview failed to start\n");
        return 108;
    }
    session.advanceAnimationPreview(0.25f);
    if (!session.isAnimationPreviewPlaying()) {
        std::fprintf(stderr, "animation live loop enable failed\n");
        return 109;
    }
    session.setAnimationPreviewLoop(false);
    session.advanceAnimationPreview(0.001f);
    if (session.isAnimationPreviewPlaying()) {
        std::fprintf(stderr, "animation live loop disable failed\n");
        return 110;
    }
    session.stopAnimationPreview();

    ayt::ui::CubicBezierParameters easeIn;
    easeIn.x1 = 0.42f;
    easeIn.y1 = 0.0f;
    easeIn.x2 = 1.0f;
    easeIn.y2 = 1.0f;
    const bool authoredBezier = session.setAnimationKeyframeCurve(
        0, 0, 1, ayt::ui::AnimationCurve::CubicBezier, easeIn);
    const bool previewedBezier = session.previewAnimationFrame(0, 50.0f);
    const float bezierOpacity = session.selected()->getOpacity();
    const float expectedBezierOpacity = 1.0f -
        ayt::ui::evaluateCubicBezier(0.5f, easeIn);
    if (!authoredBezier || !previewedBezier ||
        std::fabs(bezierOpacity - expectedBezierOpacity) > 0.001f) {
        std::fprintf(stderr,
            "animation Bezier authoring failed (authored=%d previewed=%d opacity=%.6f)\n",
            authoredBezier ? 1 : 0, previewedBezier ? 1 : 0,
            static_cast<double>(bezierOpacity));
        return 119;
    }
    session.stopAnimationPreview();
    if (!session.setAnimationKeyframeCurve(
            0, 0, 1, ayt::ui::AnimationCurve::Linear)) {
        std::fprintf(stderr, "animation curve reset failed\n");
        return 120;
    }

    if (!session.beginAnimationKeyframeDrag(0, 0, 1) ||
        session.updateAnimationKeyframeDrag(200.0f) != 1) {
        std::fprintf(stderr, "animation key drag failed\n");
        return 102;
    }
    session.endAnimationKeyframeDrag();
    session.stopAnimationPreview();
    if (session.animationLibrary().clips()[0].tracks[0]
            .keyframes[1].timeMs != 200.0f) {
        std::fprintf(stderr, "animation key drag ordering failed\n");
        return 103;
    }
    session.undo();
    session.selectById("btn_hello");
    if (session.animationLibrary().clips()[0].tracks[0]
            .keyframes[1].timeMs != 100.0f ||
        session.selected() == nullptr) {
        std::fprintf(stderr, "animation key drag undo failed\n");
        return 104;
    }

    // Refresh the timeline after snapshot restore, then exercise the actual
    // host routing contract: Session must leave middle-button presses outside
    // Canvas to the timeline, and capture cancellation must roll a retime back.
    session.setAnimationPreviewLoop(true);
    session.setAnimationPreviewLoop(false);
    ayt::ui::LayoutAnimationTimelineView* timelineView = nullptr;
    for (ayt::ui::Widget* child : timelineHost->getChildren()) {
        if (child != nullptr &&
            child->getId() == "__le_animation_timeline_view") {
            timelineView =
                dynamic_cast<ayt::ui::LayoutAnimationTimelineView*>(child);
            break;
        }
    }
    if (timelineView == nullptr) {
        std::fprintf(stderr, "animation timeline view unavailable\n");
        return 111;
    }
    session.pumpDeferred(0.0f);
    const ayt::math::FRectangle timelineBounds = timelineView->getWorldBounds();
    const ayt::math::FVector2 timelinePanPoint(
        timelineBounds.minX + 8.0f, timelineBounds.minY + 8.0f);
    if (session.onPointerDown(timelinePanPoint, 2) ||
        !ui.onMouseButtonDown(timelinePanPoint.x, timelinePanPoint.y, 2)) {
        std::fprintf(stderr, "animation timeline middle-pan routing failed\n");
        return 112;
    }
    ui.cancelCapture();

    const ayt::math::FRectangle keyBounds =
        timelineView->keyframeBounds(0, 0);
    const ayt::math::FVector2 keyCenter(
        (keyBounds.minX + keyBounds.maxX) * 0.5f,
        (keyBounds.minY + keyBounds.maxY) * 0.5f);
    const ayt::math::FRectangle timelinePlot = timelineView->plotBounds();
    const float movedX = timelinePlot.minX +
        (timelinePlot.maxX - timelinePlot.minX) * 0.2f;
    if (!ui.onMouseButtonDown(keyCenter.x, keyCenter.y, 0) ||
        !ui.onMouseMove(movedX, keyCenter.y) ||
        session.animationLibrary().clips()[0].tracks[0]
                .keyframes[0].timeMs == 0.0f) {
        std::fprintf(stderr, "animation timeline cancel setup failed\n");
        return 113;
    }
    ui.cancelCapture();
    const auto& cancelledKeys =
        session.animationLibrary().clips()[0].tracks[0].keyframes;
    if (cancelledKeys.size() != 2u || cancelledKeys[0].timeMs != 0.0f ||
        cancelledKeys[1].timeMs != 100.0f) {
        std::fprintf(stderr, "animation key drag cancel rollback failed\n");
        return 114;
    }
    session.applyProperty("text", L"RoundTrip");
    session.applyProperty("w", L"150");
    const ayt::math::FVector2 authoredRootSize =
        session.documentRoot()->getSize();
    session.setPreviewPreset(3);
    if (session.documentRoot()->getSize().x != 390.0f ||
        session.documentRoot()->getSize().y != 844.0f ||
        !session.previewSettings().showSafeArea) {
        std::fprintf(stderr, "device preview profile failed\n");
        return 81;
    }
    session.selectById(reusableCopyId);
    if (session.selected() == nullptr || session.selected()->isVisible()) {
        std::fprintf(stderr, "responsive compact preview failed\n");
        return 89;
    }

    const std::filesystem::path outFile =
        std::filesystem::temp_directory_path() /
        ("ayui_layout_editor_roundtrip_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) +
         ".ui.json");
    struct TempFileCleanup {
        std::filesystem::path path;
        ~TempFileCleanup() {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    } cleanup{outFile};
    const std::string outPath = outFile.string();
    {
        std::ofstream sentinel(outFile, std::ios::binary | std::ios::trunc);
        sentinel << "existing file must survive until atomic replacement";
    }
    if (!session.saveAs(outPath)) {
        std::fprintf(stderr, "save failed\n");
        return 4;
    }
    const std::string tempPrefix = outFile.filename().string() + ".tmp.";
    int temporaryFiles = 0;
    for (const auto& entry :
         std::filesystem::directory_iterator(outFile.parent_path())) {
        if (entry.path().filename().string().rfind(tempPrefix, 0) == 0) {
            ++temporaryFiles;
        }
    }
    if (temporaryFiles != 0) {
        std::fprintf(stderr, "atomic save left %d temporary file(s)\n",
                     temporaryFiles);
        return 73;
    }
    {
        std::ifstream encodedDocument(outFile, std::ios::binary);
        const std::string encoded(
            (std::istreambuf_iterator<char>(encodedDocument)),
            std::istreambuf_iterator<char>());
        if (encoded.find("\"reusable\"") == std::string::npos ||
            encoded.find("Primary Action") == std::string::npos ||
            encoded.find("\"responsive\"") == std::string::npos ||
            encoded.find("\"animations\"") == std::string::npos ||
            encoded.find("\"Intro\"") == std::string::npos) {
            std::fprintf(stderr,
                "reusable/responsive/animation envelope was not persisted\n");
            return 90;
        }
    }

    ayt::ui::UILayoutLoader loader;
    loader.setWidgetFactory(&ayt::ui::WidgetFactory::get());
    ayt::ui::Widget* reloaded = loader.loadFromFile(outPath);
    if (reloaded == nullptr) {
        std::fprintf(stderr, "reload failed\n");
        return 5;
    }
    if (reloaded->getSize().x != authoredRootSize.x ||
        reloaded->getSize().y != authoredRootSize.y) {
        std::fprintf(stderr, "preview size leaked into saved document\n");
        ayt::ui::destroyWidgetTree(reloaded);
        return 82;
    }

    auto* loadedImage = dynamic_cast<ayt::ui::Image*>(
        loader.findWidgetById(imageId));
    auto* loadedList = dynamic_cast<ayt::ui::ListView*>(
        loader.findWidgetById(listId));
    auto* loadedRadio = dynamic_cast<ayt::ui::RadioButton*>(
        loader.findWidgetById(radioId));
    auto* loadedGrid = dynamic_cast<ayt::ui::GridPanel*>(
        loader.findWidgetById(gridId));
    auto* loadedTabs = dynamic_cast<ayt::ui::TabControl*>(
        loader.findWidgetById(tabsId));
    auto* loadedScroll = dynamic_cast<ayt::ui::ScrollView*>(
        loader.findWidgetById(scrollId));
    auto* loadedModal = dynamic_cast<ayt::ui::Modal*>(
        loader.findWidgetById(modalId));
    auto* loadedStrip = dynamic_cast<ayt::ui::TabStrip*>(
        loader.findWidgetById(stripId));
    auto* loadedDialog = dynamic_cast<ayt::ui::ModalDialog*>(
        loader.findWidgetById(dialogId));
    ayt::ui::Widget* loadedAnchored = loader.findWidgetById(anchoredId);
    ayt::ui::Widget* loadedAnimated = loader.findWidgetById("btn_hello");
    std::size_t unresolvedAnimationTracks = 99u;
    ayt::ui::AnimationTimeline loadedTimeline =
        loader.createAnimationTimeline("Intro", &unresolvedAnimationTracks);
    loadedTimeline.seek(50.0f);
    if (loadedAnimated == nullptr || unresolvedAnimationTracks != 0u ||
        std::fabs(loadedAnimated->getOpacity() - 0.5f) > 0.001f) {
        std::fprintf(stderr, "runtime animation timeline reload failed\n");
        ayt::ui::destroyWidgetTree(reloaded);
        return 97;
    }

    if (loadedImage == nullptr ||
        loadedImage->getTextureName() != "Assets/ui/test.png" ||
        loadedImage->getControllerId() != "HudController" ||
        loadedImage->getEventBinding("onClick") != "inspectImage" ||
        std::fabs(loadedImage->getColor().x - 128.0f / 255.0f) > 0.001f ||
        std::fabs(loadedImage->getUV().minX - 0.1f) > 0.001f ||
        std::fabs(loadedImage->getUV().maxY - 0.9f) > 0.001f ||
        loadedList == nullptr || loadedList->getItemCount() != 3u ||
        loadedList->getSelectionMode() !=
            ayt::ui::ListView::SelectionMode::Extended ||
        loadedList->getItemHeight() != 31.0f ||
        loadedList->getEventBinding("onSelectionChanged") != "selectEntry" ||
        loadedRadio == nullptr || loadedRadio->getText() != L"Choice" ||
        !loadedRadio->isChecked() ||
        loadedRadio->getControllerId() != "FormController" ||
        loadedRadio->getEventBinding("onToggled") != "toggleChoice") {
        std::fprintf(stderr, "reloaded leaf/collection payload failed\n");
        ayt::ui::destroyWidgetTree(reloaded);
        return 31;
    }
    if (loadedGrid == nullptr || loadedGrid->getRowCount() != 2 ||
        loadedGrid->getColumnCount() != 2 ||
        loadedGrid->getHorizontalSpacing() != 7.0f ||
        loadedGrid->getVerticalSpacing() != 9.0f ||
        loadedGrid->getCell(0, 0) == nullptr ||
        loadedGrid->getCell(1, 1) == nullptr) {
        std::fprintf(stderr,
            "reloaded GridPanel structure failed: grid=%p rows=%d cols=%d c00=%p c11=%p\n",
            static_cast<void*>(loadedGrid),
            loadedGrid != nullptr ? loadedGrid->getRowCount() : -1,
            loadedGrid != nullptr ? loadedGrid->getColumnCount() : -1,
            static_cast<void*>(loadedGrid != nullptr ? loadedGrid->getCell(0, 0) : nullptr),
            static_cast<void*>(loadedGrid != nullptr ? loadedGrid->getCell(1, 1) : nullptr));
        ayt::ui::destroyWidgetTree(reloaded);
        return 44;
    }

    ayt::ui::Widget* loadedInactivePage =
        loader.findWidgetById(inactivePageId);
    if (loadedTabs == nullptr || loadedTabs->getTabCount() != 3u ||
        loadedTabs->getTabLabel(2) != L"Input" ||
        loadedTabs->getTabContent(2) != loadedInactivePage ||
        loader.findWidgetById(tabChildId) == nullptr) {
        std::fprintf(stderr, "reloaded TabControl structure failed\n");
        ayt::ui::destroyWidgetTree(reloaded);
        return 32;
    }

    if (loadedScroll == nullptr ||
        loadedScroll->getContent() != loader.findWidgetById(scrollContentId) ||
        loader.findWidgetById(scrollChildId) == nullptr ||
        loadedScroll->getVerticalScrollBarVisibility() !=
            ayt::ui::ScrollView::ScrollBarVisibility::Always ||
        loadedScroll->getHorizontalScrollBarVisibility() !=
            ayt::ui::ScrollView::ScrollBarVisibility::Auto) {
        std::fprintf(stderr, "reloaded ScrollView structure failed\n");
        ayt::ui::destroyWidgetTree(reloaded);
        return 33;
    }

    if (loadedModal == nullptr ||
        loadedModal->getContent() != loader.findWidgetById(modalContentId) ||
        loader.findWidgetById(modalChildId) == nullptr ||
        loadedStrip == nullptr || loadedStrip->getTabCount() != 3 ||
        loadedStrip->getTabLabel(2) != L"Profiler" ||
        loadedStrip->getOverflowMode() !=
            ayt::ui::TabStrip::OverflowMode::Compress ||
        loadedStrip->getMinTabWidth() != 96.0f) {
        std::fprintf(stderr, "reloaded modal/tab-strip structure failed\n");
        ayt::ui::destroyWidgetTree(reloaded);
        return 34;
    }

    if (loadedDialog == nullptr || loadedDialog->getBodyContent() !=
            loader.findWidgetById(dialogBodyId) ||
        loader.findWidgetById(dialogChildId) == nullptr) {
        std::fprintf(stderr, "reloaded ModalDialog structure failed\n");
        ayt::ui::destroyWidgetTree(reloaded);
        return 35;
    }
    if (loadedAnchored == nullptr || !loadedAnchored->hasAnchorLayout() ||
        loadedAnchored->getAnchorLayout().anchorMin.x != 1.0f ||
        loadedAnchored->getAnchorLayout().anchorMin.y != 1.0f) {
        std::fprintf(stderr, "reloaded anchor layout failed\n");
        ayt::ui::destroyWidgetTree(reloaded);
        return 51;
    }
    ayt::ui::Widget* loadedReusable = loader.findWidgetById(reusableCopyId);
    if (loadedReusable == nullptr ||
        loadedReusable->getResponsiveLayoutRules().size() != 1u) {
        std::fprintf(stderr, "reloaded responsive reusable copy failed\n");
        ayt::ui::destroyWidgetTree(reloaded);
        return 91;
    }
    reloaded->setSize({390.0f, reloaded->getSize().y});
    if (loadedReusable->isVisible()) {
        std::fprintf(stderr, "reloaded responsive rule did not evaluate\n");
        ayt::ui::destroyWidgetTree(reloaded);
        return 92;
    }

    std::string json;
    const bool okJson = loader.saveLayoutToString(reloaded, json, true);
    ayt::ui::destroyWidgetTree(reloaded);
    if (!okJson || json.find("RoundTrip") == std::string::npos) {
        std::fprintf(stderr, "verify text failed\n%s\n", json.c_str());
        return 6;
    }
    if (json.find("150") == std::string::npos) {
        std::fprintf(stderr, "verify size failed\n%s\n", json.c_str());
        return 7;
    }
    if (json.find("Assets/ui/test.png") == std::string::npos ||
        json.find("HudController") == std::string::npos ||
        json.find("inspectImage") == std::string::npos ||
        json.find("selectEntry") == std::string::npos ||
        json.find("\"type\": \"TabControl\"") == std::string::npos ||
        json.find("\"type\": \"Modal\"") == std::string::npos) {
        std::fprintf(stderr, "expanded designer round-trip failed\n%s\n",
                     json.c_str());
        return 19;
    }

    session.selectById("btn_hello");
    auto* previewButton = dynamic_cast<ayt::ui::Button*>(session.selected());
    if (previewButton == nullptr || previewButton->isEnabled()) {
        std::fprintf(stderr, "edit-mode interaction freeze failed\n");
        return 83;
    }
    session.setMode(ayt::ui::LayoutEditorSession::Mode::Interact);
    if (!session.isInteractionPreview() || !previewButton->isEnabled()) {
        std::fprintf(stderr, "interaction preview did not activate controls\n");
        return 84;
    }
    previewButton->setText(L"Transient preview state");
    session.setMode(ayt::ui::LayoutEditorSession::Mode::Edit);
    session.selectById("btn_hello");
    previewButton = dynamic_cast<ayt::ui::Button*>(session.selected());
    if (previewButton == nullptr || previewButton->getText() != L"RoundTrip" ||
        previewButton->isEnabled()) {
        std::fprintf(stderr, "interaction preview rollback failed\n");
        return 85;
    }

    session.detach();
    ui.shutdown();
    ayt::ui::setClipboardImpl(nullptr);
    std::printf("ROUNDTRIP_OK\n");
    return 0;
}
