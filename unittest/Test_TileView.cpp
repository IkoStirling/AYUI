#include "AYTest.h"

#include "AYUI/TileView.h"
#include "AYUI/LayoutLoader.h"
#include "AYUI/MockRenderer.h"
#include "AYUI/UIKeyCode.h"
#include "AYUI/UIManager.h"
#include "AYUI/WidgetFactory.h"
#include "AYUI/WidgetSerializer.h"

#include <string>
#include <vector>

using namespace ayt::math;
using namespace ayt::ui;

namespace {

std::vector<std::wstring> makeTileItems(int count) {
    std::vector<std::wstring> items;
    items.reserve(static_cast<size_t>(count));
    for (int index = 0; index < count; ++index) {
        items.push_back(L"tile " + std::to_wstring(index));
    }
    return items;
}

FVector2 rectCenter(const FRectangle& rectangle) {
    return FVector2((rectangle.minX + rectangle.maxX) * 0.5f,
                    (rectangle.minY + rectangle.maxY) * 0.5f);
}

void clickCell(TileCell& cell, const FVector2& point) {
    cell.onMouseButtonDown(UIMouseEvent(point, 0));
    cell.onMouseButtonUp(UIMouseEvent(point, 0));
}

const AccessibilityNode* findAccessibilityLabel(
    const AccessibilityNode& node, const std::wstring& label) {
    if (node.label == label) return &node;
    for (const AccessibilityNode& child : node.children) {
        if (const AccessibilityNode* found =
                findAccessibilityLabel(child, label)) {
            return found;
        }
    }
    return nullptr;
}

} // namespace

TEST_SUITE(AYUI_TileView)

TEST_CASE(tileview_grid_virtualizes_large_item_sets) {
    TileView view;
    view.setSize(FVector2(352.0f, 220.0f));
    view.setTileSize(FVector2(100.0f, 100.0f));
    view.setTileSpacing(8.0f);
    view.setContentPadding(4.0f);
    view.setOverscanRows(1);
    view.setItems(makeTileItems(10000));
    view.performLayout();

    CHECK(view.getColumnCount() == 3);
    CHECK(view.getLogicalRowCount() == 3334);
    CHECK(view.getCellPoolSize() <= 15u);
    CHECK(view.getCellPoolSize() < view.getItemCount());
    CHECK(view.getFirstPooledIndex() == 0);

    int invalidMappings = 0;
    for (size_t slot = 0; slot < view.getCellPoolSize(); ++slot) {
        if (view.getCellPoolLogicalIndex(slot) != static_cast<int>(slot)) {
            ++invalidMappings;
        }
    }
    CHECK(invalidMappings == 0);
}

TEST_CASE(tileview_scroll_rebinds_existing_pool_cells) {
    TileView view;
    view.setSize(FVector2(352.0f, 220.0f));
    view.setTileSize(FVector2(100.0f, 100.0f));
    view.setTileSpacing(8.0f);
    view.setItems(makeTileItems(500));
    view.performLayout();

    TileCell* firstSlot = view.cellForLogicalIndex(0);
    const size_t poolSize = view.getCellPoolSize();
    CHECK_NOT_NULL(firstSlot);

    view.setScrollOffset(FVector2(0.0f, 10.0f * 108.0f));
    const int firstLogical = view.getFirstPooledIndex();
    CHECK(firstLogical == 27); // visible row 10 minus one overscan row
    CHECK(view.getCellPoolSize() == poolSize);
    CHECK(view.cellForLogicalIndex(firstLogical) == firstSlot);

    int invalidMappings = 0;
    for (size_t slot = 0; slot < poolSize; ++slot) {
        if (view.getCellPoolLogicalIndex(slot)
            != firstLogical + static_cast<int>(slot)) {
            ++invalidMappings;
        }
    }
    CHECK(invalidMappings == 0);
}

TEST_CASE(tileview_responsive_width_reflows_columns_without_item_widgets) {
    TileView view;
    view.setTileSize(FVector2(100.0f, 100.0f));
    view.setTileSpacing(8.0f);
    view.setItems(makeTileItems(1000));
    view.setSize(FVector2(352.0f, 220.0f));
    view.performLayout();
    CHECK(view.getColumnCount() == 3);

    view.setSize(FVector2(244.0f, 220.0f));
    view.performLayout();
    CHECK(view.getColumnCount() == 2);
    CHECK(view.getLogicalRowCount() == 500);
    CHECK(view.getCellPoolSize() <= 10u);
}

TEST_CASE(tileview_scrollbar_auto_hides_and_offset_clamps) {
    TileView view;
    view.setSize(FVector2(352.0f, 220.0f));
    view.setTileSize(FVector2(100.0f, 100.0f));
    view.setItems({L"a", L"b", L"c"});
    view.performLayout();
    CHECK_NOT_NULL(view.getVerticalScrollBar());
    CHECK_FALSE(view.getVerticalScrollBar()->isVisible());

    view.setItems(makeTileItems(100));
    view.performLayout();
    CHECK(view.getVerticalScrollBar()->isVisible());
    view.setScrollOffset(FVector2(0.0f, 100000.0f));
    const float bottom = view.getScrollOffset().y;
    CHECK(bottom > 0.0f);
    CHECK_FALSE(view.scrollBy(100.0f));

    view.clearItems();
    CHECK_FALSE(view.getVerticalScrollBar()->isVisible());
    CHECK_FLOAT_EQ(view.getScrollOffset().y, 0.0f, 1e-5f);
}

TEST_CASE(tileview_binder_only_visits_the_recycled_cell_pool) {
    TileView view;
    view.setSize(FVector2(352.0f, 220.0f));
    view.setTileSize(FVector2(100.0f, 100.0f));
    int bindCount = 0;
    view.setCellBinder([&](TileCell& cell, int index,
                           const std::wstring&) {
        ++bindCount;
        cell.setSecondaryText(L"bound " + std::to_wstring(index));
    });
    view.setItems(makeTileItems(10000));
    const int initialBinds = bindCount;
    CHECK(initialBinds == static_cast<int>(view.getCellPoolSize()));
    CHECK(initialBinds < 100);

    view.setScrollOffset(FVector2(0.0f, 1080.0f));
    CHECK(bindCount - initialBinds
          == static_cast<int>(view.getCellPoolSize()));
    TileCell* first = view.cellForLogicalIndex(view.getFirstPooledIndex());
    CHECK_NOT_NULL(first);
    CHECK_FALSE(first->getSecondaryText().empty());
}

TEST_CASE(tileview_recycled_cell_clears_runtime_decoration_state) {
    TileView view;
    view.setSize(FVector2(352.0f, 220.0f));
    view.setTileSize(FVector2(100.0f, 150.0f));
    view.setTileSpacing(8.0f);
    view.setCellBinder([](TileCell& cell, int index,
                          const std::wstring&) {
        if (index != 0) return;
        cell.setSecondaryText(L"transient detail");
        cell.setBadgeText(L"OLD");
        cell.setInfoStrip(
            L"TYPE",
            FVector4(0.10f, 0.20f, 0.30f, 1.0f),
            FVector4(0.80f, 0.90f, 1.00f, 1.0f));
        cell.setCornerMarkerVisible(true);
        cell.setCornerMarkerColor(
            FVector4(0.20f, 0.30f, 0.40f, 1.0f));
    });
    view.setItems(makeTileItems(500));

    TileCell* recycled = view.cellForLogicalIndex(0);
    CHECK_NOT_NULL(recycled);
    CHECK(recycled->getInfoStripText() == L"TYPE");
    CHECK(recycled->isCornerMarkerVisible());

    view.setScrollOffset(FVector2(0.0f, 10.0f * 158.0f));
    const int reboundIndex = view.getFirstPooledIndex();
    CHECK(reboundIndex > 0);
    CHECK(view.cellForLogicalIndex(reboundIndex) == recycled);
    CHECK(recycled->getText()
          == L"tile " + std::to_wstring(reboundIndex));
    CHECK(recycled->getSecondaryText().empty());
    CHECK(recycled->getBadgeText().empty());
    CHECK(recycled->getInfoStripText().empty());
    CHECK_FALSE(recycled->isCornerMarkerVisible());
    CHECK(recycled->getCornerMarkerColor()
          == FVector4(0.92f, 0.68f, 0.20f, 1.0f));
}

TEST_CASE(tileview_info_strip_marker_and_thumbnail_share_uniform_geometry) {
    TileView view;
    view.setTileSize(FVector2(104.0f, 150.0f));
    view.setLabelHeight(28.0f);
    view.setInfoStripHeight(16.0f);
    view.setCornerMarkerSize(12.0f);
    view.setThumbnailAspectRatio(1.0f);
    view.setItems({L"character.mesh"});

    TileCell* cell = view.cellForLogicalIndex(0);
    CHECK_NOT_NULL(cell);
    cell->setInfoStrip(
        L"MESH", FVector4(0.18f, 0.48f, 0.78f, 1.0f),
        FVector4(1.0f, 1.0f, 1.0f, 1.0f));
    cell->setCornerMarkerVisible(true);

    const FRectangle thumbnail = cell->getThumbnailRect();
    const FRectangle strip = cell->getInfoStripRect();
    const FRectangle label = cell->getLabelRect();
    const FRectangle marker = cell->getCornerMarkerBounds();
    CHECK_FLOAT_EQ(thumbnail.maxX - thumbnail.minX,
                   thumbnail.maxY - thumbnail.minY, 1e-5f);
    CHECK_FLOAT_EQ(strip.maxY - strip.minY, 16.0f, 1e-5f);
    CHECK(strip.minY >= thumbnail.maxY);
    CHECK_FLOAT_EQ(strip.maxY, label.minY, 1e-5f);
    CHECK_FLOAT_EQ(marker.maxX - marker.minX, 12.0f, 1e-5f);
    CHECK_FLOAT_EQ(marker.maxY - marker.minY, 12.0f, 1e-5f);
    CHECK_FLOAT_EQ(marker.maxX, thumbnail.maxX, 1e-5f);
    CHECK_FLOAT_EQ(marker.minY, thumbnail.minY, 1e-5f);
    CHECK(cell->hitRegion(rectCenter(strip)) == TileCell::HitRegion::Body);
    CHECK(cell->hitRegion(rectCenter(marker)) == TileCell::HitRegion::Body);
    CHECK(cell->hitRegion(rectCenter(label)) == TileCell::HitRegion::Label);

    view.setThumbnailAspectRatio(2.0f);
    const FRectangle wideThumbnail = cell->getThumbnailRect();
    CHECK_FLOAT_EQ(
        wideThumbnail.maxX - wideThumbnail.minX,
        (wideThumbnail.maxY - wideThumbnail.minY) * 2.0f, 1e-5f);
}

TEST_CASE(tileview_decorations_render_centered_strip_path_marker_then_border) {
    TileView view;
    view.setTileSize(FVector2(104.0f, 150.0f));
    view.setLabelHeight(28.0f);
    view.setItems({L"tile"});
    TileCell* cell = view.cellForLogicalIndex(0);
    CHECK_NOT_NULL(cell);
    cell->setInfoStrip(
        L"TYPE", FVector4(0.10f, 0.20f, 0.30f, 1.0f),
        FVector4(0.90f, 0.80f, 0.70f, 1.0f));
    cell->setCornerMarkerVisible(true);

    MockRenderer renderer;
    cell->render(renderer);
    const auto& calls = renderer.getDrawCalls();
    int markerPath = -1;
    int infoText = -1;
    for (int index = 0; index < static_cast<int>(calls.size()); ++index) {
        if (calls[index].type == MockRenderer::DrawCall::Path) {
            markerPath = index;
        }
        if (calls[index].type == MockRenderer::DrawCall::Text
            && calls[index].text == L"TYPE") {
            infoText = index;
        }
    }
    CHECK(markerPath >= 0);
    CHECK(infoText >= 0);
    CHECK(calls[infoText].textStyle.align
          == IRenderBackend::TextStyle::Align::Center);
    CHECK(calls[infoText].textStyle.valign
          == IRenderBackend::TextStyle::VAlign::Middle);
    CHECK(markerPath < static_cast<int>(calls.size()) - 1);
    CHECK(calls.back().type == MockRenderer::DrawCall::Rect);
}

TEST_CASE(tileview_long_label_middle_elides_without_losing_full_model_text) {
    TileView view;
    view.setTileSize(FVector2(104.0f, 150.0f));
    const std::wstring fullName =
        L"Sour_fix_0830_character_body_basecolor_texture.fbm_Texture.dds";
    view.setItems({fullName});

    TileCell* cell = view.cellForLogicalIndex(0);
    CHECK_NOT_NULL(cell);
    MockRenderer renderer;
    cell->render(renderer);

    const MockRenderer::DrawCall* labelCall = nullptr;
    for (const auto& call : renderer.getDrawCalls()) {
        if (call.type == MockRenderer::DrawCall::Text) {
            labelCall = &call;
            break;
        }
    }
    CHECK_NOT_NULL(labelCall);
    if (labelCall != nullptr) {
        CHECK(labelCall->text != fullName);
        CHECK(labelCall->text.find(L"\x2026") != std::wstring::npos);
        CHECK(labelCall->text.front() == fullName.front());
        CHECK(labelCall->text.back() == fullName.back());
        CHECK(renderer.measureText(labelCall->text, 13).width
              <= cell->getLabelRect().maxX - cell->getLabelRect().minX);
    }
    CHECK(cell->getText() == fullName);
    CHECK(cell->getAccessibilityLabel() == fullName);
    CHECK(renderer.isClipStackBalanced());
}

TEST_CASE(tileview_keyboard_focus_selection_and_grid_navigation) {
    UIManager manager;
    manager.initialize(nullptr);

    TileView view;
    view.setSize(FVector2(352.0f, 220.0f));
    view.setTileSize(FVector2(100.0f, 100.0f));
    view.setItems(makeTileItems(30));
    view.setSelectionMode(TileView::SelectionMode::Extended);
    view.setSelectedIndex(0);

    CHECK(view.onKeyDown(UIKey_Right));
    CHECK(view.getFocusedIndex() == 1);
    CHECK(view.getSelectedIndex() == 1);

    manager.onKeyDown(UIKey_Shift);
    CHECK(view.onKeyDown(UIKey_Down));
    manager.onKeyUp(UIKey_Shift);
    CHECK(view.getFocusedIndex() == 4);
    CHECK(view.getSelectedIndices().size() == 4u);

    manager.onKeyDown(UIKey_Control);
    CHECK(view.onKeyDown(UIKey_Right));
    manager.onKeyUp(UIKey_Control);
    CHECK(view.getFocusedIndex() == 5);
    CHECK(view.getSelectedIndices().size() == 4u);

    manager.shutdown();
}

TEST_CASE(tileview_f2_and_enter_target_the_focus_cursor) {
    TileView view;
    view.setItems(makeTileItems(12));
    view.setFocusedIndex(7, false);
    int renamed = -1;
    int activated = -1;
    view.setOnRenameRequested([&](int index) { renamed = index; });
    view.setOnItemActivated([&](int index) { activated = index; });

    CHECK(view.onKeyDown(UIKey_F2));
    CHECK(renamed == 7);
    CHECK(view.onKeyDown(UIKey_Enter));
    CHECK(activated == 7);
}

TEST_CASE(tileview_double_click_reports_label_and_thumbnail_regions) {
    TileView view;
    view.setSize(FVector2(240.0f, 180.0f));
    view.setItems({L"asset"});
    view.performLayout();
    TileCell* cell = view.cellForLogicalIndex(0);
    CHECK_NOT_NULL(cell);

    int doubleCount = 0;
    int activateCount = 0;
    TileCell::HitRegion lastRegion = TileCell::HitRegion::None;
    view.setOnItemDoubleClicked(
        [&](int index, TileCell::HitRegion region) {
            if (index == 0) ++doubleCount;
            lastRegion = region;
        });
    view.setOnItemActivated([&](int index) {
        if (index == 0) ++activateCount;
    });

    const FVector2 labelPoint = rectCenter(cell->getLabelRect());
    clickCell(*cell, labelPoint);
    view.tick(0.1f);
    clickCell(*cell, labelPoint);
    CHECK(doubleCount == 1);
    CHECK(activateCount == 0);
    CHECK(lastRegion == TileCell::HitRegion::Label);

    view.tick(0.5f);
    const FVector2 thumbnailPoint = rectCenter(cell->getThumbnailRect());
    clickCell(*cell, thumbnailPoint);
    view.tick(0.1f);
    clickCell(*cell, thumbnailPoint);
    CHECK(doubleCount == 2);
    CHECK(activateCount == 0);
    CHECK(lastRegion == TileCell::HitRegion::Thumbnail);
}

TEST_CASE(tileview_double_click_falls_back_to_item_activation) {
    TileView view;
    view.setItems({L"asset"});
    TileCell* cell = view.cellForLogicalIndex(0);
    CHECK_NOT_NULL(cell);
    int activated = -1;
    view.setOnItemActivated([&](int index) { activated = index; });
    const FVector2 point = rectCenter(cell->getThumbnailRect());
    clickCell(*cell, point);
    view.tick(0.1f);
    clickCell(*cell, point);
    CHECK(activated == 0);
}

TEST_CASE(tileview_drag_uses_selected_set_and_custom_payload) {
    UIManager manager;
    manager.initialize(nullptr);

    TileView view;
    manager.root()->addChildExternal(&view);
    view.setSize(FVector2(352.0f, 220.0f));
    view.setItems(makeTileItems(100));
    view.setSelectionMode(TileView::SelectionMode::Extended);
    view.setSelectedIndices({0, 1, 2});
    view.setDragPayloadBuilder(
        [](int pressed, const std::vector<int>& indices) {
            DragPayload payload;
            payload.kind = "EditorAssets";
            payload.userData = pressed;
            payload.text = std::to_wstring(indices.size());
            return payload;
        });

    int startedCount = 0;
    int startedItems = 0;
    bool finishedAccepted = true;
    view.setOnItemDragStarted([&](const std::vector<int>& indices) {
        ++startedCount;
        startedItems = static_cast<int>(indices.size());
    });
    view.setOnItemDragFinished([&](bool accepted) {
        finishedAccepted = accepted;
    });

    TileCell* cell = view.cellForLogicalIndex(1);
    CHECK_NOT_NULL(cell);
    const FVector2 start = rectCenter(cell->getThumbnailRect());
    cell->onMouseButtonDown(UIMouseEvent(start, 0));
    cell->onMouseMove(UIMouseEvent(FVector2(start.x + 20.0f, start.y), 0));

    CHECK(manager.isDragging());
    CHECK(manager.getDragPayload().kind == "EditorAssets");
    CHECK(manager.getDragPayload().userData == 1);
    CHECK(startedCount == 1);
    CHECK(startedItems == 3);
    CHECK(view.getActiveDragIndices().size() == 3u);

    manager.cancelDrag();
    CHECK_FALSE(finishedAccepted);
    CHECK(view.getActiveDragIndices().empty());
    manager.shutdown();
}

TEST_CASE(tileview_drag_edge_autoscroll_rebinds_visible_cells) {
    UIManager manager;
    manager.initialize(nullptr);

    TileView view;
    manager.root()->addChildExternal(&view);
    view.setSize(FVector2(352.0f, 220.0f));
    view.setItems(makeTileItems(1000));
    TileCell* cell = view.cellForLogicalIndex(0);
    CHECK_NOT_NULL(cell);

    const FVector2 start = rectCenter(cell->getThumbnailRect());
    cell->onMouseButtonDown(UIMouseEvent(start, 0));
    cell->onMouseMove(UIMouseEvent(FVector2(start.x + 20.0f, start.y), 0));
    CHECK(manager.isDragging());

    const FRectangle client = view.getClientRect();
    manager.updateDrag((client.minX + client.maxX) * 0.5f,
                       client.maxY - 1.0f);
    const int firstBefore = view.getFirstPooledIndex();
    for (int frame = 0; frame < 30; ++frame) view.tick(1.0f / 60.0f);
    CHECK(view.getScrollOffset().y > 0.0f);
    CHECK(view.getFirstPooledIndex() > firstBefore);

    manager.cancelDrag();
    manager.shutdown();
}

TEST_CASE(tileview_factory_and_serializer_round_trip) {
    Widget* widget = WidgetFactory::get().create("TileView");
    TileView* original = dynamic_cast<TileView*>(widget);
    CHECK_NOT_NULL(original);
    original->setSize(FVector2(352.0f, 220.0f));
    original->setTileSize(FVector2(96.0f, 108.0f));
    original->setTileSpacing(7.0f);
    original->setContentPadding(5.0f);
    original->setLabelHeight(30.0f);
    original->setInfoStripHeight(15.0f);
    original->setCornerMarkerSize(11.0f);
    original->setThumbnailAspectRatio(1.5f);
    original->setOverscanRows(2);
    original->setItems(makeTileItems(50));
    original->setSelectionMode(TileView::SelectionMode::Extended);
    original->setSelectedIndices({2, 5, 9});
    original->setFocusedIndex(5, false);
    original->setScrollOffset(FVector2(0.0f, 120.0f));
    if (TileCell* visible = original->cellForLogicalIndex(
            original->getFirstPooledIndex())) {
        visible->setInfoStrip(
            L"RUNTIME_ONLY", FVector4(1, 0, 0, 1),
            FVector4(1, 1, 1, 1));
        visible->setCornerMarkerVisible(true);
    }

    const std::string encoded = WidgetSerializer::serialize(original);
    CHECK(encoded.find("RUNTIME_ONLY") == std::string::npos);
    CHECK(encoded.find("infoStripText") == std::string::npos);
    CHECK(encoded.find("cornerMarkerVisible") == std::string::npos);
    Widget* restoredWidget = WidgetSerializer::deserialize(encoded);
    TileView* restored = dynamic_cast<TileView*>(restoredWidget);
    CHECK_NOT_NULL(restored);
    CHECK(restored->getItemCount() == 50u);
    CHECK(restored->getSelectionMode()
          == TileView::SelectionMode::Extended);
    CHECK(restored->getSelectedIndices().size() == 3u);
    CHECK(restored->getFocusedIndex() == 5);
    CHECK_FLOAT_EQ(restored->getTileSize().x, 96.0f, 1e-5f);
    CHECK_FLOAT_EQ(restored->getTileSpacing(), 7.0f, 1e-5f);
    CHECK_FLOAT_EQ(restored->getInfoStripHeight(), 15.0f, 1e-5f);
    CHECK_FLOAT_EQ(restored->getCornerMarkerSize(), 11.0f, 1e-5f);
    CHECK_FLOAT_EQ(restored->getThumbnailAspectRatio(), 1.5f, 1e-5f);
    CHECK(restored->getOverscanRows() == 2);
    CHECK(restored->getChildren().size()
          == restored->getCellPoolSize() + 1u); // pool + one vbar, no JSON cells

    destroyWidgetTree(widget);
    destroyWidgetTree(restoredWidget);
}

TEST_CASE(tileview_layout_loader_applies_virtual_grid_contract) {
    UILayoutLoader loader;
    Widget* widget = loader.loadFromString(R"({
        "type": "TileView",
        "size": {"w": 352, "h": 220},
        "items": ["a", "b", "c", "d", "e", "f", "g", "h"],
        "selectionMode": 1,
        "selectedIndices": [1, 6],
        "focusedIndex": 6,
        "tileSize": {"w": 96, "h": 108},
        "tileSpacing": 7,
        "contentPadding": 5,
        "labelHeight": 30,
        "infoStripHeight": 15,
        "cornerMarkerSize": 11,
        "thumbnailAspectRatio": 1.5,
        "overscanRows": 2,
        "dragEnabled": false
    })");
    TileView* view = dynamic_cast<TileView*>(widget);
    CHECK_NOT_NULL(view);
    CHECK(view->getItemCount() == 8u);
    CHECK(view->getSelectionMode() == TileView::SelectionMode::Extended);
    CHECK(view->getSelectedIndices().size() == 2u);
    CHECK(view->getFocusedIndex() == 6);
    CHECK_FLOAT_EQ(view->getTileSize().x, 96.0f, 1e-5f);
    CHECK_FLOAT_EQ(view->getTileSpacing(), 7.0f, 1e-5f);
    CHECK_FLOAT_EQ(view->getContentPadding(), 5.0f, 1e-5f);
    CHECK_FLOAT_EQ(view->getLabelHeight(), 30.0f, 1e-5f);
    CHECK_FLOAT_EQ(view->getInfoStripHeight(), 15.0f, 1e-5f);
    CHECK_FLOAT_EQ(view->getCornerMarkerSize(), 11.0f, 1e-5f);
    CHECK_FLOAT_EQ(view->getThumbnailAspectRatio(), 1.5f, 1e-5f);
    CHECK(view->getOverscanRows() == 2);
    CHECK_FALSE(view->isDragEnabled());
    destroyWidgetTree(widget);
}

TEST_CASE(tileview_visible_cells_publish_focus_and_selection_semantics) {
    UIManager manager;
    manager.initialize(nullptr);
    manager.setClientSize(400.0f, 300.0f);

    TileView view;
    manager.root()->addChildExternal(&view);
    view.setSize(FVector2(352.0f, 220.0f));
    view.setItems({L"tile 0", L"tile 1", L"tile 2"});
    view.setFocusedIndex(1, false);
    manager.setFocus(&view);

    const AccessibilityNode tree = manager.buildAccessibilityTree();
    const AccessibilityNode* tile = findAccessibilityLabel(tree, L"tile 1");
    CHECK_NOT_NULL(tile);
    CHECK(tile->role == AccessibilityRole::ListItem);
    CHECK((tile->states & AccessibilityState_Focusable) != 0);
    CHECK((tile->states & AccessibilityState_Focused) != 0);
    CHECK((tile->actions
        & accessibilityActionMask(AccessibilityAction::Focus)) != 0);
    CHECK((tile->actions
        & accessibilityActionMask(AccessibilityAction::Select)) != 0);
    CHECK(manager.performAccessibilityAction(
        tile->id, AccessibilityAction::Select));
    CHECK(view.getSelectedIndex() == 1);

    manager.shutdown();
}

TEST_SUITE_END
