// Headless round-trip check for LayoutEditorSession (no HWND).
#include "AYUI/LayoutEditor/LayoutEditorSession.h"

#include "AYUI/LayoutLoader.h"
#include "AYUI/Button.h"
#include "AYUI/Clipboard.h"
#include "AYUI/GridPanel.h"
#include "AYUI/Image.h"
#include "AYUI/ListView.h"
#include "AYUI/MenuBar.h"
#include "AYUI/Modal.h"
#include "AYUI/ModalDialog.h"
#include "AYUI/RadioButton.h"
#include "AYUI/ScrollView.h"
#include "AYUI/Separator.h"
#include "AYUI/TabControl.h"
#include "AYUI/TabStrip.h"
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
    ayt::ui::ThemeManager::get().ensureDefaultThemes();
    ayt::ui::ThemeManager::get().setActiveTheme("dark");

    ayt::ui::UIManager ui;
    ui.initialize(nullptr);
    if (!ui.loadLayout("assets/layout_editor.ui.json")) {
        std::fprintf(stderr, "chrome load failed\n");
        return 1;
    }

    ayt::ui::LayoutEditorSession session;
    auto* clipboard = new TestClipboard();
    ayt::ui::setClipboardImpl(clipboard);
    if (!session.attach(ui)) {
        std::fprintf(stderr, "attach failed\n");
        return 2;
    }
    if (session.documentRoot() == nullptr ||
        session.documentRoot()->isLayoutSizeManaged()) {
        std::fprintf(stderr, "new document root size is not authorable\n");
        return 64;
    }
    auto* menuBar = dynamic_cast<ayt::ui::MenuBar*>(
        ui.findById("designer_menubar"));
    if (menuBar == nullptr || menuBar->getMenuCount() != 3u) {
        std::fprintf(stderr, "designer menu regression: count=%zu\n",
            menuBar != nullptr ? menuBar->getMenuCount() : 0u);
        return 9;
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
        "validation_status", "btn_validate", "reuse_name", "reuse_list",
        "btn_reuse_define", "btn_reuse_insert", "responsive_breakpoint",
        "responsive_visibility", "btn_responsive_preview",
        "btn_responsive_capture"
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
        std::fprintf(stderr, "root inspector visibility/scroll reset failed\n");
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

    session.addWidget("Image");
    auto* image = dynamic_cast<ayt::ui::Image*>(session.selected());
    if (image == nullptr) {
        std::fprintf(stderr, "Image palette creation failed\n");
        return 14;
    }
    session.setTextureResourceProvider([]() {
        return std::vector<ayt::ui::LayoutTextureResource>{
            {"Assets/ui/test.png", L"Project / ui / test.png",
             "D:/PreviewRoot/ui/test.png", L"PNG"}
        };
    });
    session.applyProperty("texture", L"Assets/ui/test.png");
    session.applyProperty("controller", L"HudController");
    session.applyProperty("event:onClick", L"inspectImage");
    if (image->getTextureName() != "Assets/ui/test.png" ||
        image->getControllerId() != "HudController" ||
        image->getEventBinding("onClick") != "inspectImage") {
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
    session.applyProperty("event:onSelectionChanged", L"selectEntry");
    if (list == nullptr || list->getItemCount() != 3u ||
        list->getItem(1) != L"Beta") {
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
    if (strip->getTabCount() != 3 || strip->getTabLabel(2) != L"Profiler") {
        std::fprintf(stderr, "TabStrip label editing failed\n");
        return 28;
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
            encoded.find("\"responsive\"") == std::string::npos) {
            std::fprintf(stderr,
                "reusable/responsive document envelope was not persisted\n");
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

    if (loadedImage == nullptr ||
        loadedImage->getTextureName() != "Assets/ui/test.png" ||
        loadedImage->getControllerId() != "HudController" ||
        loadedImage->getEventBinding("onClick") != "inspectImage" ||
        loadedList == nullptr || loadedList->getItemCount() != 3u ||
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
        loader.findWidgetById(scrollChildId) == nullptr) {
        std::fprintf(stderr, "reloaded ScrollView structure failed\n");
        ayt::ui::destroyWidgetTree(reloaded);
        return 33;
    }

    if (loadedModal == nullptr ||
        loadedModal->getContent() != loader.findWidgetById(modalContentId) ||
        loader.findWidgetById(modalChildId) == nullptr ||
        loadedStrip == nullptr || loadedStrip->getTabCount() != 3 ||
        loadedStrip->getTabLabel(2) != L"Profiler") {
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
