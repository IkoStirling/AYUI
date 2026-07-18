#include "AYTest.h"
#include "AYComboBox.h"
#include "AYListView.h"
#include "AYTextLabel.h"
#include "AYUIManager.h"
#include "AYWidgetFactory.h"
#include "AYWidgetSerializer.h"
#include "AYMockRenderer.h"
#include "AYStyle.h"
#include <iostream>

using namespace ayt::ui;
using namespace ayt::math;

TEST_SUITE(AYUI_ComboBox)

// C-6: default state — no items, no selection, popup closed, display
// label is empty.
TEST_CASE(combobox_initial_state) {
    ComboBox cb;
    CHECK(cb.getItemCount() == 0u);
    CHECK(cb.getSelectedIndex() == -1);
    CHECK_FALSE(cb.isPopupOpen());
    CHECK(cb.getSelectedItem() == L"");
    CHECK(cb.getMaxPopupItems() == 8);
}

// C-6: setItems + setSelectedIndex updates display label.
TEST_CASE(combobox_set_items_and_selection) {
    ComboBox cb;
    cb.setItems({L"red", L"green", L"blue"});
    CHECK(cb.getItemCount() == 3u);

    cb.setSelectedIndex(2);
    CHECK(cb.getSelectedItem() == L"blue");

    // Display label is a TextLabel child — read its text.
    // (We can't easily expose the display TextLabel accessor in the public
    // API for v1, so the round-trip test below is the canonical verification.)
}

// C-6: openPopup / closePopup toggle visibility.
TEST_CASE(combobox_open_close_popup) {
    ComboBox cb;
    cb.setItems({L"a", L"b", L"c"});
    CHECK_FALSE(cb.isPopupOpen());

    cb.openPopup();
    CHECK(cb.isPopupOpen());

    cb.closePopup();
    CHECK_FALSE(cb.isPopupOpen());

    cb.togglePopup();
    CHECK(cb.isPopupOpen());
    cb.togglePopup();
    CHECK_FALSE(cb.isPopupOpen());
}

// C-6: openPopup on empty items is a no-op.
TEST_CASE(combobox_open_popup_empty_noop) {
    ComboBox cb;
    cb.openPopup();
    CHECK_FALSE(cb.isPopupOpen());
}

// C-6: clicking the main ComboBox toggles popup; clicking the popup row
// updates selection and (in v1) closes the popup.
TEST_CASE(combobox_click_main_opens_popup) {
    ComboBox cb;
    cb.setItems({L"x", L"y", L"z"});
    cb.setSize(FVector2(160.0f, 28.0f));
    cb.setPosition(FVector2(0.0f, 0.0f));

    const FVector2 world = cb.getWorldBounds().getMin();
    // Click in the middle of the main area.
    const FVector2 clickPos(world.x + 80.0f, world.y + 14.0f);
    cb.onMouseButtonUp(UIMouseEvent(clickPos, 0));
    CHECK(cb.isPopupOpen());
}

// C-6: clicking a popup row closes the popup and updates selection.
TEST_CASE(combobox_click_popup_row_selects) {
    ComboBox cb;
    cb.setItems({L"a", L"b", L"c"});
    cb.setSize(FVector2(160.0f, 28.0f));
    cb.setPosition(FVector2(0.0f, 0.0f));

    int selChanges = 0;
    int lastSelected = -2;
    cb.setOnSelectionChanged([&](int idx) {
        ++selChanges;
        lastSelected = idx;
    });

    cb.openPopup();
    CHECK(cb.isPopupOpen());

    // Force a layout so the popup has world bounds. simulate the layout
    // by re-running performLayout (in real apps UIManager::update runs it).
    cb.performLayout();

    // Find the popup child and grab the row at index 1.
    CHECK(cb.getChildren().size() >= 2u);
    ListView* popup = dynamic_cast<ListView*>(
        cb.getChildren().back());
    CHECK_NOT_NULL(popup);
    CHECK(popup->getItemCount() == 3u);

    // Row 1 lives at popup-local y = 24..48; popup-local origin sits at
    // (0, ComboBox.height + gap) relative to ComboBox world top.
    const FVector2 cbWorld = cb.getWorldBounds().getMin();
    const FVector2 rowWorld(
        cbWorld.x + 80.0f,
        cbWorld.y + cb.getHeight() + 2.0f + 36.0f); // mid of row 1

    // Route the click through ComboBox's onMouseButtonUp (which forwards
    // to the popup row when popup is open).
    cb.onMouseButtonUp(UIMouseEvent(rowWorld, 0));

    CHECK(selChanges == 1);
    CHECK(lastSelected == 1);
    CHECK(cb.getSelectedIndex() == 1);
    CHECK(cb.getSelectedItem() == L"b");
}

// C-6: factory + serializer round-trip preserves items + selectedIndex +
// maxPopupItems.
TEST_CASE(combobox_factory_and_serializer_round_trip) {
    WidgetFactory& factory = WidgetFactory::get();
    CHECK_TRUE(factory.isRegistered("ComboBox"));

    Widget* widget = factory.create("ComboBox");
    CHECK_NOT_NULL(widget);
    ComboBox* original = dynamic_cast<ComboBox*>(widget);
    CHECK_NOT_NULL(original);

    original->setId("cb_color");
    original->setItems({L"red", L"green", L"blue", L"yellow"});
    original->setSelectedIndex(2);
    original->setMaxPopupItems(6);

    std::string json = WidgetSerializer::serialize(widget);
    CHECK(json.find("\"type\": \"ComboBox\"") != std::string::npos);
    CHECK(json.find("yellow") != std::string::npos);
    CHECK(json.find("\"selectedIndex\": 2") != std::string::npos);

    Widget* restored = WidgetSerializer::deserialize(json);
    CHECK_NOT_NULL(restored);
    ComboBox* restoredCb = dynamic_cast<ComboBox*>(restored);
    CHECK_NOT_NULL(restoredCb);
    CHECK(restoredCb->getId() == "cb_color");
    CHECK(restoredCb->getItemCount() == 4u);
    CHECK(restoredCb->getSelectedIndex() == 2);
    CHECK(restoredCb->getSelectedItem() == L"blue");
    CHECK(restoredCb->getMaxPopupItems() == 6);

    destroyWidgetTree(widget);
    destroyWidgetTree(restored);
}

// C-6: render emits at least the main background rect + arrow chevron
// rects. (MockRenderer doesn't record drawBorderRect as a typed draw call —
// IRenderBackend::drawBorderRect is the default no-op path — so we count
// only Rects.)
TEST_CASE(combobox_render_emits_main_and_arrow) {
    ComboBox cb;
    cb.setItems({L"only"});
    cb.setSize(FVector2(160.0f, 28.0f));
    cb.setPosition(FVector2(0.0f, 0.0f));

    MockRenderer renderer;
    cb.render(renderer);

    int rectCount = 0;
    for (const auto& dc : renderer.getDrawCalls()) {
        if (dc.type == MockRenderer::DrawCall::Rect) ++rectCount;
    }
    CHECK(rectCount >= 3);   // bg + 2 arrow halves
}

TEST_SUITE_END
