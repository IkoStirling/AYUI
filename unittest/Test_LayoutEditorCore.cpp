#include "AYTest.h"

#include "AYUI/Button.h"
#include "AYUI/GridPanel.h"
#include "AYUI/LayoutEditor/LayoutCanvasViewport.h"
#include "AYUI/LayoutEditor/LayoutCommandStack.h"
#include "AYUI/LayoutEditor/LayoutDocumentModel.h"
#include "AYUI/LayoutEditor/LayoutPreviewModel.h"
#include "AYUI/LayoutEditor/LayoutResourceCatalog.h"
#include "AYUI/LayoutEditor/LayoutSelectionModel.h"
#include "AYUI/LayoutEditor/LayoutStyleInspectorModel.h"
#include "AYUI/LayoutEditor/LayoutStructuredContentModel.h"
#include "AYUI/LayoutEditor/LayoutValidationModel.h"
#include "AYUI/LayoutEditor/WidgetAuthoringRegistry.h"
#include "AYUI/ListView.h"
#include "AYUI/Image.h"
#include "AYUI/Panel.h"
#include "AYUI/RichText.h"
#include "AYUI/TabControl.h"
#include "AYUI/TreeView.h"
#include "AYUI/Widget.h"

#include <string>
#include <unordered_set>
#include <vector>

using namespace ayt::math;
using namespace ayt::ui;

TEST_SUITE(AYUI_LayoutEditorCore)

TEST_CASE(canvas_viewport_round_trips_document_coordinates) {
    LayoutCanvasViewport viewport;
    viewport.setPosition({10.0f, 20.0f});
    viewport.setSize({400.0f, 300.0f});
    viewport.setView(2.0f, {5.0f, -3.0f});

    const FVector2 screen = viewport.documentToScreen(FVector2(20.0f, 40.0f));
    CHECK_FLOAT_EQ(screen.x, 35.0f, 1e-5f);
    CHECK_FLOAT_EQ(screen.y, 57.0f, 1e-5f);
    const FVector2 document = viewport.screenToDocument(screen);
    CHECK_FLOAT_EQ(document.x, 20.0f, 1e-5f);
    CHECK_FLOAT_EQ(document.y, 40.0f, 1e-5f);
}

TEST_CASE(document_model_indexes_ids_and_rejects_duplicates) {
    Button first;
    Button second;
    first.setId("first");
    second.setId("second");

    LayoutDocumentModel document;
    document.setRoot(&first);
    document.rebuildIndex({&first, &second});
    CHECK(document.findById("first") == &first);
    CHECK(document.findById("second") == &second);
    CHECK(document.validateId("renamed", &first));
    CHECK_FALSE(document.validateId("second", &first));
    CHECK_FALSE(document.validateId("__le_overlay", &first));

    second.setId("first");
    document.rebuildIndex({&first, &second});
    CHECK(document.hasDuplicateIds());
    CHECK_FALSE(document.validateId("first", &first));
}

TEST_CASE(selection_model_owns_primary_and_multi_selection_invariants) {
    Button first;
    Button second;
    LayoutSelectionModel selection;
    selection.setSingle(&first);
    CHECK(selection.primary() == &first);
    CHECK(selection.items().size() == 1u);
    CHECK(selection.add(&second));
    CHECK(selection.primary() == &second);
    CHECK(selection.contains(&first));
    CHECK(selection.remove(&second));
    CHECK(selection.primary() == &first);
    selection.prune([&](Widget* widget) { return widget == &second; });
    CHECK(selection.items().empty());
    CHECK(selection.primary() == nullptr);
}

TEST_CASE(command_stack_preserves_kind_and_snapshot_fallback) {
    LayoutCommandStack commands(2);
    LayoutEditorSnapshot clean;
    clean.json = "clean";
    clean.dirty = false;
    commands.begin(clean, LayoutEditKind::Property, "Edit text");
    commands.begin(clean, LayoutEditKind::Property, "same transaction");
    CHECK(commands.undoDepth() == 1u);
    CHECK(commands.transactionOpen());
    commands.end();
    CHECK(commands.nextUndoKind() == LayoutEditKind::Property);

    LayoutEditorSnapshot edited;
    edited.json = "edited";
    edited.dirty = true;
    const auto undo = commands.undo(edited);
    CHECK(undo.has_value());
    CHECK(undo->json == "clean");
    CHECK_FALSE(undo->dirty);
    CHECK(commands.canRedo());

    const auto redo = commands.redo(*undo);
    CHECK(redo.has_value());
    CHECK(redo->json == "edited");
    CHECK(redo->dirty);
}

TEST_CASE(authoring_registry_is_the_single_palette_and_schema_source) {
    WidgetAuthoringRegistry& registry = WidgetAuthoringRegistry::get();
    std::unordered_set<std::string> types;
    std::unordered_set<std::string> buttons;
    int missingMetadata = 0;
    for (const WidgetAuthoringDescriptor& descriptor : registry.descriptors()) {
        if (!types.insert(descriptor.typeName).second ||
            descriptor.idPrefix.empty() || descriptor.icon == nullptr ||
            !descriptor.properties.contains(AuthoringProperty::Id) ||
            !descriptor.properties.contains(AuthoringProperty::Style)) {
            ++missingMetadata;
        }
        if (!descriptor.paletteButtonId.empty() &&
            !buttons.insert(descriptor.paletteButtonId).second) {
            ++missingMetadata;
        }
    }
    CHECK(missingMetadata == 0);
    CHECK(registry.descriptors().size() == 27u);

    int id = 0;
    Widget* created = registry.create("GridPanel",
        [&id](const std::string& prefix, Widget*) {
            return prefix + "_" + std::to_string(++id);
        });
    auto* grid = dynamic_cast<GridPanel*>(created);
    CHECK_NOT_NULL(grid);
    if (grid != nullptr) {
        CHECK(grid->getRowCount() == 2);
        CHECK(grid->getColumnCount() == 2);
        CHECK(registry.findForWidget(grid)->typeName == "GridPanel");
    }
    destroyWidgetTree(created);
}

TEST_CASE(structured_content_model_edits_lists_trees_tabs_and_rich_runs) {
    LayoutStructuredContentModel model;

    ListView list;
    list.setItems({L"One", L"Two"});
    model.bind(&list);
    model.setSelectedIndex(0);
    CHECK(model.add(L"Inserted"));
    CHECK(model.entries().size() == 3u);
    CHECK(model.setSelectedLabel(L"Renamed"));
    CHECK(list.getItemsRef()[1] == L"Renamed");
    CHECK(model.moveSelected(1));
    CHECK(list.getItemsRef()[2] == L"Renamed");
    CHECK(model.removeSelected());
    CHECK(list.getItemsRef().size() == 2u);

    TreeView tree;
    tree.setTree({{L"Root", {}, false, true, -1}});
    model.bind(&tree);
    model.setSelectedIndex(0);
    CHECK(model.add(L"Child", true));
    CHECK(tree.getTreeDataRef().size() == 2u);
    CHECK(tree.getTreeDataRef()[1].parentIndex == 0);
    model.setSelectedIndex(0);
    CHECK(model.removeSelected());
    CHECK(tree.getTreeDataRef().empty());

    TabControl tabs;
    auto makePage = []() -> Widget* { return new Panel(); };
    model.bind(&tabs);
    CHECK(model.add(L"First", false, makePage));
    CHECK(model.add(L"Second", false, makePage));
    CHECK(model.moveSelected(-1));
    CHECK(tabs.getTabLabel(0) == L"Second");
    CHECK(model.removeSelected());
    CHECK(tabs.getTabCount() == 1u);

    RichText rich;
    rich.addRun(L"Alpha", FVector4(1, 1, 1, 1), 14);
    model.bind(&rich);
    model.setSelectedIndex(0);
    RichRun run = model.entries()[0].richRun;
    run.text = L"Styled";
    run.bold = true;
    run.fontSize = 20;
    CHECK(model.setSelectedRichRun(run));
    CHECK(rich.getRun(0).text == L"Styled");
    CHECK(rich.getRun(0).bold);
    CHECK(rich.getRun(0).fontSize == 20);
}

TEST_CASE(resource_catalog_filters_case_insensitively_and_keeps_stable_keys) {
    LayoutResourceCatalog catalog;
    catalog.setEntries({
        {"Assets/button.png", L"Button", "D:/Project/Assets/button.png"},
        {"Assets/Hero.PNG", L"Hero portrait", "D:/Project/Assets/Hero.PNG",
         L"PNG"},
        {"Assets/button.png", L"Duplicate"}
    });
    CHECK(catalog.entries().size() == 2u);
    CHECK(catalog.contains("Assets/Hero.PNG"));
    CHECK(catalog.resolvePreviewPath("Assets/Hero.PNG") ==
          "D:/Project/Assets/Hero.PNG");
    CHECK(catalog.resolvePreviewPath("legacy/path.png") == "legacy/path.png");
    CHECK(catalog.findByPreviewPath("D:/Project/Assets/button.png") != nullptr);
    catalog.setFilter(L"hero");
    CHECK(catalog.visibleIndices().size() == 1u);
    const LayoutTextureResource* result = catalog.visibleEntry(0);
    CHECK_NOT_NULL(result);
    if (result != nullptr) CHECK(result->key == "Assets/Hero.PNG");
}

TEST_CASE(style_inspector_reports_source_overrides_and_state_colors) {
    StyleSheet sheet;
    WidgetStyle style = StyleBuilder::makeButton();
    style.backgroundColor = FVector4(0.1f, 0.2f, 0.3f, 1.0f);
    style.bgToken = "color.accent";
    style.backgroundStates.enabled = true;
    style.backgroundStates.normal = style.backgroundColor;
    style.backgroundStates.hovered = FVector4(0.3f, 0.4f, 0.5f, 1.0f);
    style.backgroundStates.pressed = FVector4(0.05f, 0.1f, 0.2f, 1.0f);
    style.backgroundStates.disabled = FVector4(0.2f, 0.2f, 0.2f, 0.5f);
    sheet.setStyle("quality_button", style);

    Panel parent;
    Button button;
    parent.setStyleTokenOverride("color.accent", FVector4(1, 0, 0, 1));
    parent.addChildExternal(&button);
    button.setStyleId("quality_button");

    LayoutStyleInspectorModel inspector;
    LayoutStyleInspection inherited = inspector.inspect(&button, &sheet);
    CHECK(inherited.styleExists);
    CHECK(inherited.source == LayoutStyleSource::InheritedTokenOverride);
    CHECK(inherited.inheritedOverrideCount == 1u);
    const FVector4 hovered = inspector.backgroundForState(
        inherited, StyleState::Hovered);
    CHECK_FLOAT_EQ(hovered.x, 0.3f, 1e-5f);

    button.setStyleTokenOverride("color.accent", FVector4(0, 1, 0, 1));
    const LayoutStyleInspection local = inspector.inspect(&button, &sheet);
    CHECK(local.source == LayoutStyleSource::LocalTokenOverride);
    CHECK(local.localOverrideCount == 1u);
}

TEST_CASE(validation_model_reports_actionable_authoring_issues) {
    Panel root;
    root.setId("document_root");
    root.setSize({320.0f, 200.0f});

    Button first;
    first.setId("duplicate");
    first.setSize({80.0f, 30.0f});
    first.setEventBinding("onClick", "handleClick");
    root.addChildExternal(&first);

    Button second;
    second.setId("duplicate");
    second.setSize({0.0f, 30.0f});
    second.setStyleId("missing_style");
    root.addChildExternal(&second);

    Image image;
    image.setId("preview");
    image.setSize({64.0f, 64.0f});
    image.setTexture("Assets/missing.png");
    root.addChildExternal(&image);

    LayoutResourceCatalog resources;
    resources.setEntries({
        {"Assets/ready.png", L"Ready", "D:/Assets/ready.png"}
    });
    StyleSheet styles;
    LayoutValidationContext context;
    context.textureCatalog = &resources;
    context.styleSheet = &styles;

    LayoutValidationModel validation;
    validation.run({&root, &first, &second, &image}, context);

    size_t duplicateCount = 0;
    size_t missingStyleCount = 0;
    size_t missingTextureCount = 0;
    size_t missingControllerCount = 0;
    size_t nonPositiveCount = 0;
    for (const LayoutDiagnostic& diagnostic : validation.diagnostics()) {
        if (diagnostic.code == LayoutDiagnosticCode::DuplicateId) {
            ++duplicateCount;
        } else if (diagnostic.code == LayoutDiagnosticCode::MissingStyle) {
            ++missingStyleCount;
        } else if (diagnostic.code == LayoutDiagnosticCode::MissingTexture) {
            ++missingTextureCount;
        } else if (diagnostic.code == LayoutDiagnosticCode::EventWithoutController) {
            ++missingControllerCount;
        } else if (diagnostic.code == LayoutDiagnosticCode::NonPositiveSize) {
            ++nonPositiveCount;
        }
    }
    CHECK(duplicateCount == 2u);
    CHECK(missingStyleCount == 1u);
    CHECK(missingTextureCount == 1u);
    CHECK(missingControllerCount == 1u);
    CHECK(nonPositiveCount == 1u);
    CHECK(validation.hasErrors());
}

TEST_CASE(preview_model_maps_physical_resolution_dpi_and_safe_area_to_dip) {
    LayoutPreviewModel preview;
    CHECK(preview.selectPreset(3));
    const FVector2 logical = preview.logicalSize({640.0f, 480.0f});
    CHECK_FLOAT_EQ(logical.x, 390.0f, 1e-5f);
    CHECK_FLOAT_EQ(logical.y, 844.0f, 1e-5f);
    const FVector4 safe = preview.logicalSafeArea();
    CHECK_FLOAT_EQ(safe.y, 44.0f, 1e-5f);
    CHECK_FLOAT_EQ(safe.w, 34.0f, 1e-5f);

    preview.setPixelSize(1920.0f, 1080.0f);
    preview.setDpiScale(2.0f);
    CHECK(preview.presetIndex() == -1);
    const FVector2 custom = preview.logicalSize({1.0f, 1.0f});
    CHECK_FLOAT_EQ(custom.x, 960.0f, 1e-5f);
    CHECK_FLOAT_EQ(custom.y, 540.0f, 1e-5f);
}

TEST_SUITE_END
