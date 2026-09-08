#include "AYTest.h"

#include "AYUI/Button.h"
#include "AYUI/GridPanel.h"
#include "AYUI/LayoutEditor/LayoutCanvasViewport.h"
#include "AYUI/LayoutEditor/LayoutCommandStack.h"
#include "AYUI/LayoutEditor/LayoutDocumentModel.h"
#include "AYUI/LayoutEditor/LayoutSelectionModel.h"
#include "AYUI/LayoutEditor/WidgetAuthoringRegistry.h"
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

TEST_SUITE_END
